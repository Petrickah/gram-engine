#include "Gram/Common.hpp"
#include "Gram/Platform.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <dlfcn.h>
    #include <mach-o/dyld.h>
    #include <spawn.h>
    #include <sys/wait.h>
    #include <unistd.h>
    extern char** environ;
#else
    #include <dlfcn.h>
    #include <spawn.h>
    #include <sys/wait.h>
    #include <unistd.h>
    extern char** environ;
#endif

#include <fstream>

GRAM_NAMESPACE_BEGIN

std::filesystem::path GetExecutableDirectory(const char* argv0)
{
    std::filesystem::path executable;

#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) break;

        if (length < buffer.size())
        {
            buffer.resize(length);
            executable = buffer;
            break;
        }

        buffer.resize(buffer.size() * 2); // truncated: retry with a bigger buffer
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // fails by design and reports the needed size
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0)
    {
        executable = std::filesystem::weakly_canonical(buffer.c_str());
    }
#else
    std::error_code error;
    executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) executable.clear();
#endif

    if (executable.empty() && argv0) executable = argv0;

    return executable.parent_path();
}

std::string GetDynamicLibraryFileName(std::string_view name)
{
#if defined(_WIN32)
    return std::string(name) + ".dll";
#elif defined(__APPLE__)
    return "lib" + std::string(name) + ".dylib";
#else
    return "lib" + std::string(name) + ".so";
#endif
}

// The loader's last error as text: dlerror() is null once read, FormatMessage needs a buffer.
static std::string LastLoaderError()
{
#if defined(_WIN32)
    const DWORD code = GetLastError();
    char* buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = length != 0 ? std::string(buffer, length) : std::format("error code {}", code);
    LocalFree(buffer);
    return message;
#else
    const char* message = dlerror();
    return message ? message : "unknown error";
#endif
}

DynamicLibrary::DynamicLibrary(const std::filesystem::path& path)
{
#if defined(_WIN32)
    m_Handle = LoadLibraryW(path.c_str());
#else
    m_Handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    GramCheckVoid(m_Handle, "error loading the library {}: {}", path.string(), LastLoaderError());
}

DynamicLibrary::~DynamicLibrary()
{
    if (!m_Handle) return; // never loaded: nothing to unload
#if defined(_WIN32)
    GramCheckVoid(FreeLibrary(static_cast<HMODULE>(m_Handle)), "error unloading the library: {}", LastLoaderError());
#else
    GramCheckVoid(dlclose(m_Handle) == 0, "error unloading the library: {}", LastLoaderError());
#endif
}

void* DynamicLibrary::FindSymbolAddress(const char* name) const
{
    GramCheckNull(m_Handle, "can't find symbol {}: the library isn't loaded", name);
#if defined(_WIN32)
    void* symbol = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_Handle), name));
#else
    void* symbol = dlsym(m_Handle, name);
#endif
    GramCheckNull(symbol, "error finding the symbol {}: {}", name, LastLoaderError());
    return symbol;
}

std::optional<ProcessCommand> ReadProcessCommand(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) return std::nullopt;

    ProcessCommand command;
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;

        const size_t space = line.find(' ');
        const std::string_view key   = std::string_view(line).substr(0, space);
        const std::string_view value = space == std::string::npos ? std::string_view() : std::string_view(line).substr(space + 1);
        if (key == "dir")
        {
            command.Directory = std::filesystem::path(std::u8string(value.begin(), value.end()));
        }
        else if (key == "arg")
        {
            command.Arguments.emplace_back(value);
        }
        else if (key == "env")
        {
            const size_t equals = value.find('=');
            if (equals != std::string_view::npos) command.Environment.emplace_back(value.substr(0, equals), value.substr(equals + 1));
        }
    }

    if (command.Arguments.empty()) return std::nullopt;
    return command;
}

// Hands every complete line of `chunk` to onLine; what's left after the last line break stays in `pending`.
static void SplitLines(std::string& pending, std::string_view chunk, const std::function<void(std::string_view)>& onLine)
{
    pending.append(chunk);
    size_t start = 0;
    for (size_t end; (end = pending.find('\n', start)) != std::string::npos; start = end + 1)
    {
        std::string_view line(pending.data() + start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        onLine(line);
    }
    pending.erase(0, start);
}

#if defined(_WIN32)
static std::wstring Widen(std::string_view text)
{
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

// One argument as CommandLineToArgvW reads it back: quoted when needed, with the backslashes before a
// quote (or before the closing quote) doubled.
static std::wstring QuoteArgument(const std::wstring& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;

    std::wstring quoted = L"\"";
    for (auto it = argument.begin(); ; ++it)
    {
        size_t backslashes = 0;
        while (it != argument.end() && *it == L'\\') { ++it; ++backslashes; }

        if (it == argument.end())
        {
            quoted.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') quoted.append(backslashes * 2 + 1, L'\\');
        else             quoted.append(backslashes, L'\\');
        quoted.push_back(*it);
    }
    quoted.push_back(L'"');
    return quoted;
}

// This process's environment with `overrides` set on top, as CreateProcessW wants it: NAME=value\0...\0\0,
// sorted by name, which Windows compares without case.
static std::wstring EnvironmentBlock(const std::vector<std::pair<std::string, std::string>>& overrides)
{
    auto lower = [](std::wstring text) { std::transform(text.begin(), text.end(), text.begin(), ::towlower); return text; };
    std::map<std::wstring, std::wstring> variables;   // lowercase name → "Name=value"

    if (wchar_t* strings = GetEnvironmentStringsW())
    {
        for (const wchar_t* entry = strings; *entry; entry += wcslen(entry) + 1)
        {
            const std::wstring_view variable(entry);
            const size_t equals = variable.find(L'=', 1);   // "=C:=C:\..." entries start with '='
            if (equals != std::wstring_view::npos) variables[lower(std::wstring(variable.substr(0, equals)))] = variable;
        }
        FreeEnvironmentStringsW(strings);
    }
    for (const auto& [name, value] : overrides)
        variables[lower(Widen(name))] = Widen(name) + L"=" + Widen(value);

    std::wstring block;
    for (const auto& [_, variable] : variables) { block += variable; block.push_back(L'\0'); }
    block.push_back(L'\0');
    return block;
}

int RunProcess(const ProcessCommand& command, const std::function<void(std::string_view)>& onLine)
{
    GramCheck_(!command.Arguments.empty(), return -1, "RunProcess: no program to run");

    // The child writes into the pipe; only its write end is inherited.
    SECURITY_ATTRIBUTES inherit { .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE };
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    GramCheck_(CreatePipe(&readEnd, &writeEnd, &inherit, 0), return -1, "RunProcess: couldn't create a pipe: {}", LastLoaderError());
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    std::wstring commandLine;
    for (const std::string& argument : command.Arguments)
    {
        if (!commandLine.empty()) commandLine.push_back(L' ');
        commandLine += QuoteArgument(Widen(argument));
    }
    std::wstring environment = EnvironmentBlock(command.Environment);
    const std::wstring program   = Widen(command.Arguments.front());
    const std::wstring directory = command.Directory.wstring();

    STARTUPINFOW startup { .cb = sizeof(STARTUPINFOW) };
    startup.dwFlags    = STARTF_USESTDHANDLES;
    startup.hStdInput  = nullptr;
    startup.hStdOutput = writeEnd;
    startup.hStdError  = writeEnd;
    PROCESS_INFORMATION process {};
    const BOOL started = CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(),
        directory.empty() ? nullptr : directory.c_str(), &startup, &process);
    const std::string startError = started ? std::string() : LastLoaderError();
    CloseHandle(writeEnd);   // ours: ReadFile only sees the end of the pipe once every write end is closed
    if (!started)
    {
        CloseHandle(readEnd);
        GramCheck_(false, return -1, "RunProcess: couldn't start {}: {}", command.Arguments.front(), startError);
    }

    std::string pending;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        SplitLines(pending, std::string_view(buffer, read), onLine);
    if (!pending.empty()) onLine(pending);
    CloseHandle(readEnd);

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = static_cast<DWORD>(-1);
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
#else
int RunProcess(const ProcessCommand& command, const std::function<void(std::string_view)>& onLine)
{
    GramCheck_(!command.Arguments.empty(), return -1, "RunProcess: no program to run");

    int pipeEnds[2];
    GramCheck_(pipe(pipeEnds) == 0, return -1, "RunProcess: couldn't create a pipe: {}", std::strerror(errno));

    // The child: stdout and stderr into the pipe, in the command's directory.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipeEnds[0]);
    posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeEnds[1]);
    const std::string directory = command.Directory.string();
    // POSIX's own name since macOS 26 (the _np one is deprecated there); glibc has had the _np one since 2.29.
#ifdef GRAM_PLATFORM_APPLE
    if (!directory.empty()) posix_spawn_file_actions_addchdir(&actions, directory.c_str());
#else
    if (!directory.empty()) posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
#endif

    // This process's environment with the command's variables set on top.
    std::vector<std::string> environment;
    for (char** entry = environ; *entry; ++entry)
    {
        const std::string_view variable(*entry);
        const bool overridden = std::ranges::any_of(command.Environment, [&](const auto& override)
        {
            return variable.size() > override.first.size() && variable.starts_with(override.first) && variable[override.first.size()] == '=';
        });
        if (!overridden) environment.emplace_back(variable);
    }
    for (const auto& [name, value] : command.Environment) environment.push_back(name + "=" + value);

    std::vector<char*> argv, envp;
    for (const std::string& argument : command.Arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    for (const std::string& variable : environment)       envp.push_back(const_cast<char*>(variable.c_str()));
    argv.push_back(nullptr);
    envp.push_back(nullptr);

    pid_t child = 0;
    const int spawnError = posix_spawn(&child, argv.front(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    close(pipeEnds[1]);   // ours: read() only sees the end of the pipe once every write end is closed
    if (spawnError != 0)
    {
        close(pipeEnds[0]);
        GramCheck_(false, return -1, "RunProcess: couldn't start {}: {}", command.Arguments.front(), std::strerror(spawnError));
    }

    std::string pending;
    char buffer[4096];
    for (ssize_t count; (count = read(pipeEnds[0], buffer, sizeof(buffer))) != 0; )
    {
        if (count < 0) { if (errno == EINTR) continue; break; }
        SplitLines(pending, std::string_view(buffer, static_cast<size_t>(count)), onLine);
    }
    if (!pending.empty()) onLine(pending);
    close(pipeEnds[0]);

    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif

GRAM_NAMESPACE_END
