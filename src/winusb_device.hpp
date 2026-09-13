#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include <winusb.h>

class WinUsbMouse {
public:
  WinUsbMouse() = default;
  ~WinUsbMouse();

  WinUsbMouse(const WinUsbMouse&) = delete;
  WinUsbMouse& operator=(const WinUsbMouse&) = delete;

  // Open by our device interface GUID after WinUSB bind, matching VID/PID/MI.
  bool OpenByVidPid(std::uint16_t vid, std::uint16_t pid, int mi, std::wstring& error);
  bool OpenByPath(const std::wstring& device_path, std::wstring& error);

  // Read HID report descriptor via class request.
  bool ReadReportDescriptor(std::vector<std::uint8_t>& out, std::wstring& error);

  // Blocking read from interrupt IN pipe. Returns false on close/error.
  bool ReadInterrupt(std::vector<std::uint8_t>& buffer, DWORD timeout_ms, std::wstring& error);

  void Close();
  bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE && winusb_ != nullptr; }

  static const GUID& InterfaceGuid();

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  WINUSB_INTERFACE_HANDLE winusb_ = nullptr;
  UCHAR pipe_id_ = 0;
  USHORT max_packet_ = 64;
};