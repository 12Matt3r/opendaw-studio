# Developer Documentation: VST3 Plugin Integration PoC for openDAW

## 1. Overview
This document describes the architecture and technical details of the VST3 plugin integration Proof-of-Concept (PoC) for the openDAW desktop application. The goal of this PoC was to validate the feasibility of hosting VST3 plugins, managing their lifecycle, processing audio, handling parameters, and displaying their custom UIs.

The architecture comprises:
*   **Tauri Desktop Application:** The main shell, using a web frontend (JS/TS) and a Rust backend.
*   **Native C++ VST Hosting Module:** Core logic for interacting with VST3 plugins using the Steinberg VST3 SDK.
*   **Rust Bridge:** Uses `cxx` to interface between the Tauri Rust backend and the C++ module.
*   **AudioWorklet (`Vst3WorkletProcessor`):** Handles audio processing for VST3 plugins within openDAW's web audio graph, communicating with the native module via the main thread and Tauri.
*   **Schema Definitions:** New box types for VST3 instruments and effects.

## 2. Native C++ VST Hosting Module
*   **Location:** `src-tauri/cpp_vst_host/` (conceptual path within the Tauri app structure).
*   **Key Classes:**
    *   `Vst3Plugin`: Encapsulates a single loaded VST3 plugin instance. Manages loading the `.vst3` module, instantiating the plugin via `IPluginFactory`, interacting with `IComponent`, `IAudioProcessor`, `IEditController`, and `IPlugView`.
    *   `MinimalHostApplication`: Implements `Steinberg::Vst::IHostApplication` and `Steinberg::Vst::IComponentHandler` / `IComponentHandler2`. This provides necessary host context to the plugin and handles callbacks from the plugin (e.g., when parameters are changed via its custom UI).
    *   Global functions (e.g., `load_new_vst3_plugin`, `open_plugin_editor_cpp`): C-style functions exposed via `cxx` to the Rust bridge, managing a global map of loaded plugin instances.
*   **Build Process:** Compiled as part of the Rust bridge crate (`vst_bridge`) via its `build.rs` script, which uses `cxx_build`.
*   **Exposing to Rust:** Functions intended for Rust are typically global or static, matched by declarations in the `vst_bridge/src/lib.rs` `cxx::bridge` module.

## 3. Rust Bridge (`vst_bridge`)
*   **Location:** `src-tauri/vst_bridge/` (conceptual).
*   **Role:** Safely call C++ functions from Rust and expose a Rust API to the Tauri backend.
*   **`cxx` crate:** Used to define the FFI boundary. C++ types and functions are declared in a `#[cxx::bridge]` module in `lib.rs`.
*   **`build.rs`:** Crucial for building the bridge. It:
    *   Compiles the C++ source files (`cpp_vst_host/*.cpp`).
    *   Links against necessary platform libraries (e.g., `dl` on Linux; `CoreFoundation`, `AudioUnit` on macOS).
    *   Sets include paths for C++ code and the VST3 SDK.
*   **API:** Provides Rust functions that wrap the C++ calls (e.g., `rust_load_vst3_plugin`, `rust_open_plugin_editor`). These often handle type conversions between Rust and C++ (e.g., `String` to `std::string`, error handling).

## 4. Tauri Integration
*   **Tauri Commands:** Rust functions in `src-tauri/src/main.rs` are exposed to the JavaScript frontend using the `#[tauri::command]` attribute. These commands call functions from the `vst_bridge`. Examples: `scan_vst3_plugins_command`, `load_vst3_plugin_command`, `open_plugin_editor_command`.
*   **Event System:** For asynchronous updates from the native layer to the frontend (e.g., parameter changes initiated by the plugin's UI), Tauri's event system (`window.emit("event-name", payload)`) is used. The Rust backend (e.g., the polling thread for parameter changes) emits these events.

## 5. Frontend Integration (TypeScript/JavaScript)
*   **`VstPluginService.ts` (Conceptual - e.g., in `src/services/`):**
    *   Acts as a singleton service on the main browser thread to manage communication between AudioWorklets and the Tauri backend.
    *   Receives messages from `Vst3WorkletProcessor` instances (e.g., "request-load-plugin", "request-process-audio").
    *   Invokes Tauri commands to interact with the native VST hosting module.
    *   Forwards results or data back to the requesting AudioWorklet via `port.postMessage()`.
    *   Listens for Tauri events (e.g., `vst_param_changed_by_ui`) and dispatches updates to the relevant worklets.
*   **`Vst3WorkletProcessor.ts` (Conceptual - e.g., in `src/audio-engine/processors/`):**
    *   Extends `AudioWorkletProcessor`.
    *   **Lifecycle:**
        *   Instantiated by the main audio engine with `pluginPath` and `pluginCID`.
        *   Posts message to `VstPluginService` to request plugin loading and initialization.
        *   Receives parameter definitions once loaded and posts them to the main openDAW application for UI generation via `vst3-parameter-definitions` message.
    *   **Audio Processing:**
        *   In its `process()` method, it posts input audio data to `VstPluginService` for native processing.
        *   Receives processed audio asynchronously and outputs it. (Note: This PoC uses an async path with inherent latency).
    *   **Parameter Handling:**
        *   Receives OpenDAW automation values in `process()`. Sends updates to `VstPluginService` using VST3 ParamIDs.
        *   Receives updates from `VstPluginService` if a parameter was changed via the plugin's native UI, and posts these to the main openDAW application.

## 6. Schema Definitions
*   **Location:** `studio-boxes/src/schema/devices/` (conceptual).
*   New schemas:
    *   `instruments/Vst3InstrumentDeviceBox.ts`
    *   `audio-effects/Vst3AudioEffectDeviceBox.ts`
*   These define the data structure for VST3 plugins within an openDAW project, including plugin path, CID, cached metadata, and a placeholder for parameter states. They use the existing `createInstrumentDevice` and `createAudioEffectDevice` builders.

## 7. Key Future Refinements (Summary from `REFINEMENT_SUGGESTIONS.md`)
*   **Audio Path Latency:** The current `postMessage`-based audio path between Worklet and native code needs to be optimized, likely using `SharedArrayBuffer` and a dedicated worker thread for lower latency.
*   **Parameter Sync Callbacks:** Replace polling for plugin UI parameter changes with a true callback mechanism from C++ to Rust to Tauri events.
*   **Error Handling & Stability:** Enhance plugin crash isolation and error propagation across all layers.
*   **Resource Management:** Rigorous review of COM object lifetimes, memory, and handles.
*   **Full Parameter Type Support:** Extend beyond normalized floats.
*   **MIDI Event Handling:** Fully implement for VST instruments.

## 8. Building & Testing PoC
*   **VST3 SDK:** Ensure the `VST3_SDK_DIR` environment variable points to the root of the Steinberg VST3 SDK. The `vst_bridge/build.rs` script relies on this.
*   **Build:** Standard Tauri build process (`npm run tauri build` or `npm run tauri dev`).
*   **Testing:**
    *   Place some VST3 plugins in standard OS locations.
    *   Use the "Scan VST3 Plugins" button/command in the PoC UI.
    *   Select a plugin to load it.
    *   Use "Open Editor" to view the plugin UI.
    *   Refer to `VST3_INTEGRATION_TEST_PLAN.md` for detailed test cases.
```
