#ifndef VST3_PLUGIN_H
#define VST3_PLUGIN_H

#include <string>
#include <vector>

// Forward declarations for VST3 SDK types to minimize header dependencies.
// Full includes will be in vst3_plugin.cpp.
namespace Steinberg {
    namespace Vst {
        class IComponent;
        class IAudioProcessor;
        class IEditController;
        class IConnectionPoint;
        class HostApplication; // If we use UPtr for pluginInstance
        using ParamID = uint32;
    }
    template <class T> class IPtr;
    // Define UPtr if needed, or use IPtr directly
    // For simplicity, we might start with IPtr and raw pointers where appropriate.
}

class Vst3Plugin {
public:
    Vst3Plugin(const std::string& pluginPath);
    ~Vst3Plugin();

    bool load(); // Combined loading and factory fetching
    bool initialize(double sampleRate, int32_t maxBlockSize);
    void terminate();

    int32_t getParameterCount();
    // Define a simple struct for parameter info for now
    struct ParameterInfo {
        std::string title;
        Steinberg::Vst::ParamID id;
        std::string units;
        int32_t stepCount; // For discrete parameters
        float defaultValueNormalized;
    };
    bool getParameterInfo(int32_t index, ParameterInfo& info);

    float getParameterValue(Steinberg::Vst::ParamID id);
    bool setParameterValue(Steinberg::Vst::ParamID id, float value);

    void process(float** inputs, float** outputs, int32_t numSamples);

    const std::string& getPath() const { return pluginPath_; }
    const std::string& getError() const { return lastError_; }

private:
    std::string pluginPath_;
    std::string lastError_;

    // Shared library handle
#ifdef _WIN32
    typedef struct HMODULE__* HMODULE;
    HMODULE libraryHandle_ = nullptr;
#else
    void* libraryHandle_ = nullptr;
#endif

    // VST3 Interfaces
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_ = nullptr;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> audioProcessor_ = nullptr;
    Steinberg::IPtr<Steinberg::Vst::IEditController> editController_ = nullptr;
    // Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> componentConnectionPoint_ = nullptr;
    // Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> controllerConnectionPoint_ = nullptr;


    // Plugin factory and class info
    // These are typically used during loading and then can be released.
    // Steinberg::IPluginFactory* pluginFactory_ = nullptr; // Raw pointer, manage lifetime

    double sampleRate_ = 0.0;
    int32_t maxBlockSize_ = 0;

    // Bus information (simplified for PoC)
    // We'll need to manage bus structures and activation.

    // Helper for QueryInterface, addRef, release (if not using IPtr extensively)
    // For now, IPtr handles this.

    // Helper to load shared library and get factory
    bool loadSharedLibrary();
    void unloadSharedLibrary();
    bool getPluginFactory(); // Temp helper, might be integrated into load()


    // For storing factory and class ID, used during instance creation
    void* factory_ = nullptr; // Store as IPluginFactory*
    unsigned char componentClassID_[16]{}; // Steinberg::TUID

    // Function pointer type for GetPluginFactory
    typedef Steinberg::IPluginFactory* (*GetPluginFactoryFunc)();
    GetPluginFactoryFunc getFactoryFunc_ = nullptr;
};

#endif // VST3_PLUGIN_H
