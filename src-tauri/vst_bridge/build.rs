fn main() {
    // Assume VST3_SDK_DIR environment variable is set.
    // Fallback to a relative path for local development if not set.
    // IMPORTANT: This relative path is from `src-tauri/vst_bridge` (where build.rs runs)
    // to the root of the VST3 SDK.
    let vst3_sdk_dir = std::env::var("VST3_SDK_DIR")
        .unwrap_or_else(|_| "../../../vst3sdk".to_string());
        // Corrected relative path:
        // current dir is opendaw-desktop/src-tauri/vst_bridge
        // ../ -> opendaw-desktop/src-tauri
        // ../../ -> opendaw-desktop
        // ../../../ -> parent of opendaw-desktop (expecting vst3sdk to be here)
        // This assumes a directory structure like:
        // parent-directory/
        //   opendaw-desktop/
        //   vst3sdk/

    println!("cargo:rerun-if-env-changed=VST3_SDK_DIR");
    println!("cargo:rerun-if-changed=../cpp_vst_host/plugin_scanner.cpp");
    println!("cargo:rerun-if-changed=../cpp_vst_host/plugin_scanner.h");
    println!("cargo:rerun-if-changed=../cpp_vst_host/host_application.cpp");
    println!("cargo:rerun-if-changed=../cpp_vst_host/host_application.h");

    // Check if VST3 SDK path exists, panic if not.
    // This provides a clearer error message to the developer.
    let vst3_sdk_path = std::path::Path::new(&vst3_sdk_dir);
    if !vst3_sdk_path.is_dir() {
        panic!("VST3 SDK directory not found or is not a directory: '{}'. \
                Please set the VST3_SDK_DIR environment variable or ensure the \
                relative path in vst_bridge/build.rs is correct for your setup.", vst3_sdk_dir);
    }


    let mut builder = cxx_build::bridge("src/lib.rs"); // Path to the Rust source file with cxx::bridge

    builder.cpp(true); // Enable C++ support
    builder.std("c++17"); // Specify C++ standard for <filesystem>

    // Paths are relative to build.rs (i.e., src-tauri/vst_bridge)
    builder.files(&[
        "../cpp_vst_host/plugin_scanner.cpp",
        "../cpp_vst_host/host_application.cpp",
        // Do NOT include main.cpp from the C++ PoC as it has its own main()
        // Do NOT include vst3_plugin.cpp yet as it's not directly used by scan_plugins_cpp
    ]);

    // Add include paths for the C++ code itself and for the VST3 SDK
    builder.include("../cpp_vst_host"); // For plugin_scanner.h, host_application.h
    builder.include(vst3_sdk_path.join("pluginterfaces"));
    builder.include(vst3_sdk_path.join("public.sdk/source"));

    if cfg!(target_os = "windows") {
        builder.flag("/EHsc"); // Necessary for C++ exceptions on MSVC
        // MSVC typically links Ole32.lib and Shell32.lib by default.
    } else if cfg!(target_os = "macos") {
        // dlopen/dlsym are part of libc/libSystem on macOS.
        // The -framework CoreFoundation was in the original CMakeLists.txt for the C++ PoC.
        // It might be required by some VST3 SDK headers or for bundle loading logic,
        // even if not directly used by plugin_scanner.cpp.
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
    } else if cfg!(target_os = "linux") {
        builder.library("dl");    // For dlopen, dlsym
    }

    // The output name here is for the static library that Rust will link against.
    // e.g. libvst_host_native_code.a
    // This name doesn't affect Cargo's linking directly but is good for debugging.
    builder.compile("vst_bridge_cpp_code");

    // cxx_build handles the necessary cargo:rustc-link-lib and cargo:rustc-link-search lines.
    // We don't need to print them manually for the compiled C++ library.

    // Additional system libraries can be linked here if needed by the C++ code,
    // beyond what cxx_build handles for the compiled sources.
    // For example, if the C++ code explicitly used functionalities from ole32 on Windows beyond COM basics:
    // if cfg!(target_os = "windows") {
    //     println!("cargo:rustc-link-lib=ole32");
    // }
}
