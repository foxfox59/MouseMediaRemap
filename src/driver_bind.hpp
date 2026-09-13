#pragma once

#include "devices.hpp"
#include <string>

struct BindResult {
  bool ok = false;
  std::wstring message;
};

// Install WinUSB for the given hardware ID (requires elevation).
BindResult BindWinUsb(const std::wstring& hardware_id, const std::wstring& inf_dir);

// Restore Microsoft HID drivers for one hardware id, or all interfaces of a VID/PID.
BindResult RestoreHidMouse(const std::wstring& hardware_id);
BindResult RestoreHidMouseAllInterfaces(std::uint16_t vid, std::uint16_t pid);

bool IsElevated();
int RelaunchElevated(const std::wstring& args);

// Elevated helper writes here; UI reads after relaunch.
std::wstring ResultFilePath();
void WriteBindResultFile(const BindResult& result);
BindResult ReadBindResultFile();
