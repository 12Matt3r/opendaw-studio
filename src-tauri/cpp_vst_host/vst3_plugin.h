#ifndef VST3_PLUGIN_H
#define VST3_PLUGIN_H

#include <string>
#include <vector>
#include <cstdint> // For int32_t, uint32_t

// Forward declarations for VST3 SDK types to minimize header dependencies.
namespace Steinberg {
    namespace Vst {
        class IComponent;
        class IAudioProcessor;
        class IEditController;
        // class IConnectionPoint; // Not used in current PoC for parameter changes
        // class HostApplication; // No UPtr, using direct interfaces
        using ParamID = uint32_t; // Explicitly use uint32_t
        class BusInfo; // Forward declare if only pointers/references used in header
    }
    template <class T> class IPtr; // For smart pointer usage with VST interfaces
    using TUID = unsigned char[16]; // Standard VST TUID definition
    class IPluginFactory; // Forward declaration
}


class Vst3Plugin {
public:
    Vst3Plugin(const std::string& pluginPath);
    ~Vst3Plugin();

    bool load();
    bool initialize(double sampleRate, int32_t maxBlockSize);
    void terminate();

    int32_t getParameterCount();
    struct ParameterInfo {
        std::string title;
        Steinberg::Vst::ParamID id;
        std::string units;
        int32_t stepCount;
        float defaultValueNormalized;
        // Add other fields as needed, e.g. plain string value
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
    typedef struct HMODULE__* HMODULE; // Windows specific type for library handle
    HMODULE libraryHandle_ = nullptr;
#else
    void* libraryHandle_ = nullptr; // POSIX type for library handle
#endif

    // VST3 Interfaces (using Steinberg::IPtr for automatic ref-counting)
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_ = nullptr;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> audioProcessor_ = nullptr;
    Steinberg::IPtr<Steinberg::Vst::IEditController> editController_ = nullptr;

    double sampleRate_ = 0.0;
    int32_t maxBlockSize_ = 0;

    Steinberg::TUID componentClassID_{}; // Store the TUID of the component to instantiate

    // Function pointer type for GetPluginFactory
    typedef Steinberg::IPluginFactory* (*GetPluginFactoryFunc)();
    GetPluginFactoryFunc getFactoryFunc_ = nullptr;

    // Helper methods
    bool loadSharedLibrary();
    void unloadSharedLibrary();
    bool getPluginFactory();
};

#endif // VST3_PLUGIN_H
