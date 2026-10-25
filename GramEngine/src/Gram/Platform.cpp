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
    #include <mach-o/dyld.h>
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

GRAM_NAMESPACE_END
