#include "app.hpp"
#include "driver_bind.hpp"
#include "media_keys.hpp"
#include "resource.h"

#include <commctrl.h>
#include <objbase.h>

#include <cwctype>
#include <string>

namespace {

constexpr UINT kTrayId = 1;

MouseDevice* SelectedDevice(std::vector<MouseDevice>& devices, int selected) {
  if (selected < 0 || selected >= static_cast<int>(devices.size())) return nullptr;
  return &devices[selected];
}

LRESULT CALLBACK MainDlgWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  App* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<App*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);

  if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
    const HDC hdc = reinterpret_cast<HDC>(wparam);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
  }
  if (msg == WM_CLOSE) {
    self->ShowMain(false);
    return 0;
  }
  if (msg == WM_COMMAND) {
    self->HandleDialog(hwnd, msg, wparam, lparam);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

App::App(HINSTANCE instance) : instance_(instance) {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  settings_ = LoadSettings();
  settings_.start_with_windows = IsStartWithWindowsEnabled();
}

int App::RunElevatedCommand(int argc, wchar_t** argv) {
  std::wstring cmd = argc > 1 ? argv[1] : L"";

  BindResult result;
  if (cmd == L"--elevate-bind") {
    std::wstring hwid;
    for (int i = 2; i < argc; ++i) {
      if (i > 2) hwid.push_back(L' ');
      hwid += argv[i];
    }
    result = BindWinUsb(hwid, L"");
  } else if (cmd == L"--elevate-restore") {
    std::wstring hwid;
    for (int i = 2; i < argc; ++i) {
      if (i > 2) hwid.push_back(L' ');
      hwid += argv[i];
    }
    result = RestoreHidMouse(hwid);
  } else if (cmd == L"--elevate-restore-all" && argc >= 4) {
    unsigned vid = 0, pid = 0;
    swscanf(argv[2], L"%x", &vid);
    swscanf(argv[3], L"%x", &pid);
    result = RestoreHidMouseAllInterfaces(static_cast<std::uint16_t>(vid),
                                          static_cast<std::uint16_t>(pid));
  } else {
    return 2;
  }
  return result.ok ? 0 : 1;
}

HWND App::CreateMainDialog() {
  const int large = GetSystemMetrics(SM_CXICON);
  const int small = GetSystemMetrics(SM_CXSMICON);
  HICON app_big = LoadSizedIcon(IDI_APP, large, large);
  HICON app_small = LoadSizedIcon(IDI_APP, small, small);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = MainDlgWndProc;
  wc.hInstance = instance_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = L"MouseMediaRemapDlg";
  wc.hIcon = app_big ? app_big : LoadIconW(nullptr, IDI_APPLICATION);
  wc.hIconSm = app_small ? app_small : wc.hIcon;
  RegisterClassExW(&wc);

  HWND dlg = CreateWindowExW(
      WS_EX_APPWINDOW, L"MouseMediaRemapDlg", L"MouseMediaRemap",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 560, 490, nullptr, nullptr, instance_, this);
  if (!dlg) return nullptr;

  CreateWindowExW(0, L"STATIC", L"Mouse", WS_CHILD | WS_VISIBLE, 16, 16, 60, 18, dlg,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC_DEVICE)), instance_, nullptr);
  CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                  16, 36, 318, 200, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DEVICE_LIST)), instance_, nullptr);
  CreateWindowExW(0, L"BUTTON", L"Detect", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 348, 34, 84, 26, dlg,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DETECT)), instance_, nullptr);
  CreateWindowExW(0, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 440, 34, 88, 26, dlg,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_REFRESH)), instance_, nullptr);
  CreateWindowExW(0, L"STATIC", L"Controls", WS_CHILD | WS_VISIBLE, 16, 72, 200, 18, dlg,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC_MAPPINGS)), instance_, nullptr);
  CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 94, 512, 260, dlg,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAPPINGS_HOST)), instance_, nullptr);
  CreateWindowExW(0, L"BUTTON", L"Start remapping", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 16, 368, 160, 28, dlg,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TOGGLE_REMAP)), instance_, nullptr);
  CreateWindowExW(0, L"BUTTON", L"Start with Windows",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 188, 372, 160, 22, dlg,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_START_WITH_WINDOWS)), instance_, nullptr);
  CreateWindowExW(0, L"STATIC", L"Ready.", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, 16, 412, 512, 20, dlg,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), instance_, nullptr);
  return dlg;
}

int App::Run(int show_cmd) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = App::WndProc;
  wc.hInstance = instance_;
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = L"MouseMediaRemapHidden";
  RegisterClassExW(&wc);

  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"MouseMediaRemap", WS_OVERLAPPED,
                          0, 0, 0, 0, nullptr, nullptr, instance_, this);
  if (!hwnd_) return 1;

  dialog_ = CreateMainDialog();
  if (!dialog_) return 1;

  InitTray(hwnd_);
  RefreshDevices();
  SyncStartWithWindowsCheckbox();

  if (selected_ < 0 && !settings_.bound_hardware_id.empty()) {
    SetStatus(L"Previous device not found — plug it in and Refresh.");
  }

  const bool start_hidden = (show_cmd == SW_HIDE || show_cmd == SW_SHOWMINNOACTIVE);
  ShowMain(!start_hidden);
  if (settings_.remapping_active && selected_ >= 0) {
    OnStartRemap();
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (dialog_ && IsDialogMessageW(dialog_, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  remapper_.Stop();
  RemoveTray();
  DestroyTrayIcons();
  return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  App* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<App*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
  return self->HandleMessage(hwnd, msg, wparam, lparam);
}

INT_PTR CALLBACK App::DialogProc(HWND, UINT, WPARAM, LPARAM) {
  return FALSE;
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_HOTKEY:
      if (wparam == HOTKEY_STOP_REMAP) {
        OnStopRemap();
      }
      return 0;
    case WM_INPUT:
      OnRawInput(reinterpret_cast<HRAWINPUT>(lparam));
      // Required so the system can clean up the raw input buffer.
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_TIMER:
      if (wparam == 1) {
        KillTimer(hwnd, 1);
        detecting_ = true;
        SetStatus(L"Click a button on the target mouse… (15s)");
        detect_timer_ = SetTimer(hwnd, 2, 15000, nullptr);
      } else if (wparam == 2) {
        StopDetect(L"Detect timed out.");
      }
      return 0;
    case WM_TRAYICON:
      if (lparam == WM_LBUTTONDBLCLK) {
        ShowMain(true);
      } else if (lparam == WM_LBUTTONUP) {
        ShowMain(true);
      } else if (lparam == WM_RBUTTONUP) {
        POINT pt;
        GetCursorPos(&pt);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"Show");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (remapper_.Running()) {
          AppendMenuW(menu, MF_STRING | MF_CHECKED, IDM_TRAY_TOGGLE_REMAP, L"Stop remapping");
        } else {
          AppendMenuW(menu, MF_STRING, IDM_TRAY_TOGGLE_REMAP, L"Start remapping");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");
        SetForegroundWindow(hwnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
      }
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case IDM_TRAY_SHOW: ShowMain(true); break;
        case IDM_TRAY_TOGGLE_REMAP:
          if (remapper_.Running()) OnStopRemap();
          else OnStartRemap();
          break;
        case IDM_TRAY_EXIT: DestroyWindow(hwnd); break;
      }
      return 0;
    case WM_APP_READER_STATUS:
      if (lparam) {
        auto* text = reinterpret_cast<std::wstring*>(lparam);
        SetStatus(*text);
        delete text;
      }
      UpdateTrayIcon();
      return 0;
    case WM_DESTROY:
      remapper_.Stop();
      StopDetect();
      UnregisterHotKey(hwnd, HOTKEY_STOP_REMAP);
      RemoveTray();
      DestroyTrayIcons();
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}

INT_PTR App::HandleDialog(HWND, UINT msg, WPARAM wparam, LPARAM) {
  switch (msg) {
    case WM_COMMAND: {
      const int id = LOWORD(wparam);
      const int code = HIWORD(wparam);
      if (id == IDC_REFRESH && code == BN_CLICKED) {
        RefreshDevices();
        return TRUE;
      }
      if (id == IDC_DETECT && code == BN_CLICKED) {
        StartDetect();
        return TRUE;
      }
      if (id == IDC_TOGGLE_REMAP && code == BN_CLICKED) {
        if (remapper_.Running()) OnStopRemap();
        else OnStartRemap();
        return TRUE;
      }
      if (id == IDC_START_WITH_WINDOWS && code == BN_CLICKED) {
        OnStartWithWindowsChanged();
        return TRUE;
      }
      if (id == IDC_DEVICE_LIST && code == CBN_SELCHANGE) {
        if (!refreshing_devices_) OnDeviceChosen();
        return TRUE;
      }
      if (id >= 5000 && id < 5000 + 256 && code == CBN_SELCHANGE) {
        SyncMappingsFromUi();
        return TRUE;
      }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

void App::LoadTrayIcons() {
  DestroyTrayIcons();
  const int small = GetSystemMetrics(SM_CXSMICON);
  const int large = GetSystemMetrics(SM_CXICON);
  icon_idle_ = LoadSizedIcon(IDI_TRAY_IDLE, small, small);
  icon_active_ = LoadSizedIcon(IDI_TRAY_ACTIVE, small, small);
  if (!icon_idle_) icon_idle_ = LoadSizedIcon(IDI_APP, small, small);
  if (!icon_active_) icon_active_ = LoadSizedIcon(IDI_APP, small, small);

  // Window / taskbar icons (larger).
  if (dialog_) {
    HICON big = LoadSizedIcon(IDI_APP, large, large);
    HICON sm = LoadSizedIcon(IDI_APP, small, small);
    if (big) SendMessageW(dialog_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    if (sm) SendMessageW(dialog_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(sm));
  }
}

void App::DestroyTrayIcons() {
  if (icon_active_ && icon_active_ != icon_idle_) {
    DestroyIcon(icon_active_);
  }
  if (icon_idle_) {
    DestroyIcon(icon_idle_);
  }
  icon_idle_ = nullptr;
  icon_active_ = nullptr;
}

HICON App::LoadSizedIcon(int resource_id, int cx, int cy) {
  return static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(resource_id), IMAGE_ICON, cx, cy,
                                       LR_DEFAULTCOLOR));
}

void App::InitTray(HWND hwnd) {
  LoadTrayIcons();
  HICON fallback = LoadIconW(nullptr, IDI_APPLICATION);
  HICON start_icon = icon_idle_ ? icon_idle_ : fallback;

  ZeroMemory(&tray_, sizeof(tray_));
  tray_.cbSize = sizeof(tray_);
  tray_.hWnd = hwnd;
  tray_.uID = kTrayId;
  tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  tray_.uCallbackMessage = WM_TRAYICON;
  tray_.hIcon = start_icon;
  wcsncpy_s(tray_.szTip, L"MouseMediaRemap", _TRUNCATE);
  tray_added_ = Shell_NotifyIconW(NIM_ADD, &tray_) == TRUE;
  UpdateTrayIcon();
  UpdateRemapButton();
}

void App::UpdateRemapButton() {
  if (!dialog_) return;
  const bool active = remapper_.Running();
  SetDlgItemTextW(dialog_, IDC_TOGGLE_REMAP, active ? L"Stop remapping" : L"Start remapping");
}

void App::UpdateTrayIcon() {
  if (!tray_added_) return;
  const bool active = remapper_.Running();
  HICON fallback = LoadIconW(nullptr, IDI_APPLICATION);
  HICON icon = active ? (icon_active_ ? icon_active_ : icon_idle_) : icon_idle_;
  if (!icon) icon = fallback;

  tray_.hIcon = icon;
  if (active) {
    wcsncpy_s(tray_.szTip, L"MouseMediaRemap — remapping active", _TRUNCATE);
  } else {
    wcsncpy_s(tray_.szTip, L"MouseMediaRemap", _TRUNCATE);
  }
  tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  Shell_NotifyIconW(NIM_MODIFY, &tray_);
  UpdateRemapButton();
}

void App::RemoveTray() {
  if (tray_added_) {
    Shell_NotifyIconW(NIM_DELETE, &tray_);
    tray_added_ = false;
  }
}

void App::ShowMain(bool show) {
  if (!dialog_) return;
  ShowWindow(dialog_, show ? SW_SHOW : SW_HIDE);
  if (show) SetForegroundWindow(dialog_);
}

void App::SetStatus(const std::wstring& text) {
  if (dialog_) SetDlgItemTextW(dialog_, IDC_STATUS, text.c_str());
}

void App::RefreshDevices() {
  // Combo selection is source of truth — CBN_SELCHANGE can be missed.
  SyncSelectedIndexFromUi();
  SyncMappingsFromUi();

  std::wstring prefer_instance = settings_.bound_instance_id;
  std::wstring prefer_hardware = settings_.bound_hardware_id;
  std::wstring prefer_key = settings_.last_device_key;
  std::wstring prefer_hid = settings_.last_hid_path;
  if (selected_ >= 0 && selected_ < static_cast<int>(devices_.size())) {
    const auto& cur = devices_[selected_];
    prefer_instance = cur.instance_id;
    prefer_hardware = cur.hardware_id;
    prefer_key = DeviceSettingsKey(cur);
    prefer_hid = cur.hid_path;
    StoreMappings(settings_, cur);
    RememberDevice(settings_, cur);
    SaveSettings(settings_);
  }

  devices_ = EnumerateMice();
  for (auto& d : devices_) {
    ApplySavedMappings(d, settings_);
  }

  refreshing_devices_ = true;
  HWND combo = GetDlgItem(dialog_, IDC_DEVICE_LIST);
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  for (const auto& d : devices_) {
    const std::wstring label = DescribeDevice(d);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
  }

  selected_ = FindPreferredDeviceIndex(prefer_instance, prefer_hardware, prefer_key, prefer_hid);
  if (selected_ < 0 && !devices_.empty()) selected_ = 0;

  if (selected_ >= 0) {
    SendMessageW(combo, CB_SETCURSEL, selected_, 0);
  }
  refreshing_devices_ = false;

  if (selected_ >= 0) {
    OnDeviceChosen();
  } else {
    RebuildMappingUi();
    SetStatus(L"No HID mice found.");
  }
}

int App::FindPreferredDeviceIndex(const std::wstring& instance_id,
                                  const std::wstring& hardware_id,
                                  const std::wstring& device_key,
                                  const std::wstring& hid_path) const {
  auto norm = [](std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(towupper(ch));
    return s;
  };

  if (!instance_id.empty()) {
    for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
      if (!_wcsicmp(devices_[i].instance_id.c_str(), instance_id.c_str())) {
        return i;
      }
    }
  }

  if (!hid_path.empty()) {
    const std::wstring needle = norm(hid_path);
    for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
      const std::wstring cand = norm(devices_[i].hid_path);
      if (!cand.empty() && (cand == needle || needle.find(cand) != std::wstring::npos ||
                            cand.find(needle) != std::wstring::npos)) {
        return i;
      }
    }
  }

  if (!device_key.empty()) {
    for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
      if (DeviceSettingsKey(devices_[i]) == device_key) {
        return i;
      }
    }
  }

  if (!hardware_id.empty()) {
    for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
      if (!_wcsicmp(devices_[i].hardware_id.c_str(), hardware_id.c_str())) {
        return i;
      }
    }

    auto extract = [](const std::wstring& text, const wchar_t* key) -> unsigned {
      std::wstring up = text;
      for (auto& ch : up) ch = static_cast<wchar_t>(towupper(ch));
      auto pos = up.find(key);
      if (pos == std::wstring::npos) return 0;
      unsigned v = 0;
      swscanf(up.c_str() + pos + wcslen(key), L"%x", &v);
      return v;
    };

    std::wstring up = hardware_id;
    for (auto& ch : up) ch = static_cast<wchar_t>(towupper(ch));
    const unsigned vid = extract(up, L"VID_");
    const unsigned pid = extract(up, L"PID_");
    const bool has_mi = up.find(L"MI_") != std::wstring::npos;
    const unsigned mi = has_mi ? extract(up, L"MI_") : 0;
    if (vid != 0 && pid != 0) {
      for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
        if (devices_[i].vid == vid && devices_[i].pid == pid) {
          if (!has_mi || devices_[i].mi < 0 || static_cast<unsigned>(devices_[i].mi) == mi) {
            return i;
          }
        }
      }
    }
  }

  return -1;
}

void App::PersistSelectedDevice() {
  auto* device = SelectedDevice(devices_, selected_);
  if (!device) return;
  const std::wstring key = DeviceSettingsKey(*device);
  if (settings_.bound_instance_id == device->instance_id &&
      settings_.bound_hardware_id == device->hardware_id &&
      settings_.last_device_key == key &&
      settings_.last_hid_path == device->hid_path) {
    return;
  }
  RememberDevice(settings_, *device);
  SaveSettings(settings_);
}

void App::OnDeviceChosen() {
  HWND combo = GetDlgItem(dialog_, IDC_DEVICE_LIST);
  selected_ = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (auto* device = SelectedDevice(devices_, selected_)) {
    PersistSelectedDevice();
    if (device->controls.empty()) {
      PopulateHidCaps(*device);
      ApplySavedMappings(*device, settings_);
    }
    wchar_t buf[128];
    swprintf(buf, 128, L"%u controls.", static_cast<unsigned>(device->controls.size()));
    SetStatus(buf);
  }
  RebuildMappingUi();
}

void App::RebuildMappingUi() {
  for (HWND h : mapping_combos_) {
    DestroyWindow(h);
  }
  mapping_combos_.clear();

  HWND host = GetDlgItem(dialog_, IDC_MAPPINGS_HOST);
  RECT host_rect{};
  GetWindowRect(host, &host_rect);
  MapWindowPoints(nullptr, dialog_, reinterpret_cast<POINT*>(&host_rect), 2);

  auto* device = SelectedDevice(devices_, selected_);
  if (!device) return;

  const int row_h = 26;
  const int label_w = 180;
  int y = host_rect.top + 8;
  int index = 0;
  for (const auto& control : device->controls) {
    if (y + row_h > host_rect.bottom - 8) break;

    HWND label = CreateWindowExW(0, L"STATIC", control.label.c_str(), WS_CHILD | WS_VISIBLE,
                                 host_rect.left + 8, y + 4, label_w, 18, dialog_, nullptr, instance_, nullptr);
    mapping_combos_.push_back(label);

    HWND action = CreateWindowExW(
        0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        host_rect.left + label_w + 16, y, host_rect.right - host_rect.left - label_w - 28, 200, dialog_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(5000 + index)), instance_, nullptr);
    for (int a = 0; a < static_cast<int>(MediaAction::Count); ++a) {
      SendMessageW(action, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(MediaActionName(static_cast<MediaAction>(a))));
    }
    SendMessageW(action, CB_SETCURSEL, static_cast<int>(control.action), 0);
    mapping_combos_.push_back(action);

    y += row_h + 4;
    ++index;
  }
}

void App::SyncSelectedIndexFromUi() {
  if (!dialog_) return;
  HWND combo = GetDlgItem(dialog_, IDC_DEVICE_LIST);
  if (!combo) return;
  const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (sel >= 0 && sel < static_cast<int>(devices_.size())) {
    selected_ = sel;
  }
}

void App::SyncMappingsFromUi() {
  SyncSelectedIndexFromUi();
  auto* device = SelectedDevice(devices_, selected_);
  if (!device) return;
  for (size_t i = 0; i < device->controls.size(); ++i) {
    HWND action = GetDlgItem(dialog_, static_cast<int>(5000 + i));
    if (!action) continue;
    const int sel = static_cast<int>(SendMessageW(action, CB_GETCURSEL, 0, 0));
    if (sel >= 0) device->controls[i].action = static_cast<MediaAction>(sel);
  }
  StoreMappings(settings_, *device);
  RememberDevice(settings_, *device);
  SaveSettings(settings_);
  if (remapper_.Running()) remapper_.UpdateDeviceMappings(*device);
}

void App::OnStartRemap() {
  if (remapper_.Running()) {
    SetStatus(L"Already remapping.");
    return;
  }
  StopDetect();
  SyncMappingsFromUi();
  auto* device = SelectedDevice(devices_, selected_);
  if (!device) {
    SetStatus(L"Select a mouse first.");
    return;
  }
  if (device->hid_path.empty()) {
    SetStatus(L"No HID path for this device.");
    return;
  }

  RememberDevice(settings_, *device);
  StoreMappings(settings_, *device);
  settings_.remapping_active = true;
  SaveSettings(settings_);

  if (!remapper_.Start(hwnd_, *device, [this](const std::wstring& text) {
        auto* heap = new std::wstring(text);
        PostMessageW(hwnd_, WM_APP_READER_STATUS, 0, reinterpret_cast<LPARAM>(heap));
      })) {
    settings_.remapping_active = false;
    SaveSettings(settings_);
    return;
  }
  RegisterHotKey(hwnd_, HOTKEY_STOP_REMAP, MOD_CONTROL | MOD_SHIFT, VK_F8);
  UpdateTrayIcon();
  SetStatus(L"Remapping: " + device->friendly_name);
}

void App::OnStopRemap() {
  UnregisterHotKey(hwnd_, HOTKEY_STOP_REMAP);
  remapper_.Stop();
  settings_.remapping_active = false;
  SaveSettings(settings_);
  UpdateTrayIcon();
}

void App::SyncStartWithWindowsCheckbox() {
  if (!dialog_) return;
  const bool enabled = IsStartWithWindowsEnabled();
  settings_.start_with_windows = enabled;
  CheckDlgButton(dialog_, IDC_START_WITH_WINDOWS, enabled ? BST_CHECKED : BST_UNCHECKED);
}

void App::OnStartWithWindowsChanged() {
  if (!dialog_) return;
  const bool want = IsDlgButtonChecked(dialog_, IDC_START_WITH_WINDOWS) == BST_CHECKED;
  if (!SetStartWithWindows(want)) {
    SyncStartWithWindowsCheckbox();
    SetStatus(L"Could not update startup setting.");
    return;
  }
  settings_.start_with_windows = want;
  SaveSettings(settings_);
  SetStatus(want ? L"Start with Windows enabled." : L"Start with Windows disabled.");
}

void App::StartDetect() {
  if (remapper_.Running()) {
    SetStatus(L"Stop remapping first.");
    return;
  }
  if (devices_.empty()) {
    RefreshDevices();
  }
  if (devices_.empty()) {
    SetStatus(L"No mice found.");
    return;
  }

  StopDetect();
  ShowMain(true);

  RAWINPUTDEVICE rid{};
  rid.usUsagePage = 0x01;
  rid.usUsage = 0x02;
  rid.dwFlags = RIDEV_INPUTSINK;
  rid.hwndTarget = hwnd_;
  if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
    SetStatus(L"Detect failed (Raw Input).");
    return;
  }

  SetStatus(L"Click a button on the target mouse…");
  // Ignore the Detect button click itself.
  SetTimer(hwnd_, 1, 400, nullptr);
}

void App::StopDetect(const std::wstring& status_if_idle) {
  detecting_ = false;
  KillTimer(hwnd_, 1);
  KillTimer(hwnd_, 2);
  detect_timer_ = 0;

  RAWINPUTDEVICE rid{};
  rid.usUsagePage = 0x01;
  rid.usUsage = 0x02;
  rid.dwFlags = RIDEV_REMOVE;
  rid.hwndTarget = nullptr;
  RegisterRawInputDevices(&rid, 1, sizeof(rid));

  if (!status_if_idle.empty()) {
    SetStatus(status_if_idle);
  }
}

bool App::SelectDeviceByHidPath(const std::wstring& hid_path) {
  auto norm = [](std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(towupper(ch));
    return s;
  };
  const std::wstring needle = norm(hid_path);

  auto extract = [](const std::wstring& path, const wchar_t* key) -> unsigned {
    auto pos = path.find(key);
    if (pos == std::wstring::npos) return 0;
    unsigned v = 0;
    swscanf(path.c_str() + pos + wcslen(key), L"%x", &v);
    return v;
  };

  int best = -1;
  for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
    const std::wstring cand = norm(devices_[i].hid_path);
    if (!cand.empty() && (cand == needle || needle.find(cand) != std::wstring::npos ||
                          cand.find(needle) != std::wstring::npos)) {
      best = i;
      break;
    }
  }

  if (best < 0) {
    const unsigned vid = extract(needle, L"VID_");
    const unsigned pid = extract(needle, L"PID_");
    const bool has_mi = needle.find(L"MI_") != std::wstring::npos;
    const unsigned mi = has_mi ? extract(needle, L"MI_") : 0;
    for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
      if (devices_[i].vid == vid && devices_[i].pid == pid) {
        if (!has_mi || devices_[i].mi < 0 || static_cast<unsigned>(devices_[i].mi) == mi) {
          best = i;
          break;
        }
      }
    }
  }

  if (best < 0) return false;

  selected_ = best;
  HWND combo = GetDlgItem(dialog_, IDC_DEVICE_LIST);
  SendMessageW(combo, CB_SETCURSEL, selected_, 0);
  OnDeviceChosen();
  SetStatus(L"Detected: " + devices_[selected_].friendly_name);
  return true;
}

void App::OnRawInput(HRAWINPUT raw) {
  // Remapping runs on its own input thread. Main-window WM_INPUT is only for Detect.
  if (remapper_.Running() || !detecting_) return;

  UINT size = 0;
  GetRawInputData(raw, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
  if (size == 0) return;
  std::vector<BYTE> buf(size);
  if (GetRawInputData(raw, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
    return;
  }

  auto* input = reinterpret_cast<RAWINPUT*>(buf.data());
  if (input->header.dwType != RIM_TYPEMOUSE) return;

  const USHORT flags = input->data.mouse.usButtonFlags;
  const bool button_down =
      (flags & (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_DOWN |
                RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_5_DOWN)) != 0;
  if (!button_down) return;

  wchar_t name[512]{};
  UINT name_size = sizeof(name);
  if (GetRawInputDeviceInfoW(input->header.hDevice, RIDI_DEVICENAME, name, &name_size) == static_cast<UINT>(-1)) {
    return;
  }

  if (SelectDeviceByHidPath(name)) {
    StopDetect();
  }
}