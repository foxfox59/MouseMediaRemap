#include "driver_bind.hpp"

#include <windows.h>
#include <newdev.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "shlwapi.lib")

namespace {

constexpr wchar_t kInterfaceGuid[] = L"{9F3A8C2E-6B1D-4E7A-9C11-B055E0ED1A01}";
constexpr wchar_t kWinUsbCompatId[] = L"USB\\MS_COMP_WINUSB";

std::wstring TempDirPath() {
  wchar_t buf[MAX_PATH]{};
  GetTempPathW(MAX_PATH, buf);
  std::wstring dir = std::wstring(buf) + L"MouseMediaRemap\\";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring PrimaryUsbHardwareId(const std::wstring& hardware_id) {
  std::wstring id = hardware_id;
  const auto pos = id.find(L"USB\\VID_");
  if (pos == std::wstring::npos) return id;
  id = id.substr(pos);
  auto rev = id.find(L"&REV_");
  if (rev != std::wstring::npos) id = id.substr(0, rev);
  // Cut at first null if multi-sz was flattened oddly
  const auto z = id.find(L'\0');
  if (z != std::wstring::npos) id = id.substr(0, z);
  return id;
}

bool FindDeviceByHardwareId(const std::wstring& hardware_id, HDEVINFO& out_info, SP_DEVINFO_DATA& out_data) {
  out_info = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
  if (out_info == INVALID_HANDLE_VALUE) return false;

  const std::wstring want = PrimaryUsbHardwareId(hardware_id);
  SP_DEVINFO_DATA data{};
  data.cbSize = sizeof(data);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(out_info, i, &data); ++i) {
    DWORD type = 0;
    DWORD needed = 0;
    SetupDiGetDeviceRegistryPropertyW(out_info, &data, SPDRP_HARDWAREID, &type, nullptr, 0, &needed);
    if (needed == 0) continue;
    std::vector<wchar_t> buf(needed / sizeof(wchar_t) + 2);
    if (!SetupDiGetDeviceRegistryPropertyW(out_info, &data, SPDRP_HARDWAREID, &type,
                                           reinterpret_cast<BYTE*>(buf.data()), needed, nullptr)) {
      continue;
    }
    // MULTI_SZ walk
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
      std::wstring entry = PrimaryUsbHardwareId(p);
      if (_wcsicmp(entry.c_str(), want.c_str()) == 0) {
        out_data = data;
        return true;
      }
      // Also accept if hardware id string contains our id
      if (wcsstr(p, want.c_str()) != nullptr) {
        out_data = data;
        return true;
      }
    }
  }
  SetupDiDestroyDeviceInfoList(out_info);
  out_info = INVALID_HANDLE_VALUE;
  return false;
}

bool SetCompatibleIdWinUsb(HDEVINFO info, SP_DEVINFO_DATA& data) {
  // MULTI_SZ: USB\MS_COMP_WINUSB\0\0
  wchar_t multi[64]{};
  wcscpy_s(multi, kWinUsbCompatId);
  const DWORD bytes = static_cast<DWORD>((wcslen(multi) + 2) * sizeof(wchar_t));
  return SetupDiSetDeviceRegistryPropertyW(info, &data, SPDRP_COMPATIBLEIDS,
                                           reinterpret_cast<const BYTE*>(multi), bytes) == TRUE;
}

bool SetDeviceInterfaceGuid(HDEVINFO info, SP_DEVINFO_DATA& data) {
  HKEY key = SetupDiOpenDevRegKey(info, &data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_SET_VALUE);
  if (key == INVALID_HANDLE_VALUE) return false;

  // DeviceInterfaceGUIDs as REG_MULTI_SZ
  wchar_t multi[80]{};
  wcscpy_s(multi, kInterfaceGuid);
  const DWORD bytes = static_cast<DWORD>((wcslen(multi) + 2) * sizeof(wchar_t));
  const LONG st = RegSetValueExW(key, L"DeviceInterfaceGUIDs", 0, REG_MULTI_SZ,
                                 reinterpret_cast<const BYTE*>(multi), bytes);
  RegCloseKey(key);
  return st == ERROR_SUCCESS;
}

bool InstallInboxWinUsb(const std::wstring& hardware_id, std::wstring& message) {
  HDEVINFO info = INVALID_HANDLE_VALUE;
  SP_DEVINFO_DATA data{};
  data.cbSize = sizeof(data);
  if (!FindDeviceByHardwareId(hardware_id, info, data)) {
    message = L"Could not find device instance for bind.";
    return false;
  }

  if (!SetCompatibleIdWinUsb(info, data)) {
    const DWORD err = GetLastError();
    wchar_t buf[128];
    swprintf(buf, 128, L"Failed to set WinUSB compatible ID (error %lu).", err);
    message = buf;
    SetupDiDestroyDeviceInfoList(info);
    return false;
  }

  wchar_t windir[MAX_PATH]{};
  GetWindowsDirectoryW(windir, MAX_PATH);
  const std::wstring inf = std::wstring(windir) + L"\\INF\\winusb.inf";

  BOOL reboot = FALSE;
  // Match against the compatible ID so inbox winusb.inf (Microsoft-signed) can install.
  if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, kWinUsbCompatId, inf.c_str(), INSTALLFLAG_FORCE, &reboot)) {
    // Retry with the device hardware ID — some hosts accept this after compat ID is set.
    if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, PrimaryUsbHardwareId(hardware_id).c_str(), inf.c_str(),
                                            INSTALLFLAG_FORCE, &reboot)) {
      const DWORD err = GetLastError();
      wchar_t buf[384];
      swprintf(buf, 384,
               L"Inbox winusb.inf install failed (error %lu). "
               L"Windows only auto-matches WinUSB when firmware reports MS_COMP_WINUSB. "
               L"Use Zadig (signed WinUSB package): Options → List All Devices → your mouse → WinUSB → Replace Driver, "
               L"then click Start reader.",
               err);
      message = buf;
      SetupDiDestroyDeviceInfoList(info);
      return false;
    }
  }

  SetDeviceInterfaceGuid(info, data);
  SetupDiDestroyDeviceInfoList(info);
  message = reboot ? L"Bound with inbox WinUSB. Reboot required." : L"Bound with inbox WinUSB.";
  return true;
}

}  // namespace

std::wstring ResultFilePath() {
  return TempDirPath() + L"last_result.txt";
}

void WriteBindResultFile(const BindResult& result) {
  const std::wstring path = ResultFilePath();
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  std::wstring body = (result.ok ? L"1\n" : L"0\n") + result.message;
  WORD bom = 0xFEFF;
  DWORD written = 0;
  WriteFile(file, &bom, sizeof(bom), &written, nullptr);
  WriteFile(file, body.c_str(), static_cast<DWORD>((body.size() + 1) * sizeof(wchar_t)), &written, nullptr);
  CloseHandle(file);
}

BindResult ReadBindResultFile() {
  BindResult result;
  const std::wstring path = ResultFilePath();
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    result.message = L"No result from elevated helper.";
    return result;
  }
  LARGE_INTEGER li{};
  GetFileSizeEx(file, &li);
  if (li.QuadPart < 4) {
    CloseHandle(file);
    return result;
  }
  std::vector<wchar_t> buf(static_cast<size_t>(li.QuadPart / sizeof(wchar_t)) + 1);
  DWORD read = 0;
  ReadFile(file, buf.data(), static_cast<DWORD>(li.QuadPart), &read, nullptr);
  CloseHandle(file);
  size_t start = 0;
  if (buf[0] == 0xFEFF) start = 1;
  std::wstring text(buf.data() + start);
  if (!text.empty() && text[0] == L'1') result.ok = true;
  auto nl = text.find(L'\n');
  if (nl != std::wstring::npos) {
    result.message = text.substr(nl + 1);
    while (!result.message.empty() && (result.message.back() == L'\0' || result.message.back() == L'\r' ||
                                       result.message.back() == L'\n')) {
      result.message.pop_back();
    }
  }
  return result;
}

bool IsElevated() {
  BOOL elevated = FALSE;
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    TOKEN_ELEVATION te{};
    DWORD size = 0;
    if (GetTokenInformation(token, TokenElevation, &te, sizeof(te), &size)) {
      elevated = te.TokenIsElevated;
    }
    CloseHandle(token);
  }
  return elevated == TRUE;
}

int RelaunchElevated(const std::wstring& args) {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);

  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"runas";
  sei.lpFile = path;
  sei.lpParameters = args.c_str();
  sei.nShow = SW_HIDE;
  if (!ShellExecuteExW(&sei)) {
    return static_cast<int>(GetLastError());
  }
  WaitForSingleObject(sei.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(sei.hProcess, &code);
  CloseHandle(sei.hProcess);
  return static_cast<int>(code);
}

BindResult BindWinUsb(const std::wstring& hardware_id, const std::wstring& /*inf_dir*/) {
  BindResult result;
  if (!IsElevated()) {
    result.message = L"Administrator rights required to bind WinUSB.";
    WriteBindResultFile(result);
    return result;
  }

  // Prefer Microsoft-signed inbox winusb.inf (no custom unsigned INF).
  if (InstallInboxWinUsb(hardware_id, result.message)) {
    result.ok = true;
    WriteBindResultFile(result);
    return result;
  }

  WriteBindResultFile(result);
  return result;
}

BindResult RestoreHidMouse(const std::wstring& hardware_id) {
  BindResult result;
  if (!IsElevated()) {
    result.message = L"Administrator rights required to restore HID mouse.";
    WriteBindResultFile(result);
    return result;
  }

  const std::wstring hwid = PrimaryUsbHardwareId(hardware_id);
  wchar_t windir[MAX_PATH]{};
  GetWindowsDirectoryW(windir, MAX_PATH);
  const std::wstring candidates[] = {
      std::wstring(windir) + L"\\INF\\input.inf",
      std::wstring(windir) + L"\\INF\\msmouse.inf",
  };

  DWORD last_err = 0;
  for (const auto& inf : candidates) {
    if (GetFileAttributesW(inf.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
    BOOL reboot = FALSE;
    if (UpdateDriverForPlugAndPlayDevicesW(nullptr, hwid.c_str(), inf.c_str(), INSTALLFLAG_FORCE, &reboot)) {
      result.ok = true;
      result.message = reboot ? L"Restored HID mouse. Reboot required." : L"Restored HID mouse.";
      WriteBindResultFile(result);
      return result;
    }
    last_err = GetLastError();
  }

  wchar_t buf[256];
  swprintf(buf, 256,
           L"Restore failed (error %lu). Use Device Manager → Update driver → HID-compliant mouse.",
           last_err);
  result.message = buf;
  WriteBindResultFile(result);
  return result;
}

BindResult RestoreHidMouseAllInterfaces(std::uint16_t vid, std::uint16_t pid) {
  BindResult result;
  if (!IsElevated()) {
    result.message = L"Administrator rights required to restore HID drivers.";
    WriteBindResultFile(result);
    return result;
  }

  wchar_t prefix[64];
  swprintf(prefix, 64, L"USB\\VID_%04X&PID_%04X", vid, pid);

  wchar_t windir[MAX_PATH]{};
  GetWindowsDirectoryW(windir, MAX_PATH);
  const std::wstring inf = std::wstring(windir) + L"\\INF\\input.inf";

  HDEVINFO info = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (info == INVALID_HANDLE_VALUE) {
    result.message = L"Could not enumerate USB devices for restore.";
    WriteBindResultFile(result);
    return result;
  }

  int restored = 0;
  DWORD last_err = 0;
  SP_DEVINFO_DATA data{};
  data.cbSize = sizeof(data);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(info, i, &data); ++i) {
    DWORD type = 0;
    DWORD needed = 0;
    SetupDiGetDeviceRegistryPropertyW(info, &data, SPDRP_HARDWAREID, &type, nullptr, 0, &needed);
    if (needed == 0) continue;
    std::vector<wchar_t> buf(needed / sizeof(wchar_t) + 2);
    if (!SetupDiGetDeviceRegistryPropertyW(info, &data, SPDRP_HARDWAREID, &type,
                                           reinterpret_cast<BYTE*>(buf.data()), needed, nullptr)) {
      continue;
    }
    bool match = false;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
      if (wcsstr(p, prefix) != nullptr) {
        match = true;
        break;
      }
    }
    if (!match) continue;

    // Prefer updating using the first hardware id string.
    BOOL reboot = FALSE;
    if (UpdateDriverForPlugAndPlayDevicesW(nullptr, buf.data(), inf.c_str(), INSTALLFLAG_FORCE, &reboot)) {
      ++restored;
    } else {
      last_err = GetLastError();
      // Try msmouse.inf
      const std::wstring mouse_inf = std::wstring(windir) + L"\\INF\\msmouse.inf";
      if (UpdateDriverForPlugAndPlayDevicesW(nullptr, buf.data(), mouse_inf.c_str(), INSTALLFLAG_FORCE, &reboot)) {
        ++restored;
      } else {
        last_err = GetLastError();
      }
    }
  }
  SetupDiDestroyDeviceInfoList(info);

  if (restored > 0) {
    result.ok = true;
    wchar_t msg[128];
    swprintf(msg, 128, L"Restored HID drivers on %d interface(s). Unplug/replug the dongle if needed.", restored);
    result.message = msg;
  } else {
    wchar_t msg[256];
    swprintf(msg, 256,
             L"Could not auto-restore (error %lu). Device Manager → each WinUSB interface → Update driver → "
             L"Search automatically / HID-compliant mouse. Or uninstall device + scan for hardware changes.",
             last_err);
    result.message = msg;
  }
  WriteBindResultFile(result);
  return result;
}
