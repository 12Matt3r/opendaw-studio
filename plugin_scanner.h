#ifndef PLUGIN_SCANNER_H
#define PLUGIN_SCANNER_H

#include <string>
#include <vector>
#include <memory> // For std::unique_ptr

// Forward declare VST3 types if possible, or include minimal headers.
// For PClassInfo, we might need the actual definition.
#include "pluginterfaces/base/ipluginbase.h" // For TUID, kResultOk
#include "pluginterfaces/vst/vsttypes.h"     // For String128, etc.

// Define a structure to hold basic plugin metadata obtained during scan.
struct Vst3PluginInfo {
    std::string path;
    std::string name;
    std::string vendor;
    std::string subCategories; // e.g., "Fx|Distortion"
    Steinberg::TUID cid;       // Class ID of the component
    std::string error;         // If any error occurred trying to get info for this specific plugin

    // Default constructor
    Vst3PluginInfo() {
        memset(cid, 0, sizeof(Steinberg::TUID));
    }
};

class PluginScanner {
public:
    PluginScanner();
    ~PluginScanner();

    // Scans standard VST3 plugin locations and returns a list of found plugins.
    std::vector<Vst3PluginInfo> scanForAllPlugins();

    // Scans a specific directory (recursive) for VST3 plugins.
    std::vector<Vst3PluginInfo> scanDirectory(const std::string& directoryPath);

    // Tries to load basic info from a single .vst3 file.
    // Returns a Vst3PluginInfo struct. If loading fails, 'error' field will be set.
    Vst3PluginInfo getPluginInfo(const std::string& pluginFilePath);

private:
    // Platform-specific method to get default VST3 search paths.
    std::vector<std::string> getDefaultScanPaths();

    // Helper to load shared library and get factory (similar to Vst3Plugin but for scanning)
    // This is temporary and light-weight, doesn't create full plugin instances.
#ifdef _WIN32
    typedef struct HMODULE__* HMODULE;
    HMODULE loadLibraryTemp(const std::string& path);
    void unloadLibraryTemp(HMODULE handle);
    void* getFunctionAddressTemp(HMODULE handle, const std::string& funcName);
#else
    void* loadLibraryTemp(const std::string& path);
    void unloadLibraryTemp(void* handle);
    void* getFunctionAddressTemp(void* handle, const std::string& funcName);
#endif

    // Function pointer type for GetPluginFactory
    typedef Steinberg::IPluginFactory* (*GetPluginFactoryFunc)();
};

#endif // PLUGIN_SCANNER_H
