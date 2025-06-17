#include "vst3_plugin.h"
#include "host_application.h"

// VST3 SDK Includes
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstplugview.h" // For IPlugView
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h" // For IPlugViewContentScaleSupport
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <iostream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cctype>

// Helper string128ToStdString (from previous step)
std::string string128ToStdString(const Steinberg::Vst::String128& str128) {
    #ifdef _WIN32
        std::wstring wstr(str128);
        if (wstr.empty()) return "";
        std::string narrow_str(wstr.length() * 2 + 1, '\0'); // Max possible size for UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(), &narrow_str[0], narrow_str.length(), NULL, NULL);
        if (len > 0) narrow_str.resize(len); else narrow_str.clear();
        return narrow_str;
    #else
        return std::string(str128);
    #endif
}

// --- (stringToTUID, Global Management, Vst3Plugin Constructor, Destructor, load/unload library, factory/component instance, initialize, terminate, get_info_with_params, process_audio, Vst3Plugin::set_parameter - mostly same as previous versions, ensure they are complete and correct) ---
// ... (These methods should be copied from the previous correct version of vst3_plugin.cpp) ...
// Helper to convert hex string CID to Steinberg::TUID
bool stringToTUID(const std::string& str, Steinberg::TUID& tuid) {
    if (str.length() != 32) {
        std::cerr << "stringToTUID: Invalid CID string length: " << str.length() << " for '" << str << "'" << std::endl;
        return false;
    }
    for (int i = 0; i < 16; ++i) {
        std::string byteString = str.substr(i * 2, 2);
        if (!isxdigit(static_cast<unsigned char>(byteString[0])) || !isxdigit(static_cast<unsigned char>(byteString[1]))) {
            std::cerr << "stringToTUID: Invalid hex character in byte string: " << byteString << std::endl;
            return false;
        }
        try {
            tuid[i] = static_cast<unsigned char>(std::stoul(byteString, nullptr, 16));
        } catch (const std::exception& e) {
            std::cerr << "stringToTUID: std::stoul conversion error for '" << byteString << "': " << e.what() << std::endl;
            return false;
        }
    }
    return true;
}

std::map<Vst3PluginHandle, std::unique_ptr<Vst3Plugin>> g_loadedPlugins;
std::mutex g_pluginsMutex;
Vst3PluginHandle g_nextPluginHandle = 1;

Vst3Plugin::Vst3Plugin(const std::string& pluginPath, const std::string& pluginCID_str)
    : pluginPath_(pluginPath), pluginCID_(pluginCID_str), successfullyLoaded_(false), editorOpen_(false) { // Init editorOpen_
    if (!stringToTUID(pluginCID_str, componentClassID_)) {
        lastError_ = "Invalid CID format: " + pluginCID_str; return;
    }
    if (!loadSharedLibrary()) { return; }
    if (!getPluginFactoryAndInfo()) { unloadSharedLibrary(); return; }
    if (!createComponentInstance()) { unloadSharedLibrary(); return; }
    successfullyLoaded_ = true;
    std::cout << "Vst3Plugin constructed and basic load completed for CID: " << pluginCID_ << std::endl;
}

Vst3Plugin::~Vst3Plugin() { close_editor(); terminate(); } // Ensure editor is closed before termination

bool Vst3Plugin::loadSharedLibrary() {
    if (libraryHandle_) return true;
#ifdef _WIN32
    libraryHandle_ = LoadLibraryA(pluginPath_.c_str());
    if (!libraryHandle_) {
        lastError_ = "Failed to load VST3 plugin DLL '" + pluginPath_ + "'. Error code: " + std::to_string(GetLastError());
        std::cerr << "ERROR: " << lastError_ << std::endl; return false;
    }
#else
    libraryHandle_ = dlopen(pluginPath_.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!libraryHandle_) {
        lastError_ = "Failed to load VST3 plugin bundle/so '" + pluginPath_ + "': ";
        const char* dlerr = dlerror(); if (dlerr) lastError_ += dlerr;
        std::cerr << "ERROR: " << lastError_ << std::endl; return false;
    }
#endif
    std::cout << "Plugin library loaded: " << pluginPath_ << std::endl; return true;
}

void Vst3Plugin::unloadSharedLibrary() {
    if (libraryHandle_) {
#ifdef _WIN32
        FreeLibrary(libraryHandle_);
#else
        dlclose(libraryHandle_);
#endif
        libraryHandle_ = nullptr; getFactoryFunc_ = nullptr;
        std::cout << "Plugin library unloaded: " << pluginPath_ << std::endl;
    }
}

bool Vst3Plugin::getPluginFactoryAndInfo() {
    if (!libraryHandle_) { lastError_ = "Library not loaded"; std::cerr << "ERROR: " << lastError_ << std::endl; return false; }
    if (!getFactoryFunc_) {
        getFactoryFunc_ = (GetPluginFactoryFunc)
    #ifdef _WIN32
            GetProcAddress(libraryHandle_, "GetPluginFactory");
    #else
            dlsym(libraryHandle_, "GetPluginFactory");
    #endif
        if (!getFactoryFunc_) { lastError_ = "GetProcAddress/dlsym failed"; std::cerr << "ERROR: " << lastError_ << std::endl; return false; }
    }
    Steinberg::IPtr<Steinberg::IPluginFactory> factory = getFactoryFunc_();
    if (!factory) { lastError_ = "GetPluginFactory call failed"; std::cerr << "ERROR: " << lastError_ << std::endl; return false; }
    bool cidFound = false;
    for (int32 i = 0; i < factory->countClasses(); ++i) {
        Steinberg::PClassInfo classInfo;
        if (factory->getClassInfo(i, &classInfo) == Steinberg::kResultOk) {
            if (memcmp(classInfo.cid, componentClassID_, sizeof(Steinberg::TUID)) == 0) {
                cidFound = true; break;
            }
        }
    }
    if (!cidFound) { lastError_ = "CID not found in factory"; std::cerr << "ERROR: " << lastError_ << std::endl; return false; }
    return true;
}

bool Vst3Plugin::createComponentInstance() {
    if (!getFactoryFunc_) { lastError_ = "Factory func not retrieved"; std::cerr << "ERROR: " << lastError_ << std::endl; return false; }
    Steinberg::IPtr<Steinberg::IPluginFactory> factory = getFactoryFunc_();
    if (!factory) { lastError_ = "GetPluginFactory call failed (2nd)"; std::cerr << "ERROR: " << lastError_ << std::endl; return false; }
    Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostApp = Steinberg::Vst::MinimalHostApplication::getInstance();
    Steinberg::Vst::IComponent* rawComponent = nullptr;
    Steinberg::tresult result = factory->createInstance(componentClassID_, Steinberg::Vst::IComponent::iid, (void**)&rawComponent);
    if (result != Steinberg::kResultOk || !rawComponent) { lastError_ = "createInstance failed"; std::cerr << "ERROR: " << lastError_ << std::endl; return false; }
    component_ = rawComponent;
    if(component_->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&audioProcessor_) != Steinberg::kResultOk || !audioProcessor_){
        lastError_ = "Query IAudioProcessor failed"; std::cerr << "ERROR: " << lastError_ << std::endl; component_ = nullptr; return false;
    }
    component_->getController((Steinberg::Vst::IEditController**)&editController_);
    return true;
}

bool Vst3Plugin::initialize(double sampleRate, int32_t maxBlockSize) {
    if (!component_ || !audioProcessor_) { lastError_ = "Not loaded for init"; std::cerr << "ERROR: " << lastError_ << std::endl; return false; }
    sampleRate_ = sampleRate; maxBlockSize_ = maxBlockSize;
    if (component_->initialize(Steinberg::Vst::MinimalHostApplication::getInstance()) != Steinberg::kResultOk) {
        lastError_ = "IComponent::initialize failed"; std::cerr << "ERROR: " << lastError_ << std::endl; return false;
    }
    Steinberg::Vst::ProcessSetup setup;
    setup.processMode = Steinberg::Vst::kRealtime; setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = maxBlockSize_; setup.sampleRate = sampleRate_;
    if (audioProcessor_->setupProcessing(setup) != Steinberg::kResultOk) {
        lastError_ = "setupProcessing failed"; std::cerr << "ERROR: " << lastError_ << std::endl; component_->terminate(); return false;
    }
    for(int dir = 0; dir < 2; ++dir) {
        Steinberg::Vst::BusDirection busDir = (dir == 0) ? Steinberg::Vst::kInput : Steinberg::Vst::kOutput;
        int numBuses = component_->getBusCount(Steinberg::Vst::kAudio, busDir);
        for (int i = 0; i < numBuses; ++i) component_->activateBus(Steinberg::Vst::kAudio, busDir, i, true);
    }
    if (component_->activate() != Steinberg::kResultOk) {
        lastError_ = "IComponent::activate failed"; std::cerr << "ERROR: " << lastError_ << std::endl;
        audioProcessor_->setupProcessing(Steinberg::Vst::ProcessSetup{}); component_->terminate(); return false;
    }
    return true;
}

void Vst3Plugin::terminate() {
    close_editor(); // Ensure editor is closed first
    if (component_ && component_->isActive()) { component_->deactivate(); }
    if (audioProcessor_) { audioProcessor_->setupProcessing(Steinberg::Vst::ProcessSetup{}); }
    if (component_) { component_->terminate(); }
    editController_ = nullptr; audioProcessor_ = nullptr; component_ = nullptr; plugView_ = nullptr; // Release IPlugView
    unloadSharedLibrary(); successfullyLoaded_ = false; editorOpen_ = false;
}

Vst3PluginInstanceInfoWithParams Vst3Plugin::get_info_with_params() {
    Vst3PluginInstanceInfoWithParams resultInfo;
    if (!component_) { resultInfo.error = "Not loaded"; return resultInfo; }
    if (!component_->isActive()) { resultInfo.error = "Not initialized/active"; } // Continue to get info

    for(int dir = 0; dir < 2; ++dir) {
        Steinberg::Vst::BusDirection busDir = (dir == 0) ? Steinberg::Vst::kInput : Steinberg::Vst::kOutput;
        int numBuses = component_->getBusCount(Steinberg::Vst::kAudio, busDir);
        int32_t* targetChannelCount = (dir == 0) ? &resultInfo.numAudioInputs : &resultInfo.numAudioOutputs;
        for (int i = 0; i < numBuses; ++i) {
            Steinberg::Vst::BusInfo busInfo;
            if (component_->getBusInfo(Steinberg::Vst::kAudio, busDir, i, busInfo) == Steinberg::kResultOk) {
                *targetChannelCount += busInfo.channelCount;
            }
        }
    }
    resultInfo.numMidiInputs = component_->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
    resultInfo.numMidiOutputs = component_->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kOutput);

    if (editController_) {
        int32 paramCount = editController_->getParameterCount();
        resultInfo.parameters.reserve(paramCount);
        for (int32 i = 0; i < paramCount; ++i) {
            Steinberg::Vst::ParameterInfo vstParamInfo;
            if (editController_->getParameterInfo(i, vstParamInfo) == Steinberg::kResultOk) {
                ParamDefCpp def;
                def.id = vstParamInfo.id;
                def.title = string128ToStdString(vstParamInfo.title);
                def.short_title = string128ToStdString(vstParamInfo.shortTitle);
                if (def.title.empty() && !def.short_title.empty()) def.title = def.short_title;
                def.units = string128ToStdString(vstParamInfo.units);
                def.step_count = vstParamInfo.stepCount;
                def.default_normalized_value = vstParamInfo.defaultNormalizedValue;
                def.flags = vstParamInfo.flags;
                if (vstParamInfo.stepCount > 0) {
                    for (int32_t j = 0; j <= vstParamInfo.stepCount; ++j) {
                        Steinberg::Vst::ParamValue normalizedValue = static_cast<Steinberg::Vst::ParamValue>(j) / vstParamInfo.stepCount;
                        Steinberg::Vst::String128 stringValue;
                        if (editController_->getParamStringByValue(vstParamInfo.id, normalizedValue, stringValue) == Steinberg::kResultOk) {
                            def.string_values.push_back(string128ToStdString(stringValue));
                        } else { def.string_values.push_back(std::to_string(normalizedValue)); }
                    }
                }
                resultInfo.parameters.push_back(def);
            }
        }
    }
    return resultInfo;
}

void Vst3Plugin::process_audio(float** input_buffers, float** output_buffers, int32_t num_samples) { /* ... same as before ... */
    if (!audioProcessor_ || !component_ || !component_->isActive()) { /* zero outputs */ return; }
    Steinberg::Vst::ProcessData data;
    data.processMode = Steinberg::Vst::kRealtime; data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = num_samples;
    int numInputBuses = component_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    int numOutputBuses = component_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    data.numInputs = (input_buffers && numInputBuses > 0) ? numInputBuses : 0;
    data.numOutputs = (output_buffers && numOutputBuses > 0) ? numOutputBuses : 0;
    std::vector<Steinberg::Vst::AudioBusBuffers> vstInputBuffers(data.numInputs);
    std::vector<Steinberg::Vst::AudioBusBuffers> vstOutputBuffers(data.numOutputs);
    if (data.numInputs > 0) {
        Steinberg::Vst::BusInfo busInfo;
        if (component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, busInfo) == Steinberg::kResultOk) {
            vstInputBuffers[0].numChannels = busInfo.channelCount;
            vstInputBuffers[0].silenceFlags = 0;
            vstInputBuffers[0].channelBuffers32 = input_buffers;
        } else { data.numInputs = 0; }
    }
    if (data.numOutputs > 0) {
        Steinberg::Vst::BusInfo busInfo;
        if (component_->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, busInfo) == Steinberg::kResultOk) {
            vstOutputBuffers[0].numChannels = busInfo.channelCount;
            vstOutputBuffers[0].silenceFlags = 0;
            vstOutputBuffers[0].channelBuffers32 = output_buffers;
        } else { data.numOutputs = 0; }
    }
    data.inputs = data.numInputs > 0 ? vstInputBuffers.data() : nullptr;
    data.outputs = data.numOutputs > 0 ? vstOutputBuffers.data() : nullptr;
    data.inputParameterChanges = nullptr; data.outputParameterChanges = nullptr;
    audioProcessor_->process(data); // simplified error check
}


bool Vst3Plugin::set_parameter(Steinberg::Vst::ParamID param_id, float normalized_value) {
    if (!editController_) { lastError_ = "No edit controller"; return false; }
    if (!component_ || !component_->isActive()) { lastError_ = "Plugin not active"; /* return false; */ } // Some plugins allow set when inactive
    Steinberg::tresult result = editController_->setParamNormalized(param_id, normalized_value);
    if (result != Steinberg::kResultOk) {
        lastError_ = "setParamNormalized failed: " + std::to_string(result);
        return false;
    }
    return true;
}


// --- UI Methods ---
bool Vst3Plugin::open_editor(void* parent_platform_handle) {
    if (!editController_) {
        lastError_ = "No EditController to open editor.";
        std::cerr << "ERROR: " << lastError_ << " for plugin " << pluginCID_ << std::endl;
        return false;
    }
    if (plugView_) { // Editor already open
        std::cout << "Editor for " << pluginCID_ << " already open." << std::endl;
        // Optionally bring to front or focus, platform dependent.
        return true;
    }

    // Set the component handler for parameter changes from UI
    if (editController_->setComponentHandler(Steinberg::Vst::MinimalHostApplication::getInstance()) != Steinberg::kResultOk) {
        lastError_ = "Failed to set component handler on edit controller.";
        std::cerr << "Warning: " << lastError_ << " for plugin " << pluginCID_ << std::endl;
        // Continue anyway, parameter sync from UI to host might not work.
    }

    Steinberg::IPlugView* view = editController_->createView(Steinberg::Vst::ViewType::kEditor);
    if (!view) {
        lastError_ = "EditController failed to create view (kEditor).";
        std::cerr << "ERROR: " << lastError_ << " for plugin " << pluginCID_ << std::endl;
        return false;
    }
    plugView_ = view; // IPtr takes ownership

    // Determine platform type for attachment
    const char* platformType = nullptr;
#if SMTG_OS_WINDOWS
    platformType = Steinberg::kPlatformTypeHWND;
#elif SMTG_OS_MACOS
    platformType = Steinberg::kPlatformTypeNSView;
#elif SMTG_OS_LINUX
    platformType = Steinberg::kPlatformTypeX11EmbedWindowID;
#endif

    if (!platformType || plugView_->isPlatformTypeSupported(platformType) != Steinberg::kResultTrue) {
        lastError_ = "Selected platform type for view not supported by plugin, or platformType is null.";
        if(platformType) lastError_ += " Type: " + std::string(platformType);
        std::cerr << "ERROR: " << lastError_ << " for plugin " << pluginCID_ << std::endl;
        plugView_ = nullptr; // Release view
        return false;
    }

    // For a separate window, parent_platform_handle is often null.
    // The plugin is responsible for creating its own top-level window.
    // If parent_platform_handle is a valid window handle from Tauri, it's for embedding.
    // This PoC targets a separate window, so parent_platform_handle is passed as is (likely null from Rust).
    if (plugView_->attached(parent_platform_handle, platformType) == Steinberg::kResultOk) {
        editorOpen_ = true;
        std::cout << "Plugin editor attached for " << pluginCID_ << ". Platform: " << platformType << std::endl;
        // Initial size negotiation
        Steinberg::ViewRect currentSize;
        if (plugView_->getSize(&currentSize) == Steinberg::kResultOk) {
            plugView_->onSize(&currentSize); // Inform plugin of its current size
        }
        return true;
    } else {
        lastError_ = "plugView->attached failed.";
        std::cerr << "ERROR: " << lastError_ << " for plugin " << pluginCID_ << std::endl;
        plugView_ = nullptr;
        return false;
    }
}

void Vst3Plugin::close_editor() {
    if (plugView_) {
        std::cout << "Closing editor for plugin " << pluginCID_ << std::endl;
        plugView_->removed();
        plugView_ = nullptr; // IPtr releases the view
    }
    editorOpen_ = false;
}

bool Vst3Plugin::get_editor_rect(Steinberg::Vst::ViewRect& rect) {
    if (!plugView_ || !editorOpen_) {
        lastError_ = "Editor not open or no view to get rect.";
        return false;
    }
    if (plugView_->getSize(&rect) == Steinberg::kResultOk) {
        // The `onSize` call might be needed if the plugin needs to recalculate layout before reporting size.
        // plugView_->onSize(&rect);
        return true;
    }
    lastError_ = "plugView->getSize failed.";
    return false;
}


// --- Global Instance Management Function Implementations (same as before) ---
Vst3PluginHandle load_new_vst3_plugin(const std::string& path, const std::string& cid_str) { /* ... */
    std::lock_guard<std::mutex> lock(g_pluginsMutex);
    auto plugin = std::make_unique<Vst3Plugin>(path, cid_str);
    if (!plugin || !plugin->isLoadedSuccessfully()) {
        std::cerr << "Failed to construct/load VST3 plugin. Error: " << (plugin ? plugin->getError() : "Plugin null.") << std::endl;
        return 0;
    }
    Vst3PluginHandle handle = g_nextPluginHandle++;
    g_loadedPlugins[handle] = std::move(plugin); return handle;
}
bool unload_vst3_plugin_instance(Vst3PluginHandle handle) { /* ... */
    std::unique_ptr<Vst3Plugin> pluginToUnload = nullptr; {
        std::lock_guard<std::mutex> lock(g_pluginsMutex);
        auto it = g_loadedPlugins.find(handle);
        if (it != g_loadedPlugins.end()) {
            pluginToUnload = std::move(it->second); g_loadedPlugins.erase(it);
        } else { return false; }
    }
    if (pluginToUnload) { pluginToUnload->terminate(); return true; } return false;
}
Vst3Plugin* get_vst3_plugin_instance(Vst3PluginHandle handle) { /* ... */
    std::lock_guard<std::mutex> lock(g_pluginsMutex);
    auto it = g_loadedPlugins.find(handle);
    if (it != g_loadedPlugins.end()) { return it->second.get(); } return nullptr;
}

// --- CXX Bridge Helper Function Implementations (existing ones + new UI ones) ---
bool initialize_plugin_cpp(Vst3PluginHandle handle, double sr, int32_t bs) { /* ... */
    Vst3Plugin* p = get_vst3_plugin_instance(handle); if(p) return p->initialize(sr, bs); return false;
}
Vst3PluginInstanceInfoWithParams get_plugin_info_with_params_cpp(Vst3PluginHandle h) { /* ... */
    Vst3Plugin* p = get_vst3_plugin_instance(h); if(p) return p->get_info_with_params();
    Vst3PluginInstanceInfoWithParams err; err.error = "Plugin not found (get_plugin_info_with_params_cpp)"; return err;
}
std::vector<std::vector<float>> process_audio_cpp(Vst3PluginHandle h, const std::vector<std::vector<float>>& i, int32_t ns) { /* ... */
    Vst3Plugin* p = get_vst3_plugin_instance(h); if(!p) return {};
    std::vector<std::vector<float>> mutable_i = i;
    std::vector<float*> ip_vec; for (auto& ch : mutable_i) ip_vec.push_back(ch.data());
    float** pi_bufs = ip_vec.empty() ? nullptr : ip_vec.data();
    Vst3PluginInstanceInfoWithParams info = p->get_info_with_params(); if (info.numAudioOutputs == 0 && !info.error.empty()) return {};
    std::vector<std::vector<float>> o_vec(std::max(1,info.numAudioOutputs), std::vector<float>(ns)); // ensure at least 1 output buffer if numAudioOutputs might be 0 due to error in info
    if (info.numAudioOutputs == 0 && info.error.empty()) o_vec.clear(); // No outputs if plugin genuinely has none
    std::vector<float*> op_vec; for (auto& ch : o_vec) op_vec.push_back(ch.data());
    float** po_bufs = op_vec.empty() ? nullptr : op_vec.data();
    p->process_audio(pi_bufs, po_bufs, ns); return o_vec;
}
bool set_parameter_cpp(Vst3PluginHandle h, uint32_t pid, float v) { /* ... */
    Vst3Plugin* p = get_vst3_plugin_instance(h); if(p) return p->set_parameter(pid, v); return false;
}

// New UI CXX helpers
bool open_plugin_editor_cpp(Vst3PluginHandle handle, void* parent_window_handle) {
    Vst3Plugin* plugin = get_vst3_plugin_instance(handle);
    if (plugin) {
        return plugin->open_editor(parent_window_handle);
    }
    std::cerr << "open_plugin_editor_cpp: Plugin not found for handle " << handle << std::endl;
    return false;
}

void close_plugin_editor_cpp(Vst3PluginHandle handle) {
    Vst3Plugin* plugin = get_vst3_plugin_instance(handle);
    if (plugin) {
        plugin->close_editor();
    } else {
        std::cerr << "close_plugin_editor_cpp: Plugin not found for handle " << handle << std::endl;
    }
}

EditorRectCpp get_plugin_editor_rect_cpp(Vst3PluginHandle handle) {
    EditorRectCpp resultRect; // Initializes members to 0/false
    Vst3Plugin* plugin = get_vst3_plugin_instance(handle);
    if (plugin) {
        Steinberg::Vst::ViewRect vstRect;
        if (plugin->get_editor_rect(vstRect)) {
            resultRect.top = vstRect.top;
            resultRect.left = vstRect.left;
            resultRect.bottom = vstRect.bottom;
            resultRect.right = vstRect.right;
            resultRect.success = true;
        } else {
             std::cerr << "get_plugin_editor_rect_cpp: plugin->get_editor_rect failed for handle " << handle << std::endl;
        }
    } else {
        std::cerr << "get_plugin_editor_rect_cpp: Plugin not found for handle " << handle << std::endl;
    }
    return resultRect;
}

// Poll for parameter changes from the plugin UI
Steinberg::Vst::PluginParamChangeInfo check_for_plugin_initiated_parameter_changes_cpp(Vst3PluginHandle handle) {
    // For this PoC, we assume the MinimalHostApplication::getInstance() is the one associated
    // with ALL plugin instances. A more complex app would need to route this to the correct
    // IComponentHandler instance if there were multiple or if it was per-plugin.
    // The Vst3PluginHandle is not used here yet, but would be if the host stored changes per plugin.
    // MinimalHostApplication* hostApp = Steinberg::Vst::MinimalHostApplication::getInstance();
    // return hostApp->getAndClearLastParamChange();

    // To make this specific to a plugin instance, MinimalHostApplication would need to store changes
    // in a map keyed by something related to the plugin, e.g., IEditController*.
    // For now, this global poll is a simplification.
    // The handle isn't used in this simplified version.
    if (g_loadedPlugins.count(handle) == 0) { // Check if handle is valid at least
         std::cerr << "check_for_plugin_initiated_parameter_changes_cpp: Invalid plugin handle " << handle << std::endl;
         return Steinberg::Vst::PluginParamChangeInfo{0, 0.0, false};
    }
    // If the plugin has an editor open and its controller is associated with MinimalHostApplication,
    // then MinimalHostApplication::lastChange would reflect changes from that plugin's UI.
    return Steinberg::Vst::MinimalHostApplication::getInstance()->getAndClearLastParamChange();
}
