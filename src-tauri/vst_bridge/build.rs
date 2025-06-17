fn main() {
    let vst3_sdk_dir = std::env::var("VST3_SDK_DIR")
        .unwrap_or_else(|_| "../../../vst3sdk".to_string());

    println!("cargo:rerun-if-env-changed=VST3_SDK_DIR");
    println!("cargo:rerun-if-changed=../cpp_vst_host/plugin_scanner.cpp");
    println!("cargo:rerun-if-changed=../cpp_vst_host/plugin_scanner.h");
    println!("cargo:rerun-if-changed=../cpp_vst_host/vst3_plugin.cpp"); // Added
    println!("cargo:rerun-if-changed=../cpp_vst_host/vst3_plugin.h");   // Added
    println!("cargo:rerun-if-changed=../cpp_vst_host/host_application.cpp");
    println!("cargo:rerun-if-changed=../cpp_vst_host/host_application.h");

    let vst3_sdk_path = std::path::Path::new(&vst3_sdk_dir);
    if !vst3_sdk_path.is_dir() {
        panic!("VST3 SDK directory not found or is not a directory: '{}'. \
                Please set the VST3_SDK_DIR environment variable or ensure the \
                relative path in vst_bridge/build.rs is correct for your setup.", vst3_sdk_dir);
    }

    let mut builder = cxx_build::bridge("src/lib.rs");

    builder.cpp(true);
    builder.std("c++17");

    builder.files(&[
        "../cpp_vst_host/plugin_scanner.cpp", // For scanning
        "../cpp_vst_host/vst3_plugin.cpp",    // For plugin lifecycle and processing
        "../cpp_vst_host/host_application.cpp",
    ]);

    builder.include("../cpp_vst_host");
    builder.include(vst3_sdk_path.join("pluginterfaces"));
    builder.include(vst3_sdk_path.join("public.sdk/source"));

    if cfg!(target_os = "windows") {
        builder.flag("/EHsc");
    } else if cfg!(target_os = "macos") {
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
        println!("cargo:rustc-link-lib=framework=AudioUnit"); // Might be needed by VST3 SDK or plugins
        println!("cargo:rustc-link-lib=framework=CoreAudio"); // Might be needed
    } else if cfg!(target_os = "linux") {
        builder.library("dl");
        // Potentially add asound for ALSA if any part of SDK/plugin needs it, though unlikely for VST3 processing itself.
        // builder.library("asound");
    }

    builder.compile("vst_bridge_cpp_code");
}
