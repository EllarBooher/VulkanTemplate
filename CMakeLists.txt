cmake_minimum_required(VERSION 3.28)

project(VulkanTemplate)

include(cmake/dependencies.cmake)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory("shaders")
add_subdirectory("vulkan_template")

add_subdirectory("application")

include(cmake/include-what-you-use.cmake)
include(cmake/clang-format.cmake)
include(cmake/clang-tidy.cmake)