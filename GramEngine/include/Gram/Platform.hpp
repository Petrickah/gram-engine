#pragma once

GRAM_NAMESPACE_BEGIN

// Directory holding the running executable, resolved through the OS rather than argv[0]:
// argv[0] can be a bare name (PATH lookup, or a Windows .exe launched through WSL interop),
// which would turn every asset path into one relative to the working directory.
// Falls back to argv0's parent only if the OS query fails.
std::filesystem::path GetExecutableDirectory(const char* argv0 = nullptr);

// "GameLogic" → libGameLogic.dylib / libGameLogic.so / GameLogic.dll
std::string GetDynamicLibraryFileName(std::string_view name);

// A shared library loaded for the lifetime of this object (dlopen / LoadLibrary).
class DynamicLibrary : UniqueObject
{
public:
    explicit DynamicLibrary(const std::filesystem::path& path);
    ~DynamicLibrary();

    bool IsValid() const { return m_Handle != nullptr; }

    // nullptr when the library doesn't export `name`.
    template<typename T>
    T FindSymbol(const char* name) const { return reinterpret_cast<T>(FindSymbolAddress(name)); }

private:
    void* FindSymbolAddress(const char* name) const;
    void* m_Handle = nullptr;
};

// A program to run: what a `<module>.gram-build` file next to the executable holds (written by gram build:
// how the editor builds the game's library again).
struct ProcessCommand
{
    std::filesystem::path                            Directory;    // its working directory; empty: this process's
    std::vector<std::pair<std::string, std::string>> Environment;  // set on top of this process's own
    std::vector<std::string>                         Arguments;    // the program (a full path) first
};

// Reads a `<key> <value>` per line (`dir`, `env NAME=value`, `arg`; `#` comments). nullopt when the file
// can't be read or names no program.
std::optional<ProcessCommand> ReadProcessCommand(const std::filesystem::path& path);

// Runs `command` to its end, blocking, calling `onLine` with every line it writes (stdout and stderr
// together, in order, without the line break). Returns its exit code, or -1 when it couldn't be started.
// Meant for a worker thread: `onLine` runs on the calling thread. No console window opens on Windows.
int RunProcess(const ProcessCommand& command, const std::function<void(std::string_view)>& onLine);

GRAM_NAMESPACE_END
