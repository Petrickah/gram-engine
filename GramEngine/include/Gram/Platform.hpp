#pragma once

GRAM_NAMESPACE_BEGIN

// Directory holding the running executable, resolved through the OS rather than argv[0]:
// argv[0] can be a bare name (PATH lookup, or a Windows .exe launched through WSL interop),
// which would turn every asset path into one relative to the working directory.
// Falls back to argv0's parent only if the OS query fails.
std::filesystem::path GetExecutableDirectory(const char* argv0 = nullptr);

// "game_logic" → libgame_logic.dylib / libgame_logic.so / game_logic.dll
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

GRAM_NAMESPACE_END
