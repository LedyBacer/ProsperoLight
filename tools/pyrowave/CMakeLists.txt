cmake_minimum_required(VERSION 3.20)
project(prosperolight_pyrowave LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(DEPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH "Pinned sources")
set(PYRO "${DEPS_ROOT}/pyrowave")
set(GRANITE "${DEPS_ROOT}/Granite")
# Use upstream sources and their CMake target definitions. Disable desktop subsystems.
foreach(flag GRANITE_RENDERER GRANITE_TOOLS GRANITE_RENDERDOC_CAPTURE GRANITE_VULKAN_SYSTEM_HANDLES
             GRANITE_VULKAN_SPIRV_CROSS GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
             GRANITE_VULKAN_FOSSILIZE GRANITE_INSTALL_TARGETS GRANITE_AUDIO GRANITE_FFMPEG
             GRANITE_FFMPEG_VULKAN PYROWAVE_DEVEL PYROWAVE_UTILS)
    set(${flag} OFF CACHE BOOL "" FORCE)
endforeach()
set(GRANITE_PLATFORM "null" CACHE STRING "" FORCE)
set(GRANITE_SHIPPING ON CACHE BOOL "" FORCE)
set(GRANITE_POSITION_INDEPENDENT ON CACHE BOOL "" FORCE)
set(PYROWAVE_FP32_MATH ON CACHE BOOL "" FORCE)
set(PYROWAVE_FP32_STORAGE OFF CACHE BOOL "" FORCE)
add_subdirectory("${GRANITE}" granite EXCLUDE_FROM_ALL)
add_subdirectory("${PYRO}" pyrowave EXCLUDE_FROM_ALL)
# Upstream only exposes a shared C API when top-level. Compose the same source
# files into a static API here; no codec or shader changes.
add_library(pyrowave-c STATIC "${PYRO}/pyrowave_c.cpp" "${GRANITE}/video/scaler.cpp")
target_include_directories(pyrowave-c PRIVATE "${PYRO}/shaders" "${GRANITE}/video")
target_link_libraries(pyrowave-c PUBLIC pyrowave granite-vulkan granite-math)
target_compile_definitions(pyrowave-c PRIVATE PROSPEROLIGHT_PYROWAVE_DIAGNOSTICS=1)
target_compile_options(pyrowave-c PRIVATE -Wno-missing-field-initializers)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/archives.txt" CONTENT
"$<TARGET_FILE:pyrowave-c>\n$<TARGET_FILE:pyrowave>\n$<TARGET_FILE:granite-vulkan>\n$<TARGET_FILE:granite-util>\n$<TARGET_FILE:granite-math>\n$<TARGET_FILE:granite-volk>\n")
