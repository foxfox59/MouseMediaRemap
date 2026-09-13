#pragma once

#include "devices.hpp"
#include <string>
#include <map>

struct AppSettings {
  std::wstring bound_instance_id;
  std::wstring bound_hardware_id;
  std::wstring last_device_key;  // VID:PID:MI from DeviceSettingsKey
  std::wstring last_hid_path;
  bool start_with_windows = false;
  bool remapping_active = false;
  std::map<std::wstring, std::map<std::wstring, MediaAction>> mappings; // deviceKey -> controlId -> action
};

std::wstring SettingsPath();
std::wstring DeviceSettingsKey(const MouseDevice& device);
AppSettings LoadSettings();
bool SaveSettings(const AppSettings& settings);
void ApplySavedMappings(MouseDevice& device, const AppSettings& settings);
void StoreMappings(AppSettings& settings, const MouseDevice& device);
void RememberDevice(AppSettings& settings, const MouseDevice& device);

bool IsStartWithWindowsEnabled();
bool SetStartWithWindows(bool enabled);
