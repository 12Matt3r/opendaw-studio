#include "vst3_plugin.h"

// Conditional includes for platform-specific library loading
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <iostream> // For logging errors, etc.
#include <vector> // For std::vector

// --- VST3 SDK Includes ---
// These are the core interfaces needed.
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"

// For PClassInfo, IPluginFactory
#include "public.sdk/source/vst/hosting/plugprovider.h" // Helper for loading, includes factory.h
// #include "public.sdk/source/vst/hosting/hostclasses.h" // For Steinberg::Vst::HostApplication
#include "host_application.h" // Include the new host application header


// --- Vst3Plugin Method Implementations ---

Vst3Plugin::Vst3Plugin(const std::string& pluginPath) : pluginPath_(pluginPath) {
    // COM initialization for Windows if not already done globally
#ifdef _WIN32
    // Consider CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    // And CoUninitialize(); in the destructor or when app exits.
    // For a command-line PoC, it might be acceptable to do it once here
    // or require the main application to handle it.
    // For this Tauri integration, COM should be initialized by the Rust thread calling C++ functions if needed.
    // Typically, the main thread of the Tauri app or a dedicated audio thread would handle this.
#endif
}

Vst3Plugin::~Vst3Plugin() {
    terminate();
    // COM uninitialization if done in constructor
}

bool Vst3Plugin::loadSharedLibrary() {
    if (libraryHandle_) {
        lastError_ = "Library already loaded.";
        return true; // Or false, depending on desired strictness
    }

#ifdef _WIN32
    libraryHandle_ = LoadLibraryA(pluginPath_.c_str());
    if (!libraryHandle_) {
        lastError_ = "Failed to load VST3 plugin DLL. Error code: " + std::to_string(GetLastError());
        return false;
    }
#else
    libraryHandle_ = dlopen(pluginPath_.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!libraryHandle_) {
        lastError_ = "Failed to load VST3 plugin bundle/so: ";
        lastError_ += dlerror();
        return false;
    }
#endif
    std::cout << "Plugin library loaded: " << pluginPath_ << std::endl;
    return true;
}

void Vst3Plugin::unloadSharedLibrary() {
    if (libraryHandle_) {
#ifdef _WIN32
        FreeLibrary(libraryHandle_);
#else
        dlclose(libraryHandle_);
#endif
        libraryHandle_ = nullptr;
        getFactoryFunc_ = nullptr;
        std::cout << "Plugin library unloaded: " << pluginPath_ << std::endl;
    }
}

bool Vst3Plugin::getPluginFactory() {
    if (!libraryHandle_) {
        lastError_ = "Library not loaded, cannot get factory.";
        return false;
    }

    getFactoryFunc_ = (GetPluginFactoryFunc)
#ifdef _WIN32
        GetProcAddress(libraryHandle_, "GetPluginFactory");
#else
        dlsym(libraryHandle_, "GetPluginFactory");
#endif

    if (!getFactoryFunc_) {
        lastError_ = "Failed to find GetPluginFactory function in VST3 plugin.";
#ifdef _WIN32
        if(GetLastError()) lastError_ += " Error code: " + std::to_string(GetLastError());
#else
        char* err = dlerror();
        if(err) lastError_ += " Details: " + std::string(err);
#endif
        return false;
    }
    std::cout << "GetPluginFactory function retrieved." << std::endl;
    return true;
}


bool Vst3Plugin::load() {
    if (!loadSharedLibrary()) {
        // lastError_ is set by loadSharedLibrary
        return false;
    }

    if (!getPluginFactory()) {
        // lastError_ is set by getPluginFactory
        unloadSharedLibrary(); // Clean up loaded library
        return false;
    }

    Steinberg::IPluginFactory* rawFactory = getFactoryFunc_();
    if (!rawFactory) {
        lastError_ = "GetPluginFactory did not return a valid factory instance.";
        unloadSharedLibrary();
        return false;
    }
    // factory_ = rawFactory; // Store as void* for now, cast to IPluginFactory* when used
    // We don't need to store the factory pointer if we get all info during load.
    // For this PoC, PlugProvider is not used, direct factory interaction.

    Steinberg::PClassInfo classInfo;
    if (rawFactory->countClasses() == 0) {
        lastError_ = "Plugin factory has no classes.";
        rawFactory->release();
        unloadSharedLibrary();
        return false;
    }

    // For PoC, assume the first plugin component (Audio Effect or Instrument) is the one.
    bool componentFound = false;
    for (int32 i = 0; i < rawFactory->countClasses(); ++i) {
        if (rawFactory->getClassInfo(i, &classInfo) == Steinberg::kResultOk) {
            if (strcmp(classInfo.category, Steinberg::kVstAudioEffectClass) == 0 ||
                strstr(classInfo.category, "Instrument") != nullptr) {
                memcpy(componentClassID_, classInfo.cid, sizeof(Steinberg::TUID));
                componentFound = true;
                break;
            }
        }
    }

    if (!componentFound) {
        // Fallback: if no "Audio Effect" or "Instrument", try the very first one.
        if (rawFactory->getClassInfo(0, &classInfo) != Steinberg::kResultOk) {
            lastError_ = "Failed to get class info for the first plugin, and no specific audio component found.";
            rawFactory->release();
            unloadSharedLibrary();
            return false;
        }
        memcpy(componentClassID_, classInfo.cid, sizeof(Steinberg::TUID));
        std::cout << "Warning: No component explicitly categorized as AudioEffect/Instrument. Using first available: " << classInfo.name << std::endl;
    }

    Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostApp = Steinberg::Vst::MinimalHostApplication::getInstance();
    Steinberg::Vst::IComponent* rawComponent = nullptr;
    Steinberg::tresult result = rawFactory->createInstance(classInfo.cid, Steinberg::Vst::IComponent::iid, (void**)&rawComponent);

    if (result != Steinberg::kResultOk || !rawComponent) {
        lastError_ = "Failed to create plugin component instance. Result: " + std::to_string(result);
        rawFactory->release();
        unloadSharedLibrary();
        return false;
    }
    component_ = rawComponent;

    std::cout << "Plugin component instance created: " << classInfo.name << std::endl;
    rawFactory->release(); // Factory released after instance creation

    component_->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&audioProcessor_);
    if (!audioProcessor_) {
        lastError_ = "Failed to query IAudioProcessor interface.";
        component_ = nullptr;
        unloadSharedLibrary();
        return false;
    }
    std::cout << "IAudioProcessor interface queried." << std::endl;

    component_->getController((Steinberg::Vst::IEditController**)&editController_);
    if (editController_) {
        std::cout << "IEditController interface queried." << std::endl;
    } else {
        std::cout << "Plugin has no IEditController (or failed to query)." << std::endl;
    }

    return true;
}

bool Vst3Plugin::initialize(double sampleRate, int32_t maxBlockSize) {
    if (!component_ || !audioProcessor_) {
        lastError_ = "Plugin not loaded or core interfaces missing.";
        return false;
    }
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    if (component_->initialize(Steinberg::Vst::MinimalHostApplication::getInstance()) != Steinberg::kResultOk) {
        lastError_ = "Failed to initialize component.";
        return false;
    }
    std::cout << "Component initialized." << std::endl;

    Steinberg::Vst::ProcessSetup setup;
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = maxBlockSize_;
    setup.sampleRate = sampleRate_;

    if (audioProcessor_->setupProcessing(setup) != Steinberg::kResultOk) {
        lastError_ = "Failed to setup processing on audioProcessor.";
        component_->terminate(); // Use the component's terminate method
        return false;
    }
    std::cout << "AudioProcessor processing setup." << std::endl;

    if (component_->activate() != Steinberg::kResultOk) {
        lastError_ = "Failed to activate component.";
        // audioProcessor_->setupProcessing(Steinberg::Vst::ProcessSetup()); // Try to clean up - not standard way
        component_->terminate();
        return false;
    }
    std::cout << "Component activated." << std::endl;

    int numInputBuses = component_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    int numOutputBuses = component_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    std::cout << "Found " << numInputBuses << " input audio buses, " << numOutputBuses << " output audio buses." << std::endl;

    for (int i = 0; i < numInputBuses; ++i) {
        Steinberg::Vst::BusInfo busInfo;
        if (component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, busInfo) == Steinberg::kResultOk) {
            if (component_->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, true) == Steinberg::kResultOk) {
                 std::cout << "Activated input bus: " << busInfo.name << std::endl;
            } else {
                 std::cout << "Failed to activate input bus: " << busInfo.name << std::endl;
            }
        }
    }
    for (int i = 0; i < numOutputBuses; ++i) {
        Steinberg::Vst::BusInfo busInfo;
        if (component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i, busInfo) == Steinberg::kResultOk) {
             if (component_->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i, true) == Steinberg::kResultOk) {
                 std::cout << "Activated output bus: " << busInfo.name << std::endl;
            } else {
                 std::cout << "Failed to activate output bus: " << busInfo.name << std::endl;
            }
        }
    }
    return true;
}

void Vst3Plugin::terminate() {
    if (component_ && component_->isActive()) {
        component_->deactivate();
        std::cout << "Component deactivated." << std::endl;
    }
    // The component's terminate method should handle releasing/resetting its own state,
    // including what was done in setupProcessing.
    if (component_) {
        component_->terminate();
        std::cout << "Component (instance) terminated." << std::endl;
    }

    editController_ = nullptr;
    audioProcessor_ = nullptr;
    component_ = nullptr; // Release the main component instance
    std::cout << "VST3 interfaces released." << std::endl;

    unloadSharedLibrary();
}

int32_t Vst3Plugin::getParameterCount() {
    if (!editController_) {
        lastError_ = "No edit controller available to get parameter count.";
        return 0;
    }
    return editController_->getParameterCount();
}

bool Vst3Plugin::getParameterInfo(int32_t index, ParameterInfo& info) {
    if (!editController_) {
        lastError_ = "No edit controller available to get parameter info.";
        return false;
    }
    if (index < 0 || index >= editController_->getParameterCount()) {
        lastError_ = "Parameter index out of bounds.";
        return false;
    }

    Steinberg::Vst::ParameterInfo vstParamInfo;
    if (editController_->getParameterInfo(index, vstParamInfo) == Steinberg::kResultOk) {
        info.id = vstParamInfo.id;
#ifdef _WIN32
        std::wstring wtitle(vstParamInfo.title);
        std::wstring wshortTitle(vstParamInfo.shortTitle);
        std::wstring wunits(vstParamInfo.units);

        // Extremely simplified conversion for PoC. Production code needs WideCharToMultiByte.
        info.title.assign(wtitle.begin(), wtitle.end());
        if (info.title.empty() && !wshortTitle.empty()) {
             info.title.assign(wshortTitle.begin(), wshortTitle.end());
        }
        if (!wunits.empty()) {
            info.units.assign(wunits.begin(), wunits.end());
        }
#else
        info.title = vstParamInfo.title;
        if (info.title.empty() && vstParamInfo.shortTitle[0] != 0) {
            info.title = vstParamInfo.shortTitle;
        }
        if (vstParamInfo.units[0] != 0) {
            info.units = vstParamInfo.units;
        }
#endif
        info.stepCount = vstParamInfo.stepCount;
        info.defaultValueNormalized = vstParamInfo.defaultNormalizedValue;
        return true;
    }
    lastError_ = "Failed to get parameter info from controller.";
    return false;
}

float Vst3Plugin::getParameterValue(Steinberg::Vst::ParamID id) {
    if (!editController_) {
        lastError_ = "No edit controller to get parameter value.";
        return 0.0f;
    }
    return editController_->getParamNormalized(id);
}

bool Vst3Plugin::setParameterValue(Steinberg::Vst::ParamID id, float valueNormalized) {
    if (!editController_) {
        lastError_ = "No edit controller to set parameter value.";
        return false;
    }
    if (editController_->setParamNormalized(id, valueNormalized) == Steinberg::kResultOk) {
        return true;
    }
    lastError_ = "Failed to set parameter value on controller.";
    return false;
}

void Vst3Plugin::process(float** inputs, float** outputs, int32_t numSamples) {
    if (!audioProcessor_ || !component_ || !component_->isActive()) {
        lastError_ = "Plugin not ready for processing.";
        // Zero output buffers if an error occurs or not ready
        if (outputs && component_) { // Check component_ to get bus count
            int numOutputBuses = component_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
            if (numOutputBuses > 0) {
                 Steinberg::Vst::BusInfo busInfo;
                 if(component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, busInfo) == Steinberg::kResultOk) {
                    for(int ch = 0; ch < busInfo.channelCount; ++ch) {
                        if (outputs[ch]) {
                            std::fill_n(outputs[ch], numSamples, 0.0f);
                        }
                    }
                 }
            }
        }
        return;
    }

    Steinberg::Vst::ProcessData data;
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = numSamples;
    data.numInputs = component_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    data.numOutputs = component_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);

    std::vector<Steinberg::Vst::AudioBusBuffers> inputBuffers(data.numInputs);
    std::vector<Steinberg::Vst::AudioBusBuffers> outputBuffers(data.numOutputs);

    // Simplified: assumes inputs/outputs are for the first bus, matching its channel count.
    if (data.numInputs > 0) {
         Steinberg::Vst::BusInfo busInfo;
         component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, busInfo);
         inputBuffers[0].numChannels = busInfo.channelCount;
         inputBuffers[0].silenceFlags = 0;
         inputBuffers[0].channelBuffers32 = inputs;
    }
    if (data.numOutputs > 0) {
         Steinberg::Vst::BusInfo busInfo;
         component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, busInfo);
         outputBuffers[0].numChannels = busInfo.channelCount;
         outputBuffers[0].silenceFlags = 0;
         outputBuffers[0].channelBuffers32 = outputs;
    }

    data.inputs = inputBuffers.data();
    data.outputs = outputBuffers.data();

    if (audioProcessor_->process(data) != Steinberg::kResultOk) {
        lastError_ = "AudioProcessor process() call failed.";
        // Zero output buffers on error
        if (data.numOutputs > 0 && outputs && outputBuffers[0].channelBuffers32) {
            for(uint32_t ch = 0; ch < outputBuffers[0].numChannels; ++ch) {
                if (outputBuffers[0].channelBuffers32[ch]) {
                     std::fill_n(outputBuffers[0].channelBuffers32[ch], numSamples, 0.0f);
                }
            }
        }
    }
}
