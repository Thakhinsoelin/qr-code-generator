# NOTE: This is root cmake file

cmake_minimum_required(VERSION 3.31)
project(itachi LANGUAGES C CXX RC)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_COMPILER "cl.exe" CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "cl.exe" CACHE FILEPATH "C++ compiler" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") 
#find_package(Vulkan REQUIRED)
#find_program(SLANGC_EXECUTABLE NAMES slangc REQUIRED)

# Add easy_profiler directory (adjust path to match your folder structure)
add_subdirectory(Dependencies/Raven)
set(MY_CUSTOM_BIN_DIR "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIG>-$<PLATFORM_ID>")

# Force all executable targets to compile into this specific directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${MY_CUSTOM_BIN_DIR}")

add_subdirectory(src)



