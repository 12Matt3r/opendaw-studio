// Vst3WorkletProcessor.ts

declare var sampleRate: number;

interface AudioWorkletProcessor { /* ... as before ... */
  readonly port: MessagePort;
  process(inputs: Float32Array[][], outputs: Float32Array[][], parameters: Record<string, Float32Array>): boolean;
}
declare function registerProcessor(name: string, processorCtor: new (options?: any) => AudioWorkletProcessor): void;

interface ParameterDef { /* ... as before ... */
    id: number; title: string; // etc.
}
interface Vst3ProcessorOptions { /* ... as before ... */
  processorOptions: { pluginPath: string; pluginCID: string; pluginInstanceHandle: number; };
}

class Vst3WorkletProcessor extends AudioWorkletProcessor {
  private pluginPath: string;
  private pluginCID: string;
  private pluginInstanceHandle: number;

  private nativePluginHandle: number | null = null;
  private pluginInfo: any | null = null;
  private parameterDefinitions: ParameterDef[] = [];
  private pluginLoadedSuccessfully = false;
  private awaitingNativeProcess = false;
  private lastProcessedOutput: Float32Array[][] | null = null;

  // Store last known values from DAW automation to compare with native UI changes
  private dawParameterValues: Record<number, number> = {};


  constructor(options: Vst3ProcessorOptions) { /* ... as before ... */
    super();
    this.pluginPath = options.processorOptions.pluginPath;
    this.pluginCID = options.processorOptions.pluginCID;
    this.pluginInstanceHandle = options.processorOptions.pluginInstanceHandle;
    this.port.onmessage = this.handleMessage.bind(this);
    this.port.postMessage({ type: 'request-load-plugin', pluginPath: this.pluginPath, pluginCID: this.pluginCID, workletInstanceHandle: this.pluginInstanceHandle });
  }

  handleMessage(event: MessageEvent) {
    const { type, data } = event.data;

    switch (type) {
      case 'plugin-loaded-ack': /* ... as before, store definitions ... */
        if (data.workletInstanceHandle === this.pluginInstanceHandle) {
          this.nativePluginHandle = data.nativePluginHandle;
          this.pluginInfo = data.pluginInfo;
          this.parameterDefinitions = data.parameterDefinitions || [];
          this.pluginLoadedSuccessfully = true;
          console.log(`Vst3Worklet (h:${this.pluginInstanceHandle}): Plugin loaded. NativeH: ${this.nativePluginHandle}, Params: ${this.parameterDefinitions.length}`);
          this.port.postMessage({ type: 'vst3-parameter-definitions', workletInstanceHandle: this.pluginInstanceHandle, nativePluginHandle: this.nativePluginHandle, definitions: this.parameterDefinitions });
        }
        break;
      case 'plugin-load-failed': /* ... as before ... */
        if (data.workletInstanceHandle === this.pluginInstanceHandle) {
            this.pluginLoadedSuccessfully = false; console.error(`Vst3Worklet (h:${this.pluginInstanceHandle}): Load FAILED. Error: ${data.error}`);
            this.port.postMessage({ type: 'vst3-load-error', workletInstanceHandle: this.pluginInstanceHandle, error: data.error });
        }
        break;
      case 'audio-processed-native': /* ... as before ... */
        if (data.workletInstanceHandle === this.pluginInstanceHandle) {
          this.lastProcessedOutput = data.processedAudio; this.awaitingNativeProcess = false;
        }
        break;
      case 'native-process-error': /* ... as before ... */
         if (data.workletInstanceHandle === this.pluginInstanceHandle) {
            console.error(`Vst3Worklet (h:${this.pluginInstanceHandle}): Native process error: ${data.error}`); this.awaitingNativeProcess = false;
         }
         break;
      case 'parameter-update-from-native-ui': // New: Parameter changed in Native UI
        if (data.workletInstanceHandle === this.pluginInstanceHandle || data.nativePluginHandle === this.nativePluginHandle) { // Check either handle
            const { paramId, value } = data;
            console.log(`Vst3Worklet (h:${this.pluginInstanceHandle}): Received param update from Native UI. ParamID: ${paramId}, Value: ${value}`);

            // Store this new value as the current "DAW" value to avoid feedback loops
            // if DAW UI is also updated by this event.
            this.dawParameterValues[paramId] = value;

            // Inform the main openDAW application/UI layer about this change.
            // This allows openDAW's generic UI controls for this parameter to update.
            this.port.postMessage({
                type: 'vst3-parameter-update-for-daw', // Message for DAW's internal parameter state / generic UI
                workletInstanceHandle: this.pluginInstanceHandle,
                paramId: paramId,
                value: value
            });
        }
        break;
    }
  }

  process(inputs: Float32Array[][], outputs: Float32Array[][], parameters: Record<string, Float32Array>): boolean {
    if (!this.pluginLoadedSuccessfully || this.nativePluginHandle === null) { /* ... passthrough ... */ return true; }

    // Handle OpenDAW automation parameter changes -> VST parameter changes
    for (const paramIdStr in parameters) {
        const vstParamId = parseInt(paramIdStr, 10);
        if (isNaN(vstParamId)) continue;

        const paramValueArray = parameters[paramIdStr];
        if (paramValueArray && paramValueArray.length > 0) {
            const normalizedValue = paramValueArray[0];

            // Only send update if value has changed from what DAW last knew or what native UI set
            if (this.dawParameterValues[vstParamId] !== normalizedValue) {
                this.dawParameterValues[vstParamId] = normalizedValue; // Update our tracked value

                this.port.postMessage({
                    type: 'request-set-parameter',
                    workletInstanceHandle: this.pluginInstanceHandle,
                    nativePluginHandle: this.nativePluginHandle,
                    paramId: vstParamId,
                    value: normalizedValue,
                });
            }
        }
    }

    // Audio processing request and output handling (same as before)
    const inputAudioDataForNative: Float32Array[] = []; /* ... populate ... */
    if (inputs.length > 0 && inputs[0]) {
        for (let i = 0; i < inputs[0].length; i++) inputAudioDataForNative.push(new Float32Array(inputs[0][i]));
    }
    if (!this.awaitingNativeProcess) {
        this.awaitingNativeProcess = true;
        this.port.postMessage({ type: 'request-process-audio', workletInstanceHandle: this.pluginInstanceHandle,
            nativePluginHandle: this.nativePluginHandle, audioData: inputAudioDataForNative,
            numSamples: outputs[0]?.[0]?.length || 128 });
    }
    if (this.lastProcessedOutput && outputs.length > 0 && outputs[0]) { /* ... render lastProcessedOutput ... */
        for (let ch = 0; ch < Math.min(this.lastProcessedOutput.length, outputs[0].length); ++ch) {
            if (this.lastProcessedOutput[ch] && outputs[0][ch]) {
                if (this.lastProcessedOutput[ch].length === outputs[0][ch].length) outputs[0][ch].set(this.lastProcessedOutput[ch]);
                else outputs[0][ch].fill(0); // Length mismatch
            }
        }
        this.lastProcessedOutput = null;
    } else if (inputs.length > 0 && inputs[0] && outputs.length > 0 && outputs[0]) { /* ... passthrough ... */
        for (let ch = 0; ch < Math.min(inputs[0].length, outputs[0].length); ++ch) {
            if (inputs[0][ch] && outputs[0][ch]) outputs[0][ch].set(inputs[0][ch]);
        }
    } else if (outputs.length > 0 && outputs[0]) { /* ... silence ... */
        for (let ch = 0; ch < outputs[0].length; ++ch) if (outputs[0][ch]) outputs[0][ch].fill(0);
    }
    return true;
  }
}

registerProcessor('vst3-worklet-processor', Vst3WorkletProcessor);
