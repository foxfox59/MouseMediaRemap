#include "devices.hpp"
#include "hid_caps.hpp"

#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <initguid.h>
#include <devpkey.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>

#pragma comment(lib, "hid.lib")

namespace {

std::wstring MultiSzFirst(const std::vector<wchar_t>& buf) {
  if (buf.empty() || buf[0] == L'\0') return {};
  return std::wstring(buf.data());
}

bool GetDeviceProperty(HDEVINFO info, SP_DEVINFO_DATA& data, DWORD prop, std::wstring& out) {
  DWORD type = 0;
  DWORD needed = 0;
  SetupDiGetDeviceRegistryPropertyW(info, &data, prop, &type, nullptr, 0, &needed);
  if (needed == 0) return false;
  std::vector<wchar_t> buf(needed / sizeof(wchar_t) + 1);
  if (!SetupDiGetDeviceRegistryPropertyW(info, &data, prop, &type,
                                         reinterpret_cast<BYTE*>(buf.data()),
                                         needed, &needed)) {
    return false;
  }
  if (type == REG_MULTI_SZ) {
    out = MultiSzFirst(buf);
  } else {
    out = buf.data();
  }
  return !out.empty();
}

bool GetDevicePropertyKey(HDEVINFO info, SP_DEVINFO_DATA& data, const DEVPROPKEY& key, std::wstring& out) {
  DEVPROPTYPE type = 0;
  DWORD needed = 0;
  SetupDiGetDevicePropertyW(info, &data, &key, &type, nullptr, 0, &needed, 0);
  if (needed == 0) return false;
  std::vector<BYTE> buf(needed);
  if (!SetupDiGetDevicePropertyW(info, &data, &key, &type, buf.data(), needed, &needed, 0)) {
    return false;
  }
  if (type != DEVPROP_TYPE_STRING) return false;
  out = reinterpret_cast<wchar_t*>(buf.data());
  return !out.empty();
}

bool ParseVidPidMi(const std::wstring& hwid, std::uint16_t& vid, std::uint16_t& pid, int& mi) {
  vid = pid = 0;
  mi = -1;
  std::wstring up = hwid;
  for (auto& ch : up) ch = static_cast<wchar_t>(towupper(ch));

  auto grab = [&](const wchar_t* key, std::uint16_t& dest) {
    auto pos = up.find(key);
    if (pos == std::wstring::npos) return;
    pos += wcslen(key);
    unsigned value = 0;
    if (swscanf(up.c_str() + pos, L"%x", &value) == 1) {
      dest = static_cast<std::uint16_t>(value);
    }
  };
  grab(L"VID_", vid);
  grab(L"PID_", pid);

  auto mi_pos = up.find(L"MI_");
  if (mi_pos != std::wstring::npos) {
    unsigned value = 0;
    if (swscanf(up.c_str() + mi_pos + 3, L"%x", &value) == 1) {
      mi = static_cast<int>(value);
    }
  }
  return vid != 0 && pid != 0;
}

#ifndef IOCTL_HID_GET_REPORT_DESCRIPTOR
#define IOCTL_HID_GET_REPORT_DESCRIPTOR CTL_CODE(FILE_DEVICE_KEYBOARD, 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

bool ReadReportDescriptor(HANDLE device, std::vector<std::uint8_t>& out) {
  out.assign(4096, 0);
  DWORD returned = 0;
  if (!DeviceIoControl(device, IOCTL_HID_GET_REPORT_DESCRIPTOR, nullptr, 0, out.data(),
                       static_cast<DWORD>(out.size()), &returned, nullptr)) {
    out.clear();
    return false;
  }
  out.resize(returned);
  return returned > 0;
}

bool IsGenericMouseName(const std::wstring& name) {
  if (name.empty()) return true;
  std::wstring up = name;
  for (auto& ch : up) ch = static_cast<wchar_t>(towupper(ch));
  return up == L"HID-COMPLIANT MOUSE" ||
         up == L"HID COMPLIANT MOUSE" ||
         up == L"USB INPUT DEVICE" ||
         up == L"HID MOUSE" ||
         up == L"MOUSE" ||
         up.find(L"HID-COMPLIANT") != std::wstring::npos;
}

std::wstring ReadHidString(HANDLE handle, BOOLEAN (*fn)(HANDLE, PVOID, ULONG)) {
  wchar_t buf[127]{};
  if (!fn(handle, buf, sizeof(buf))) return {};
  std::wstring s(buf);
  while (!s.empty() && (s.back() == L'\0' || iswspace(s.back()))) s.pop_back();
  while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
  return s;
}

std::wstring NameFromParentUsb(DEVINST child) {
  DEVINST parent = 0;
  if (CM_Get_Parent(&parent, child, 0) != CR_SUCCESS) return {};

  wchar_t id[MAX_DEVICE_ID_LEN]{};
  if (CM_Get_Device_IDW(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return {};

  HDEVINFO info = SetupDiCreateDeviceInfoList(nullptr, nullptr);
  if (info == INVALID_HANDLE_VALUE) return {};

  SP_DEVINFO_DATA data{};
  data.cbSize = sizeof(data);
  std::wstring result;
  if (SetupDiOpenDeviceInfoW(info, id, nullptr, 0, &data)) {
    if (!GetDevicePropertyKey(info, data, DEVPKEY_Device_BusReportedDeviceDesc, result) ||
        IsGenericMouseName(result)) {
      std::wstring friendly;
      if (GetDeviceProperty(info, data, SPDRP_FRIENDLYNAME, friendly) && !IsGenericMouseName(friendly)) {
        result = friendly;
      } else if (GetDeviceProperty(info, data, SPDRP_DEVICEDESC, friendly) && !IsGenericMouseName(friendly)) {
        result = friendly;
      }
    }
  }
  SetupDiDestroyDeviceInfoList(info);
  return IsGenericMouseName(result) ? std::wstring{} : result;
}

std::wstring ResolveFriendlyName(HDEVINFO info, SP_DEVINFO_DATA& data, HANDLE hid_handle) {
  std::wstring product = ReadHidString(hid_handle, HidD_GetProductString);
  std::wstring manufacturer = ReadHidString(hid_handle, HidD_GetManufacturerString);

  std::wstring bus_reported;
  GetDevicePropertyKey(info, data, DEVPKEY_Device_BusReportedDeviceDesc, bus_reported);

  std::wstring friendly;
  GetDeviceProperty(info, data, SPDRP_FRIENDLYNAME, friendly);

  std::wstring desc;
  GetDeviceProperty(info, data, SPDRP_DEVICEDESC, desc);

  std::wstring parent = NameFromParentUsb(data.DevInst);

  if (!product.empty() && !IsGenericMouseName(product)) {
    if (!manufacturer.empty() && !IsGenericMouseName(manufacturer) &&
        _wcsicmp(manufacturer.c_str(), product.c_str()) != 0) {
      return manufacturer + L" " + product;
    }
    return product;
  }
  if (!bus_reported.empty() && !IsGenericMouseName(bus_reported)) return bus_reported;
  if (!parent.empty()) return parent;
  if (!friendly.empty() && !IsGenericMouseName(friendly)) return friendly;
  if (!desc.empty() && !IsGenericMouseName(desc)) return desc;

  if (!product.empty()) return product;
  if (!friendly.empty()) return friendly;
  if (!desc.empty()) return desc;
  return L"USB Mouse";
}

}  // namespace

std::wstring DescribeDevice(const MouseDevice& device) {
  wchar_t id[64];
  if (device.mi >= 0) {
    swprintf(id, 64, L"(%04X:%04X MI_%02X)", device.vid, device.pid, device.mi);
  } else {
    swprintf(id, 64, L"(%04X:%04X)", device.vid, device.pid);
  }
  return device.friendly_name + L"  " + id;
}

bool PopulateHidCaps(MouseDevice& device) {
  if (device.hid_path.empty()) return false;

  HANDLE handle = CreateFileW(
      device.hid_path.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    handle = CreateFileW(
        device.hid_path.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
  }
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  std::vector<std::uint8_t> desc;
  const bool ok = ReadReportDescriptor(handle, desc);
  CloseHandle(handle);
  if (!ok) {
    return ParseMouseReportDescriptor(nullptr, 0, device.layout, device.controls);
  }
  return ParseMouseReportDescriptor(desc.data(), desc.size(), device.layout, device.controls);
}

std::vector<MouseDevice> EnumerateMice() {
  std::vector<MouseDevice> result;

  GUID hid_guid{};
  HidD_GetHidGuid(&hid_guid);

  HDEVINFO info = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr,
                                       DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (info == INVALID_HANDLE_VALUE) {
    return result;
  }

  SP_DEVICE_INTERFACE_DATA if_data{};
  if_data.cbSize = sizeof(if_data);

  for (DWORD index = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &hid_guid, index, &if_data); ++index) {
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(info, &if_data, nullptr, 0, &needed, nullptr);
    if (needed == 0) continue;

    std::vector<BYTE> detail_buf(needed);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detail_buf.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SP_DEVINFO_DATA dev_data{};
    dev_data.cbSize = sizeof(dev_data);
    if (!SetupDiGetDeviceInterfaceDetailW(info, &if_data, detail, needed, nullptr, &dev_data)) {
      continue;
    }

    HANDLE handle = CreateFileW(detail->DevicePath, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      continue;
    }

    HIDD_ATTRIBUTES attrs{};
    attrs.Size = sizeof(attrs);
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    bool is_mouse = false;
    if (HidD_GetAttributes(handle, &attrs) && HidD_GetPreparsedData(handle, &preparsed)) {
      HIDP_CAPS caps{};
      if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
        is_mouse = (caps.UsagePage == 0x01 && caps.Usage == 0x02);
      }
      HidD_FreePreparsedData(preparsed);
    }
    if (!is_mouse) {
      CloseHandle(handle);
      continue;
    }

    MouseDevice device;
    device.hid_path = detail->DevicePath;
    device.vid = attrs.VendorID;
    device.pid = attrs.ProductID;
    device.friendly_name = ResolveFriendlyName(info, dev_data, handle);
    CloseHandle(handle);

    wchar_t id_buf[MAX_DEVICE_ID_LEN]{};
    if (CM_Get_Device_IDW(dev_data.DevInst, id_buf, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
      device.instance_id = id_buf;
    }

    GetDeviceProperty(info, dev_data, SPDRP_HARDWAREID, device.hardware_id);
    if (device.hardware_id.empty()) {
      wchar_t tmp[128];
      swprintf(tmp, 128, L"USB\\VID_%04X&PID_%04X", device.vid, device.pid);
      device.hardware_id = tmp;
    }

    std::uint16_t vid = 0, pid = 0;
    int mi = -1;
    if (ParseVidPidMi(device.hardware_id, vid, pid, mi)) {
      device.vid = vid;
      device.pid = pid;
      device.mi = mi;
    } else {
      ParseVidPidMi(device.instance_id, device.vid, device.pid, device.mi);
    }

    PopulateHidCaps(device);

    const bool exists = std::any_of(result.begin(), result.end(), [&](const MouseDevice& m) {
      return !_wcsicmp(m.instance_id.c_str(), device.instance_id.c_str());
    });
    if (!exists) {
      result.push_back(std::move(device));
    }
  }

  SetupDiDestroyDeviceInfoList(info);

  std::sort(result.begin(), result.end(), [](const MouseDevice& a, const MouseDevice& b) {
    return _wcsicmp(a.friendly_name.c_str(), b.friendly_name.c_str()) < 0;
  });
  return result;
}
