#pragma once

#if defined(_WIN32)
    #if defined(FLUIDUI_BUILD)
        #define FLUID_API __declspec(dllexport)
    #else
        #define FLUID_API __declspec(dllimport)
    #endif
#else
    #define FLUID_API __attribute__((visibility("default")))
#endif