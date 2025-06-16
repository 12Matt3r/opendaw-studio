#include "plugin_scanner.h"

#include <iostream> // For debug output
#include <filesystem> // Requires C++17
#include <algorithm> // for std::sort, std::unique, std::remove_if
#include <vector> // ensure vector is included

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
        try {
            return !fs::exists(p) || !fs::is_directory(p);
        } catch (const fs::filesystem_error& e) {
            // This can happen if a path is invalid or permissions are insufficient
            std::cerr << "Filesystem error checking path '" << p << "': " << e.what() << std::endl;
            return true; // Treat as non-existent or problematic
        }
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
    fs::recursive_directory_iterator dirIter;
    try {
        dirIter = fs::recursive_directory_iterator(directoryPath, fs::directory_options::follow_directory_symlinks);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating directory iterator for " << directoryPath << ": " << e.what() << std::endl;
        return foundPlugins;
    }


    for (const auto& entry : dirIter) {
        try {
            bool isVst3Extension = false;
            fs::path entryPath = entry.path();
            std::string extension = entryPath.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c){ return static_cast<char>(::tolower(c)); });

            if (extension == ".vst3") {
                isVst3Extension = true;
            }

            if (entry.is_regular_file() || (entry.is_directory() && isVst3Extension) || entry.is_symlink() ) { // Symlinks could point to files or bundles
                 if (isVst3Extension) { // Check extension again, as symlinks might not have it directly
                    std::cout << "Found potential VST3: " << entryPath.string() << std::endl;
                    Vst3PluginInfo info = getPluginInfo(entryPath.string());
                    if (!info.name.empty() && info.error.empty()) {
                        foundPlugins.push_back(info);
                        std::cout << "  -> Added: " << info.name << std::endl;
                    } else if (info.error.find("category is not standard") != std::string::npos) {
                        foundPlugins.push_back(info);
                        std::cout << "  -> Added with warning: " << info.name << " (" << info.error << ")" << std::endl;
                    }
                    else {
                        std::cerr << "  -> Skipped (error/no name): " << entryPath.string() << " - " << info.error << std::endl;
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            // Catch errors from accessing entry properties, e.g. permission denied
            std::cerr << "Filesystem error processing entry in " << directoryPath << ": " << e.what() << std::endl;
            // If it's a directory and we can't iterate into it, skip it.
            // For recursive_directory_iterator, this usually means stopping recursion for that path.
            if (entry.is_directory()) {
                 // dirIter.disable_recursion_pending(); // C++17, use pop() or no_push() in C++20/23 if needed.
                 // For now, just log and continue. The iterator might handle some errors gracefully.
            }
            continue;
        }
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

// This function is what the CXX bridge will call.
// It needs to be available for the CXX bridge.
// We might need a wrapper if the Vst3PluginInfo struct needs modification for CXX.
std::vector<Vst3PluginInfo> scan_plugins_cpp() {
    PluginScanner scanner;
#ifdef _WIN32
    // COM should be initialized by the caller of this CXX bridge function,
    // typically once per thread when the application/Tauri plugin starts.
    // Or, ensure PluginScanner itself doesn't rely on COM per-call if CoInitialize happens higher up.
    // For this PoC, we assume main.rs (or the thread it's called from) handles COM.
#endif
    return scanner.scanForAllPlugins();
}
