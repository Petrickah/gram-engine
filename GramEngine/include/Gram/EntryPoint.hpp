#pragma once

// Including this header turns the translation unit into the app's entry point
// (defines main/WinMain). A build that never includes it has no entry point,
// so its CMake target must be a static library instead of an executable.
#include "Gram/Application.hpp"
#include "Gram/Layer.hpp"
#include "Gram/Platform.hpp"

#if defined(_WIN32)
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
extern std::shared_ptr<LayerOwner<Application>> CreateApplication(int argc, char** argv);
GRAM_NAMESPACE_END

#if defined(_WIN32)

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    std::shared_ptr<Gram::LayerOwner<Gram::Application>> app = Gram::CreateApplication(__argc, __argv);
    app->Run();
    return 0;
}

#else

int main(int argc, char** argv) {
    std::shared_ptr<Gram::LayerOwner<Gram::Application>> app = Gram::CreateApplication(argc, argv);
    app->Run();
    return 0;
}

#endif
