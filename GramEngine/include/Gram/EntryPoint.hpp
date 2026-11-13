#pragma once

// Including this header turns the translation unit into the app's entry point
// (defines main/WinMain). A build that never includes it has no entry point,
// so its CMake target must be a static library instead of an executable.
#include "Gram/Common.hpp"

// mingw builds enter through WinMain; MSVC doesn't: the game is a console application (no WIN32
// in its CMake target), whose entry point the linker expects to be main. It also keeps <windows.h>
// out of every translation unit that includes this header, and with it the min/max macros that
// break std::min/std::max.
#if defined(_WIN32) && !defined(_MSC_VER)
    #define GRAM_WINMAIN_ENTRY
#endif

#ifdef GRAM_WINMAIN_ENTRY
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    // Lowercase: mingw's header is installed as "windows.h", and unlike
    // Windows/MSVC, cross-compiling from Linux hits a case-sensitive filesystem.
    #include <windows.h>

    // Set up by the CRT before WinMain runs, same argc/argv a classic main
    // would get — avoids hand-parsing lpCmdLine (which also drops argv[0]).
    extern "C" {
        extern int __argc;
        extern char** __argv;
    }
#endif

GRAM_NAMESPACE_BEGIN
extern std::shared_ptr<Gram::Application> CreateApplication(int argc, char** argv);
GRAM_NAMESPACE_END

#ifdef GRAM_WINMAIN_ENTRY
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    if (std::shared_ptr<Gram::Application> app = Gram::CreateApplication(__argc, __argv))
    {
        app->Run();
        return 0;
    }

    return 1;
}
#else
int main(int argc, char** argv)
{
    if (std::shared_ptr<Gram::Application> app = Gram::CreateApplication(argc, argv))
    {
        app->Run();
        return 0;
    }

    return 1;
}
#endif