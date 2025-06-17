#ifndef VST3_PLUGIN_H
#define VST3_PLUGIN_H

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <mutex>
#include <memory>

// Forward declarations for VST3 SDK types
namespace Steinberg {
    namespace Vst {
        class IComponent;
        class IAudioProcessor;
        class IEditController;
        class IPlugView; // For UI
        using ParamID = uint32_t;
        class BusInfo;
        struct ParameterInfo;
        struct ViewRect; // For UI size
    }
    template <class T> class IPtr;
    using TUID = unsigned char[16];
    class IPluginFactory;
}

// Structure for detailed parameter definition (C++ side)
struct ParamDefCpp { // As defined in previous task
    Steinberg::Vst::ParamID id = 0;
    std::string title;
    std::string short_title;
    std::string units;
    int32_t step_count = 0;
    float default_normalized_value = 0.0f;
    int32_t flags = 0;
    std::vector<std::string> string_values;
};

struct Vst3PluginInstanceInfoWithParams { // As defined in previous task
    int32_t numAudioInputs = 0;
    int32_t numAudioOutputs = 0;
    int32_t numMidiInputs = 0;
    int32_t numMidiOutputs = 0;
    std::vector<ParamDefCpp> parameters;
    std::string error;
};


class Vst3Plugin {
public:
    Vst3Plugin(const std::string& pluginPath, const std::string& pluginCID);
    ~Vst3Plugin();

    bool isLoadedSuccessfully() const { return successfullyLoaded_; }

    bool initialize(double sampleRate, int32_t maxBlockSize);
    void terminate();

    Vst3PluginInstanceInfoWithParams get_info_with_params();
    void process_audio(float** input_buffers, float** output_buffers, int32_t num_samples);
    bool set_parameter(Steinberg::Vst::ParamID param_id, float normalized_value);

    // --- UI Methods ---
    bool open_editor(void* parent_platform_handle);
    void close_editor();
    bool get_editor_rect(Steinberg::Vst::ViewRect& rect); // Pass by reference to fill
    bool isEditorOpen() const { return plugView_ != nullptr && editorOpen_; }

    const std::string& getPath() const { return pluginPath_; }
    const std::string& getCID() const { return pluginCID_; }
    const std::string& getError() const { return lastError_; }

private:
    std::string pluginPath_;
    std::string pluginCID_;
    std::string lastError_;
    bool successfullyLoaded_ = false;

#ifdef _WIN32
    typedef struct HMODULE__* HMODULE;
    HMODULE libraryHandle_ = nullptr;
#else
    void* libraryHandle_ = nullptr;
#endif

    Steinberg::IPtr<Steinberg::Vst::IComponent> component_ = nullptr;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> audioProcessor_ = nullptr;
    Steinberg::IPtr<Steinberg::Vst::IEditController> editController_ = nullptr;
    Steinberg::IPtr<Steinberg::Vst::IPlugView> plugView_ = nullptr; // For UI
    bool editorOpen_ = false; // Flag to track editor state

    double sampleRate_ = 0.0;
    int32_t maxBlockSize_ = 0;
    Steinberg::TUID componentClassID_{};

    typedef Steinberg::IPluginFactory* (*GetPluginFactoryFunc)();
    GetPluginFactoryFunc getFactoryFunc_ = nullptr;

    bool loadSharedLibrary();
    void unloadSharedLibrary();
    bool getPluginFactoryAndInfo();
    bool createComponentInstance();
};

// --- Global VST3 Plugin Instance Management ---
using Vst3PluginHandle = uint32_t;
extern std::map<Vst3PluginHandle, std::unique_ptr<Vst3Plugin>> g_loadedPlugins;
extern std::mutex g_pluginsMutex;
extern Vst3PluginHandle g_nextPluginHandle;

Vst3PluginHandle load_new_vst3_plugin(const std::string& path, const std::string& cid_str);
bool unload_vst3_plugin_instance(Vst3PluginHandle handle);
Vst3Plugin* get_vst3_plugin_instance(Vst3PluginHandle handle);

// --- Global CXX Bridge Helper Declarations (from previous task, ensure they match definition) ---
// These are implemented in vst3_plugin.cpp
bool initialize_plugin_cpp(Vst3PluginHandle handle, double sample_rate, int32_t max_block_size);
Vst3PluginInstanceInfoWithParams get_plugin_info_with_params_cpp(Vst3PluginHandle handle);
std::vector<std::vector<float>> process_audio_cpp(
    Vst3PluginHandle handle,
    const std::vector<std::vector<float>>& inputs_vec_vec,
    int32_t num_samples
);
bool set_parameter_cpp(Vst3PluginHandle handle, uint32_t param_id, float value);

// New CXX Bridge Helpers for UI
bool open_plugin_editor_cpp(Vst3PluginHandle handle, void* parent_window_handle);
void close_plugin_editor_cpp(Vst3PluginHandle handle);
// For get_editor_rect_cpp, returning a struct or multiple values via CXX needs careful definition.
// Let's define a simple struct for rect to be returned by CXX.
struct EditorRectCpp {
    int32_t top = 0;
    int32_t left = 0;
    int32_t bottom = 0;
    int32_t right = 0;
    bool success = false;
};
EditorRectCpp get_plugin_editor_rect_cpp(Vst3PluginHandle handle);
Steinberg::Vst::PluginParamChangeInfo check_for_plugin_initiated_parameter_changes_cpp(Vst3PluginHandle handle);


#endif // VST3_PLUGIN_H
