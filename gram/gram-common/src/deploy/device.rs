//! `Devices.ron`: the devices `gram deploy` can reach from this machine. It sits next to the root `Gram.ron` and
//! is git-ignored: SSH aliases, addresses and folders belong to one machine, not to the project.
//!
//! ```ron
//! Devices(
//!     default: Some("legion"),
//!     devices: [
//!         Device(
//!             name: "legion",
//!             host: "legion",
//!             dir: "Projects/gram-engine",
//!             container: Some("gram"),
//!             env: Some("~/.gram-env"),
//!             steam: Some(2661953691),
//!         ),
//!         Device(name: "laptop", host: "laptop", dir: "gram-engine", os: Windows),
//!     ],
//! )
//! ```

use std::fs;
use std::io;
use std::path::Path;

use serde::Deserialize;

pub const DEVICES_FILE: &str = "Devices.ron";

/// What a device runs, which decides how gram deploys to it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Deserialize)]
pub enum DeviceOs {
    /// Built on the device from the synced sources (macOS can't build Linux), in `container` when there's one.
    #[default]
    Linux,
    /// Built here (mingw cross-compiles Windows from anywhere) and copied: the device needs no build tools, only
    /// OpenSSH Server and a Vulkan driver. Started on the desktop through a scheduled task (a program started over
    /// ssh runs in session 0, without one). `container`, `env` and `steam` don't apply.
    Windows,
}

impl DeviceOs {
    /// The `gram build` target for it.
    pub fn target(self) -> &'static str {
        match self {
            DeviceOs::Linux => "linux",
            DeviceOs::Windows => "windows",
        }
    }
}

/// One machine `gram deploy` builds and runs on.
#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
pub struct Device {
    /// What `--device` takes.
    pub name: String,
    /// The SSH destination: an alias from `~/.ssh/config` (`legion`) or `user@address`.
    pub host: String,
    /// Where the project lives on the device, relative to the device user's home (`Projects/gram-engine`). On
    /// Windows, where the builds go, one folder per configuration (`gram-engine` → `%USERPROFILE%\gram-engine\release`).
    pub dir: String,
    /// `Linux` (the default) or `Windows`.
    #[serde(default)]
    pub os: DeviceOs,
    /// A distrobox to build and run in (SteamOS's system is read-only). `None`: straight on the device.
    #[serde(default)]
    pub container: Option<String>,
    /// A shell file sourced before building and running: `VCPKG_ROOT`, the Vulkan SDK, cargo's `env`.
    #[serde(default)]
    pub env: Option<String>,
    /// The Non-Steam Game shortcut pointing at `~/gram-launch.sh` (which every deploy installs): its app id, the
    /// `SteamAppId` the launcher logs in `~/gram-launch.log` once started from the library. With it, the game is
    /// started through Steam, which Game Mode needs (`--direct` still starts it over ssh). `None`: over ssh.
    #[serde(default)]
    pub steam: Option<u32>,
}

#[derive(Debug, Deserialize)]
pub struct Devices {
    /// The device used when `--device` is omitted. Without one, the only device listed, if there's one.
    #[serde(default)]
    pub default: Option<String>,
    pub devices: Vec<Device>,
}

impl Devices {
    /// Reads `<root>/Devices.ron`; a missing file is an error saying what to write in it.
    pub fn open(root: &Path) -> io::Result<Self> {
        let path = root.join(DEVICES_FILE);
        let text = fs::read_to_string(&path).map_err(|err| match err.kind() {
            io::ErrorKind::NotFound => io::Error::new(io::ErrorKind::NotFound, format!(
                "no {DEVICES_FILE} next to Gram.ron: list the devices gram deploy can reach there (it's git-ignored), e.g.\n\n\
                 Devices(\n    devices: [\n        Device(name: \"legion\", host: \"legion\", dir: \"Projects/gram-engine\", container: Some(\"gram\"), env: Some(\"~/.gram-env\")),\n    ],\n)"
            )),
            _ => io::Error::new(err.kind(), format!("couldn't read {}: {err}", path.display())),
        })?;
        Self::parse(&text).map_err(|err| io::Error::new(io::ErrorKind::InvalidData, format!("{}: {err}", path.display())))
    }

    pub fn parse(text: &str) -> Result<Self, ron::error::SpannedError> {
        ron::from_str(text)
    }

    /// `--device`'s device, else the default one, else the only one.
    pub fn find(&self, name: Option<&str>) -> io::Result<&Device> {
        let names = || self.devices.iter().map(|device| device.name.as_str()).collect::<Vec<_>>().join(", ");
        let wanted = match name.or(self.default.as_deref()) {
            Some(wanted) => wanted,
            None => match self.devices.as_slice() {
                [only] => return Ok(only),
                [] => return Err(io::Error::new(io::ErrorKind::NotFound, format!("{DEVICES_FILE} lists no device"))),
                _ => return Err(io::Error::new(io::ErrorKind::InvalidInput, format!(
                    "more than one device in {DEVICES_FILE} and no default: pass --device ({})", names()
                ))),
            },
        };
        self.devices.iter().find(|device| device.name == wanted).ok_or_else(|| io::Error::new(
            io::ErrorKind::NotFound,
            format!("no device named {wanted} in {DEVICES_FILE} (known: {})", names()),
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TWO: &str = r#"
        Devices(
            default: Some("legion"),
            devices: [
                Device(name: "legion", host: "legion", dir: "Projects/gram-engine", container: Some("gram"), env: Some("~/.gram-env")),
                Device(name: "box", host: "me@10.0.0.2", dir: "gram-engine"),
            ],
        )
    "#;

    #[test]
    fn parses_optional_fields_as_none() {
        let devices = Devices::parse(TWO).unwrap();
        assert_eq!(devices.devices[1], Device {
            name: "box".into(), host: "me@10.0.0.2".into(), dir: "gram-engine".into(), os: DeviceOs::Linux, container: None, env: None, steam: None,
        });
    }

    #[test]
    fn finds_by_name_then_default_then_the_only_one() {
        let devices = Devices::parse(TWO).unwrap();
        assert_eq!(devices.find(Some("box")).unwrap().name, "box");
        assert_eq!(devices.find(None).unwrap().name, "legion");

        let one = Devices::parse(r#"Devices(devices: [Device(name: "box", host: "box", dir: "g")])"#).unwrap();
        assert_eq!(one.find(None).unwrap().name, "box");
    }

    #[test]
    fn unknown_or_ambiguous_device_names_the_known_ones() {
        let devices = Devices::parse(TWO).unwrap();
        let err = devices.find(Some("deck")).unwrap_err().to_string();
        assert!(err.contains("deck") && err.contains("legion, box"), "{err}");

        let ambiguous = Devices::parse(r#"Devices(devices: [Device(name: "a", host: "a", dir: "g"), Device(name: "b", host: "b", dir: "g")])"#).unwrap();
        let err = ambiguous.find(None).unwrap_err().to_string();
        assert!(err.contains("--device") && err.contains("a, b"), "{err}");
    }
}
