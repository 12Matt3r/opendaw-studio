#include "plugin_scanner.h"

#include <iostream> // For debug output
#include <filesystem> // Requires C++17

// VST3 SDK specific includes for IPluginFactory and PClassInfo
#include "pluginterfaces/base/ipluginbase.h" // For IPluginFactory, etc.
#include "public.sdk/source/vst/hosting/plugprovider.h" // For Steinberg::PlugProvider, not strictly needed but has factory.h

// Platform specific includes for library loading
#ifdef _WIN32
#include <windows.h>
// For known folder paths like APPDATA, ProgramFiles
#include <shlobj.h> // For SHGetKnownFolderPath
#include <knownfolders.h> // For FOLDERID_ProgramFiles, FOLDERID_CommonProgramFiles, FOLDERID_LocalAppData
#else
#include <dlfcn.h>
// For Linux/macOS standard paths
#include <cstdlib> // For getenv
#include <pwd.h>   // For getpwuid
#include <unistd.h> // For getuid
#endif

namespace fs = std::filesystem;

// --- PluginScanner Method Implementations ---

PluginScanner::PluginScanner() {
    // COM initialization is handled by main.cpp for this PoC.
}

PluginScanner::~PluginScanner() {
    // COM uninitialization is handled by main.cpp.
}

#ifdef _WIN32
HMODULE PluginScanner::loadLibraryTemp(const std::string& path) {
    return LoadLibraryA(path.c_str());
}
void PluginScanner::unloadLibraryTemp(HMODULE handle) {
    if (handle) FreeLibrary(handle);
}
void* PluginScanner::getFunctionAddressTemp(HMODULE handle, const std::string& funcName) {
    if (!handle) return nullptr;
    return (void*)GetProcAddress(handle, funcName.c_str());
}
#else
void* PluginScanner::loadLibraryTemp(const std::string& path) {
    return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
}
void PluginScanner::unloadLibraryTemp(void* handle) {
    if (handle) dlclose(handle);
}
void* PluginScanner::getFunctionAddressTemp(void* handle, const std::string& funcName) {
    if (!handle) return nullptr;
    return dlsym(handle, funcName.c_str());
}
#endif

Vst3PluginInfo PluginScanner::getPluginInfo(const std::string& pluginFilePath) {
    Vst3PluginInfo info;
    info.path = pluginFilePath;

#ifdef _WIN32
    HMODULE libraryHandle = loadLibraryTemp(pluginFilePath);
#else
    void* libraryHandle = loadLibraryTemp(pluginFilePath);
#endif

    if (!libraryHandle) {
        info.error = "Failed to load library.";
#ifndef _WIN32 // dlerror() is specific to dlopen systems
        const char* dlerr = dlerror();
        if (dlerr) {
            info.error += " Details: ";
            info.error += dlerr;
        }
#else
        info.error += " WinError: " + std::to_string(GetLastError());
#endif
        return info;
    }

    GetPluginFactoryFunc getFactoryFunc = (GetPluginFactoryFunc)getFunctionAddressTemp(libraryHandle, "GetPluginFactory");

    if (!getFactoryFunc) {
        info.error = "Failed to find GetPluginFactory function.";
        unloadLibraryTemp(libraryHandle);
        return info;
    }

    Steinberg::IPluginFactory* factory = getFactoryFunc();
    if (!factory) {
        info.error = "GetPluginFactory call failed or returned null.";
        unloadLibraryTemp(libraryHandle);
        return info;
    }

    // We are interested in the first audio effect or instrument component.
    // A more robust scanner might iterate all classes or specific CIDs.
    bool foundComponent = false;
    for (int32 i = 0; i < factory->countClasses(); ++i) {
        Steinberg::PClassInfo classInfo;
        if (factory->getClassInfo(i, &classInfo) == Steinberg::kResultOk) {
            // Check if it's an audio effect or instrument
            if (strcmp(classInfo.category, Steinberg::kVstAudioEffectClass) == 0 ||
                strstr(classInfo.category, "Instrument") != nullptr) { // Basic check for "Instrument"

                // Steinberg::String128 is wchar_t on Windows, char on macOS/Linux (usually UTF-8)
#ifdef _WIN32
                // Simplified conversion for PoC.
                std::wstring wname(classInfo.name);
                info.name.assign(wname.begin(), wname.end());
                // Vendor and subCategories might also need conversion if used from PClassInfo2/PClassInfoW
                // For PClassInfo, they are char.
                info.vendor = classInfo.vendor; // PClassInfo has char for these
                info.subCategories = classInfo.subCategories;

#else
                info.name = classInfo.name;
                info.vendor = classInfo.vendor;
                info.subCategories = classInfo.subCategories;
#endif
                memcpy(info.cid, classInfo.cid, sizeof(Steinberg::TUID));
                foundComponent = true;
                break; // Found the primary component we're interested in
            }
        }
    }

    if (!foundComponent && factory->countClasses() > 0) {
        // If no explicitly audio effect/instrument, take the first one for basic info
        // but flag that it might not be the desired type or issue a warning.
        // This is a fallback for plugins that might use custom categories.
        Steinberg::PClassInfo classInfo; // Use the first one
        if (factory->getClassInfo(0, &classInfo) == Steinberg::kResultOk) {
#ifdef _WIN32
            std::wstring wname(classInfo.name);
            info.name.assign(wname.begin(), wname.end());
            info.vendor = classInfo.vendor;
            info.subCategories = classInfo.subCategories;
#else
            info.name = classInfo.name;
            info.vendor = classInfo.vendor;
            info.subCategories = classInfo.subCategories;
#endif
            memcpy(info.cid, classInfo.cid, sizeof(Steinberg::TUID));
            // info.error = "Plugin found, but category is not standard Audio/Instrument: " + std::string(classInfo.category);
            // Or just log this instead of making it an error for the PoC
             std::cout << "Warning: Plugin '" << info.name << "' at path '" << pluginFilePath
                      << "' has category: " << classInfo.category << std::endl;
        } else {
             info.error = "Could not get ClassInfo for the first plugin component.";
        }
    } else if (factory->countClasses() == 0) {
        info.error = "Factory reported zero classes.";
    }


    factory->release(); // Release the factory instance
    unloadLibraryTemp(libraryHandle);

    if (info.name.empty() && info.error.empty()) {
         info.error = "No suitable VST component found in the factory or failed to read its info.";
    }

    return info;
}


std::vector<std::string> PluginScanner::getDefaultScanPaths() {
    std::vector<std::string> paths;

#ifdef _WIN32
    // Common VST3 paths on Windows
    // System-wide VST3
    PWSTR programFilesPath = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &programFilesPath))) {
        fs::path p = programFilesPath;
        paths.push_back((p / "Common Files" / "VST3").string());
        CoTaskMemFree(programFilesPath);
    }
    // Some plugins might use ProgramFilesX86 for 32-bit hosts, or if installed there by mistake
    // PWSTR programFilesX86Path = NULL;
    // if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, NULL, &programFilesX86Path))) {
    //     fs::path p = programFilesX86Path;
    //     paths.push_back((p / "Common Files" / "VST3").string());
    //     CoTaskMemFree(programFilesX86Path);
    // }

    // User-specific VST3
    PWSTR localAppDataPath = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppDataPath))) {
        fs::path p = localAppDataPath;
        paths.push_back((p / "Programs" / "Common" / "VST3").string()); // Newer user path
        CoTaskMemFree(localAppDataPath);
    }

    // Steinberg specific paths (often duplicated by Common Files/VST3)
    // Might be relevant for older SDKs or specific vendor installers
    // fs::path commonFiles = fs::path(getenv("COMMONPROGRAMFILES")) / "VST3";
    // paths.push_back(commonFiles.string());
    // fs::path userAppData = fs::path(getenv("APPDATA")) / "VST3"; // This is Roaming, less common for VST3
    // paths.push_back(userAppData.string());


#elif __APPLE__
    // Standard VST3 paths on macOS
    paths.push_back("/Library/Audio/Plug-Ins/VST3");
    if (const char* homeDir = getenv("HOME")) {
        paths.push_back(std::string(homeDir) + "/Library/Audio/Plug-Ins/VST3");
    }
#else // Linux
    // Standard VST3 paths on Linux
    paths.push_back("/usr/lib/vst3");
    paths.push_back("/usr/local/lib/vst3");
    if (const char* homeDir = getenv("HOME")) {
        paths.push_back(std::string(homeDir) + "/.vst3"); // User-specific
        paths.push_back(std::string(homeDir) + "/.local/lib/vst3"); // XDG Base Directory alternative
    }
    // Check for XDG_DATA_DIRS
    if (const char* xdgDataDirs = getenv("XDG_DATA_DIRS")) {
        std::string xdgPaths = xdgDataDirs;
        size_t start = 0;
        size_t end = xdgPaths.find(':');
        while (end != std::string::npos) {
            paths.push_back(xdgPaths.substr(start, end - start) + "/vst3");
            start = end + 1;
            end = xdgPaths.find(':', start);
        }
        paths.push_back(xdgPaths.substr(start) + "/vst3");
    }


#endif
    // Add VST3_PLUGIN_PATH environment variable if set
    if (const char* envPath = getenv("VST3_PLUGIN_PATH")) {
        std::string envPaths = envPath;
        // Split by platform-specific path separator (e.g., ';' on Win, ':' on Nix/Mac)
#ifdef _WIN32
        char separator = ';';
#else
        char separator = ':';
#endif
        size_t start = 0;
        size_t end = envPaths.find(separator);
        while (end != std::string::npos) {
            paths.push_back(envPaths.substr(start, end - start));
            start = end + 1;
            end = envPaths.find(separator, start);
        }
        paths.push_back(envPaths.substr(start));
    }


    // Remove duplicate paths and non-existent ones
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    paths.erase(std::remove_if(paths.begin(), paths.end(), [](const std::string& p){
        return !fs::exists(p) || !fs::is_directory(p);
    }), paths.end());

    return paths;
}

std::vector<Vst3PluginInfo> PluginScanner::scanDirectory(const std::string& directoryPath) {
    std::vector<Vst3PluginInfo> foundPlugins;
    if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
        std::cerr << "Scan directory does not exist or is not a directory: " << directoryPath << std::endl;
        return foundPlugins;
    }

    std::cout << "Scanning directory: " << directoryPath << std::endl;
    for (const auto& entry : fs::recursive_directory_iterator(directoryPath, fs::directory_options::follow_directory_symlinks)) {
        if (entry.is_regular_file() || entry.is_symlink()) {
            std::string extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            if (extension == ".vst3") {
                 // On macOS, .vst3 is a bundle (directory). We need to check this.
#ifdef __APPLE__
                if (!entry.is_directory()) { // If it's a file named .vst3, it's not a bundle.
                    // This case should ideally not happen with correct .vst3 bundles on macOS
                    // std::cout << "Skipping non-bundle .vst3 file on macOS: " << entry.path().string() << std::endl;
                    // continue;
                    // For now, let's try to process it anyway, it might be a malformed bundle or a single file plugin (rare for VST3)
                }
#endif
                std::cout << "Found potential VST3: " << entry.path().string() << std::endl;
                Vst3PluginInfo info = getPluginInfo(entry.path().string());
                if (!info.name.empty() && info.error.empty()) { // Successfully got name and no major error
                    foundPlugins.push_back(info);
                    std::cout << "  -> Added: " << info.name << std::endl;
                } else if (info.error.find("category is not standard") != std::string::npos) {
                    // It's a non-critical error (e.g. wrong category but still a plugin), so add it
                    foundPlugins.push_back(info);
                     std::cout << "  -> Added with warning: " << info.name << " (" << info.error << ")" << std::endl;
                }
                else {
                    std::cerr << "  -> Skipped (error): " << entry.path().string() << " - " << info.error << std::endl;
                }
            }
        }
#ifdef __APPLE__ // Special handling for bundles on macOS
        else if (entry.is_directory()) {
            std::string dir_extension = entry.path().extension().string();
            std::transform(dir_extension.begin(), dir_extension.end(), dir_extension.begin(), ::tolower);
            if (dir_extension == ".vst3") {
                std::cout << "Found potential VST3 bundle: " << entry.path().string() << std::endl;
                Vst3PluginInfo info = getPluginInfo(entry.path().string()); // Pass the bundle path
                if (!info.name.empty() && info.error.empty()) {
                    foundPlugins.push_back(info);
                    std::cout << "  -> Added bundle: " << info.name << std::endl;
                } else if (info.error.find("category is not standard") != std::string::npos) {
                     foundPlugins.push_back(info);
                     std::cout << "  -> Added bundle with warning: " << info.name << " (" << info.error << ")" << std::endl;
                }
                else {
                    std::cerr << "  -> Skipped bundle (error): " << entry.path().string() << " - " << info.error << std::endl;
                }
                // Do not iterate further into the bundle with recursive_directory_iterator
                // For bundles, entry.path() IS the plugin.
                // Potentially disable recursion for this entry if iterator allows, or just skip.
                // fs::recursive_directory_iterator by default does not recurse into bundles if they are opaque.
            }
        }
#endif
    }
    return foundPlugins;
}


std::vector<Vst3PluginInfo> PluginScanner::scanForAllPlugins() {
    std::vector<Vst3PluginInfo> allPlugins;
    std::vector<std::string> pathsToScan = getDefaultScanPaths();

    std::cout << "Default VST3 search paths:" << std::endl;
    for (const auto& path : pathsToScan) {
        std::cout << "- " << path << std::endl;
    }

    for (const auto& path : pathsToScan) {
        std::vector<Vst3PluginInfo> pluginsInDir = scanDirectory(path);
        allPlugins.insert(allPlugins.end(), pluginsInDir.begin(), pluginsInDir.end());
    }

    // Remove duplicates that might occur if paths overlap or symlinks resolve to same file
    std::sort(allPlugins.begin(), allPlugins.end(), [](const Vst3PluginInfo& a, const Vst3PluginInfo& b){
        return a.path < b.path;
    });
    allPlugins.erase(std::unique(allPlugins.begin(), allPlugins.end(), [](const Vst3PluginInfo& a, const Vst3PluginInfo& b){
        return a.path == b.path; // Or compare CIDs if paths could differ but point to same plugin binary
    }), allPlugins.end());

    std::cout << "Total unique plugins found: " << allPlugins.size() << std::endl;
    return allPlugins;
}
