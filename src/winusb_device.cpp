#include "winusb_device.hpp"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <cwctype>
#include <vector>

// App-specific WinUSB interface GUID {9F3A8C2E-6B1D-4E7A-9C11-B055E0ED1A01}
DEFINE_GUID(GUID_MOUSEMEDIA_WINUSB,
  0x9f3a8c2e, 0x6b1d, 0x4e7a, 0x9c, 0x11, 0xb0, 0x55, 0xe0, 0xed, 0x1a, 0x01);

const GUID& WinUsbMouse::InterfaceGuid() {
  return GUID_MOUSEMEDIA_WINUSB;
}

WinUsbMouse::~WinUsbMouse() {
  Close();
}

void WinUsbMouse::Close() {
  if (winusb_) {
    WinUsb_Free(winusb_);
    winusb_ = nullptr;
  }
  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
  pipe_id_ = 0;
}

bool WinUsbMouse::OpenByVidPid(std::uint16_t vid, std::uint16_t pid, int mi, std::wstring& error) {
  Close();

  auto try_open_path = [&](const wchar_t* device_path) -> bool {
    HANDLE h = CreateFileW(device_path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    WINUSB_INTERFACE_HANDLE wh = nullptr;
    if (!WinUsb_Initialize(h, &wh)) {
      CloseHandle(h);
      return false;
    }

    USB_INTERFACE_DESCRIPTOR iface{};
    if (!WinUsb_QueryInterfaceSettings(wh, 0, &iface)) {
      WinUsb_Free(wh);
      CloseHandle(h);
      return false;
    }

    UCHAR pipe_id = 0;
    USHORT max_packet = 64;
    bool found_pipe = false;
    for (UCHAR p = 0; p < iface.bNumEndpoints; ++p) {
      WINUSB_PIPE_INFORMATION pipe{};
      if (!WinUsb_QueryPipe(wh, 0, p, &pipe)) continue;
      if (pipe.PipeType == UsbdPipeTypeInterrupt && (pipe.PipeId & 0x80)) {
        pipe_id = pipe.PipeId;
        max_packet = pipe.MaximumPacketSize;
        found_pipe = true;
        break;
      }
    }
    if (!found_pipe) {
      WinUsb_Free(wh);
      CloseHandle(h);
      return false;
    }

    handle_ = h;
    winusb_ = wh;
    pipe_id_ = pipe_id;
    max_packet_ = max_packet;
    return true;
  };

  auto path_matches = [&](std::wstring path) {
    for (auto& ch : path) ch = static_cast<wchar_t>(towupper(ch));
    wchar_t needle[64];
    swprintf(needle, 64, L"VID_%04X&PID_%04X", vid, pid);
    if (path.find(needle) == std::wstring::npos) return false;
    if (mi >= 0) {
      wchar_t mi_needle[32];
      swprintf(mi_needle, 32, L"MI_%02X", mi);
      if (path.find(mi_needle) == std::wstring::npos) return false;
    }
    return true;
  };

  auto enumerate_guid = [&](const GUID& guid) -> bool {
    HDEVINFO info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA if_data{};
    if_data.cbSize = sizeof(if_data);
    bool opened = false;
    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &guid, index, &if_data); ++index) {
      DWORD needed = 0;
      SetupDiGetDeviceInterfaceDetailW(info, &if_data, nullptr, 0, &needed, nullptr);
      std::vector<BYTE> buf(needed);
      auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
      detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
      if (!SetupDiGetDeviceInterfaceDetailW(info, &if_data, detail, needed, nullptr, nullptr)) continue;
      if (!path_matches(detail->DevicePath)) continue;
      if (try_open_path(detail->DevicePath)) {
        opened = true;
        error.clear();
        break;
      }
    }
    SetupDiDestroyDeviceInfoList(info);
    return opened;
  };

  if (enumerate_guid(GUID_MOUSEMEDIA_WINUSB)) return true;

  // Fallback: any USB device interface (e.g. Zadig WinUSB bind).
  static const GUID kUsbDeviceInterface =
      {0xA5DCBF10, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};
  if (enumerate_guid(kUsbDeviceInterface)) return true;

  if (error.empty()) error = L"Matching WinUSB mouse interface not found.";
  return false;
}

bool WinUsbMouse::ReadReportDescriptor(std::vector<std::uint8_t>& out, std::wstring& error) {
  if (!IsOpen()) {
    error = L"Device not open.";
    return false;
  }

  // HID GET_DESCRIPTOR (Report) over control pipe: bmRequest 0x81, GET_DESCRIPTOR 0x06, wValue 0x2200
  WINUSB_SETUP_PACKET setup{};
  setup.RequestType = 0x81;
  setup.Request = 0x06;
  setup.Value = 0x2200;
  setup.Index = 0;
  setup.Length = 4096;

  out.assign(4096, 0);
  ULONG transferred = 0;
  if (!WinUsb_ControlTransfer(winusb_, setup, out.data(), static_cast<ULONG>(out.size()), &transferred, nullptr)) {
    error = L"Failed to read HID report descriptor over WinUSB.";
    out.clear();
    return false;
  }
  out.resize(transferred);
  return transferred > 0;
}

bool WinUsbMouse::ReadInterrupt(std::vector<std::uint8_t>& buffer, DWORD timeout_ms, std::wstring& error) {
  if (!IsOpen()) {
    error = L"Device not open.";
    return false;
  }

  buffer.assign(max_packet_ ? max_packet_ : 64, 0);
  ULONG transferred = 0;

  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) {
    error = L"CreateEvent failed.";
    return false;
  }

  const BOOL ok = WinUsb_ReadPipe(winusb_, pipe_id_, buffer.data(), static_cast<ULONG>(buffer.size()),
                                  &transferred, &ov);
  if (!ok) {
    const DWORD err = GetLastError();
    if (err != ERROR_IO_PENDING) {
      CloseHandle(ov.hEvent);
      error = L"WinUsb_ReadPipe failed.";
      return false;
    }
    const DWORD wait = WaitForSingleObject(ov.hEvent, timeout_ms);
    if (wait == WAIT_TIMEOUT) {
      WinUsb_AbortPipe(winusb_, pipe_id_);
      CancelIoEx(handle_, &ov);
      WaitForSingleObject(ov.hEvent, 1000);
      CloseHandle(ov.hEvent);
      buffer.clear();
      return true;  // timeout is not fatal
    }
    if (wait != WAIT_OBJECT_0 || !GetOverlappedResult(handle_, &ov, &transferred, FALSE)) {
      CloseHandle(ov.hEvent);
      error = L"Interrupted WinUSB read failed.";
      return false;
    }
  }

  CloseHandle(ov.hEvent);
  buffer.resize(transferred);
  return true;
}