# VST3 Plugin Integration Test Plan for openDAW

## 1. Introduction
This document outlines the test plan for validating the VST3 plugin integration in the openDAW desktop application. It covers plugin scanning, loading, audio processing, parameter synchronization, and plugin UI interaction.

## 2. Test Environment Setup
*   **Operating Systems:** Windows 10/11, macOS (latest version), Linux (e.g., Ubuntu LTS).
*   **openDAW Version:** Desktop application build including the latest VST3 integration features.
*   **Required Tools:**
    *   Standard audio interface.
    *   MIDI keyboard (for VST instruments).
*   **Test VST3 Plugins:** (A mix of free and potentially commercial if available)
    *   **Instruments:**
        *   Steinberg VST3 SDK Examples (e.g., NoteExpressionSynth, HostChecker from `vst3sdk/public.sdk/samples/vst/`)
        *   Surge XT (Open Source Synthesizer)
        *   Vital (Free version of the wavetable synthesizer)
        *   Helm (Open Source Synthesizer)
    *   **Effects:**
        *   Steinberg VST3 SDK Examples (e.g., AGain, Delay, PitchCorrection from `vst3sdk/public.sdk/samples/vst/`)
        *   Valhalla Supermassive (Free Reverb/Delay)
        *   MeldaProduction MFreeFXBundle (Select a few like MEqualizer, MCompressor, MDelay)
        *   TAL Reverb-4 (Free Reverb)
*   **Test Audio Material:**
    *   Short WAV/MP3 audio clips (mono and stereo, e.g., drum loops, vocal phrases, synth lines) for testing effects.
    *   Simple MIDI clips (e.g., scales, short melodies, chords) for testing instruments.

## 3. Test Cases

### 3.1. Plugin Scanning
*   **TC_SCAN_001:** Verify successful scan of default VST3 paths on each OS.
    *   **Steps:** Launch openDAW. Trigger plugin scan.
    *   **Expected:** All known installed VST3 plugins in default OS locations (e.g., `/Library/Audio/Plug-Ins/VST3`, `C:\Program Files\Common Files\VST3`) are listed in the openDAW plugin browser. No errors in console related to scanning valid plugins.
*   **TC_SCAN_002:** Verify scan of a user-specified custom VST3 path.
    *   **Steps:** Add a custom path containing VST3 plugins to openDAW's scan list. Trigger scan.
    *   **Expected:** Plugins in the custom path are listed, in addition to default path plugins.
*   **TC_SCAN_003:** Verify behavior with an empty custom VST3 path string.
    *   **Steps:** Add an empty string as a custom path. Trigger scan.
    *   **Expected:** Scan completes without errors. Only default path plugins are listed.
*   **TC_SCAN_004:** Verify behavior with an invalid/non-existent custom path.
    *   **Steps:** Add a non-existent directory as a custom path. Trigger scan.
    *   **Expected:** Scan completes. No crashes. Appropriate feedback (e.g., console warning "Path X not found/scannable", or UI notification). No plugins from this path are listed.
*   **TC_SCAN_005 (Advanced):** If possible, test with a known problematic/malformed VST3 (e.g., an incompletely installed plugin or a dummy file renamed to .vst3).
    *   **Steps:** Place such a file in a scanned path. Trigger scan.
    *   **Expected:** Application does not crash. Problematic plugin is either skipped (with a console error/warning) or reported as invalid in the UI. Other valid plugins are scanned correctly.

### 3.2. Plugin Loading & Unloading
*   **TC_LOAD_001:** Load a known valid VST3 instrument from the scanned list.
    *   **Steps:** Select a VSTi from the plugin browser and add it to a track.
    *   **Expected:** Plugin loads successfully. openDAW UI updates to show the loaded plugin's name/basic info. No errors in console. `VstPluginService` logs successful load and initialization.
*   **TC_LOAD_002:** Load a known valid VST3 effect from the scanned list.
    *   **Steps:** Select a VST FX from the plugin browser and add it to an audio track or bus.
    *   **Expected:** Plugin loads successfully. openDAW UI updates. No errors.
*   **TC_LOAD_003:** Unload/Remove an active plugin from a track.
    *   **Steps:** Remove the plugin instance from the track.
    *   **Expected:** Plugin unloads cleanly. `VstPluginService` logs unload. Native resources (memory, file handles) associated with that specific instance are released (verify via OS tools if concerned about leaks during stability testing). No errors. Audio path is restored as if plugin was never there.
*   **TC_LOAD_004:** Attempt to load an incompatible file (e.g., a VST2 .dll/.vst, a random .dll, .txt file) if the scanner accidentally picked it up or if user tries to force load.
    *   **Steps:** (If applicable, depends on scanner robustness) Attempt to load such a file.
    *   **Expected:** Graceful failure. Clear error message in console and ideally to UI. No application crash.
*   **TC_LOAD_005:** Load multiple different VST3 plugins simultaneously (e.g., 2 VSTi, 3 VST FX) on different tracks.
    *   **Steps:** Create multiple tracks, load different plugins on each.
    *   **Expected:** All plugins load correctly and function independently.
*   **TC_LOAD_006:** Load multiple instances of the *same* VST3 plugin on different tracks or slots.
    *   **Steps:** Load PluginA on Track1. Load PluginA again on Track2.
    *   **Expected:** Both instances load. They function independently (e.g., changing a parameter on instance 1 does not affect instance 2).

### 3.3. Audio Processing
*   **TC_AUDIO_001 (Effect - Passthrough):** Load a simple gain/utility effect (e.g., Steinberg AGain). Send a test audio signal through it with gain parameter at unity (0dB change).
    *   **Steps:** Route audio to track with AGain. Set AGain's gain to 0dB. Play audio.
    *   **Expected:** Output audio is identical to input audio. Level meters show consistent levels. (Note: PoC latency due to async processing will be present).
*   **TC_AUDIO_002 (Effect - Modification):** Load a delay effect (e.g., Steinberg Delay or MDelay). Send short audio pulses (e.g., drum hit).
    *   **Steps:** Route audio pulses to track with delay. Enable delay.
    *   **Expected:** Audible delay effect on the output. Output signal changes when delay time, feedback parameters are modified.
*   **TC_AUDIO_003 (Instrument - Basic Sound):** Load a VST instrument (e.g., Surge XT, NoteExpressionSynth). Send MIDI notes using a MIDI keyboard or by drawing notes in a MIDI clip.
    *   **Steps:** Create MIDI track, load VSTi. Play MIDI notes.
    *   **Expected:** Instrument produces sound corresponding to the MIDI notes played. Sound characteristics change if VSTi presets or key parameters are altered.
*   **TC_AUDIO_004 (Silence):** Load an effect. Process silence (no audio input) through it.
    *   **Steps:** Route no audio to track with effect, or mute input clip.
    *   **Expected:** Output is silent. No unexpected noise, hum, or artifacts generated by the plugin itself.
*   **TC_AUDIO_005 (Bypass - if openDAW bypass is implemented):** Test openDAW's generic plugin bypass functionality on a VST3 effect.
    *   **Steps:** Load an audible effect (e.g., reverb). Toggle bypass on/off.
    *   **Expected:** When bypassed, audio is clean. When unbypassed, effect is audible. Smooth transition if possible (no clicks).

### 3.4. Parameter Synchronization
*   **TC_PARAM_001 (Host to Plugin):** Load a plugin with clearly audible parameters (e.g., filter cutoff on a synth, mix level on a reverb). Change this parameter from openDAW's generic UI controls for the plugin.
    *   **Steps:** Adjust parameter from openDAW.
    *   **Expected:** Sonic change in plugin output. If plugin's custom UI is also open, the corresponding control in the custom UI updates to reflect the new value. `Vst3WorkletProcessor` sends `request-set-parameter`. C++ `set_parameter_cpp` is called.
*   **TC_PARAM_002 (Plugin UI to Host):** Load a plugin. Open its custom UI. Change a key parameter from within the plugin's custom UI.
    *   **Steps:** Adjust parameter in plugin's native UI.
    *   **Expected:** Sonic change. openDAW's generic UI for that parameter updates to the new value. `MinimalHostApplication::performEdit` is called. Rust polling detects change. Tauri event `vst_param_changed_by_ui` is emitted. `VstPluginService` relays to worklet. Worklet posts `vst3-parameter-update-for-daw`.
*   **TC_PARAM_003 (Discrete Parameters):** Test with a plugin parameter that has discrete steps (e.g., oscillator waveform selector: Sine, Square, Saw).
    *   **Steps:** Change from host UI, change from plugin UI.
    *   **Expected:** Sync works correctly for both directions. String values for steps are displayed correctly in openDAW generic UI (if this UI part is implemented). Parameter definitions retrieved should include `string_values`.
*   **TC_PARAM_004 (Automation - Conceptual):** Draw automation for a VST parameter in an openDAW automation lane. Play back.
    *   **Steps:** Automate a parameter. Play.
    *   **Expected:** Plugin's sound changes according to the automation curve. `Vst3WorkletProcessor` receives these changes via its `parameters` argument in `process()`.
*   **TC_PARAM_005 (Multiple Instances):** Load two instances of the same plugin. Open both UIs if possible. Change a parameter on instance 1 (either from host or plugin UI).
    *   **Steps:** Adjust param on instance 1. Observe instance 2.
    *   **Expected:** Only instance 1 is affected. Instance 2's parameters and sound remain unchanged.

### 3.5. Plugin UI Interaction (Separate Window)
*   **TC_UI_001 (Open Editor):** For a loaded plugin, click the "Open Editor" button in openDAW.
    *   **Steps:** Click button.
    *   **Expected:** Plugin's custom UI opens in a separate native window. Window is responsive. `open_plugin_editor_command` is successful.
*   **TC_UI_002 (Close Editor via Plugin):** Close the plugin's custom UI window using its own window close button (X).
    *   **Steps:** Click plugin window's close button.
    *   **Expected:** Window closes cleanly. Plugin remains active and processing audio. (Note: Host notification of this close might not be standard; openDAW's "Close Editor" button is the more reliable way to track state).
*   **TC_UI_003 (Close Editor via Host):** Click openDAW's "Close Editor" button (if available, or trigger equivalent action).
    *   **Steps:** Click host's close editor button.
    *   **Expected:** Plugin window closes. `close_plugin_editor_command` is successful.
*   **TC_UI_004 (Interaction with Editor):** Interact with various controls (knobs, sliders, buttons) in the plugin's custom UI.
    *   **Steps:** Manipulate UI elements.
    *   **Expected:** UI is responsive. Changes trigger parameter updates as per TC_PARAM_002.
*   **TC_UI_005 (Open Editor for Multiple Plugins):** Load several plugins and open their UIs.
    *   **Steps:** Load 2-3 plugins, open each editor.
    *   **Expected:** All UIs open (may be overlapping). Each is interactive and controls its respective plugin instance.
*   **TC_UI_006 (Editor Rect):** After opening an editor, verify if `get_plugin_editor_rect_command` (if used by host to manage window) returns reasonable dimensions.
    *   **Steps:** Open editor, then (conceptually) check rect.
    *   **Expected:** Returned rect is sensible. (Plugin manages its own window size mostly).

### 3.6. Stability & Performance (Basic Qualitative Checks)
*   **TC_STABILITY_001 (Repeated Load/Unload):** Load and unload a single plugin 10-20 times consecutively.
    *   **Steps:** Add plugin to track, remove, repeat.
    *   **Expected:** No application crashes, no obvious memory leaks (monitor task manager/activity monitor for excessive growth). No errors in console.
*   **TC_STABILITY_002 (Rapid Parameter Changes):** Rapidly change parameters from host generic UI and (if possible) simultaneously from plugin's custom UI.
    *   **Steps:** Vigorously adjust parameters.
    *   **Expected:** Application remains stable and responsive. Audio output changes without glitches (beyond normal DSP behavior).
*   **TC_STABILITY_003 (Open/Close UI Repeatedly):** Open and close a plugin's custom UI 10-20 times.
    *   **Steps:** Click "Open Editor", then "Close Editor" repeatedly.
    *   **Expected:** No crashes, UI opens and closes correctly each time.
*   **TC_PERF_001 (CPU Usage - Basic):** With a project playing, add several VST instruments and effects.
    *   **Steps:** Build up a small project with 3-5 VST plugins. Play.
    *   **Expected:** Audio playback is smooth (no dropouts at reasonable buffer sizes). CPU usage in openDAW and system monitor is within expected range for the plugins used (this is highly subjective and depends on plugins). Watch for runaway CPU usage.

## 4. Test Data Management
*   Test audio clips, MIDI files, and example openDAW project files used for testing should be stored in a shared, version-controlled location.
*   Screenshots/videos of issues should be captured and stored with defect reports.

## 5. Defect Reporting
*   Bugs should be reported in the project's issue tracker.
*   Reports should include:
    *   Test Case ID (if applicable).
    *   OS and version.
    *   openDAW version/build.
    *   VST3 Plugin name and version.
    *   Clear steps to reproduce.
    *   Actual vs. Expected results.
    *   Relevant logs (from browser console, Rust/Tauri console, C++ if possible).
    *   Screenshots/videos.
    ```
