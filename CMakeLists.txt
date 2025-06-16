cmake_minimum_required(VERSION 3.15)
project(VST3HostPoC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- VST3 SDK ---
# Placeholder for VST3 SDK root directory.
# Users will need to set this path, e.g., via CMake GUI or -DVST3_SDK_ROOT=/path/to/vst3sdk
set(VST3_SDK_ROOT "" CACHE PATH "Root directory of the Steinberg VST3 SDK")

if(NOT VST3_SDK_ROOT)
    message(FATAL_ERROR "VST3_SDK_ROOT is not set. Please specify the path to your VST3 SDK.")
endif()

# Add VST3 SDK include directories
include_directories(
    "${VST3_SDK_ROOT}/pluginterfaces/vst"
    "${VST3_SDK_ROOT}/public.sdk/source" # For VST3_HOSTING_MODULE_UID, common VST SDK files
    # Potentially others depending on specific needs
)

# --- Source Files ---
add_executable(vst3host
    main.cpp
    vst3_plugin.cpp
    plugin_scanner.cpp
    host_application.cpp
)

# --- Linking ---
# VST3 plugins are typically loaded as shared libraries,
# so direct linking is often minimal for the host itself.
# However, some SDK utilities or base classes might need to be compiled and linked.
# For example, if we use parts of public.sdk/source/main or public.sdk/source/common
# target_link_libraries(vst3host PRIVATE some_sdk_library)

# --- Platform Specific Settings ---
if(WIN32)
    # Windows specific settings
    # ole32.lib is generally linked by default for COM functions like CoInitializeEx
    # target_link_libraries(vst3host PRIVATE ole32.lib)
elseif(APPLE)
    # macOS specific settings
    # Link against CoreFoundation for CFStringRef and other bundle management utilities if directly used.
    # dlopen/dlsym are part of the default C library on macOS.
    target_link_libraries(vst3host PRIVATE "-framework CoreFoundation")
elseif(UNIX AND NOT APPLE)
    # Linux specific settings
    target_link_libraries(vst3host PRIVATE dl)
endif()

# --- Output ---
# Optional: Set output directory for the executable
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "VST3 SDK Path: ${VST3_SDK_ROOT}")
