#include "host_application.h"
#include "pluginterfaces/base/ustring.h" // For Steinberg::String
#include <iostream> // For debug output

namespace Steinberg {
namespace Vst {

MinimalHostApplication::MinimalHostApplication() {
    lastChange.hasChanged = false;
}
MinimalHostApplication::~MinimalHostApplication() {}

MinimalHostApplication* MinimalHostApplication::getInstance() {
    static MinimalHostApplication instance;
    return &instance;
}

// --- IHostApplication ---
tresult PLUGIN_API MinimalHostApplication::getName(String128 name) {
    Steinberg::String hostName("OpenDAW_PoC_Host"); // Updated name
    hostName.copyTo(name, 128);
    return kResultOk;
}

tresult PLUGIN_API MinimalHostApplication::createInstance(TUID cid, TUID iid, void** obj) {
    *obj = nullptr;
    return kNotImplemented;
}

// --- IComponentHandler ---
tresult PLUGIN_API MinimalHostApplication::beginEdit(ParamID id) {
    std::lock_guard<std::mutex> lock(paramChangeMutex);
    std::cout << "Host: Plugin BeginEdit for ParamID: " << id << std::endl;
    // Store this information if needed, e.g., for undo systems or gestures.
    // For current polling, performEdit is the most critical.
    return kResultOk;
}

tresult PLUGIN_API MinimalHostApplication::performEdit(ParamID id, ParamValue valueNormalized) {
    std::lock_guard<std::mutex> lock(paramChangeMutex);
    std::cout << "Host: Plugin PerformEdit for ParamID: " << id << " Value: " << valueNormalized << std::endl;
    lastChange.id = id;
    lastChange.valueNormalized = valueNormalized;
    lastChange.hasChanged = true;
    // In a real host, this would likely post a message to a queue for the main UI thread.
    return kResultOk;
}

tresult PLUGIN_API MinimalHostApplication::endEdit(ParamID id) {
    std::lock_guard<std::mutex> lock(paramChangeMutex);
    std::cout << "Host: Plugin EndEdit for ParamID: " << id << std::endl;
    // Store this information or finalize gestures.
    return kResultOk;
}

tresult PLUGIN_API MinimalHostApplication::restartComponent(int32 flags) {
    std::cout << "Host: Plugin requests restartComponent with flags: " << flags << std::endl;
    // e.g., kReloadComponent, kParamValuesChanged, kLatencyChanged
    // The host should handle these requests appropriately.
    return kResultOk;
}

// --- IComponentHandler2 ---
tresult PLUGIN_API MinimalHostApplication::setDirty(TBool state) {
    std::cout << "Host: Plugin setDirty: " << (state ? "true" : "false") << std::endl;
    // Indicates plugin state has changed and needs saving.
    return kResultOk;
}

tresult PLUGIN_API MinimalHostApplication::requestOpenEditor(const TChar* name) {
    std::cout << "Host: Plugin requests open editor (name: " << (name ? name : "default") << ")" << std::endl;
    // This could be used by a plugin to ask the host to open its editor if it's currently closed.
    // For this PoC, we are opening editor from host side.
    return kNotImplemented; // Or kResultOk if we want to acknowledge but do nothing.
}

tresult PLUGIN_API MinimalHostApplication::startGroupEdit() {
    std::cout << "Host: Plugin startGroupEdit" << std::endl;
    return kResultOk;
}

tresult PLUGIN_API MinimalHostApplication::finishGroupEdit() {
    std::cout << "Host: Plugin finishGroupEdit" << std::endl;
    return kResultOk;
}


// --- FUnknown Implementation ---
tresult PLUGIN_API MinimalHostApplication::queryInterface(const TUID _iid, void** obj) {
    if (FUnknownPrivate::iidEqual(_iid, IHostApplication::iid)) {
        *obj = static_cast<IHostApplication*>(this);
        addRef(); return kResultOk;
    } else if (FUnknownPrivate::iidEqual(_iid, IComponentHandler::iid)) {
        *obj = static_cast<IComponentHandler*>(this);
        addRef(); return kResultOk;
    } else if (FUnknownPrivate::iidEqual(_iid, IComponentHandler2::iid)) {
        // IComponentHandler2 inherits IComponentHandler.
        // If a plugin specifically asks for IComponentHandler2, provide it.
        *obj = static_cast<IComponentHandler2*>(this);
        addRef(); return kResultOk;
    } else if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid)) {
        *obj = static_cast<FUnknown*>(this);
        addRef(); return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API MinimalHostApplication::addRef() {
    return 1000; // Static instance
}

uint32 PLUGIN_API MinimalHostApplication::release() {
    return 1000; // Static instance
}

// --- Parameter Change Handling (for polling from Rust) ---
PluginParamChangeInfo MinimalHostApplication::getAndClearLastParamChange() {
    std::lock_guard<std::mutex> lock(paramChangeMutex);
    if (lastChange.hasChanged) {
        PluginParamChangeInfo currentChange = lastChange;
        lastChange.hasChanged = false; // Clear the flag
        return currentChange;
    }
    return PluginParamChangeInfo{0, 0.0, false}; // No change
}

} // namespace Vst
} // namespace Steinberg
