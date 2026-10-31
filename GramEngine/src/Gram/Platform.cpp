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
#else
    #include <dlfcn.h>
#endif

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

GRAM_NAMESPACE_END
