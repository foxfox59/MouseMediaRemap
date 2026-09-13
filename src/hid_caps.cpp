#include "hid_caps.hpp"

#include <algorithm>
#include <cstring>

namespace {

struct LocalState {
  std::uint16_t usage_page = 0;
  std::uint16_t usage = 0;
  std::vector<std::uint16_t> usages;
  int report_size = 0;
  int report_count = 0;
  int report_id = -1;
  int logical_min = 0;
  int logical_max = 0;
  bool mouse_collection = false;
  int bit_pos = 0;
};

std::uint32_t ReadUnsigned(const std::uint8_t* data, int size) {
  std::uint32_t v = 0;
  for (int i = 0; i < size; ++i) {
    v |= static_cast<std::uint32_t>(data[i]) << (8 * i);
  }
  return v;
}

int ReadSigned(const std::uint8_t* data, int size) {
  std::uint32_t u = ReadUnsigned(data, size);
  int bits = size * 8;
  if (bits == 0) return 0;
  if (u & (1u << (bits - 1))) {
    return static_cast<int>(u) - (1 << bits);
  }
  return static_cast<int>(u);
}

std::wstring ButtonLabel(std::uint16_t usage) {
  switch (usage) {
    case 1: return L"Left button";
    case 2: return L"Right button";
    case 3: return L"Middle button";
    case 4: return L"Back button (X1)";
    case 5: return L"Forward button (X2)";
    default: return L"Button " + std::to_wstring(usage);
  }
}

}  // namespace

void ApplyDefaultMappings(std::vector<DeviceControl>& controls) {
  for (auto& c : controls) {
    if (c.kind == ControlKind::Button && c.usage == 1) {
      c.action = MediaAction::PreviousTrack;
    } else if (c.kind == ControlKind::Button && c.usage == 2) {
      c.action = MediaAction::NextTrack;
    } else if (c.kind == ControlKind::Button && c.usage == 3) {
      c.action = MediaAction::PlayPause;
    } else if (c.kind == ControlKind::WheelUp) {
      c.action = MediaAction::VolumeUp;
    } else if (c.kind == ControlKind::WheelDown) {
      c.action = MediaAction::VolumeDown;
    } else {
      c.action = MediaAction::None;
    }
  }
}

bool ParseMouseReportDescriptor(const std::uint8_t* data, size_t size,
                                MouseFieldLayout& layout,
                                std::vector<DeviceControl>& controls) {
  layout = {};
  controls.clear();

  LocalState st;
  std::vector<LocalState> stack;
  bool found_mouse = false;
  int global_bit_pos = 0;

  auto reset_locals = [&] {
    st.usages.clear();
    st.usage = 0;
  };

  size_t i = 0;
  while (i < size) {
    const std::uint8_t prefix = data[i++];
    const int size_code = prefix & 0x03;
    const int size_bytes = size_code == 3 ? 4 : size_code;
    const int type = (prefix >> 2) & 0x03;
    const int tag = (prefix >> 4) & 0x0F;

    if (prefix == 0xFE) {
      // Long item — skip
      if (i >= size) break;
      const std::uint8_t data_size = data[i++];
      if (i >= size) break;
      ++i;  // long tag
      i += data_size;
      continue;
    }

    if (i + static_cast<size_t>(size_bytes) > size) break;
    const std::uint8_t* value = data + i;
    i += size_bytes;

    if (type == 0) {  // Main
      if (tag == 8) {  // Input
        const bool is_const = size_bytes > 0 && (value[0] & 0x01);
        const bool is_variable = size_bytes > 0 && (value[0] & 0x02);
        const int field_bits = st.report_size;
        const int total_bits = st.report_size * st.report_count;

        if (!is_const && st.mouse_collection) {
          found_mouse = true;
          if (st.report_id >= 0) {
            layout.has_report_id = true;
            layout.report_id = static_cast<std::uint8_t>(st.report_id);
          }

          if (st.usage_page == 0x09) {
            layout.button_bit_offset = global_bit_pos;
            layout.button_count = st.report_count;
            std::uint16_t usage_min = st.usage ? st.usage : 1;
            if (!st.usages.empty()) {
              usage_min = st.usages.front();
            }
            layout.button_usage_min = usage_min;
            for (int b = 0; b < st.report_count; ++b) {
              std::uint16_t usage = usage_min + static_cast<std::uint16_t>(b);
              if (!st.usages.empty() && b < static_cast<int>(st.usages.size())) {
                usage = st.usages[b];
              }
              DeviceControl c;
              c.kind = ControlKind::Button;
              c.usage = usage;
              c.id = L"btn:" + std::to_wstring(usage);
              c.label = ButtonLabel(usage);
              controls.push_back(c);
            }
            global_bit_pos += total_bits;
          } else if (st.usage_page == 0x01 || st.usage_page == 0x0C) {
            // Possibly multiple axis usages in one Input (X, Y, Wheel, ...).
            std::vector<std::uint16_t> axis = st.usages;
            if (axis.empty() && st.usage) axis.push_back(st.usage);
            if (axis.empty()) {
              global_bit_pos += total_bits;
            } else {
              for (size_t ai = 0; ai < axis.size(); ++ai) {
                const std::uint16_t u = axis[ai];
                if (st.usage_page == 0x01 && u == 0x38) {
                  layout.wheel_bit_offset = global_bit_pos;
                  layout.wheel_bit_size = field_bits > 0 ? field_bits : 8;
                  DeviceControl up;
                  up.kind = ControlKind::WheelUp;
                  up.id = L"wheel:up";
                  up.label = L"Wheel up";
                  controls.push_back(up);
                  DeviceControl down;
                  down.kind = ControlKind::WheelDown;
                  down.id = L"wheel:down";
                  down.label = L"Wheel down";
                  controls.push_back(down);
                } else if ((st.usage_page == 0x01 && u == 0x48) ||
                           (st.usage_page == 0x0C && u == 0x238)) {
                  layout.hwheel_bit_offset = global_bit_pos;
                  layout.hwheel_bit_size = field_bits > 0 ? field_bits : 8;
                  DeviceControl left;
                  left.kind = ControlKind::HWheelLeft;
                  left.id = L"hwheel:left";
                  left.label = L"Wheel tilt left";
                  controls.push_back(left);
                  DeviceControl right;
                  right.kind = ControlKind::HWheelRight;
                  right.id = L"hwheel:right";
                  right.label = L"Wheel tilt right";
                  controls.push_back(right);
                }
                // X(0x30), Y(0x31), etc. still consume bits
                global_bit_pos += field_bits;
              }
              // If report_count > usages, remaining fields are padding-like extras
              if (static_cast<int>(axis.size()) < st.report_count) {
                global_bit_pos += field_bits * (st.report_count - static_cast<int>(axis.size()));
              }
            }
          } else {
            global_bit_pos += total_bits;
          }
          layout.report_bit_size = global_bit_pos;
        } else {
          global_bit_pos += total_bits;
        }

        reset_locals();
      } else if (tag == 10) {  // Collection
        std::uint32_t coll = size_bytes ? ReadUnsigned(value, size_bytes) : 0;
        stack.push_back(st);
        if (st.usage_page == 0x01 && st.usage == 0x02) {
          st.mouse_collection = true;
          global_bit_pos = 0;  // start of mouse input report fields
        }
        (void)coll;
        reset_locals();
      } else if (tag == 12) {  // End Collection
        if (!stack.empty()) {
          bool was_mouse = st.mouse_collection;
          st = stack.back();
          stack.pop_back();
          if (was_mouse) {
            // keep layout bit size
          }
        }
        reset_locals();
      } else {
        reset_locals();
      }
    } else if (type == 1) {  // Global
      const std::uint32_t u = size_bytes ? ReadUnsigned(value, size_bytes) : 0;
      const int s = size_bytes ? ReadSigned(value, size_bytes) : 0;
      switch (tag) {
        case 0: st.usage_page = static_cast<std::uint16_t>(u); break;
        case 1: st.logical_min = s; break;
        case 2: st.logical_max = s; break;
        case 7: st.report_size = static_cast<int>(u); break;
        case 8: st.report_id = static_cast<int>(u); break;
        case 9: st.report_count = static_cast<int>(u); break;
        default: break;
      }
    } else if (type == 2) {  // Local
      const std::uint32_t u = size_bytes ? ReadUnsigned(value, size_bytes) : 0;
      switch (tag) {
        case 0:
          st.usage = static_cast<std::uint16_t>(u);
          st.usages.push_back(st.usage);
          break;
        case 1: {  // Usage Minimum
          st.usage = static_cast<std::uint16_t>(u);
          break;
        }
        case 2: {  // Usage Maximum
          const std::uint16_t usage_max = static_cast<std::uint16_t>(u);
          st.usages.clear();
          for (std::uint16_t uage = st.usage; uage <= usage_max; ++uage) {
            st.usages.push_back(uage);
          }
          break;
        }
        default: break;
      }
    }
  }

  if (controls.empty()) {
    // Fallback: assume boot mouse protocol layout.
    layout.has_report_id = false;
    layout.button_bit_offset = 0;
    layout.button_count = 3;
    layout.button_usage_min = 1;
    layout.wheel_bit_offset = 24;
    layout.wheel_bit_size = 8;
    layout.report_bit_size = 32;
    for (std::uint16_t u = 1; u <= 3; ++u) {
      DeviceControl c;
      c.kind = ControlKind::Button;
      c.usage = u;
      c.id = L"btn:" + std::to_wstring(u);
      c.label = ButtonLabel(u);
      controls.push_back(c);
    }
    DeviceControl up{ControlKind::WheelUp, 0, L"wheel:up", L"Wheel up"};
    DeviceControl down{ControlKind::WheelDown, 0, L"wheel:down", L"Wheel down"};
    controls.push_back(up);
    controls.push_back(down);
    found_mouse = true;
  }

  ApplyDefaultMappings(controls);
  return found_mouse || !controls.empty();
}

static int ReadSignedBits(const std::uint8_t* report, size_t size, int bit_offset, int bit_size) {
  if (bit_offset < 0 || bit_size <= 0) return 0;
  int value = 0;
  for (int b = 0; b < bit_size; ++b) {
    const int abs_bit = bit_offset + b;
    const int byte_index = abs_bit / 8;
    const int bit_index = abs_bit % 8;
    if (static_cast<size_t>(byte_index) >= size) break;
    if (report[byte_index] & (1u << bit_index)) {
      value |= (1 << b);
    }
  }
  if (bit_size < 32 && (value & (1 << (bit_size - 1)))) {
    value -= (1 << bit_size);
  }
  return value;
}

bool DecodeMouseReport(const MouseFieldLayout& layout,
                       const std::uint8_t* report, size_t size,
                       std::uint32_t& io_button_state,
                       ReportEdges& edges) {
  edges = {};
  if (!report || size == 0) return false;

  size_t offset = 0;
  if (layout.has_report_id) {
    if (report[0] != layout.report_id) {
      return false;
    }
    offset = 1;
  }

  const std::uint8_t* body = report + offset;
  const size_t body_size = size - offset;

  std::uint32_t buttons = 0;
  if (layout.button_bit_offset >= 0 && layout.button_count > 0) {
    for (int b = 0; b < layout.button_count && b < 32; ++b) {
      const int abs_bit = layout.button_bit_offset + b;
      const int byte_index = abs_bit / 8;
      const int bit_index = abs_bit % 8;
      if (static_cast<size_t>(byte_index) < body_size) {
        if (body[byte_index] & (1u << bit_index)) {
          buttons |= (1u << b);
        }
      }
    }
  }

  edges.button_down_mask = buttons & ~io_button_state;
  edges.button_up_mask = io_button_state & ~buttons;
  io_button_state = buttons;

  if (layout.wheel_bit_offset >= 0) {
    edges.wheel_steps = ReadSignedBits(body, body_size, layout.wheel_bit_offset, layout.wheel_bit_size);
  }
  if (layout.hwheel_bit_offset >= 0) {
    edges.hwheel_steps = ReadSignedBits(body, body_size, layout.hwheel_bit_offset, layout.hwheel_bit_size);
  }
  return true;
}