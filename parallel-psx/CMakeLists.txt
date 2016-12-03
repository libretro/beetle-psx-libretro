cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 11)
project(parallel-PSX LANGUAGES CXX C)

if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-missing-field-initializers")
endif()

option(VULKAN_WSI "Enable Vulkan WSI." ON)

add_subdirectory(vulkan)
add_subdirectory(glsl)
add_subdirectory(atlas)
add_subdirectory(renderer)
add_subdirectory(stb)

add_executable(rsx-player main.cpp)
add_dependencies(rsx-player shaders)
target_link_libraries(rsx-player renderer stb)

