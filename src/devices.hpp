#pragma once

#include <string>
#include <vector>
#include <cstdint>

enum class MediaAction : int {
  None = 0,
  PreviousTrack,
  PlayPause,
  NextTrack,
  Stop,
  Mute,
  VolumeUp,
  VolumeDown,
  Count
};

const wchar_t* MediaActionName(MediaAction action);
MediaAction MediaActionFromName(const std::wstring& name);

enum class ControlKind {
  Button,
  WheelUp,
  WheelDown,
  HWheelLeft,
  HWheelRight
};

struct DeviceControl {
  ControlKind kind{};
  std::uint16_t usage = 0; // button usage, or 0 for wheels
  std::wstring id;         // stable id e.g. "btn:1", "wheel:up"
  std::wstring label;      // UI label
  MediaAction action = MediaAction::None;
};

struct MouseFieldLayout {
  bool has_report_id = false;
  std::uint8_t report_id = 0;
  int report_bit_size = 0;

  // Button bitfield
  int button_bit_offset = -1;
  int button_count = 0;
  std::uint16_t button_usage_min = 1;

  // Relative wheel (HID 0x38), signed
  int wheel_bit_offset = -1;
  int wheel_bit_size = 0;

  // Horizontal wheel / AC Pan (0x0C/0x238) or 0x48
  int hwheel_bit_offset = -1;
  int hwheel_bit_size = 0;
};

struct MouseDevice {
  std::wstring instance_id;
  std::wstring hardware_id;   // primary USB\VID_...&PID_...[&MI_..]
  std::wstring friendly_name;
  std::wstring hid_path;      // \\?\hid#... when still HID
  std::wstring winusb_path;   // \\?\usb#... when already on WinUSB
  std::uint16_t vid = 0;
  std::uint16_t pid = 0;
  int mi = -1;                // interface number if present
  bool on_winusb = false;
  MouseFieldLayout layout{};
  std::vector<DeviceControl> controls;
};

std::vector<MouseDevice> EnumerateMice();
bool PopulateHidCaps(MouseDevice& device);
std::wstring DescribeDevice(const MouseDevice& device);