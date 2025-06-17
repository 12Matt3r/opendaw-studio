import { BoxSchema, UniversalBox } from "box-forge"
import { Pointers } from "@/data/pointers"
import { createInstrumentDevice } from "../builder"
import { DefaultParameterPointerRules, StringPointerRules } from "../../defaults"

// This is a placeholder for how VST parameters might be stored.
// Each key would be a VST-specific parameter ID (string or number).
// The value would be its normalized float value (0-1).
// This structure will need to be refined significantly in later steps
// to include automation pointers, discrete value steps, display strings etc.
const Vst3ParameterValuesSchema: BoxSchema<Pointers> = {
	type: "box",
	class: {
		name: "Vst3ParameterValues",
		// Dynamically populated fields based on the specific VST3 plugin loaded.
		// For schema purposes, we define it as allowing any string key (paramID)
		// to a float32 value with default automation rules.
		// Actual population happens at runtime when a plugin is loaded.
		fields: {}, // This will be treated as a map-like structure.
		dynamicFields: {
			type: "float32",
			pointerRules: DefaultParameterPointerRules
		}
	}
};

export const Vst3InstrumentDeviceBox: BoxSchema<Pointers> = createInstrumentDevice("Vst3InstrumentDeviceBox", {
	// --- Core VST3 Plugin Identification ---
	20: { type: "string", name: "pluginPath", pointerRules: StringPointerRules, value: "" }, // Filesystem path to .vst3 file
	21: { type: "string", name: "pluginCID", pointerRules: StringPointerRules, value: "" },  // Specific plugin CID within the .vst3 file

	// --- Cached Plugin Metadata (populated after loading) ---
	22: { type: "string", name: "pluginVendor", value: "", pointerRules: StringPointerRules },
	23: { type: "string", name: "pluginVersion", value: "", pointerRules: StringPointerRules },
	24: { type: "string", name: "pluginSDKVersion", value: "", pointerRules: StringPointerRules }, // e.g., "VST 3.7.0"
	25: { type: "int32", name: "pluginCategory", value: 0 }, // Steinberg::Vst::PlugProvider::PlugCategory, e.g., kInstrument, kEffect

	// --- Plugin Parameters ---
	// This field will point to an instance of Vst3ParameterValuesBox,
	// which will hold the actual parameter states.
	30: {
		type: "field",
		name: "parameters",
		pointerRules: { accepts: [Pointers.Vst3ParameterValues], mandatory: false } // Assuming Pointers.Vst3ParameterValues is added
	},

	// --- UI State (optional, for plugins with complex UIs) ---
	40: { type: "string", name: "editorUiState", value: "", pointerRules: StringPointerRules, mandatory: false }, // Base64 encoded state

	// --- Runtime State (not typically saved in project, but part of device state) ---
	50: { type: "boolean", name: "isLoaded", value: false, pointerRules: {} }, // Transient: is the plugin currently loaded in backend?
	51: { type: "string", name: "loadError", value: "", pointerRules: {}, mandatory: false } // Transient: error message if loading failed
}, Pointers.Vst3InstrumentDevice); // Assuming Pointers.Vst3InstrumentDevice is added for specific targeting
