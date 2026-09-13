#pragma once

#include "devices.hpp"
#include "session_remap.hpp"
#include "settings.hpp"

#include <windows.h>
#include <shellapi.h>

#include <vector>

class App {
public:
  explicit App(HINSTANCE instance);
  int Run(int show_cmd);
  void ShowMain(bool show);
  INT_PTR HandleDialog(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  static int RunElevatedCommand(int argc, wchar_t** argv);

private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  HWND CreateMainDialog();
  void InitTray(HWND hwnd);
  void RemoveTray();
  void UpdateTrayIcon();
  void UpdateRemapButton();
  void LoadTrayIcons();
  void DestroyTrayIcons();
  HICON LoadSizedIcon(int resource_id, int cx, int cy);

  void SetStatus(const std::wstring& text);
  void RefreshDevices();
  void RebuildMappingUi();
  void OnDeviceChosen();
  void OnStartRemap();
  void OnStopRemap();
  void OnStartWithWindowsChanged();
  void SyncStartWithWindowsCheckbox();
  void SyncMappingsFromUi();
  void SyncSelectedIndexFromUi();
  void PersistSelectedDevice();
  int FindPreferredDeviceIndex(const std::wstring& instance_id,
                               const std::wstring& hardware_id,
                               const std::wstring& device_key,
                               const std::wstring& hid_path) const;
  void StartDetect();
  void StopDetect(const std::wstring& status_if_idle = L"");
  void OnRawInput(HRAWINPUT raw);
  bool SelectDeviceByHidPath(const std::wstring& hid_path);

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND dialog_ = nullptr;
  NOTIFYICONDATAW tray_{};
  bool tray_added_ = false;
  HICON icon_idle_ = nullptr;
  HICON icon_active_ = nullptr;

  std::vector<MouseDevice> devices_;
  int selected_ = -1;
  AppSettings settings_;
  SessionRemapper remapper_;
  std::vector<HWND> mapping_combos_;

  bool detecting_ = false;
  bool refreshing_devices_ = false;
  UINT_PTR detect_timer_ = 0;
};
