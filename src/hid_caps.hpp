#pragma once

#include "devices.hpp"
#include <span>
#include <vector>
#include <cstdint>

// Parse a raw HID report descriptor into a mouse field layout + control list.
bool ParseMouseReportDescriptor(const std::uint8_t* data, size_t size,
                                MouseFieldLayout& layout,
                                std::vector<DeviceControl>& controls);

// Apply default media actions for known controls.
void ApplyDefaultMappings(std::vector<DeviceControl>& controls);

// Decode one input report using a previously parsed layout.
struct ReportEdges {
  std::uint32_t button_down_mask = 0; // bit i => button usage_min+i pressed edge
  std::uint32_t button_up_mask = 0;
  int wheel_steps = 0;   // +up / -down notches this report
  int hwheel_steps = 0;  // +right / -left
};

bool DecodeMouseReport(const MouseFieldLayout& layout,
                       const std::uint8_t* report, size_t size,
                       std::uint32_t& io_button_state,
                       ReportEdges& edges);