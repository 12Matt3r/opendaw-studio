#ifndef HOST_APPLICATION_H
#define HOST_APPLICATION_H

#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstcomponent.h" // For IComponentHandler, IComponentHandler2
#include "pluginterfaces/base/funknown.h"    // For FUnknown, Steinberg namespace
#include "pluginterfaces/vst/vsttypes.h"      // For String128, ParamID, ParamValue, etc.

#include <string>
#include <vector>
#include <mutex>
#include <map> // To store last changed param per plugin instance (or globally for PoC)

namespace Steinberg {
namespace Vst {

// Struct to hold parameter change information initiated by the plugin UI
struct PluginParamChangeInfo {
    ParamID id = 0;
    ParamValue valueNormalized = 0.0;
    bool hasChanged = false; // Flag to indicate if a change occurred since last poll
};


class MinimalHostApplication : public IHostApplication, public IComponentHandler, public IComponentHandler2 {
public:
    MinimalHostApplication();
    virtual ~MinimalHostApplication();

    //--- IHostApplication ----------------------------------------------------
    tresult PLUGIN_API getName(String128 name) override;
    tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj) override;

    //--- IComponentHandler ---------------------------------------------------
    tresult PLUGIN_API beginEdit(ParamID id) override;
    tresult PLUGIN_API performEdit(ParamID id, ParamValue valueNormalized) override;
    tresult PLUGIN_API endEdit(ParamID id) override;
    tresult PLUGIN_API restartComponent(int32 flags) override; // For IComponentHandler

    //--- IComponentHandler2 --------------------------------------------------
    // (IComponentHandler2 inherits from IComponentHandler)
    tresult PLUGIN_API setDirty(TBool state) override;
    tresult PLUGIN_API requestOpenEditor(const TChar* name = nullptr) override; // name is deprecated
    tresult PLUGIN_API startGroupEdit() override;
    tresult PLUGIN_API finishGroupEdit() override;

    //--- FUnknown -------------------------------------------------------------
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override;
    uint32 PLUGIN_API addRef() override;
    uint32 PLUGIN_API release() override;

    // Static access method
    static MinimalHostApplication* getInstance();

    // --- Parameter Change Handling (for polling from Rust) ---
    // This is a simplified mechanism for the PoC.
    // A Vst3PluginHandle would ideally be passed to identify which plugin's param changed.
    // For a single global instance of MinimalHostApplication, we'll store one change at a time for now.
    // Or, use a map: map<Vst3PluginHandle_or_ControllerPtr, PluginParamChangeInfo>
    // For PoC, let's assume a single active editor context for simplicity of polling data.
    PluginParamChangeInfo getAndClearLastParamChange();

private:
    // For simplicity in PoC, store the last single parameter change.
    // A real host would need a queue or map per plugin instance/controller.
    PluginParamChangeInfo lastChange;
    std::mutex paramChangeMutex;
};

} // namespace Vst
} // namespace Steinberg

#endif // HOST_APPLICATION_H
