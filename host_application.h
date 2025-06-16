#ifndef HOST_APPLICATION_H
#define HOST_APPLICATION_H

#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/base/funknown.h" // For FUnknown, Steinberg namespace
#include "pluginterfaces/vst/vsttypes.h"   // For String128, etc.

namespace Steinberg {
namespace Vst {

class MinimalHostApplication : public IHostApplication {
public:
    MinimalHostApplication();
    virtual ~MinimalHostApplication();

    //--- IHostApplication ----------------------------------------------------
    tresult PLUGIN_API getName(String128 name) override;
    tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj) override;

    //--- FUnknown -------------------------------------------------------------
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override;
    uint32 PLUGIN_API addRef() override;
    uint32 PLUGIN_API release() override;

    // Static access method
    static MinimalHostApplication* getInstance();

    // Helper for COM reference counting (optional, FUnknown handles it)
    // void PASCAL DECLSPEC_NOTHROW addRefHost() { addRef(); }
    // void PASCAL DECLSPEC_NOTHROW releaseHost() { release(); }


    // Class factory for this host app (simplified)
    // static IHostApplication* get() {
    //     static MinimalHostApplication instance;
    //     return &instance;
    // }
    // Make it a singleton that can be referenced
    // static Steinberg::IPtr<MinimalHostApplication> instance;
protected:
    // To allow IPtr to manage lifetime if needed, or just keep it static.
    // For this PoC, a static instance is fine.
    // int32 refCount;
};

} // namespace Vst
} // namespace Steinberg

#endif // HOST_APPLICATION_H
