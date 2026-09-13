#include "settings.hpp"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

namespace {

std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

std::string Narrow(const std::wstring& s) {
  if (s.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring ActionKey(MediaAction a) {
  switch (a) {
    case MediaAction::PreviousTrack: return L"prev";
    case MediaAction::PlayPause: return L"playpause";
    case MediaAction::NextTrack: return L"next";
    case MediaAction::Stop: return L"stop";
    case MediaAction::Mute: return L"mute";
    case MediaAction::VolumeUp: return L"volup";
    case MediaAction::VolumeDown: return L"voldown";
    default: return L"none";
  }
}

MediaAction ActionFromKey(const std::wstring& k) {
  if (k == L"prev") return MediaAction::PreviousTrack;
  if (k == L"playpause") return MediaAction::PlayPause;
  if (k == L"next") return MediaAction::NextTrack;
  if (k == L"stop") return MediaAction::Stop;
  if (k == L"mute") return MediaAction::Mute;
  if (k == L"volup") return MediaAction::VolumeUp;
  if (k == L"voldown") return MediaAction::VolumeDown;
  return MediaAction::None;
}

void TrimInPlace(std::string& s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
    s.pop_back();
  }
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  if (i) s.erase(0, i);
}

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 4 * 1024 * 1024) {
    CloseHandle(file);
    return false;
  }
  out.assign(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  const BOOL ok = ReadFile(file, out.empty() ? nullptr : out.data(),
                           static_cast<DWORD>(out.size()), &read, nullptr);
  CloseHandle(file);
  if (!ok) return false;
  out.resize(read);
  return true;
}

bool WriteFileUtf8(const std::wstring& path, const std::string& data) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
  CloseHandle(file);
  return ok && written == data.size();
}

}  // namespace

std::wstring SettingsPath() {
  wchar_t* appdata = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata)) || !appdata) {
    return L"MouseMediaRemap.ini";
  }
  std::wstring dir = std::wstring(appdata) + L"\\MouseMediaRemap";
  CoTaskMemFree(appdata);
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\settings.ini";
}

std::wstring DeviceSettingsKey(const MouseDevice& device) {
  wchar_t buf[64];
  swprintf(buf, 64, L"%04X:%04X:%02X", device.vid, device.pid, device.mi < 0 ? 0 : device.mi);
  return buf;
}

AppSettings LoadSettings() {
  AppSettings s;
  std::string text;
  if (!ReadFileUtf8(SettingsPath(), text)) return s;

  std::wstring current_section;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    TrimInPlace(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;

    if (line.front() == '[' && line.back() == ']') {
      current_section = Widen(line.substr(1, line.size() - 2));
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto key = Widen(line.substr(0, eq));
    auto value = Widen(line.substr(eq + 1));
    if (current_section.empty()) {
      if (key == L"bound_instance") s.bound_instance_id = value;
      else if (key == L"bound_hardware") s.bound_hardware_id = value;
      else if (key == L"last_device") s.last_device_key = value;
      else if (key == L"last_hid") s.last_hid_path = value;
      else if (key == L"start_with_windows") {
        s.start_with_windows = (value == L"1" || value == L"true" || value == L"yes");
      } else if (key == L"remapping_active") {
        s.remapping_active = (value == L"1" || value == L"true" || value == L"yes");
      }
    } else if (current_section.rfind(L"map:", 0) == 0) {
      auto device_key = current_section.substr(4);
      s.mappings[device_key][key] = ActionFromKey(value);
    }
  }

  // Older settings only had bound_* — derive last_device key when possible.
  if (s.last_device_key.empty()) {
    const std::wstring& src =
        !s.bound_instance_id.empty() ? s.bound_instance_id : s.bound_hardware_id;
    if (!src.empty()) {
      auto grab = [&](const wchar_t* key) -> unsigned {
        std::wstring up = src;
        for (auto& ch : up) ch = static_cast<wchar_t>(towupper(ch));
        auto p = up.find(key);
        if (p == std::wstring::npos) return 0;
        unsigned v = 0;
        swscanf(up.c_str() + p + wcslen(key), L"%x", &v);
        return v;
      };
      const unsigned vid = grab(L"VID_");
      const unsigned pid = grab(L"PID_");
      const unsigned mi = grab(L"MI_");
      if (vid && pid) {
        wchar_t buf[64];
        swprintf(buf, 64, L"%04X:%04X:%02X", vid, pid, mi);
        s.last_device_key = buf;
      }
    }
  }
  return s;
}

bool SaveSettings(const AppSettings& settings) {
  std::string out;
  out += "bound_instance=" + Narrow(settings.bound_instance_id) + "\n";
  out += "bound_hardware=" + Narrow(settings.bound_hardware_id) + "\n";
  out += "last_device=" + Narrow(settings.last_device_key) + "\n";
  out += "last_hid=" + Narrow(settings.last_hid_path) + "\n";
  out += "start_with_windows=" + std::string(settings.start_with_windows ? "1" : "0") + "\n";
  out += "remapping_active=" + std::string(settings.remapping_active ? "1" : "0") + "\n";
  for (const auto& [device_key, maps] : settings.mappings) {
    out += "\n[map:" + Narrow(device_key) + "]\n";
    for (const auto& [control_id, action] : maps) {
      out += Narrow(control_id) + "=" + Narrow(ActionKey(action)) + "\n";
    }
  }
  return WriteFileUtf8(SettingsPath(), out);
}

void ApplySavedMappings(MouseDevice& device, const AppSettings& settings) {
  const auto key = DeviceSettingsKey(device);
  auto it = settings.mappings.find(key);
  if (it == settings.mappings.end()) return;
  for (auto& control : device.controls) {
    auto found = it->second.find(control.id);
    if (found != it->second.end()) {
      control.action = found->second;
    }
  }
}

void StoreMappings(AppSettings& settings, const MouseDevice& device) {
  auto& map = settings.mappings[DeviceSettingsKey(device)];
  map.clear();
  for (const auto& control : device.controls) {
    map[control.id] = control.action;
  }
}

void RememberDevice(AppSettings& settings, const MouseDevice& device) {
  settings.bound_instance_id = device.instance_id;
  settings.bound_hardware_id = device.hardware_id;
  settings.last_device_key = DeviceSettingsKey(device);
  settings.last_hid_path = device.hid_path;
}

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"MouseMediaRemap";

std::wstring ExePathQuoted() {
  wchar_t path[MAX_PATH]{};
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};
  return std::wstring(L"\"") + path + L"\" --minimized";
}

}  // namespace

bool IsStartWithWindowsEnabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return false;
  }
  wchar_t value[1024]{};
  DWORD type = 0;
  DWORD size = sizeof(value);
  const LONG err = RegQueryValueExW(key, kRunValueName, nullptr, &type,
                                    reinterpret_cast<BYTE*>(value), &size);
  RegCloseKey(key);
  return err == ERROR_SUCCESS && type == REG_SZ && value[0] != L'\0';
}

bool SetStartWithWindows(bool enabled) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key) !=
      ERROR_SUCCESS) {
    return false;
  }

  LONG err = ERROR_SUCCESS;
  if (enabled) {
    const std::wstring cmd = ExePathQuoted();
    if (cmd.empty()) {
      RegCloseKey(key);
      return false;
    }
    err = RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(cmd.c_str()),
                         static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    err = RegDeleteValueW(key, kRunValueName);
    if (err == ERROR_FILE_NOT_FOUND) err = ERROR_SUCCESS;
  }
  RegCloseKey(key);
  return err == ERROR_SUCCESS;
}
