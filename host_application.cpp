#include "host_application.h"
#include "pluginterfaces/base/ustring.h" // For Steinberg::String

namespace Steinberg {
namespace Vst {

// --- MinimalHostApplication Implementation ---

MinimalHostApplication::MinimalHostApplication() /*: refCount(1)*/ {}
MinimalHostApplication::~MinimalHostApplication() {}

MinimalHostApplication* MinimalHostApplication::getInstance() {
    static MinimalHostApplication instance;
    return &instance;
}

tresult PLUGIN_API MinimalHostApplication::getName(String128 name) {
    Steinberg::String hostName("VST3HostPoC");
    hostName.copyTo(name, 128);
    return kResultOk;
}

tresult PLUGIN_API MinimalHostApplication::createInstance(TUID cid, TUID iid, void** obj) {
    // This minimal host does not create any sub-instances for the plugin.
    // If a plugin queries for, e.g., a message service or other standard host components,
    // this is where they could be instantiated.
    *obj = nullptr;
    return kNotImplemented;
}

// --- FUnknown Implementation ---
tresult PLUGIN_API MinimalHostApplication::queryInterface(const TUID _iid, void** obj) {
    // Standard FUnknown implementation
    if (FUnknownPrivate::iidEqual(_iid, IHostApplication::iid)) {
        *obj = static_cast<IHostApplication*>(this);
        addRef(); // Increment ref count for the returned interface
        return kResultOk;
    } else if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid)) {
        *obj = static_cast<FUnknown*>(this);
        addRef();
        return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API MinimalHostApplication::addRef() {
    // For a static singleton instance, ref counting can be simplified.
    // If this were dynamically allocated, it would be: return FUnknownPrivate::atomicAdd(refCount, 1);
    return 1000; // Arbitrary large number for static instance
}

uint32 PLUGIN_API MinimalHostApplication::release() {
    // For a static singleton instance, ref counting can be simplified.
    // If this were dynamically allocated:
    // if (FUnknownPrivate::atomicAdd(refCount, -1) == 0) {
    //     delete this;
    //     return 0;
    // }
    // return refCount;
    return 1000; // Arbitrary large number for static instance
}

} // namespace Vst
} // namespace Steinberg
