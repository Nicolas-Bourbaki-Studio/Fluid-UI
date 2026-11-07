cmake_minimum_required(VERSION 3.24)
project(FluidUI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLUIDUI_BUILD_EXAMPLES "Build examples" ON)

add_subdirectory(src)

if(FLUIDUI_BUILD_EXAMPLES)
    find_package(glfw3 REQUIRED)
    add_subdirectory(examples)
endif()