#include "plugin_scanner.h"
#include "vst3_plugin.h"
#include <iostream>
#include <vector>
#include <string>
#include <limits> // Required for std::numeric_limits
#include <cmath>  // For std::sin, M_PI (or define M_PI if not available)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// For COM Initialization on Windows
#ifdef _WIN32
#include <objbase.h> // For CoInitializeEx, CoUninitialize
#endif

// Simple error printing helper
void printError(const std::string& message, const std::string& errorDetails) {
    std::cerr << "ERROR: " << message;
    if (!errorDetails.empty()) {
        std::cerr << " (" << errorDetails << ")";
    }
    std::cerr << std::endl;
}

// Dummy audio generator (Mono Sine Wave)
void generateSineWave(float* buffer, int numSamples, float frequency, double sampleRate, float amplitude) {
    double phaseIncrement = (2.0 * M_PI * frequency) / sampleRate;
    double currentPhase = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * static_cast<float>(std::sin(currentPhase));
        currentPhase += phaseIncrement;
        if (currentPhase >= 2.0 * M_PI) {
            currentPhase -= 2.0 * M_PI;
        }
    }
}


int main(int argc, char* argv[]) {
    std::cout << "VST3 Host PoC - Starting Application" << std::endl;

#ifdef _WIN32
    // Initialize COM for this thread. Required for some VST3 SDK functionalities and SHGetKnownFolderPath.
    HRESULT comInitResult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(comInitResult)) {
        printError("Failed to initialize COM library.", "HRESULT: " + std::to_string(comInitResult));
        // Depending on strictness, could exit here. Some things might work without COM.
    }
#endif

    // 1. Scan for plugins
    PluginScanner scanner;
    std::cout << "\nScanning for VST3 plugins..." << std::endl;
    std::vector<Vst3PluginInfo> availablePlugins = scanner.scanForAllPlugins();

    if (availablePlugins.empty()) {
        std::cout << "No VST3 plugins found. Make sure they are in standard VST3 search paths or VST3_PLUGIN_PATH is set." << std::endl;
#ifdef _WIN32
        CoUninitialize();
#endif
        return 1;
    }

    // 2. Print found plugins and prompt for selection
    std::cout << "\nAvailable VST3 Plugins:" << std::endl;
    for (size_t i = 0; i < availablePlugins.size(); ++i) {
        std::cout << "[" << i << "] " << availablePlugins[i].name
                  << " (Vendor: " << availablePlugins[i].vendor
                  << ", Path: " << availablePlugins[i].path << ")" << std::endl;
        if(!availablePlugins[i].error.empty()){
            std::cout << "    Note: " << availablePlugins[i].error << std::endl;
        }
    }

    int selection = -1;
    if (availablePlugins.size() > 1) {
        std::cout << "\nEnter the number of the plugin to load: ";
        while (!(std::cin >> selection) || selection < 0 || selection >= static_cast<int>(availablePlugins.size())) {
            std::cout << "Invalid selection. Please enter a number between 0 and " << availablePlugins.size() - 1 << ": ";
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    } else if (availablePlugins.size() == 1) {
        std::cout << "\nAutomatically selecting the only available plugin." << std::endl;
        selection = 0;
    }

    Vst3PluginInfo selectedPluginInfo = availablePlugins[selection];
    std::cout << "\nLoading plugin: " << selectedPluginInfo.name << std::endl;

    // 3. Create and initialize Vst3Plugin instance
    Vst3Plugin plugin(selectedPluginInfo.path);

    if (!plugin.load()) {
        printError("Failed to load the selected plugin.", plugin.getError());
#ifdef _WIN32
        CoUninitialize();
#endif
        return 1;
    }
    std::cout << "Plugin loaded successfully from: " << plugin.getPath() << std::endl;

    double sampleRate = 44100.0;
    int32_t blockSize = 512;
    std::cout << "Initializing plugin with Sample Rate: " << sampleRate << " Hz, Block Size: " << blockSize << " samples." << std::endl;
    if (!plugin.initialize(sampleRate, blockSize)) {
        printError("Failed to initialize the plugin.", plugin.getError());
        plugin.terminate(); // Ensure cleanup
#ifdef _WIN32
        CoUninitialize();
#endif
        return 1;
    }
    std::cout << "Plugin initialized successfully." << std::endl;

    // 4. List parameters
    int32_t paramCount = plugin.getParameterCount();
    std::cout << "\nPlugin Parameters (" << paramCount << "):" << std::endl;
    if (paramCount > 0) {
        for (int32_t i = 0; i < paramCount; ++i) {
            Vst3Plugin::ParameterInfo paramInfo;
            if (plugin.getParameterInfo(i, paramInfo)) {
                std::cout << "  [" << i << "] ID: " << paramInfo.id
                          << ", Name: \"" << paramInfo.title << "\""
                          << ", Units: \"" << paramInfo.units << "\""
                          << ", Steps: " << paramInfo.stepCount
                          << ", Default: " << paramInfo.defaultValueNormalized
                          << std::endl;
            } else {
                printError("Could not get info for parameter index " + std::to_string(i), plugin.getError());
            }
        }
    } else {
        std::cout << "  Plugin has no parameters (or no controller)." << std::endl;
    }

    // 5. Demonstrate Get/Set parameter (e.g., first parameter if available)
    if (paramCount > 0) {
        Vst3Plugin::ParameterInfo firstParam;
        if (plugin.getParameterInfo(0, firstParam)) {
            Steinberg::Vst::ParamID idToChange = firstParam.id;
            float currentValue = plugin.getParameterValue(idToChange);
            std::cout << "\nParameter '" << firstParam.title << "' (ID: " << idToChange << ") current value (normalized): " << currentValue << std::endl;

            float newValue = currentValue + 0.1f; // Example: try to change it slightly
            if (newValue > 1.0f) newValue = 0.0f; // Wrap around for testing
            if (firstParam.stepCount > 0) { // If it's a stepped parameter
                 newValue = static_cast<float>(static_cast<int>(newValue * firstParam.stepCount)) / firstParam.stepCount;
            }


            std::cout << "Attempting to set '" << firstParam.title << "' to (normalized): " << newValue << std::endl;
            if (plugin.setParameterValue(idToChange, newValue)) {
                float updatedValue = plugin.getParameterValue(idToChange);
                std::cout << "Parameter '" << firstParam.title << "' new value (normalized): " << updatedValue << std::endl;
                if (std::abs(updatedValue - newValue) > 0.0001f) {
                     std::cout << "  Note: Readback value differs slightly, which can be normal for some plugins." << std::endl;
                }
            } else {
                printError("Failed to set parameter value.", plugin.getError());
            }
        }
    }

    // 6. Audio Processing Test (Simple Sine Wave Passthrough/Generation)
    std::cout << "\n--- Audio Processing Test ---" << std::endl;
    int numChannels = 2; // Assume stereo for simplicity, a real host queries bus layout
    // For PoC, we'll assume the plugin's main input/output buses are stereo.
    // This needs proper bus querying and channel management in a real host.

    // Query actual bus information to set numChannels correctly for the first bus
    // This is a simplified version of what vst3_plugin.cpp's process method would need.
    // In main, we are preparing buffers *before* calling plugin.process().
    Steinberg::Vst::BusInfo inputBusInfo, outputBusInfo;
    int numInputChannels = 0;
    int numOutputChannels = 0;

    // Accessing component directly here is not ideal. Better to have methods in Vst3Plugin
    // like getNumInputChannels(busIndex), getNumOutputChannels(busIndex).
    // For PoC, this is a shortcut to make main.cpp work.
    // The Vst3Plugin class itself should hide these Steinberg details.
    // We will assume that the Vst3Plugin::process method correctly handles its internal bus structures.
    // The buffers we prepare here must match what plugin.process() expects.
    // For now, let's assume plugin.process() is adapted for stereo input/output if available,
    // or mono if not. For this test, we'll prepare stereo buffers.

    // The Vst3Plugin::process method currently assumes inputs/outputs are float**
    // where inputs[0] = L, inputs[1] = R etc. for the *first* active bus.
    // So, we prepare `numChannels` float arrays.

    std::vector<float*> inputChannelBuffers(numChannels);
    std::vector<float*> outputChannelBuffers(numChannels);
    std::vector<std::vector<float>> inputData(numChannels, std::vector<float>(blockSize));
    std::vector<std::vector<float>> outputData(numChannels, std::vector<float>(blockSize));

    for (int i = 0; i < numChannels; ++i) {
        inputChannelBuffers[i] = inputData[i].data();
        outputChannelBuffers[i] = outputData[i].data();
    }

    float sineFrequency = 440.0f; // A4 note
    float amplitude = 0.5f;
    int numSecondsToProcess = 1;
    int totalBlocks = static_cast<int>((sampleRate * numSecondsToProcess) / blockSize);

    std::cout << "Processing " << numSecondsToProcess << " second(s) of audio (" << totalBlocks << " blocks)..." << std::endl;
    std::cout << "Input: Mono Sine wave (" << sineFrequency << " Hz) on channel 0. Other channels silent." << std::endl;
    std::cout << "Output: Will print first few samples of channel 0." << std::endl;

    for (int block = 0; block < totalBlocks; ++block) {
        // Generate sine wave only for the first channel for this test
        generateSineWave(inputChannelBuffers[0], blockSize, sineFrequency, sampleRate, amplitude);
        // Silence other input channels if any
        for (int ch = 1; ch < numChannels; ++ch) {
            std::fill_n(inputChannelBuffers[ch], blockSize, 0.0f);
        }

        plugin.process(inputChannelBuffers.data(), outputChannelBuffers.data(), blockSize);

        if (block == 0) { // Print first few samples of the first block's output (channel 0)
            std::cout << "Output samples from first block (channel 0): ";
            for (int i = 0; i < std::min(10, blockSize); ++i) {
                std::cout << outputChannelBuffers[0][i] << " ";
            }
            std::cout << std::endl;
        }
    }
    std::cout << "Audio processing test completed." << std::endl;

    // 7. Terminate plugin
    std::cout << "\nTerminating plugin..." << std::endl;
    plugin.terminate();
    std::cout << "Plugin terminated." << std::endl;

#ifdef _WIN32
    CoUninitialize(); // Uninitialize COM
#endif

    std::cout << "\nVST3 Host PoC finished." << std::endl;
    return 0;
}
