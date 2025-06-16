#[cxx::bridge]
mod ffi {

    // This struct mirrors the C++ Vst3PluginInfo struct.
    // Ensure field names and types are compatible.
    // std::string in C++ maps to String in Rust.
    // For TUID (unsigned char[16]), direct mapping is complex.
    // We've opted for cid_str: String in C++ (if implemented) or omit for PoC.
    #[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
    pub struct Vst3PluginInfo {
        pub path: String,
        pub name: String,
        pub vendor: String,
        pub subCategories: String, // Or categories
        // pub cid_str: String, // If we decide to pass CID as string
        pub error: String,     // For any error during individual plugin scan
    }

    // This block declares the C++ functions and types we want to use from Rust.
    // The `namespace` attribute can be used if C++ functions are in a namespace.
    // For global functions like `scan_plugins_cpp`, no namespace is needed here.
    unsafe extern "C++" {
        // Path to the C++ header file declaring types/functions.
        // Relative to this file (src/lib.rs) or an include path in build.rs
        include!("cpp_vst_host/plugin_scanner.h");

        // Declare the C++ struct Vst3PluginInfo that Rust will see.
        // cxx will generate the necessary static assertions to ensure layout compatibility
        // for shared types if we were to use `type Vst3PluginInfo = crate::ffi::Vst3PluginInfo;`
        // and make the C++ side use it. For now, we define it on both sides and they map by value.
        // type Vst3PluginInfo = crate::ffi::Vst3PluginInfo; // This would be if C++ used Rust's definition.

        // Declare the C++ function signature.
        // `std::vector<Vst3PluginInfo>` in C++ maps to `Vec<Vst3PluginInfo>` in Rust.
        fn scan_plugins_cpp() -> Vec<Vst3PluginInfo>;
    }

    // You can also have Rust functions callable from C++ in an `extern "Rust"` block.
    // Not needed for this PoC.
}

// Public Rust function that wraps the C++ call.
// This is what the Tauri command will call.
pub fn scan_vst_plugins() -> Result<Vec<ffi::Vst3PluginInfo>, String> {
    // Perform COM initialization for the current thread if on Windows.
    // This is crucial because the C++ code (SHGetKnownFolderPath, LoadLibrary) might need it.
    // It's better to control this from the Rust side before calling into C++.
    // This should ideally be done once per thread that interacts with COM.
    // For a simple Tauri command, doing it here is a pragmatic approach.
    // If multiple commands use C++, a more central COM init might be needed,
    // or ensure Tauri manages this if commands run on a COM-initialized thread.
    #[cfg(windows)]
    {
        // COINIT_APARTMENTTHREADED is typical for UI threads / file dialogs.
        // COINIT_MULTITHREADED might be used if worker threads call this.
        // For plugin scanning, which involves file system access and loading DLLs,
        // APARTMENTTHREADED is generally safer.
        let coinit_result = coinit::init(coinit::APARTMENTTHREADED);
        if let Err(e) = coinit_result {
            // Ignore S_FALSE (already initialized) and S_OK (initialized successfully)
            if e.raw_code() != 0 && e.raw_code() != 1 { // 0=S_OK, 1=S_FALSE (RPC_E_CHANGED_MODE)
                 // HRESULT -2147417850 (0x80010106) is RPC_E_CHANGED_MODE, meaning COM was already init with different mode.
                // This can be problematic. For this PoC, we'll log and proceed.
                // A robust app would handle this more gracefully.
                eprintln!("COM initialization failed with code {}: {:?}. Scanning might fail.", e.raw_code(), e);
                // return Err(format!("COM initialization failed: {:?}", e));
            }
        }
        // Destructor of coinit::Initialized will call CoUninitialize.
        // Ensure `_com_guard` lives until C++ call returns.
        let _com_guard = coinit_result.ok(); // Keep guard alive
    }


    // Call the C++ function.
    // cxx handles the conversion between Vec<CppVst3PluginInfo> and Vec<RustVst3PluginInfo>.
    // If scan_plugins_cpp() can throw an exception, this call is unsafe.
    // The C++ function should be marked noexcept or errors handled via return values/output params.
    // For now, assume it doesn't throw across FFI or handle errors by checking `error` field.
    let results = ffi::scan_plugins_cpp();

    // Optional: Filter out plugins that had errors during scanning on the C++ side,
    // or transform them into a Rust error type if appropriate.
    // For this PoC, we pass them through, error string included.

    Ok(results)
}

// Add the coinit crate for COM initialization on Windows
// This should be added to vst_bridge/Cargo.toml:
// [target.'cfg(windows)'.dependencies]
// coinit = "0.5"
//
// And for serde:
// serde = { version = "1.0", features = ["derive"] }
// (already added to Vst3PluginInfo derive)
