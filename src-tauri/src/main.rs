// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use tauri::Manager; // For AppHandle, window.emit()
use std::sync::Mutex; // For polling state
use std::collections::HashSet; // To keep track of plugins being polled

// --- VST Bridge Types & Conversions (from previous steps, ensure they are up-to-date) ---
type Vst3PluginHandle = u32;

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
struct RustParamDefForTauri { /* ... */
    id: u32, title: String, short_title: String, units: String, step_count: i32,
    default_normalized_value: f32, flags: i32, string_values: Vec<String>,
}
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
struct RustVst3PluginInstanceInfoWithParamsForTauri { /* ... */
    numAudioInputs: i32, numAudioOutputs: i32, numMidiInputs: i32, numMidiOutputs: i32,
    parameters: Vec<RustParamDefForTauri>, error: String,
}
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
struct Vst3PluginInfoForTauri { /* ... */
    path: String, name: String, vendor: String, subCategories: String,
    cid_str: String, error: String,
}
impl From<vst_bridge::ffi::RustParamDef> for RustParamDefForTauri { /* ... */
    fn from(pd: vst_bridge::ffi::RustParamDef) -> Self {
        Self { id: pd.id, title: pd.title, short_title: pd.short_title, units: pd.units,
               step_count: pd.step_count, default_normalized_value: pd.default_normalized_value,
               flags: pd.flags, string_values: pd.string_values }
    }
}
impl From<vst_bridge::ffi::RustVst3PluginInstanceInfoWithParams> for RustVst3PluginInstanceInfoWithParamsForTauri { /* ... */
    fn from(info: vst_bridge::ffi::RustVst3PluginInstanceInfoWithParams) -> Self {
        Self { numAudioInputs: info.numAudioInputs, numAudioOutputs: info.numAudioOutputs,
               numMidiInputs: info.numMidiInputs, numMidiOutputs: info.numMidiOutputs,
               parameters: info.parameters.into_iter().map(Into::into).collect(), error: info.error }
    }
}
impl From<vst_bridge::ffi::Vst3PluginInfo> for Vst3PluginInfoForTauri { /* ... */
    fn from(info: vst_bridge::ffi::Vst3PluginInfo) -> Self {
        Self { path: info.path, name: info.name, vendor: info.vendor, subCategories: info.subCategories,
               cid_str: info.cid_str, error: info.error }
    }
}

// New Tauri types for UI
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
struct RustEditorRectForTauri { /* ... mirrors vst_bridge::ffi::RustEditorRect ... */
    top: i32, left: i32, bottom: i32, right: i32, success: bool,
}
impl From<vst_bridge::ffi::RustEditorRect> for RustEditorRectForTauri { /* ... */
    fn from(rect: vst_bridge::ffi::RustEditorRect) -> Self {
        Self { top: rect.top, left: rect.left, bottom: rect.bottom, right: rect.right, success: rect.success }
    }
}

// Payload for vst_param_changed_by_ui event
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
struct VstParamChangedPayload {
    plugin_handle: Vst3PluginHandle, // Use consistent naming (snake_case for JS event)
    param_id: u32,
    normalized_value: f64,
}


// --- Global State for Polling ---
struct PollingState {
    active_editor_handles: Mutex<HashSet<Vst3PluginHandle>>,
}
impl Default for PollingState {
    fn default() -> Self {
        Self { active_editor_handles: Mutex::new(HashSet::new()) }
    }
}


// --- Tauri Commands (existing + new UI commands) ---
#[tauri::command]
fn scan_vst3_plugins_command() -> Result<Vec<Vst3PluginInfoForTauri>, String> { /* ... */ vst_bridge::scan_vst_plugins_native().map(|p|p.into_iter().map(Into::into).collect()).map_err(|e|e.into()) }
#[tauri::command]
fn load_vst3_plugin_command(path: String, cid: String) -> Result<Vst3PluginHandle, String> { /* ... */ vst_bridge::load_vst3_plugin(path,cid).map_err(|e|e.into()) }
#[tauri::command]
fn initialize_vst3_plugin_command(handle: Vst3PluginHandle, sr: f64, bs: i32) -> Result<(), String> { /* ... */ vst_bridge::initialize_vst3_plugin(handle,sr,bs).map_err(|e|e.into()) }
#[tauri::command]
fn unload_vst3_plugin_command(handle: Vst3PluginHandle) -> Result<(), String> { /* ... */ vst_bridge::unload_vst3_plugin(handle).map_err(|e|e.into()) }
#[tauri::command]
fn get_vst3_plugin_info_with_params_command(h: Vst3PluginHandle) -> Result<RustVst3PluginInstanceInfoWithParamsForTauri, String> { /* ... */ vst_bridge::get_vst3_plugin_info_with_params(h).map(Into::into).map_err(|e|e.into()) }
#[tauri::command]
fn process_vst3_audio_command(h: Vst3PluginHandle, i: Vec<Vec<f32>>, ns: i32) -> Result<Vec<Vec<f32>>, String> { /* ... */ vst_bridge::process_vst3_audio(h,i,ns).map_err(|e|e.into()) }
#[tauri::command]
fn set_vst3_parameter_command(h: Vst3PluginHandle, pid: u32, v: f32) -> Result<(), String> { /* ... */ vst_bridge::set_vst3_parameter(h,pid,v).map_err(|e|e.into()) }


// New UI Commands
#[tauri::command]
fn open_plugin_editor_command(
    app_handle: tauri::AppHandle, // To emit events globally if needed, or on specific windows
    plugin_handle: Vst3PluginHandle,
    polling_state: tauri::State<PollingState>
) -> Result<RustEditorRectForTauri, String> {
    // parent_handle_usize = 0 for separate window. Platform specific handles might be needed for embedding.
    let parent_handle_usize: usize = 0;
    match vst_bridge::open_plugin_editor(plugin_handle, parent_handle_usize) {
        Ok(_) => {
            // Try to get initial rect. Some plugins might not provide it immediately or accurately.
            let rect_info = vst_bridge::get_plugin_editor_rect(plugin_handle).map(Into::into);

            // Start polling for this plugin's UI-initiated parameter changes
            let mut handles = polling_state.active_editor_handles.lock().unwrap();
            handles.insert(plugin_handle);
            println!("Rust: Added plugin handle {} to polling list. Current list size: {}", plugin_handle, handles.len());

            // If rect_info is an error, it means get_plugin_editor_rect failed.
            // We might still have opened the editor, so return Ok with a default/error rect.
            rect_info.or_else(|e| {
                eprintln!("Warning: Editor opened for handle {} but failed to get initial rect: {}", plugin_handle, e);
                Ok(RustEditorRectForTauri{ top:0, left:0, bottom:300, right:500, success:false }) // Default rect
            })
        }
        Err(e) => {
            eprintln!("Error in open_plugin_editor_command (handle: {}): {}", plugin_handle, e);
            Err(e)
        }
    }
}

#[tauri::command]
fn close_plugin_editor_command(
    plugin_handle: Vst3PluginHandle,
    polling_state: tauri::State<PollingState>
) -> Result<(), String> {
    match vst_bridge::close_plugin_editor(plugin_handle) {
        Ok(_) => {
            let mut handles = polling_state.active_editor_handles.lock().unwrap();
            handles.remove(&plugin_handle);
            println!("Rust: Removed plugin handle {} from polling list. Current list size: {}", plugin_handle, handles.len());
            Ok(())
        }
        Err(e) => {
            eprintln!("Error in close_plugin_editor_command (handle: {}): {}", plugin_handle, e);
            Err(e)
        }
    }
}

// This is not a command, but a helper for the polling loop
fn do_poll_plugin_parameters(app_handle: &tauri::AppHandle, plugin_handle: Vst3PluginHandle) {
    match vst_bridge::check_for_plugin_parameter_changes(plugin_handle) {
        Ok(change_info) => {
            if change_info.hasChanged {
                println!("Rust: Polled change for plugin {}, ParamID: {}, Value: {}", plugin_handle, change_info.id, change_info.valueNormalized);
                app_handle.emit_all("vst_param_changed_by_ui", VstParamChangedPayload {
                    plugin_handle: plugin_handle,
                    param_id: change_info.id,
                    normalized_value: change_info.valueNormalized,
                }).unwrap_or_else(|e| {
                    eprintln!("Error emitting vst_param_changed_by_ui event: {:?}", e);
                });
            }
        }
        Err(e) => {
            eprintln!("Error polling param changes for plugin {}: {}", plugin_handle, e);
            // Optionally, remove from polling list if error is persistent?
        }
    }
}


fn main() {
    tauri::Builder::default()
        .manage(PollingState::default()) // Add PollingState to Tauri's managed state
        .setup(|app| {
            // Start the polling loop
            let app_handle = app.handle();
            let polling_state = app.state::<PollingState>();

            std::thread::spawn(move || {
                loop {
                    let handles_to_poll: Vec<Vst3PluginHandle> = {
                        let active_handles = polling_state.active_editor_handles.lock().unwrap();
                        active_handles.iter().cloned().collect() // Clone to avoid holding lock during poll
                    };

                    if !handles_to_poll.is_empty() {
                        // println!("Polling {} active editors...", handles_to_poll.len());
                    }

                    for handle in handles_to_poll {
                        do_poll_plugin_parameters(&app_handle, handle);
                    }
                    std::thread::sleep(std::time::Duration::from_millis(100)); // Poll interval
                }
            });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            scan_vst3_plugins_command,
            load_vst3_plugin_command,
            initialize_vst3_plugin_command,
            unload_vst3_plugin_command,
            get_vst3_plugin_info_with_params_command,
            process_vst3_audio_command,
            set_vst3_parameter_command,
            // New UI Commands
            open_plugin_editor_command,
            close_plugin_editor_command
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
