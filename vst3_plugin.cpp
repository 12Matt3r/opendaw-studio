#include "vst3_plugin.h"

// Conditional includes for platform-specific library loading
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <iostream> // For logging errors, etc.

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
    // For now, assume it's handled by the main application or not strictly needed for this PoC scope.
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
        char* err = dlerror(); // call dlerror to clear error if any before dlsym, and then after to get error
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
    factory_ = rawFactory; // Store as void* for now, cast to IPluginFactory* when used

    // For PoC, let's assume the first plugin in the factory is the one we want.
    // A real host would iterate through PClassInfo entries.
    Steinberg::PClassInfo classInfo;
    if (rawFactory->countClasses() == 0) {
        lastError_ = "Plugin factory has no classes.";
        rawFactory->release(); // Release the factory obtained from GetPluginFactory
        factory_ = nullptr;
        unloadSharedLibrary();
        return false;
    }

    if (rawFactory->getClassInfo(0, &classInfo) != Steinberg::kResultOk) {
        lastError_ = "Failed to get class info for the first plugin.";
        rawFactory->release();
        factory_ = nullptr;
        unloadSharedLibrary();
        return false;
    }

    // Check if it's a VST component
    if (strcmp(classInfo.category, Steinberg::kVstAudioEffectClass) != 0 &&
        strcmp(classInfo.category, Steinberg::kVstComponentControllerClass) != 0 && // some plugins might only list controller
        !strstr(classInfo.category, "Instrument")) { // Basic check for instruments
        // This check might be too simplistic. Some plugins might have custom categories.
        // For PoC, we are interested in audio effect or instrument type components.
        // std::cout << "Warning: Plugin category is '" << classInfo.category << "', not a standard audio effect/instrument." << std::endl;
        // Potentially allow loading anyway, or be stricter.
    }

    memcpy(componentClassID_, classInfo.cid, sizeof(Steinberg::TUID));

    // Create the component instance
    // The host application context is needed here.
    Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostApp = Steinberg::Vst::MinimalHostApplication::getInstance();

    // Using createInstance directly on the factory with the host context
    // This is a more direct way than using PlugProvider for a single plugin.
    Steinberg::Vst::IComponent* rawComponent = nullptr;
    Steinberg::tresult result = rawFactory->createInstance(classInfo.cid, Steinberg::Vst::IComponent::iid, (void**)&rawComponent);

    if (result != Steinberg::kResultOk || !rawComponent) {
        lastError_ = "Failed to create plugin component instance. Result: " + std::to_string(result);
        rawFactory->release(); // Release factory
        factory_ = nullptr;
        unloadSharedLibrary();
        return false;
    }
    component_ = rawComponent; // IPtr takes ownership

    std::cout << "Plugin component instance created: " << classInfo.name << std::endl;

    // The factory can be released after creating the instance
    rawFactory->release();
    factory_ = nullptr;

    // Query for other essential interfaces
    component_->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&audioProcessor_);
    if (!audioProcessor_) {
        lastError_ = "Failed to query IAudioProcessor interface.";
        component_ = nullptr; // Release component
        unloadSharedLibrary();
        return false;
    }
    std::cout << "IAudioProcessor interface queried." << std::endl;

    // Controller is optional for processing, but needed for parameters
    component_->getController((Steinberg::Vst::IEditController**)&editController_);
    // It's okay if editController_ is null for some plugins, though parameters won't work.
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

    // Provide host context to the component
    // The hostApp instance is created using getInstance which returns a raw pointer.
    // The component_->initialize expects an IHostApplication*.
    // IPtr is not strictly necessary here if getInstance() always returns a valid static object.
    if (component_->initialize(Steinberg::Vst::MinimalHostApplication::getInstance()) != Steinberg::kResultOk) {
        lastError_ = "Failed to initialize component.";
        return false;
    }
    std::cout << "Component initialized." << std::endl;


    // Setup processing
    Steinberg::Vst::ProcessSetup setup;
    setup.processMode = Steinberg::Vst::kRealtime; // Or kPrefetch, kOffline
    setup.symbolicSampleSize = Steinberg::Vst::kSample32; // Or kSample64
    setup.maxSamplesPerBlock = maxBlockSize_;
    setup.sampleRate = sampleRate_;

    if (audioProcessor_->setupProcessing(setup) != Steinberg::kResultOk) {
        lastError_ = "Failed to setup processing on audioProcessor.";
        component_->terminate();
        return false;
    }
    std::cout << "AudioProcessor processing setup." << std::endl;

    // Activate component
    if (component_->activate() != Steinberg::kResultOk) {
        lastError_ = "Failed to activate component.";
        audioProcessor_->setupProcessing(Steinberg::Vst::ProcessSetup()); // Try to clean up
        component_->terminate();
        return false;
    }
    std::cout << "Component activated." << std::endl;

    // Bus activation (simplified: activate all found audio buses)
    // A real host would allow user to configure this or save/load configurations.
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
            // Speaker arrangement activation (optional for PoC, but good practice)
            // Steinberg::Vst::SpeakerArrangement arr;
            // if (audioProcessor_->getBusArrangement(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, arr) == Steinberg::kResultOk) {
            //    audioProcessor_->setBusArrangements(&arr, 1, nullptr, 0); // Simplified
            // }
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

    // Connect to controller (if it exists and we want parameter changes)
    // if (editController_ && component_) {
    //     component_->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&componentConnectionPoint_);
    //     editController_->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&controllerConnectionPoint_);
    //     if (componentConnectionPoint_ && controllerConnectionPoint_) {
    //         componentConnectionPoint_->connect(controllerConnectionPoint_);
    //         controllerConnectionPoint_->connect(componentConnectionPoint_);
    //         std::cout << "Connected component and controller connection points." << std::endl;
    //     }
    // }


    return true;
}

void Vst3Plugin::terminate() {
    // Disconnect connection points first if connected
    // if (componentConnectionPoint_ && controllerConnectionPoint_) {
    //     componentConnectionPoint_->disconnect(controllerConnectionPoint_);
    //     controllerConnectionPoint_->disconnect(componentConnectionPoint_);
    //     componentConnectionPoint_ = nullptr;
    //     controllerConnectionPoint_ = nullptr;
    //     std::cout << "Disconnected component and controller connection points." << std::endl;
    // }

    if (component_ && component_->isActive()) {
        component_->deactivate();
        std::cout << "Component deactivated." << std::endl;
    }
    if (component_) {
        component_->terminate(); // Counterpart to component->initialize()
        std::cout << "Component terminated." << std::endl;
    }

    // Release interfaces
    editController_ = nullptr;
    audioProcessor_ = nullptr;
    component_ = nullptr;
    std::cout << "VST3 interfaces released." << std::endl;

    unloadSharedLibrary(); // Unloads .vst3 file
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
        // Steinberg::String128 is wchar_t on Windows, char on macOS/Linux (usually UTF-8)
        // This simplistic conversion might need more robust handling for unicode.
#ifdef _WIN32
        // Convert wide string to UTF-8 for std::string
        // This is a simplified conversion. For production, use Windows API MultiByteToWideChar etc.
        std::wstring wtitle(vstParamInfo.title);
        std::wstring wshortTitle(vstParamInfo.shortTitle);
        std::wstring wunits(vstParamInfo.units);

        // Simple truncation/conversion for PoC
        info.title.assign(wtitle.begin(), wtitle.end());
        // info.units.assign(wunits.begin(), wunits.end());

#else
        info.title = vstParamInfo.title;
        // info.units = vstParamInfo.units; // If it's char directly
#endif
        if (info.title.empty() && vstParamInfo.shortTitle[0] != 0) {
#ifdef _WIN32
            std::wstring wshortTitle(vstParamInfo.shortTitle);
            info.title.assign(wshortTitle.begin(), wshortTitle.end());
#else
            info.title = vstParamInfo.shortTitle;
#endif
        }
         if (vstParamInfo.units[0] != 0) {
#ifdef _WIN32
            std::wstring wunits(vstParamInfo.units);
            info.units.assign(wunits.begin(), wunits.end());
#else
            info.units = vstParamInfo.units;
#endif
        }


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
    // Note: VST3 parameters are typically normalized (0.0 to 1.0)
    return editController_->getParamNormalized(id);
}

bool Vst3Plugin::setParameterValue(Steinberg::Vst::ParamID id, float valueNormalized) {
    if (!editController_) {
        lastError_ = "No edit controller to set parameter value.";
        return false;
    }
    // For live changes, one would typically use IComponent::setParamNormalized or IParameterChanges
    // For PoC, directly setting on controller might be sufficient for some plugins or for initial state.
    // This sets the "program" state. For real-time automation, use IParameterChanges during process().
    if (editController_->setParamNormalized(id, valueNormalized) == Steinberg::kResultOk) {
        // Optional: some plugins require a notification after setting parameter
        // if (component_ && controllerConnectionPoint_) {
        //    editController_->notify(nullptr /*some message for parameter change*/);
        // }
        // A more robust way for changes that should be processed is via IParameterChanges
        // during the process call, or beginEdit/performEdit/endEdit.
        // For this PoC, direct setParamNormalized is the simplest.
        return true;
    }
    lastError_ = "Failed to set parameter value on controller.";
    return false;
}

void Vst3Plugin::process(float** inputs, float** outputs, int32_t numSamples) {
    if (!audioProcessor_ || !component_ || !component_->isActive()) {
        lastError_ = "Plugin not ready for processing.";
        // Optionally zero output buffers if processing cannot occur
        // For PoC, this might be handled by the caller.
        return;
    }

    Steinberg::Vst::ProcessData data;
    data.processMode = Steinberg::Vst::kRealtime; // Match setup
    data.symbolicSampleSize = Steinberg::Vst::kSample32; // Match setup
    data.numSamples = numSamples;
    data.numInputs = component_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    data.numOutputs = component_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);

    // Manage AudioBusBuffers
    // For PoC, assuming inputs/outputs are already prepared by caller to match plugin's bus structure
    // A more robust host would query bus channel counts and adapt.
    std::vector<Steinberg::Vst::AudioBusBuffers> inputBuffers(data.numInputs);
    std::vector<Steinberg::Vst::AudioBusBuffers> outputBuffers(data.numOutputs);

    // Example: Assuming first input bus and first output bus, stereo
    // This needs to be dynamic based on actual bus info.
    if (data.numInputs > 0) {
        // This is a simplification. A real host must check busInfo.channelCount
        // and provide appropriately sized arrays for channelBuffers32/64
        // For now, assuming inputs[0] is for bus 0, inputs[1] for bus 1 if stereo, etc.
        // And that the caller has matched this structure.
        // Example: if plugin has 1 stereo input bus, inputs should be float*[2]
        // inputBuffers[0].numChannels = number of channels in bus 0 (e.g. 2 for stereo)
        // inputBuffers[0].channelBuffers32 = inputs (e.g. {leftChannelPtr, rightChannelPtr})

        // This simplified loop assumes each bus `i` corresponds to `inputs[i]` if mono,
        // or `inputs[i*num_channels_per_bus_0 + j]` if the `inputs` array is flat.
        // For PoC, let's assume inputs/outputs are arrays of buffer arrays,
        // e.g. inputs[bus_index][channel_index]
        // And that the caller has set this up correctly.
        for (int i = 0; i < data.numInputs; ++i) {
            Steinberg::Vst::BusInfo busInfo;
            component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, busInfo);
            inputBuffers[i].numChannels = busInfo.channelCount;
            inputBuffers[i].silenceFlags = 0; // Assume not silent
            inputBuffers[i].channelBuffers32 = inputs[i]; // inputs[i] is float** for bus i
                                                        // where inputs[i][channel_idx] is float*
                                                        // This implies inputs is float***, which is not what was passed.
                                                        // The signature is float** inputs.
                                                        // This means inputs[0] is channel 0, inputs[1] is channel 1 etc.
                                                        // This is more suitable for a single bus.
                                                        // Let's assume for PoC: only the first bus is used or all buses are mono.
        }
        // If we only support one input bus for simplicity in PoC:
        if (data.numInputs > 0) {
             Steinberg::Vst::BusInfo busInfo;
             component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, busInfo);
             inputBuffers[0].numChannels = busInfo.channelCount;
             inputBuffers[0].silenceFlags = 0;
             inputBuffers[0].channelBuffers32 = inputs; // inputs is float**, points to an array of channel (float*) buffers
        }


    }
    if (data.numOutputs > 0) {
        // Similar simplification for outputs
        // if (data.numOutputs > 0) {
        //     Steinberg::Vst::BusInfo busInfo;
        //     component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, busInfo);
        //     outputBuffers[0].numChannels = busInfo.channelCount;
        //     outputBuffers[0].silenceFlags = 0; // Plugin will fill this
        //     outputBuffers[0].channelBuffers32 = outputs;
        // }
        // More general loop (still simplified, assumes one bus per entry in outputs array)
        for (int i = 0; i < data.numOutputs; ++i) {
             Steinberg::Vst::BusInfo busInfo;
             component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i, busInfo);
             outputBuffers[i].numChannels = busInfo.channelCount;
             outputBuffers[i].silenceFlags = 0; // Plugin will fill this
             // This assumes 'outputs' is float*** or similar, which it isn't.
             // Let's revert to the single bus assumption for PoC for clarity.
        }
        if (data.numOutputs > 0) {
             Steinberg::Vst::BusInfo busInfo;
             component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, busInfo);
             outputBuffers[0].numChannels = busInfo.channelCount; // e.g. 2 for stereo
             outputBuffers[0].silenceFlags = 0;
             // outputs is float**, so outputs[0] is L, outputs[1] is R for stereo.
             outputBuffers[0].channelBuffers32 = outputs;
        }
    }


    data.inputs = inputBuffers.data();
    data.outputs = outputBuffers.data();

    // Process context (optional for many plugins, but good to provide)
    // Steinberg::Vst::ProcessContext processContext;
    // processContext.sampleRate = sampleRate_;
    // processContext.projectTimeSamples = 0; // Dummy value
    // processContext.tempo = 120.0;
    // data.processContext = &processContext;

    // Input/Output Parameter Changes (for automation, not covered in detail for PoC)
    // Steinberg::Vst::InputParameterChanges inputChanges(0);
    // Steinberg::Vst::OutputParameterChanges outputChanges(0);
    // data.inputParameterChanges = &inputChanges;
    // data.outputParameterChanges = &outputChanges;


    if (audioProcessor_->process(data) != Steinberg::kResultOk) {
        lastError_ = "AudioProcessor process() call failed.";
        // Handle error, maybe zero output buffers
    }

    // After processing, outputChanges might contain parameter values changed by plugin.
}
