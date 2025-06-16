// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

// Define the Tauri command that calls the vst_bridge
#[tauri::command]
fn scan_vst3_plugins() -> Result<Vec<vst_bridge::ffi::Vst3PluginInfo>, String> {
    // Call the public function from the vst_bridge crate
    match vst_bridge::scan_vst_plugins() {
        Ok(plugin_list) => Ok(plugin_list),
        Err(e) => {
            eprintln!("Error scanning VST3 plugins: {}", e);
            Err(e) // Propagate the error string to the frontend
        }
    }
}

fn main() {
    // Any platform specific setup before Tauri runs.
    // For example, on Windows, COM initialization for the main thread could be done here
    // if not handled per-command or by Tauri itself for specific operations.
    // However, for commands that might run on different threads, per-command COM init (like in vst_bridge) is safer.

    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            scan_vst3_plugins
            // Add other commands here
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
