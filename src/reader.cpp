#include "reader.hpp"
#include "media_keys.hpp"
#include "winusb_device.hpp"
#include "hid_caps.hpp"

#include <map>

MouseReader::MouseReader() = default;

MouseReader::~MouseReader() {
  Stop();
}

void MouseReader::UpdateDeviceMappings(const MouseDevice& device) {
  std::lock_guard lock(device_mutex_);
  device_.controls = device.controls;
}

bool MouseReader::Start(MouseDevice device, StatusFn on_status) {
  Stop();
  {
    std::lock_guard lock(device_mutex_);
    device_ = std::move(device);
  }
  on_status_ = std::move(on_status);
  running_ = true;
  thread_ = std::thread([this] { ThreadMain(); });
  return true;
}

void MouseReader::Stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

void MouseReader::ThreadMain() {
  MouseDevice local;
  {
    std::lock_guard lock(device_mutex_);
    local = device_;
  }

  auto status = [&](const std::wstring& s) {
    if (on_status_) on_status_(s);
  };

  WinUsbMouse usb;
  std::wstring error;
  if (!usb.OpenByVidPid(local.vid, local.pid, local.mi, error)) {
    status(L"Waiting for WinUSB device: " + error);
    // Keep trying briefly while running — bind may still be settling.
    for (int i = 0; i < 40 && running_; ++i) {
      Sleep(250);
      if (usb.OpenByVidPid(local.vid, local.pid, local.mi, error)) {
        break;
      }
    }
    if (!usb.IsOpen()) {
      status(L"Could not open WinUSB mouse: " + error);
      running_ = false;
      return;
    }
  }

  std::vector<std::uint8_t> desc;
  if (usb.ReadReportDescriptor(desc, error)) {
    MouseFieldLayout layout;
    std::vector<DeviceControl> controls;
    if (ParseMouseReportDescriptor(desc.data(), desc.size(), layout, controls)) {
      std::lock_guard lock(device_mutex_);
      // Keep user mappings by control id.
      std::map<std::wstring, MediaAction> old;
      for (const auto& c : device_.controls) old[c.id] = c.action;
      device_.layout = layout;
      device_.controls = controls;
      for (auto& c : device_.controls) {
        auto it = old.find(c.id);
        if (it != old.end()) c.action = it->second;
      }
      local = device_;
    }
  }

  status(L"Reading media controls from WinUSB mouse…");

  std::uint32_t button_state = 0;
  while (running_) {
    std::vector<std::uint8_t> report;
    if (!usb.ReadInterrupt(report, 500, error)) {
      status(L"Read error: " + error);
      break;
    }
    if (report.empty()) continue;

    ReportEdges edges{};
    if (!DecodeMouseReport(local.layout, report.data(), report.size(), button_state, edges)) {
      continue;
    }

    MouseDevice mapped;
    {
      std::lock_guard lock(device_mutex_);
      mapped = device_;
    }

    auto find_action = [&](const std::wstring& id) -> MediaAction {
      for (const auto& c : mapped.controls) {
        if (c.id == id) return c.action;
      }
      return MediaAction::None;
    };

    if (edges.button_down_mask) {
      for (int b = 0; b < mapped.layout.button_count && b < 32; ++b) {
        if (edges.button_down_mask & (1u << b)) {
          const auto usage = static_cast<std::uint16_t>(mapped.layout.button_usage_min + b);
          SendMediaAction(find_action(L"btn:" + std::to_wstring(usage)));
        }
      }
    }

    if (edges.wheel_steps > 0) {
      for (int i = 0; i < edges.wheel_steps; ++i) SendMediaAction(find_action(L"wheel:up"));
    } else if (edges.wheel_steps < 0) {
      for (int i = 0; i < -edges.wheel_steps; ++i) SendMediaAction(find_action(L"wheel:down"));
    }

    if (edges.hwheel_steps > 0) {
      for (int i = 0; i < edges.hwheel_steps; ++i) SendMediaAction(find_action(L"hwheel:right"));
    } else if (edges.hwheel_steps < 0) {
      for (int i = 0; i < -edges.hwheel_steps; ++i) SendMediaAction(find_action(L"hwheel:left"));
    }
  }

  usb.Close();
  status(L"Media reader stopped.");
  running_ = false;
}