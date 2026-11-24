//! What `gram deploy` runs for a device: a list of commands, each a program and its arguments, built here and
//! run by `deploy::execute`. Pure: no I/O, so every command line is pinned by a golden test.

use std::path::Path;

use crate::profile::Profile;

use super::device::{Device, DeviceOs, DEVICES_FILE};

/// The launcher a Steam shortcut starts, installed on the device as `~/gram-launch.sh` by every deploy.
pub const LAUNCHER: &str = include_str!("gram-launch.sh");

/// `--debug`: the port `lldb-server platform` listens on, through which lldb here reads the device's files (the
/// executable, every library it loads, `libGameLogic`'s reload copies included: their symbols).
pub const DEBUG_PLATFORM_PORT: u16 = 1234;
/// `--debug`: the port `lldb-server gdbserver` waits on with the game, stopped before its first instruction.
pub const DEBUG_GDB_PORT: u16 = 1235;
/// The platform's own gdbserver port, never used (the game's is started apart, with the device's environment), but
/// the platform wants one that doesn't clash.
const DEBUG_UNUSED_PORT: u16 = 1236;

/// One command of the plan.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Step {
    /// What it does, printed before it runs ("copy the sources to legion").
    pub what: String,
    /// The program started on this machine (`rsync`, `ssh`), found on the `PATH`.
    pub program: String,
    /// Its arguments, each passed as one argument: no shell on this machine splits or expands them.
    pub args: Vec<String>,
}

impl Step {
    /// The step as it could be pasted into a shell on this machine (`--dry-run`, error messages).
    pub fn command_line(&self) -> String {
        std::iter::once(self.program.as_str())
            .chain(self.args.iter().map(String::as_str))
            .map(quote)
            .collect::<Vec<_>>()
            .join(" ")
    }
}

/// What to deploy, from the command line and the project.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeployOptions {
    /// `--config`: what `gram build` builds on the device.
    pub config: Profile,
    /// `--module`: build only this module on the device (`gram build --module`). `None`: the whole tree.
    pub module: Option<String>,
    /// The main runtime module's executable, relative to the project root (`build/gram-engine/linux/debug/Game`).
    pub executable: String,
    /// `false` with `--no-run`: copy and build only.
    pub run: bool,
    /// The project's own `gram` (gram-engine builds itself: `gram/Cargo.toml`), built first on the device and then
    /// used for `gram build` (`gram/target/debug/gram`). `None`: the device's `gram`, from its `PATH`.
    pub gram_manifest: Option<String>,
    /// `--direct`: start the game over ssh even when the device has a Steam shortcut.
    pub direct: bool,
    /// `--debug`: start the game under lldb-server, stopped until a debugger connects through the run step's tunnels.
    pub debug: bool,
    /// This gram, which builds for a Windows device here (`std::env::current_exe`).
    pub local_gram: String,
}

/// The commands deploying to `device`, in order. Started by the user, finished by Claude (Task 0.12).
pub fn plan(device: &Device, options: &DeployOptions) -> Vec<Step> {
    if device.os == DeviceOs::Windows {
        return windows_plan(device, options);
    }

    // 1. The sources, mirrored (--delete) but for what's built or this machine's own. rsync is started without a
    //    shell, so its arguments are passed as they are: quoting them would hand rsync the quotes too.
    let excluded = [".git/", "target/", "build/", "build-*/", ".vscode/settings.json", DEVICES_FILE];
    let mut args = vec!["-az".to_owned(), "--delete".to_owned()];
    for pattern in excluded {
        args.push("--exclude".to_owned());
        args.push(pattern.to_owned());
    }
    args.push("./".to_owned());
    args.push(format!("{}:{}/", device.host, device.dir));
    let mut steps = vec![Step { what: format!("copy the sources to {}", device.name), program: "rsync".to_owned(), args }];

    // 2. The project's own gram first, when it has one: the device's could be older than this tree's Gram.ron.
    let gram = match &options.gram_manifest {
        Some(manifest) => {
            steps.push(ssh(device, false, format!("build gram on {}", device.name),
                &format!("cargo build --manifest-path {}", quote(manifest))));
            // cargo's default target directory, next to the manifest: gram/Cargo.toml → gram/target/debug/gram.
            quote(&Path::new(manifest).with_file_name("target/debug/gram").to_string_lossy())
        }
        None => "gram".to_owned(),
    };

    // 3. The build.
    let mut build = format!("{gram} build --config {}", options.config);
    if let Some(module) = &options.module {
        build += &format!(" --module {}", quote(module));
    }
    steps.push(ssh(device, false, format!("build {} on {}", options.config, device.name), &build));

    let executable = Path::new(&options.executable);
    let name = executable.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
    let dir = executable.parent().map(|dir| dir.to_string_lossy().into_owned()).unwrap_or_default();

    // 4. Steam's launcher and what it runs, at every deploy: a Non-Steam Game shortcut to ~/gram-launch.sh then
    //    always starts the last build, in the configuration deployed. Both files go through printf %s, their
    //    text quoted once for the device's shell. ~/.gram-launch is sourced by the launcher: $HOME and the
    //    environment file's ~ expand there.
    let launch = format!(
        "GRAM_DIR=$HOME/{}\nGRAM_EXECUTABLE={}\nGRAM_CONTAINER={}\nGRAM_ENV={}\nGRAM_DEBUG={}\n",
        quote(&format!("{}/{dir}", device.dir)),
        quote(&name),
        quote(device.container.as_deref().unwrap_or("")),
        device.env.as_deref().unwrap_or(""),
        if options.debug { DEBUG_GDB_PORT.to_string() } else { String::new() },
    );
    steps.push(Step {
        what: format!("install the Steam launcher on {}", device.name),
        program: "ssh".to_owned(),
        args: vec![device.host.clone(), format!(
            "printf %s {} > ~/gram-launch.sh && chmod +x ~/gram-launch.sh && printf %s {} > ~/.gram-launch",
            quote(LAUNCHER), quote(&launch),
        )],
    });

    if !options.run {
        return steps;
    }

    // 5. The game, through the device's Steam shortcut when it has one (Game Mode only shows what Steam starts),
    //    else straight over ssh. Both end the same way: the game's output here until it closes, and Ctrl+C (through
    //    ssh -t's terminal) closing it.
    //
    //    With --debug the game starts under lldb-server gdbserver (the launcher does it from GRAM_DEBUG), next to an
    //    lldb-server platform for the files, both listening only on the device's localhost: the run step's ssh
    //    carries the two ports here (-L), for as long as it runs. The platform is stopped when the step ends.
    let mut tunnels = Vec::new();
    let mut platform_start = String::new();
    let mut platform_stop = String::new();
    if options.debug {
        tunnels = vec!["-o".to_owned(), "ExitOnForwardFailure=yes".to_owned()];
        for port in [DEBUG_PLATFORM_PORT, DEBUG_GDB_PORT] {
            tunnels.push("-L".to_owned());
            tunnels.push(format!("{port}:localhost:{port}"));
        }
        // pkill -x: by process name. Not -f (the whole command line): the script itself contains "lldb-server
        // platform" and would kill itself first. Both servers go, the platform and a previous gdbserver.
        // {} is where the Steam script puts what enters the container: it runs on the device itself.
        platform_start = format!(
            "(pkill -x lldb-server || true)\n{{}}lldb-server platform --server --listen localhost:{DEBUG_PLATFORM_PORT} --gdbserver-port {DEBUG_UNUSED_PORT} > /dev/null 2>&1 &\n"
        );
        platform_stop = "pkill -x lldb-server".to_owned();
    }
    let waiting = format!(
        "echo \"gram deploy: {name} waits for the debugger: in VS Code, run Game on device (gram deploy --debug), or lldb -s build/deploy/debug.lldb -o gdb-remote localhost:{DEBUG_GDB_PORT}\"\n",
        name = quote(&name),
    );

    match (device.steam, options.direct) {
        (Some(app_id), false) => {
            // A shortcut's game id, what steam://rungameid takes (and Steam hands the game as SteamGameId).
            let game_id = (u64::from(app_id) << 32) | 0x0200_0000;
            // No ' in it: each one would be quoted again at every layer (bash -c, ssh, a printed command line).
            // The script runs on the device itself (steam is the device's), the platform inside the container.
            let (platform_start, waiting) = if options.debug {
                (platform_start.replace("{}", &in_container(device)), waiting.clone())
            } else {
                (String::new(), String::new())
            };
            let script = format!(
r#"(pkill -x {name} && sleep 1) || true
: > ~/gram-launch.log
{platform_start}steam steam://rungameid/{game_id} > /dev/null 2>&1 &
for _ in $(seq 60); do pid=$(pgrep -n -x {name}) && break; sleep 0.5; done
if [ -z "$pid" ]; then echo "gram deploy: Steam did not start {name}: is steam: Some({app_id}) the id of the shortcut?"; cat ~/gram-launch.log; exit 1; fi
trap "kill $pid 2> /dev/null" INT HUP TERM
{waiting}tail -n +1 -f --pid="$pid" ~/gram-launch.log{stop}"#,
                name = quote(&name),
                stop = if options.debug { format!("\n{platform_stop}") } else { String::new() },
            );
            let mut args = vec!["-t".to_owned()];
            args.extend(tunnels);
            args.push(device.host.clone());
            args.push(format!("bash -c {}", quote(&script)));
            let what = if options.debug { "debug" } else { "run" };
            steps.push(Step {
                what: format!("{what} {} on {} through Steam", options.executable, device.name),
                program: "ssh".to_owned(),
                args,
            });
        }
        _ => {
            // The values are a SteamOS Desktop Mode session's (the first user, uid 1000); its X authority file gets
            // a new name at every login, so the device looks it up. pkill first: one game at a time (it fails when
            // none runs, hence || true). exec: the game replaces the shell, so closing ssh closes the game.
            let session = r#"DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 XAUTHORITY="$(ls /run/user/1000/xauth_* | head -n 1)""#;
            let game = quote(&format!("./{name}"));
            if options.debug {
                // No exec: the shell stays, so its trap stops the platform and the game however the step ends.
                let platform_start = platform_start.replace("{}", ""); // already inside the container
                let run = format!(
                    "(pkill -x {n} || true) && cd {d} || exit 1\n{platform_start}trap \"pkill -x {n}; {platform_stop}\" EXIT\ntrap exit HUP INT TERM\n{waiting}env {session} lldb-server gdbserver localhost:{DEBUG_GDB_PORT} -- {game}",
                    n = quote(&name), d = quote(&dir),
                );
                steps.push(ssh_with(device, &tunnels, format!("debug {} on {}", options.executable, device.name), &run));
            } else {
                let run = format!("(pkill -x {} || true) && cd {} && exec env {session} {game}", quote(&name), quote(&dir));
                steps.push(ssh(device, true, format!("run {} on {}", options.executable, device.name), &run));
            }
        }
    }

    steps
}

/// A Windows device: built here, copied, started on its desktop. There's no shell on the device to quote for: every
/// script goes as `powershell -EncodedCommand`, its text in base64, one word nothing can split or expand.
fn windows_plan(device: &Device, options: &DeployOptions) -> Vec<Step> {
    let executable = Path::new(&options.executable);
    let file = executable.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
    let process = file.strip_suffix(".exe").unwrap_or(&file).to_owned(); // what Get-Process calls it
    let local_dir = executable.parent().map(|dir| dir.to_string_lossy().into_owned()).unwrap_or_default();
    // One folder per configuration under the device's folder, relative to the user's profile (ssh starts there).
    let remote_dir = format!("{}/{}", device.dir, options.config);

    // 1. The build, here: mingw cross-compiles Windows from anywhere.
    let mut args = vec!["build".to_owned(), "--target".to_owned(), "windows".to_owned(), "--config".to_owned(), options.config.to_string()];
    if let Some(module) = &options.module {
        args.push("--module".to_owned());
        args.push(module.clone());
    }
    let mut steps = vec![Step { what: format!("build {} for windows here", options.config), program: options.local_gram.clone(), args }];

    // 2. An empty folder there: what was deleted here goes there too. The game first, or its files are locked.
    let prepare = format!(
        "$ProgressPreference = 'SilentlyContinue'\n\
         Get-Process -Name {process} -ErrorAction SilentlyContinue | Stop-Process -Force\n\
         Start-Sleep -Milliseconds 500\n\
         $dir = Join-Path $env:USERPROFILE {dir}\n\
         if (Test-Path $dir) {{ Remove-Item -Recurse -Force $dir }}\n\
         New-Item -ItemType Directory -Force -Path $dir | Out-Null",
        process = ps_quote(&process), dir = ps_quote(&remote_dir),
    );
    steps.push(Step {
        what: format!("prepare {remote_dir} on {}", device.name),
        program: "ssh".to_owned(),
        args: vec![device.host.clone(), powershell(&prepare)],
    });

    // 3. The build's folder, through tar on both ends (Windows 10+ has tar.exe): unlike scp it can leave out gram's
    //    own executable and the static libraries. A pipe needs a shell here: sh -c. COPYFILE_DISABLE keeps macOS's
    //    tar from adding its ._ metadata files.
    let copy = format!(
        "COPYFILE_DISABLE=1 tar -C {} --exclude gram.exe --exclude {} --exclude {} --exclude {} -cf - . | ssh {} {}",
        quote(&local_dir), quote("*.a"), quote("*.lib"), quote("*.partial"),
        quote(&device.host), quote(&format!("tar -xf - -C {}", ps_quote(&remote_dir))),
    );
    steps.push(Step {
        what: format!("copy the build to {}", device.name),
        program: "sh".to_owned(),
        args: vec!["-c".to_owned(), copy],
    });

    if !options.run {
        return steps;
    }

    // 4. The game, on the desktop: a program started over ssh runs in session 0, with no desktop, so a scheduled task
    //    starts it in the logged-on user's session (Interactive). cmd's redirection catches its output in a log (a
    //    Final build has no console), followed here until the game exits; Ctrl+C (through ssh -t) closes it. cmd
    //    runs in conhost --headless: a console without a window, or cmd's own would show behind the game.
    steps.push(Step {
        what: format!("run {} on {}", options.executable, device.name),
        program: "ssh".to_owned(),
        args: vec!["-t".to_owned(), device.host.clone(), powershell(&windows_run_script(&remote_dir, &device.dir, &file, &process))],
    });
    steps
}

/// The PowerShell script starting `file` from `remote_dir` through the `gram-launch` scheduled task and following its
/// log (`<device dir>/gram-launch.log`) until it exits.
fn windows_run_script(remote_dir: &str, device_dir: &str, file: &str, process: &str) -> String {
    format!(
        "$ProgressPreference = 'SilentlyContinue'\n\
         $dir = Join-Path $env:USERPROFILE {dir}\n\
         $log = Join-Path $env:USERPROFILE {log}\n\
         Set-Content -Path $log -Value $null\n\
         $action = New-ScheduledTaskAction -Execute 'conhost.exe' -Argument ('--headless cmd.exe /c ' + {file} + ' > \"' + $log + '\" 2>&1') -WorkingDirectory $dir\n\
         $principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited\n\
         $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Seconds 0)\n\
         Register-ScheduledTask -TaskName 'gram-launch' -Action $action -Principal $principal -Settings $settings -Force | Out-Null\n\
         Start-ScheduledTask -TaskName 'gram-launch'\n\
         $game = $null\n\
         for ($i = 0; $i -lt 60 -and -not $game; $i++) {{ Start-Sleep -Milliseconds 500; $game = Get-Process -Name {process} -ErrorAction SilentlyContinue | Select-Object -First 1 }}\n\
         if (-not $game) {{ 'gram deploy: the scheduled task did not start ' + {file}; Get-Content $log; exit 1 }}\n\
         $reader = New-Object System.IO.StreamReader([System.IO.File]::Open($log, 'Open', 'Read', 'ReadWrite'))\n\
         try {{\n\
         \x20   while (-not $game.HasExited) {{ $line = $reader.ReadLine(); if ($null -ne $line) {{ $line }} else {{ Start-Sleep -Milliseconds 200 }} }}\n\
         \x20   while ($null -ne ($line = $reader.ReadLine())) {{ $line }}\n\
         }} finally {{\n\
         \x20   if (-not $game.HasExited) {{ Stop-Process -Id $game.Id -Force }}\n\
         \x20   $reader.Close()\n\
         }}",
        dir = ps_quote(remote_dir),
        log = ps_quote(&format!("{device_dir}/gram-launch.log")),
        file = ps_quote(file),
        process = ps_quote(process),
    )
}

/// `arg` as a PowerShell string literal: inside '...' nothing is expanded, and a ' is written twice.
pub fn ps_quote(arg: &str) -> String {
    format!("'{}'", arg.replace('\'', "''"))
}

/// The command line running `script` in Windows PowerShell: its text as UTF-16LE, in base64 (`-EncodedCommand`).
pub fn powershell(script: &str) -> String {
    let bytes: Vec<u8> = script.encode_utf16().flat_map(u16::to_le_bytes).collect();
    format!("powershell -NoProfile -NonInteractive -EncodedCommand {}", base64(&bytes))
}

/// Standard base64 with padding (RFC 4648), all `-EncodedCommand` needs.
fn base64(bytes: &[u8]) -> String {
    const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut text = String::with_capacity(bytes.len().div_ceil(3) * 4);
    for chunk in bytes.chunks(3) {
        let group = (u32::from(chunk[0]) << 16)
            | (u32::from(chunk.get(1).copied().unwrap_or(0)) << 8)
            | u32::from(chunk.get(2).copied().unwrap_or(0));
        for position in 0..4 {
            if position <= chunk.len() {
                text.push(ALPHABET[((group >> (18 - 6 * position)) & 0x3f) as usize] as char);
            } else {
                text.push('=');
            }
        }
    }
    text
}

/// A step running `command` on the device, inside the project's directory.
fn ssh(device: &Device, terminal: bool, what: String, command: &str) -> Step {
    let mut args = Vec::new();
    if terminal {
        args.push("-t".to_owned()); // a terminal on the device: Ctrl+C reaches the game, not just ssh
    }
    args.push(device.host.clone());
    args.push(remote(device, command));
    Step { what, program: "ssh".to_owned(), args }
}

/// Like `ssh` with a terminal, plus `extra` ssh options before the host (--debug's tunnels).
fn ssh_with(device: &Device, extra: &[String], what: String, command: &str) -> Step {
    let mut step = ssh(device, true, what, command);
    step.args.splice(1..1, extra.iter().cloned());
    step
}

/// What runs a command inside the device's container, for a script running on the device itself.
fn in_container(device: &Device) -> String {
    match &device.container {
        Some(container) => format!("distrobox enter {} -- ", quote(container)),
        None => String::new(),
    }
}

/// The lldb commands `gram deploy --debug` writes to `build/deploy/debug.lldb`: the device as lldb's platform (its
/// files read from there, libraries included), the executable by its path there, and the device's source paths
/// mapped onto this machine's tree (the same sources, deploy synced them). `home` is the device user's home,
/// `local_root` this project's absolute path. VS Code's "Game on device" sources it, then connects (`gdb-remote`).
pub fn lldb_commands(device: &Device, home: &str, executable: &str, local_root: &str) -> String {
    let remote_root = format!("{home}/{}", device.dir);
    format!(
        "# Written by gram deploy --debug for {name}; VS Code's Game on device (launch.json) sources it.\n\
         platform select remote-linux\n\
         platform connect connect://localhost:{DEBUG_PLATFORM_PORT}\n\
         target create \"{remote_root}/{executable}\"\n\
         settings set target.source-map \"{remote_root}\" \"{local_root}\"\n",
        name = device.name,
    )
}

/// `command` wrapped in the layers between ssh and the project: the device's login shell runs this text, `bash -lc`
/// runs the quoted script (inside the distrobox, when there's one), the script sources the environment and goes to
/// the project. Each layer reads one level of quotes away, so each inner layer is quoted once, here.
fn remote(device: &Device, command: &str) -> String {
    let mut script = String::new();
    if let Some(env) = &device.env {
        script += &format!("source {env} && "); // as written: ~ must stay expandable by the device's shell
    }
    script += &format!("cd ~/{} && {command}", quote(&device.dir));

    let shell = format!("bash -lc {}", quote(&script));
    match &device.container {
        Some(container) => format!("distrobox enter {} -- {shell}", quote(container)),
        None => shell,
    }
}

/// `arg` quoted for a POSIX shell, so the shell reads it back as exactly one word, unchanged. Started by the user
/// (with its tests), finished by Claude (Task 0.12).
pub fn quote(arg: &str) -> String {
    // Letters, digits and these mean nothing to a shell: left bare, so printed commands stay readable.
    let safe = |c: char| c.is_ascii_alphanumeric() || "_-./:=@,+%".contains(c);
    if !arg.is_empty() && arg.chars().all(safe) {
        return arg.to_owned();
    }

    // Inside '...' a shell takes everything literally, but a ' can't appear there: close the quotes, add an
    // escaped \', open them again. it's → 'it'\''s'.
    format!("'{}'", arg.replace('\'', r"'\''"))
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn quote_string_arguments() {
        assert_eq!(quote("legion"), "legion");
        assert_eq!(quote("build-*/"), "'build-*/'");
        assert_eq!(quote("a b"), "'a b'");
        assert_eq!(quote("~/.gram-env"), "'~/.gram-env'");
        assert_eq!(quote("it's"), r"'it'\''s'"); // raw: in a plain Rust string \' is just '
        assert_eq!(quote(""), "''");
    }

    fn legion() -> Device {
        Device {
            name: "legion".into(),
            host: "legion".into(),
            dir: "Projects/gram-engine".into(),
            container: Some("gram".into()),
            env: Some("~/.gram-env".into()),
            os: DeviceOs::Linux,
            steam: None,
        }
    }

    fn gram_engine() -> DeployOptions {
        DeployOptions {
            config: Profile::Debug,
            module: None,
            executable: "build/gram-engine/linux/debug/Game".into(),
            run: true,
            gram_manifest: Some("gram/Cargo.toml".into()),
            direct: false,
            debug: false,
            local_gram: "gram".into(),
        }
    }

    fn args(step: &Step) -> Vec<&str> {
        step.args.iter().map(String::as_str).collect()
    }

    #[test]
    fn gram_engine_on_the_legion_go() {
        let steps = plan(&legion(), &gram_engine());
        assert_eq!(steps.len(), 5);

        assert_eq!(steps[0].what, "copy the sources to legion");
        assert_eq!(steps[0].program, "rsync");
        assert_eq!(args(&steps[0]), [
            "-az", "--delete",
            "--exclude", ".git/", "--exclude", "target/", "--exclude", "build/", "--exclude", "build-*/",
            "--exclude", ".vscode/settings.json", "--exclude", "Devices.ron",
            "./", "legion:Projects/gram-engine/",
        ]);

        assert_eq!(steps[1].what, "build gram on legion");
        assert_eq!(steps[1].program, "ssh");
        assert_eq!(args(&steps[1]), [
            "legion",
            "distrobox enter gram -- bash -lc 'source ~/.gram-env && cd ~/Projects/gram-engine && cargo build --manifest-path gram/Cargo.toml'",
        ]);

        assert_eq!(steps[2].what, "build debug on legion");
        assert_eq!(args(&steps[2]), [
            "legion",
            "distrobox enter gram -- bash -lc 'source ~/.gram-env && cd ~/Projects/gram-engine && gram/target/debug/gram build --config debug'",
        ]);

        assert_eq!(steps[3].what, "install the Steam launcher on legion");
        assert_eq!(args(&steps[3]), [
            "legion".to_owned(),
            format!(
                "printf %s {} > ~/gram-launch.sh && chmod +x ~/gram-launch.sh && printf %s {} > ~/.gram-launch",
                quote(LAUNCHER),
                "'GRAM_DIR=$HOME/Projects/gram-engine/build/gram-engine/linux/debug\nGRAM_EXECUTABLE=Game\nGRAM_CONTAINER=gram\nGRAM_ENV=~/.gram-env\nGRAM_DEBUG=\n'",
            ),
        ]);

        assert_eq!(steps[4].what, "run build/gram-engine/linux/debug/Game on legion");
        assert_eq!(args(&steps[4]), [
            "-t",
            "legion",
            r#"distrobox enter gram -- bash -lc 'source ~/.gram-env && cd ~/Projects/gram-engine && (pkill -x Game || true) && cd build/gram-engine/linux/debug && exec env DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 XAUTHORITY="$(ls /run/user/1000/xauth_* | head -n 1)" ./Game'"#,
        ]);
    }

    #[test]
    fn a_plain_linux_box_without_container_env_or_run() {
        let device = Device { name: "box".into(), host: "me@10.0.0.2".into(), dir: "gram-engine".into(), os: DeviceOs::Linux, container: None, env: None, steam: None };
        // The executable comes from the command (main_executable_path), the debug one here whatever the config.
        let options = DeployOptions { config: Profile::Release, module: Some("Game".into()), run: false, gram_manifest: None, ..gram_engine() };

        let steps = plan(&device, &options);

        assert_eq!(steps.len(), 3); // no gram of its own to build, nothing to run
        assert_eq!(args(&steps[0]).last(), Some(&"me@10.0.0.2:gram-engine/"));
        assert_eq!(steps[1].what, "build release on box");
        assert_eq!(args(&steps[1]), ["me@10.0.0.2", "bash -lc 'cd ~/gram-engine && gram build --config release --module Game'"]);
        // The launcher is installed anyway, empty container and environment: it then runs the game straight.
        assert!(args(&steps[2])[1].ends_with(
            "printf %s 'GRAM_DIR=$HOME/gram-engine/build/gram-engine/linux/debug\nGRAM_EXECUTABLE=Game\nGRAM_CONTAINER='\\'''\\''\nGRAM_ENV=\nGRAM_DEBUG=\n' > ~/.gram-launch"
        ), "{}", args(&steps[2])[1]);
    }

    /// A space in the device's folder: quoted once for the script, and that quoting quoted again for bash -lc.
    #[test]
    fn a_folder_with_a_space_is_quoted_at_both_layers() {
        let device = Device { dir: "My Games/gram".into(), container: None, env: None, ..legion() };
        let options = DeployOptions { run: false, gram_manifest: None, ..gram_engine() };

        let steps = plan(&device, &options);

        assert_eq!(args(&steps[0]).last(), Some(&"legion:My Games/gram/")); // rsync: no shell, no quotes
        assert_eq!(args(&steps[1])[1], r"bash -lc 'cd ~/'\''My Games/gram'\'' && gram build --config debug'");
    }

    #[test]
    fn a_command_line_is_ready_to_paste() {
        let steps = plan(&legion(), &gram_engine());
        assert_eq!(
            steps[2].command_line(),
            r"ssh legion 'distrobox enter gram -- bash -lc '\''source ~/.gram-env && cd ~/Projects/gram-engine && gram/target/debug/gram build --config debug'\'''"
        );
    }

    #[test]
    fn a_device_with_a_steam_shortcut_runs_through_steam() {
        let device = Device { steam: Some(2661953691), ..legion() };

        let steps = plan(&device, &gram_engine());

        let run = steps.last().unwrap();
        assert_eq!(run.what, "run build/gram-engine/linux/debug/Game on legion through Steam");
        assert_eq!(args(run)[..2], ["-t", "legion"]);
        let script = r#"(pkill -x Game && sleep 1) || true
: > ~/gram-launch.log
steam steam://rungameid/11433004046345043968 > /dev/null 2>&1 &
for _ in $(seq 60); do pid=$(pgrep -n -x Game) && break; sleep 0.5; done
if [ -z "$pid" ]; then echo "gram deploy: Steam did not start Game: is steam: Some(2661953691) the id of the shortcut?"; cat ~/gram-launch.log; exit 1; fi
trap "kill $pid 2> /dev/null" INT HUP TERM
tail -n +1 -f --pid="$pid" ~/gram-launch.log"#;
        assert_eq!(args(run)[2], format!("bash -c {}", quote(script)));
    }

    #[test]
    fn direct_skips_the_steam_shortcut() {
        let device = Device { steam: Some(2661953691), ..legion() };
        let options = DeployOptions { direct: true, ..gram_engine() };

        let steps = plan(&device, &options);

        assert_eq!(steps.last().unwrap().what, "run build/gram-engine/linux/debug/Game on legion");
    }

    #[test]
    fn debug_over_ssh_tunnels_both_ports_and_starts_the_game_under_lldb_server() {
        let options = DeployOptions { debug: true, ..gram_engine() };

        let steps = plan(&legion(), &options);

        assert!(args(&steps[3])[1].contains("GRAM_DEBUG=1235\n"), "the launcher learns the port too");
        let run = steps.last().unwrap();
        assert_eq!(run.what, "debug build/gram-engine/linux/debug/Game on legion");
        assert_eq!(args(run)[..7], ["-t", "-o", "ExitOnForwardFailure=yes", "-L", "1234:localhost:1234", "-L", "1235:localhost:1235"]);
        assert_eq!(args(run)[7], "legion");
        let script = format!(
            "(pkill -x Game || true) && cd build/gram-engine/linux/debug || exit 1\n\
             (pkill -x lldb-server || true)\n\
             lldb-server platform --server --listen localhost:1234 --gdbserver-port 1236 > /dev/null 2>&1 &\n\
             trap \"pkill -x Game; pkill -x lldb-server\" EXIT\n\
             trap exit HUP INT TERM\n\
             echo \"gram deploy: Game waits for the debugger: in VS Code, run Game on device (gram deploy --debug), or lldb -s build/deploy/debug.lldb -o gdb-remote localhost:1235\"\n\
             env {} lldb-server gdbserver localhost:1235 -- ./Game",
            r#"DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 XAUTHORITY="$(ls /run/user/1000/xauth_* | head -n 1)""#,
        );
        assert_eq!(args(run)[8], remote(&legion(), &script));
    }

    #[test]
    fn debug_through_steam_starts_the_platform_in_the_container_and_stops_it_at_the_end() {
        let device = Device { steam: Some(2661953691), ..legion() };
        let options = DeployOptions { debug: true, ..gram_engine() };

        let steps = plan(&device, &options);

        let run = steps.last().unwrap();
        assert_eq!(run.what, "debug build/gram-engine/linux/debug/Game on legion through Steam");
        assert_eq!(args(run)[..7], ["-t", "-o", "ExitOnForwardFailure=yes", "-L", "1234:localhost:1234", "-L", "1235:localhost:1235"]);
        let script = r#"(pkill -x Game && sleep 1) || true
: > ~/gram-launch.log
(pkill -x lldb-server || true)
distrobox enter gram -- lldb-server platform --server --listen localhost:1234 --gdbserver-port 1236 > /dev/null 2>&1 &
steam steam://rungameid/11433004046345043968 > /dev/null 2>&1 &
for _ in $(seq 60); do pid=$(pgrep -n -x Game) && break; sleep 0.5; done
if [ -z "$pid" ]; then echo "gram deploy: Steam did not start Game: is steam: Some(2661953691) the id of the shortcut?"; cat ~/gram-launch.log; exit 1; fi
trap "kill $pid 2> /dev/null" INT HUP TERM
echo "gram deploy: Game waits for the debugger: in VS Code, run Game on device (gram deploy --debug), or lldb -s build/deploy/debug.lldb -o gdb-remote localhost:1235"
tail -n +1 -f --pid="$pid" ~/gram-launch.log
pkill -x lldb-server"#;
        assert_eq!(args(run)[8], format!("bash -c {}", quote(script)));
    }

    #[test]
    fn the_lldb_commands_map_the_device_onto_this_tree() {
        assert_eq!(
            lldb_commands(&legion(), "/home/deck", "build/gram-engine/linux/debug/Game", "/Users/me/gram-engine"),
            "# Written by gram deploy --debug for legion; VS Code's Game on device (launch.json) sources it.\n\
             platform select remote-linux\n\
             platform connect connect://localhost:1234\n\
             target create \"/home/deck/Projects/gram-engine/build/gram-engine/linux/debug/Game\"\n\
             settings set target.source-map \"/home/deck/Projects/gram-engine\" \"/Users/me/gram-engine\"\n"
        );
    }

    #[test]
    fn base64_matches_rfc_4648() {
        assert_eq!(base64(b""), "");
        assert_eq!(base64(b"M"), "TQ==");
        assert_eq!(base64(b"Ma"), "TWE=");
        assert_eq!(base64(b"Man"), "TWFu");
        assert_eq!(base64(b"foobar"), "Zm9vYmFy");
    }

    #[test]
    fn powershell_encodes_utf16_little_endian() {
        // d i r → 64 00 69 00 72 00
        assert_eq!(powershell("dir"), "powershell -NoProfile -NonInteractive -EncodedCommand ZABpAHIA");
    }

    #[test]
    fn ps_quote_doubles_a_single_quote() {
        assert_eq!(ps_quote("gram-engine/release"), "'gram-engine/release'");
        assert_eq!(ps_quote("it's"), "'it''s'");
    }

    fn laptop() -> Device {
        Device { name: "laptop".into(), host: "laptop".into(), dir: "gram-engine".into(), os: DeviceOs::Windows, container: None, env: None, steam: None }
    }

    fn windows_release() -> DeployOptions {
        DeployOptions {
            config: Profile::Release,
            executable: "build/gram-engine/windows/release/Game.exe".into(),
            local_gram: "/repo/gram/target/debug/gram".into(),
            ..gram_engine()
        }
    }

    #[test]
    fn a_windows_device_is_built_here_copied_and_started_through_a_scheduled_task() {
        let steps = plan(&laptop(), &windows_release());
        assert_eq!(steps.len(), 4);

        assert_eq!(steps[0].what, "build release for windows here");
        assert_eq!(steps[0].program, "/repo/gram/target/debug/gram");
        assert_eq!(args(&steps[0]), ["build", "--target", "windows", "--config", "release"]);

        assert_eq!(steps[1].what, "prepare gram-engine/release on laptop");
        assert_eq!(args(&steps[1]), ["laptop".to_owned(), powershell(
            "$ProgressPreference = 'SilentlyContinue'\n\
             Get-Process -Name 'Game' -ErrorAction SilentlyContinue | Stop-Process -Force\n\
             Start-Sleep -Milliseconds 500\n\
             $dir = Join-Path $env:USERPROFILE 'gram-engine/release'\n\
             if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }\n\
             New-Item -ItemType Directory -Force -Path $dir | Out-Null",
        )]);

        assert_eq!(steps[2].what, "copy the build to laptop");
        assert_eq!(steps[2].program, "sh");
        assert_eq!(args(&steps[2]), [
            "-c",
            r#"COPYFILE_DISABLE=1 tar -C build/gram-engine/windows/release --exclude gram.exe --exclude '*.a' --exclude '*.lib' --exclude '*.partial' -cf - . | ssh laptop 'tar -xf - -C '\''gram-engine/release'\'''"#,
        ]);

        assert_eq!(steps[3].what, "run build/gram-engine/windows/release/Game.exe on laptop");
        assert_eq!(args(&steps[3])[..2], ["-t", "laptop"]);
        assert_eq!(args(&steps[3])[2], powershell(&windows_run_script("gram-engine/release", "gram-engine", "Game.exe", "Game")));
    }

    #[test]
    fn the_windows_run_script_starts_the_game_from_its_folder_and_logs_it() {
        let script = windows_run_script("gram-engine/release", "gram-engine", "Game.exe", "Game");
        assert!(script.contains("$dir = Join-Path $env:USERPROFILE 'gram-engine/release'\n"), "{script}");
        assert!(script.contains("$log = Join-Path $env:USERPROFILE 'gram-engine/gram-launch.log'\n"), "{script}");
        assert!(script.contains(r#"-Execute 'conhost.exe' -Argument ('--headless cmd.exe /c ' + 'Game.exe' + ' > "' + $log + '" 2>&1') -WorkingDirectory $dir"#), "{script}");
        assert!(script.contains("-LogonType Interactive"), "{script}");
        assert!(script.contains("Get-Process -Name 'Game'"), "{script}");
    }

    #[test]
    fn a_windows_device_without_run_stops_after_the_copy() {
        let options = DeployOptions { run: false, module: Some("Runtime".into()), ..windows_release() };

        let steps = plan(&laptop(), &options);

        assert_eq!(steps.len(), 3);
        assert_eq!(args(&steps[0])[5..], ["--module", "Runtime"]);
    }
}