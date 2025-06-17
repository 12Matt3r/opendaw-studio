// --- Instrument Device Schemas ---
export * from "./instruments/Vst3InstrumentDeviceBox";

// --- Audio Effect Device Schemas ---
export * from "./audio-effects/Vst3AudioEffectDeviceBox";

// --- Aggregation into a common type or list (conceptual) ---
// The actual aggregation of these devices into a usable union type
// (e.g., `AllDevices` or `StudioDevice`) or a list for registration
// likely happens in a different file, possibly a `clash.ts` within this
// directory or a higher-level index file (e.g., src/schema/index.ts
// or src/schema/clash.ts).

// If a union type `Devices` is defined in a local `clash.ts` or similar:
/*
import { Vst3InstrumentDeviceBox } from "./instruments/Vst3InstrumentDeviceBox";
import { Vst3AudioEffectDeviceBox } from "./audio-effects/Vst3AudioEffectDeviceBox";
// ... import other existing device boxes ...

// Example of what might be in a local clash.ts or an aggregation point:
// export type Devices =
//   | typeof Vst3InstrumentDeviceBox.class.name // If using names
//   | typeof Vst3AudioEffectDeviceBox.class.name
//   // | typeof AnotherDeviceBox.class.name
//   ;

// Or, if it's an array for registration:
// export const AllDeviceSchemas = [
//   Vst3InstrumentDeviceBox,
//   Vst3AudioEffectDeviceBox,
//   // ... other device schemas
// ];
*/

// For the purpose of this task, this index file ensures the schemas are exportable.
// The integration into a central device registry/type is a subsequent step
// dependent on the existing patterns in the "studio-boxes" project.
