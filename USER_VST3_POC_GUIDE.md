# Basic User Guide: VST3 Plugin Support (Proof-of-Concept) in openDAW

This guide provides a very basic overview of how to interact with the VST3 plugin functionality in the current Proof-of-Concept (PoC) version of the openDAW desktop application.

**Important: This is a PoC and has limitations.** Features are still under development, and you might encounter bugs or incomplete functionality.

## 1. Plugin Scanning
*   **Automatic Scan:** When openDAW starts or when you trigger a scan (if a UI option is available), it will look for VST3 plugins in standard locations on your system:
    *   **Windows:** `C:\Program Files\Common Files\VST3`, `C:\Users\<YourUsername>\AppData\Local\Programs\Common\VST3` (and other standard paths like user-specific Common Files VST3).
    *   **macOS:** `/Library/Audio/Plug-Ins/VST3`, `~/Library/Audio/Plug-Ins/VST3`
    *   **Linux:** `/usr/lib/vst3`, `~/.vst3`, `/usr/local/lib/vst3` (and XDG paths like `~/.local/lib/vst3`).
*   Ensure your VST3 plugins (.vst3 files or bundles) are installed in these locations to be detected.
*   A list of detected plugins should be available through a designated UI element in the PoC (e.g., after clicking a "Scan Plugins" button, or in a plugin browser).

## 2. Adding a VST Plugin to Your Project (Conceptual)
*   The PoC includes new "VST3 Instrument" and "VST3 Audio Effect" boxes or devices within openDAW.
*   You would typically add one of these to your project (e.g., onto an audio or MIDI track) like any other instrument or effect device available in openDAW.
*   Once added, the VST Box will initially be empty or show a placeholder state. You'll need to assign a specific VST3 plugin to it.

## 3. Assigning a Scanned Plugin to a VST Box (Conceptual)
*   After adding an empty VST Box to your project:
    *   There should be an option on the box itself (e.g., a button "Choose VST3 Plugin...", a dropdown menu, or a dedicated section in its properties panel).
    *   This option will likely display the list of compatible VST3 plugins found during the scan.
*   Select the desired plugin (e.g., "Surge XT" for an instrument box, "Valhalla Supermassive" for an effect box) from this list.
*   Once selected, the VST Box should update to indicate that the chosen plugin is now associated with it (e.g., displaying the plugin's name). The system will then attempt to load this plugin into the audio engine.

## 4. Opening the Plugin's Custom Editor
*   Once a plugin is successfully loaded into a VST Box, an "Open Editor," "Show UI," or similar button should become active on the VST Box's interface within openDAW.
*   Clicking this button will command the VST3 plugin to open its own graphical user interface.
*   In this PoC, this UI will appear in a **separate native window** (not embedded within the openDAW interface).
*   You can interact with this native window to control all the visual elements and parameters the plugin developer provided.

## 5. Interacting with Parameters
*   **Plugin's Custom UI:** This is often the most comprehensive way to interact with all of the plugin's parameters, especially for complex plugins. Changes made here should affect the sound.
*   **openDAW Generic UI (Conceptual):**
    *   After a plugin is loaded, openDAW will attempt to retrieve its list of parameters.
    *   The VST Box in openDAW should then display generic controls (like sliders or knobs) for these parameters.
    *   You should be able to adjust these generic controls, and the changes should affect the plugin's sound and also be reflected in the plugin's custom UI if it's open.
    *   Conversely, changes made in the plugin's custom UI should (ideally, within PoC limits) update openDAW's generic controls for that parameter.

## 6. Known Limitations of this PoC
*   **Audio Latency:** You might experience noticeable delay (latency) when processing audio through VST plugins. This is particularly true for VST instruments played live via MIDI. This is a known issue related to how audio is currently passed to the plugin.
*   **Parameter Synchronization:** While basic synchronization between openDAW's generic controls and the plugin's custom UI is implemented, it might not be perfectly smooth, instantaneous, or may miss very rapid changes. This is because the PoC uses a polling mechanism to detect changes from the plugin's UI.
*   **Stability:** This is a Proof-of-Concept. Some plugins might not load correctly, or interactions might lead to unexpected behavior or even application crashes. Save your work frequently if this were a real application.
*   **MIDI Implementation:** Full MIDI routing to VST instruments (including MIDI CCs, pitch bend, aftertouch) might be basic or incomplete in this PoC.
*   **Resource Usage:** Performance (CPU, memory) has not been heavily optimized yet. Loading many complex plugins might strain your system.
*   **Plugin UI Window Management:** The separate plugin UI window's behavior (e.g., whether it stays on top, how it's managed by your OS window manager) is largely controlled by the plugin itself.

Thank you for exploring this early version of VST3 support in openDAW! Your feedback is valuable.
```
