import { BoxSchema, UniversalBox } from "box-forge"
import { Pointers } from "@/data/pointers"
import { createAudioEffectDevice } from "../builder"
import { DefaultParameterPointerRules, StringPointerRules } from "../../defaults"

// Re-using the placeholder from Vst3InstrumentDeviceBox.
// This structure will need refinement.
const Vst3ParameterValuesSchema: BoxSchema<Pointers> = {
	type: "box",
	class: {
		name: "Vst3ParameterValues",
		fields: {},
		dynamicFields: {
			type: "float32",
			pointerRules: DefaultParameterPointerRules
		}
	}
};

export const Vst3AudioEffectDeviceBox: BoxSchema<Pointers> = createAudioEffectDevice("Vst3AudioEffectDeviceBox", {
	// --- Core VST3 Plugin Identification ---
	20: { type: "string", name: "pluginPath", pointerRules: StringPointerRules, value: "" },
	21: { type: "string", name: "pluginCID", pointerRules: StringPointerRules, value: "" },

	// --- Cached Plugin Metadata (populated after loading) ---
	22: { type: "string", name: "pluginVendor", value: "", pointerRules: StringPointerRules },
	23: { type: "string", name: "pluginVersion", value: "", pointerRules: StringPointerRules },
	24: { type: "string", name: "pluginSDKVersion", value: "", pointerRules: StringPointerRules },
	25: { type: "int32", name: "pluginCategory", value: 0 },

	// --- Plugin Parameters ---
	30: {
		type: "field",
		name: "parameters",
		pointerRules: { accepts: [Pointers.Vst3ParameterValues], mandatory: false } // Assuming Pointers.Vst3ParameterValues is added
	},

	// --- UI State (optional) ---
	40: { type: "string", name: "editorUiState", value: "", pointerRules: StringPointerRules, mandatory: false },

	// --- Runtime State ---
	50: { type: "boolean", name: "isLoaded", value: false, pointerRules: {} },
	51: { type: "string", name: "loadError", value: "", pointerRules: {}, mandatory: false }
}, Pointers.Vst3AudioEffectDevice); // Assuming Pointers.Vst3AudioEffectDevice is added
