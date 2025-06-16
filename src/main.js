import { invoke } from '@tauri-apps/api/tauri';

const scanButton = document.getElementById('scan-button');
const pluginListElement = document.getElementById('plugin-list');
const errorMessageElement = document.getElementById('error-message');

async function scanPlugins() {
  if (!pluginListElement || !scanButton || !errorMessageElement) {
    console.error('UI elements not found!');
    return;
  }

  // Clear previous results and errors
  pluginListElement.innerHTML = '';
  errorMessageElement.textContent = '';
  scanButton.disabled = true;
  scanButton.textContent = 'Scanning...';

  try {
    console.log('Invoking scan_vst3_plugins command...');
    const plugins = await invoke('scan_vst3_plugins');
    console.log('Discovered VST3 Plugins:', plugins);

    if (plugins.length === 0) {
      pluginListElement.innerHTML = '<li>No VST3 plugins found.</li>';
    } else {
      plugins.forEach(plugin => {
        const listItem = document.createElement('li');
        listItem.textContent = `${plugin.name || 'Unknown Name'} (Vendor: ${plugin.vendor || 'N/A'}, Path: ${plugin.path})`;
        if (plugin.error && plugin.error.length > 0) {
          listItem.textContent += ` - Error: ${plugin.error}`;
          listItem.style.color = 'orange';
        }
        pluginListElement.appendChild(listItem);
      });
    }
  } catch (error) {
    console.error('Error scanning plugins:', error);
    errorMessageElement.textContent = `Error: ${error}`;
    pluginListElement.innerHTML = '<li>Error occurred during scanning.</li>';
  } finally {
    scanButton.disabled = false;
    scanButton.textContent = 'Scan for VST3 Plugins';
  }
}

if (scanButton) {
  scanButton.addEventListener('click', scanPlugins);
} else {
  console.error("Scan button not found on page load.");
}

// Optional: Automatically scan on load
// window.addEventListener('DOMContentLoaded', () => {
//   scanPlugins();
// });
