import { invoke } from '@tauri-apps/api/tauri';
import { listen, Event } from '@tauri-apps/api/event'; // For listening to events

// --- Type definitions ---
interface ParameterDefinition { /* ... as before ... */
    id: number; title: string; short_title: string; units: string; step_count: number;
    default_normalized_value: number; flags: number; string_values: string[];
}
interface NativeVst3PluginInstanceInfoWithParams { /* ... as before ... */
    numAudioInputs: number; numAudioOutputs: number; numMidiInputs: number; numMidiOutputs: number;
    parameters: ParameterDefinition[]; error: string;
}
interface WorkletMessageEvent { /* ... as before ... */
    data: {
        type: string; pluginPath?: string; pluginCID?: string; workletInstanceHandle?: number;
        nativePluginHandle?: number; audioData?: Float32Array[]; numSamples?: number;
        paramId?: number; value?: number;
    }
}
// Matches RustEditorRectForTauri
interface EditorRect {
    top: number; left: number; bottom: number; right: number; success: boolean;
}
// Matches VstParamChangedPayload from Rust
interface VstParamChangedEventPayload {
    plugin_handle: number; // This is the Native VST3 Plugin Handle
    param_id: number;
    normalized_value: number;
}


export class VstPluginService {
    private static instance: VstPluginService;
    private workletPorts: Map<number, MessagePort>; // workletInstanceHandle -> MessagePort
    private nativeHandlesMap: Map<number, number>;   // workletInstanceHandle -> nativePluginHandle
    private workletHandlesMap: Map<number, number>;  // nativePluginHandle -> workletInstanceHandle (for reverse lookup)

    private unlistenParamChanged: (() => void) | null = null; // To store unlisten function from Tauri

    private constructor() {
        this.workletPorts = new Map();
        this.nativeHandlesMap = new Map();
        this.workletHandlesMap = new Map();
        console.log("VstPluginService initialized");
        this.listenForNativeParameterChanges();
    }

    public static getInstance(): VstPluginService {
        if (!VstPluginService.instance) {
            VstPluginService.instance = new VstPluginService();
        }
        return VstPluginService.instance;
    }

    public registerWorklet(workletInstanceHandle: number, port: MessagePort) { /* ... as before ... */
        this.workletPorts.set(workletInstanceHandle, port);
        port.onmessage = (event: WorkletMessageEvent) => {
            this.handleMessageFromWorklet(event, port);
        };
        console.log(`VstPluginService: Registered worklet with handle ${workletInstanceHandle}`);
    }
    public unregisterWorklet(workletInstanceHandle: number) { /* ... as before ... */
        const port = this.workletPorts.get(workletInstanceHandle);
        if (port) { port.onmessage = null; }
        this.workletPorts.delete(workletInstanceHandle);
        // nativeHandlesMap and workletHandlesMap are cleaned up in onPluginNodeRemoved
        console.log(`VstPluginService: Unregistered worklet with handle ${workletInstanceHandle}`);
    }

    private async handleMessageFromWorklet(event: WorkletMessageEvent, workletPort: MessagePort) {
        const { type, pluginPath, pluginCID, workletInstanceHandle, nativePluginHandle, audioData, numSamples, paramId, value } = event.data;
        if (workletInstanceHandle === undefined) { /* ... error ... */ return; }

        try {
            switch (type) {
                case 'request-load-plugin':
                    if (pluginPath && pluginCID) {
                        const loadedNativeHandle: number = await invoke('load_vst3_plugin_command', { path: pluginPath, cid: pluginCID });
                        if (loadedNativeHandle > 0) {
                            this.nativeHandlesMap.set(workletInstanceHandle, loadedNativeHandle);
                            this.workletHandlesMap.set(loadedNativeHandle, workletInstanceHandle); // For reverse lookup
                            const sampleRate = audioContext.sampleRate; const blockSize = 128;
                            await invoke('initialize_vst3_plugin_command', { handle: loadedNativeHandle, sampleRate, maxBlockSize: blockSize });
                            const pluginInfoWithParams: NativeVst3PluginInstanceInfoWithParams =
                                await invoke('get_vst3_plugin_info_with_params_command', { handle: loadedNativeHandle });
                            workletPort.postMessage({
                                type: 'plugin-loaded-ack', workletInstanceHandle, nativePluginHandle: loadedNativeHandle,
                                pluginInfo: { numAudioInputs: pluginInfoWithParams.numAudioInputs, numAudioOutputs: pluginInfoWithParams.numAudioOutputs },
                                parameterDefinitions: pluginInfoWithParams.parameters
                            });
                        } else { throw new Error("Load command returned invalid handle."); }
                    } else { throw new Error("Missing path or CID for load request."); }
                    break;
                case 'request-process-audio':
                    if (nativePluginHandle !== undefined && audioData && numSamples !== undefined) {
                        const processedAudio: Float32Array[] = await invoke('process_vst3_audio_command',
                            { handle: nativePluginHandle, inputs: audioData, numSamples });
                        workletPort.postMessage({ type: 'audio-processed-native', workletInstanceHandle, processedAudio });
                    } else { throw new Error("Missing data for process audio request."); }
                    break;
                case 'request-set-parameter': // From Worklet (DAW UI change) to Native Plugin
                    if (nativePluginHandle !== undefined && paramId !== undefined && value !== undefined) {
                        await invoke('set_vst3_parameter_command', { handle: nativePluginHandle, paramId, value });
                    } else { throw new Error("Missing data for set parameter request."); }
                    break;
                default: console.warn(`VstPluginService: Unknown message type '${type}'`);
            }
        } catch (error) { /* ... error handling as before ... */
            const errorMsg = error instanceof Error ? error.message : String(error);
            if (type === 'request-load-plugin') workletPort.postMessage({ type: 'plugin-load-failed', workletInstanceHandle, error: errorMsg });
            else if (type === 'request-process-audio') workletPort.postMessage({ type: 'native-process-error', workletInstanceHandle, error: errorMsg });
        }
    }

    // --- New methods for UI ---
    public async openPluginEditor(workletInstanceHandle: number): Promise<EditorRect | null> {
        const nativeHandle = this.nativeHandlesMap.get(workletInstanceHandle);
        if (nativeHandle === undefined) {
            console.error(`VstPluginService: No native handle found for worklet ${workletInstanceHandle} to open editor.`);
            return null;
        }
        try {
            console.log(`VstPluginService: Requesting open editor for native handle ${nativeHandle}`);
            const rect: EditorRect = await invoke('open_plugin_editor_command', { pluginHandle: nativeHandle });
            if(rect.success) {
                console.log(`VstPluginService: Editor opened for native handle ${nativeHandle}, rect:`, rect);
            } else {
                console.warn(`VstPluginService: Editor opened for native handle ${nativeHandle}, but failed to get initial rect.`);
            }
            return rect; // May contain success:false if get_rect failed post-open
        } catch (error) {
            console.error(`VstPluginService: Error opening plugin editor for native handle ${nativeHandle}:`, error);
            return null;
        }
    }

    public async closePluginEditor(workletInstanceHandle: number): Promise<void> {
        const nativeHandle = this.nativeHandlesMap.get(workletInstanceHandle);
        if (nativeHandle === undefined) {
            console.error(`VstPluginService: No native handle found for worklet ${workletInstanceHandle} to close editor.`);
            return;
        }
        try {
            console.log(`VstPluginService: Requesting close editor for native handle ${nativeHandle}`);
            await invoke('close_plugin_editor_command', { pluginHandle: nativeHandle });
            console.log(`VstPluginService: Editor closed for native handle ${nativeHandle}`);
        } catch (error) {
            console.error(`VstPluginService: Error closing plugin editor for native handle ${nativeHandle}:`, error);
        }
    }

    private async listenForNativeParameterChanges() {
        this.unlistenParamChanged = await listen('vst_param_changed_by_ui', (event: Event<VstParamChangedEventPayload>) => {
            const { plugin_handle, param_id, normalized_value } = event.payload;
            console.log(`VstPluginService: Received 'vst_param_changed_by_ui' event. NativeHandle: ${plugin_handle}, ParamID: ${param_id}, Value: ${normalized_value}`);

            const workletInstanceHandle = this.workletHandlesMap.get(plugin_handle);
            if (workletInstanceHandle !== undefined) {
                const port = this.workletPorts.get(workletInstanceHandle);
                if (port) {
                    port.postMessage({
                        type: 'parameter-update-from-native-ui', // To Vst3WorkletProcessor
                        workletInstanceHandle: workletInstanceHandle, // Not strictly needed by worklet here but good for consistency
                        paramId: param_id,
                        value: normalized_value
                    });
                } else {
                    console.warn(`VstPluginService: No worklet port found for workletInstanceHandle ${workletInstanceHandle} (from nativeHandle ${plugin_handle})`);
                }
            } else {
                console.warn(`VstPluginService: No workletInstanceHandle found for nativeHandle ${plugin_handle} from event.`);
            }
        });
        console.log("VstPluginService: Subscribed to 'vst_param_changed_by_ui' Tauri events.");
    }

    public dispose() { // Call when app is closing
        if (this.unlistenParamChanged) {
            this.unlistenParamChanged();
            this.unlistenParamChanged = null;
        }
        // TODO: Iterate all nativeHandlesMap and call unload_vst3_plugin_command for each.
        this.nativeHandlesMap.forEach(async (nativeHandle, workletHandle) => {
            console.log(`Disposing: Unloading plugin for worklet handle ${workletHandle}, native handle ${nativeHandle}`);
            await this.onPluginNodeRemoved(workletHandle); // Use existing cleanup logic
        });
    }


    public async onPluginNodeRemoved(workletInstanceHandle: number) { /* ... as before, ensure it cleans nativeHandlesMap and workletHandlesMap ... */
        const nativePluginHandle = this.nativeHandlesMap.get(workletInstanceHandle);
        if (nativePluginHandle !== undefined) {
            console.log(`VstPluginService: Plugin node removed for worklet ${workletInstanceHandle}, nativeHandle ${nativePluginHandle}. Requesting unload.`);
            try {
                await invoke('close_plugin_editor_command', { pluginHandle: nativePluginHandle }); // Attempt to close editor first
                await invoke('unload_vst3_plugin_command', { handle: nativePluginHandle });
                console.log(`VstPluginService: Native plugin (handle: ${nativePluginHandle}) unloaded successfully.`);
            } catch (error) {
                console.error(`VstPluginService: Error during cleanup for native plugin (handle: ${nativePluginHandle}):`, error);
            }
            this.nativeHandlesMap.delete(workletInstanceHandle);
            this.workletHandlesMap.delete(nativePluginHandle);
        } else {
            console.warn(`VstPluginService: No native handle found for worklet ${workletInstanceHandle} during removal.`);
        }
        this.unregisterWorklet(workletInstanceHandle);
    }
}

declare var audioContext: AudioContext;
const vstPluginService = VstPluginService.getInstance();
export default vstPluginService;
console.log("VstPluginService instance created and potentially exported.");
