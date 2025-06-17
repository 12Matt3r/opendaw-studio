#[cxx::bridge]
pub mod ffi {
    // --- Existing structs from previous tasks ---
    #[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
    pub struct RustParamDef { /* ... fields ... */
        pub id: u32, pub title: String, pub short_title: String, pub units: String,
        pub step_count: i32, pub default_normalized_value: f32, pub flags: i32,
        pub string_values: Vec<String>,
    }
    #[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
    pub struct RustVst3PluginInstanceInfoWithParams { /* ... fields ... */
        pub numAudioInputs: i32, pub numAudioOutputs: i32, pub numMidiInputs: i32, pub numMidiOutputs: i32,
        pub parameters: Vec<RustParamDef>, pub error: String,
    }
    #[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
    pub struct Vst3PluginInfo { /* ... fields ... */
        pub path: String, pub name: String, pub vendor: String, pub subCategories: String,
        pub cid_str: String, pub error: String,
    }

    // --- New structs for UI and parameter changes from plugin UI ---
    #[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
    pub struct RustEditorRect {
        pub top: i32,
        pub left: i32,
        pub bottom: i32,
        pub right: i32,
        pub success: bool,
    }

    #[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
    pub struct RustPluginParamChangeInfo {
        pub id: u32, // ParamID
        pub valueNormalized: f64, // ParamValue
        pub hasChanged: bool, // If a change was actually retrieved
    }


    unsafe extern "C++" {
        include!("cpp_vst_host/plugin_scanner.h");
        include!("cpp_vst_host/vst3_plugin.h");   // Includes ParamDefCpp, Vst3PluginInstanceInfoWithParams, EditorRectCpp, PluginParamChangeInfo

        // --- Existing type and function declarations ---
        type Vst3PluginHandle = u32;
        type ParamDefCpp;
        type Vst3PluginInstanceInfoWithParams;
        // Accessors for ParamDefCpp
        fn get_id(pd: &ParamDefCpp) -> u32;
        fn get_title(pd: &ParamDefCpp) -> String;
        fn get_short_title(pd: &ParamDefCpp) -> String;
        fn get_units(pd: &ParamDefCpp) -> String;
        fn get_step_count(pd: &ParamDefCpp) -> i32;
        fn get_default_normalized_value(pd: &ParamDefCpp) -> f32;
        fn get_flags(pd: &ParamDefCpp) -> i32;
        fn get_string_values(pd: &ParamDefCpp) -> Vec<String>;
        // Accessors for Vst3PluginInstanceInfoWithParams
        fn get_num_audio_inputs(info: &Vst3PluginInstanceInfoWithParams) -> i32;
        fn get_num_audio_outputs(info: &Vst3PluginInstanceInfoWithParams) -> i32;
        fn get_num_midi_inputs(info: &Vst3PluginInstanceInfoWithParams) -> i32;
        fn get_num_midi_outputs(info: &Vst3PluginInstanceInfoWithParams) -> i32;
        fn get_parameters(info: &Vst3PluginInstanceInfoWithParams) -> Vec<UniquePtr<ParamDefCpp>>;
        fn get_error_str(info: &Vst3PluginInstanceInfoWithParams) -> String;
        // Lifecycle & Processing functions
        fn scan_plugins_cpp() -> Vec<Vst3PluginInfo>;
        fn load_new_vst3_plugin(path: &str, cid_str: &str) -> Vst3PluginHandle;
        fn unload_vst3_plugin_instance(handle: Vst3PluginHandle) -> bool;
        fn initialize_plugin_cpp(handle: Vst3PluginHandle, sample_rate: f64, max_block_size: i32) -> bool;
        fn get_plugin_info_with_params_cpp(handle: Vst3PluginHandle) -> UniquePtr<Vst3PluginInstanceInfoWithParams>;
        fn process_audio_cpp(handle: Vst3PluginHandle, inputs: Vec<Vec<f32>>, num_samples: i32) -> Vec<Vec<f32>>;
        fn set_parameter_cpp(handle: Vst3PluginHandle, param_id: u32, value: f32) -> bool;

        // --- New UI related C++ types and functions ---
        type EditorRectCpp; // Represents C++ EditorRectCpp
        // Accessors for EditorRectCpp
        fn get_rect_top(rect: &EditorRectCpp) -> i32;
        fn get_rect_left(rect: &EditorRectCpp) -> i32;
        fn get_rect_bottom(rect: &EditorRectCpp) -> i32;
        fn get_rect_right(rect: &EditorRectCpp) -> i32;
        fn get_rect_success(rect: &EditorRectCpp) -> bool;

        // Represents C++ Steinberg::Vst::PluginParamChangeInfo (from host_application.h)
        // Note: Steinberg::Vst::PluginParamChangeInfo is defined in host_application.h, not vst3_plugin.h
        // For cxx, it's better if all bridged types are in headers included by cxx.
        // Let's assume PluginParamChangeInfo is moved or re-declared in vst3_plugin.h for bridge visibility.
        // Or, we create a new simple struct in vst3_plugin.h just for bridging this.
        // For this PoC, I'll assume it's made bridge-visible (e.g. by adding #include "host_application.h" in vst3_plugin.h for CXX section)
        // Or, more cleanly, redefine a simple struct for bridging if host_application.h is too complex.
        // Let's use the one defined in vst3_plugin.h (which mirrors the one in host_application.h)
        type PluginParamChangeInfo; // Represents C++ PluginParamChangeInfo from host_application.h / vst3_plugin.h
        fn get_param_change_id(info: &PluginParamChangeInfo) -> u32;
        fn get_param_change_value(info: &PluginParamChangeInfo) -> f64; // ParamValue is double
        fn get_param_change_has_changed(info: &PluginParamChangeInfo) -> bool;


        // New C++ global functions for UI
        // parent_handle is void*, which is usize in Rust.
        fn open_plugin_editor_cpp(handle: Vst3PluginHandle, parent_window_handle: usize) -> bool;
        fn close_plugin_editor_cpp(handle: Vst3PluginHandle);
        fn get_plugin_editor_rect_cpp(handle: Vst3PluginHandle) -> UniquePtr<EditorRectCpp>;
        fn check_for_plugin_initiated_parameter_changes_cpp(handle: Vst3PluginHandle) -> UniquePtr<PluginParamChangeInfo>;
    }
}

// --- Helper conversion functions for new structs ---
fn convert_editor_rect_cpp_to_rust(cpp_rect_ptr: cxx::UniquePtr<ffi::EditorRectCpp>) -> ffi::RustEditorRect {
    if cpp_rect_ptr.is_null() { return ffi::RustEditorRect { success: false, ..Default::default() }; }
    ffi::RustEditorRect {
        top: ffi::get_rect_top(&cpp_rect_ptr),
        left: ffi::get_rect_left(&cpp_rect_ptr),
        bottom: ffi::get_rect_bottom(&cpp_rect_ptr),
        right: ffi::get_rect_right(&cpp_rect_ptr),
        success: ffi::get_rect_success(&cpp_rect_ptr),
    }
}

fn convert_param_change_info_cpp_to_rust(cpp_info_ptr: cxx::UniquePtr<ffi::PluginParamChangeInfo>) -> ffi::RustPluginParamChangeInfo {
    if cpp_info_ptr.is_null() { return ffi::RustPluginParamChangeInfo { hasChanged: false, ..Default::default() }; }
    ffi::RustPluginParamChangeInfo {
        id: ffi::get_param_change_id(&cpp_info_ptr),
        valueNormalized: ffi::get_param_change_value(&cpp_info_ptr),
        hasChanged: ffi::get_param_change_has_changed(&cpp_info_ptr),
    }
}

// Existing helper convert_param_def_cpp_to_rust
fn convert_param_def_cpp_to_rust(cpp_def: &ffi::ParamDefCpp) -> ffi::RustParamDef { /* ... as before ... */
    ffi::RustParamDef {
        id: ffi::get_id(cpp_def), title: ffi::get_title(cpp_def), short_title: ffi::get_short_title(cpp_def),
        units: ffi::get_units(cpp_def), step_count: ffi::get_step_count(cpp_def),
        default_normalized_value: ffi::get_default_normalized_value(cpp_def),
        flags: ffi::get_flags(cpp_def), string_values: ffi::get_string_values(cpp_def),
    }
}
// Existing helper convert_instance_info_cpp_to_rust
fn convert_instance_info_cpp_to_rust(cpp_info_ptr: cxx::UniquePtr<ffi::Vst3PluginInstanceInfoWithParams>) -> ffi::RustVst3PluginInstanceInfoWithParams { /* ... as before ... */
    let params_cpp = ffi::get_parameters(&cpp_info_ptr);
    let params_rust: Vec<ffi::RustParamDef> = params_cpp.iter().map(|pd_cpp| convert_param_def_cpp_to_rust(pd_cpp)).collect();
    ffi::RustVst3PluginInstanceInfoWithParams {
        numAudioInputs: ffi::get_num_audio_inputs(&cpp_info_ptr), numAudioOutputs: ffi::get_num_audio_outputs(&cpp_info_ptr),
        numMidiInputs: ffi::get_num_midi_inputs(&cpp_info_ptr), numMidiOutputs: ffi::get_num_midi_outputs(&cpp_info_ptr),
        parameters: params_rust, error: ffi::get_error_str(&cpp_info_ptr),
    }
}


// --- Public Rust API (existing functions + new UI functions) ---
// scan_vst_plugins_native, load_vst3_plugin, initialize_vst3_plugin, unload_vst3_plugin,
// get_vst3_plugin_info_with_params, process_vst3_audio, set_vst3_parameter
// ... (These function implementations remain the same as in the previous step) ...
pub fn scan_vst_plugins_native() -> Result<Vec<ffi::Vst3PluginInfo>, String> { /* ... */ Ok(ffi::scan_plugins_cpp())}
pub fn load_vst3_plugin(path: String, cid: String) -> Result<ffi::Vst3PluginHandle, String> { /* ... */ let h = ffi::load_new_vst3_plugin(&path, &cid); if h==0 { Err("load failed".to_string()) } else { Ok(h) } }
pub fn initialize_vst3_plugin(h: ffi::Vst3PluginHandle, sr: f64, bs: i32) -> Result<(), String> { /* ... */ if ffi::initialize_plugin_cpp(h,sr,bs) {Ok(())} else {Err("init failed".to_string())} }
pub fn unload_vst3_plugin(h: ffi::Vst3PluginHandle) -> Result<(), String> { /* ... */ if ffi::unload_vst3_plugin_instance(h) {Ok(())} else {Err("unload failed".to_string())} }
pub fn get_vst3_plugin_info_with_params(h: ffi::Vst3PluginHandle) -> Result<ffi::RustVst3PluginInstanceInfoWithParams, String> { /* ... */ let p = ffi::get_plugin_info_with_params_cpp(h); if p.is_null() {Err("get_info_with_params null ptr".to_string())} else { let r = convert_instance_info_cpp_to_rust(p); if !r.error.is_empty() {Err(r.error)} else {Ok(r)}} }
pub fn process_vst3_audio(h: ffi::Vst3PluginHandle, i: Vec<Vec<f32>>, ns: i32) -> Result<Vec<Vec<f32>>, String> { /* ... */ Ok(ffi::process_audio_cpp(h,i,ns)) }
pub fn set_vst3_parameter(h: ffi::Vst3PluginHandle, pid: u32, v: f32) -> Result<(), String> { /* ... */ if ffi::set_parameter_cpp(h,pid,v) {Ok(())} else {Err("set_param failed".to_string())} }


// New public Rust functions for UI
pub fn open_plugin_editor(handle: ffi::Vst3PluginHandle, parent_handle_usize: usize) -> Result<(), String> {
    #[cfg(windows)] let _com_guard = coinit::init(coinit::APARTMENTTHREADED).map_err(|e| format!("COM init failed for editor: {:?}", e))?;
    if ffi::open_plugin_editor_cpp(handle, parent_handle_usize) {
        Ok(())
    } else {
        Err(format!("Failed to open VST3 plugin editor (handle: {}) from C++.", handle))
    }
}

pub fn close_plugin_editor(handle: ffi::Vst3PluginHandle) -> Result<(), String> {
    #[cfg(windows)] let _com_guard = coinit::init(coinit::APARTMENTTHREADED).map_err(|e| format!("COM init failed for editor close: {:?}", e))?;
    // C++ side is void, so we assume success if no panic.
    ffi::close_plugin_editor_cpp(handle);
    Ok(())
}

pub fn get_plugin_editor_rect(handle: ffi::Vst3PluginHandle) -> Result<ffi::RustEditorRect, String> {
    #[cfg(windows)] let _com_guard = coinit::init(coinit::APARTMENTTHREADED).map_err(|e| format!("COM init failed for editor rect: {:?}", e))?;
    let rect_ptr = ffi::get_plugin_editor_rect_cpp(handle);
    let rust_rect = convert_editor_rect_cpp_to_rust(rect_ptr);
    if !rust_rect.success {
        Err(format!("Failed to get VST3 plugin editor rect (handle: {}) from C++.", handle))
    } else {
        Ok(rust_rect)
    }
}

pub fn check_for_plugin_parameter_changes(handle: ffi::Vst3PluginHandle) -> Result<ffi::RustPluginParamChangeInfo, String> {
    // This function is polled, so COM init per call might be too much.
    // However, if the plugin UI is on a different thread or needs COM for its message loop, this might be necessary.
    // For now, keep it consistent with other calls.
    #[cfg(windows)] let _com_guard = coinit::init(coinit::APARTMENTTHREADED).map_err(|e| format!("COM init failed for param poll: {:?}", e))?;
    let change_info_ptr = ffi::check_for_plugin_initiated_parameter_changes_cpp(handle);
    Ok(convert_param_change_info_cpp_to_rust(change_info_ptr))
}
