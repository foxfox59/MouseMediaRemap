#include "session_remap.hpp"
#include "media_keys.hpp"
#include "resource.h"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace {

std::wstring NormalizePath(std::wstring s) {
  for (auto& ch : s) ch = static_cast<wchar_t>(towupper(ch));
  return s;
}

unsigned ExtractHex(const std::wstring& path, const wchar_t* key) {
  auto pos = path.find(key);
  if (pos == std::wstring::npos) return 0;
  unsigned v = 0;
  swscanf(path.c_str() + pos + wcslen(key), L"%x", &v);
  return v;
}

#ifndef LLMHF_INJECTED
#define LLMHF_INJECTED 0x00000001
#endif
#ifndef LLMHF_LOWER_IL_INJECTED
#define LLMHF_LOWER_IL_INJECTED 0x00000002
#endif

}  // namespace

SessionRemapper* SessionRemapper::instance_ = nullptr;

SessionRemapper::~SessionRemapper() {
  Stop();
}

void SessionRemapper::PostStatus(const std::wstring& text) {
  if (!ui_hwnd_) return;
  auto* heap = new std::wstring(text);
  if (!PostMessageW(ui_hwnd_, WM_APP_READER_STATUS, 0, reinterpret_cast<LPARAM>(heap))) {
    delete heap;
  }
}

void SessionRemapper::UpdateDeviceMappings(const MouseDevice& device) {
  std::lock_guard lock(device_mutex_);
  device_.controls = device.controls;
}

LRESULT CALLBACK SessionRemapper::MouseLlProc(int code, WPARAM wparam, LPARAM lparam) {
  if (code == HC_ACTION && instance_ && instance_->running_.load()) {
    const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lparam);
    const bool injected =
        (info->flags & LLMHF_INJECTED) != 0 || (info->flags & LLMHF_LOWER_IL_INJECTED) != 0;
    if (!injected) {
      // Swallow all hardware mouse events. Per-device handling is done via Raw Input.
      return 1;
    }
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}

bool SessionRemapper::Start(HWND ui_hwnd, MouseDevice device, StatusFn on_status) {
  Stop();

  ui_hwnd_ = ui_hwnd;
  on_status_ = std::move(on_status);
  {
    std::lock_guard lock(device_mutex_);
    device_ = std::move(device);
    bound_vid_ = device_.vid;
    bound_pid_ = device_.pid;
    bound_hid_norm_ = NormalizePath(device_.hid_path);
  }
  bound_buttons_ = 0;
  stop_request_ = false;
  instance_ = this;
  running_ = true;

  // Low-level hook must live on a thread with a message pump; install on UI thread (caller).
  mouse_hook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseLlProc, GetModuleHandleW(nullptr), 0);
  if (!mouse_hook_) {
    running_ = false;
    instance_ = nullptr;
    PostStatus(L"Could not install mouse hook.");
    return false;
  }

  thread_ = std::thread([this] { ThreadMain(); });
  PostStatus(L"Remapping active.");
  return true;
}

void SessionRemapper::Stop() {
  if (!running_ && !thread_.joinable() && !mouse_hook_) return;

  stop_request_ = true;
  running_ = false;

  if (mouse_hook_) {
    UnhookWindowsHookEx(mouse_hook_);
    mouse_hook_ = nullptr;
  }
  instance_ = nullptr;

  HWND sink = raw_hwnd_;
  if (sink) {
    PostMessageW(sink, WM_CLOSE, 0, 0);
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  raw_hwnd_ = nullptr;
  stop_request_ = false;

  auto* heap = new std::wstring(L"Remapping stopped.");
  if (!(ui_hwnd_ && PostMessageW(ui_hwnd_, WM_APP_READER_STATUS, 0, reinterpret_cast<LPARAM>(heap)))) {
    delete heap;
  }
  on_status_ = nullptr;
  ui_hwnd_ = nullptr;
}

LRESULT CALLBACK SessionRemapper::RawWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* self = reinterpret_cast<SessionRemapper*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<SessionRemapper*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);

  if (msg == WM_INPUT) {
    self->HandleRaw(reinterpret_cast<HRAWINPUT>(lparam));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_CLOSE) {
    DestroyWindow(hwnd);
    return 0;
  }
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void SessionRemapper::ThreadMain() {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = SessionRemapper::RawWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"MouseMediaRemapRawSink";
  RegisterClassExW(&wc);

  raw_hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                              wc.hInstance, this);
  if (!raw_hwnd_) {
    PostStatus(L"Could not create raw input sink window.");
    return;
  }

  // No RIDEV_NOLEGACY — the LL hook suppresses hardware mouse; Raw Input is observation-only.
  RAWINPUTDEVICE rid{};
  rid.usUsagePage = 0x01;
  rid.usUsage = 0x02;
  rid.dwFlags = RIDEV_INPUTSINK;
  rid.hwndTarget = raw_hwnd_;
  if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
    PostStatus(L"Could not register Raw Input.");
    DestroyWindow(raw_hwnd_);
    raw_hwnd_ = nullptr;
    return;
  }

  MSG msg;
  while (!stop_request_.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  RAWINPUTDEVICE remove{};
  remove.usUsagePage = 0x01;
  remove.usUsage = 0x02;
  remove.dwFlags = RIDEV_REMOVE;
  remove.hwndTarget = nullptr;
  RegisterRawInputDevices(&remove, 1, sizeof(remove));

  if (raw_hwnd_) {
    DestroyWindow(raw_hwnd_);
    raw_hwnd_ = nullptr;
  }
}

bool SessionRemapper::PathIsBoundDevice(const std::wstring& path) const {
  const std::wstring needle = NormalizePath(path);
  if (!bound_hid_norm_.empty() &&
      (needle == bound_hid_norm_ || needle.find(bound_hid_norm_) != std::wstring::npos ||
       bound_hid_norm_.find(needle) != std::wstring::npos)) {
    return true;
  }
  const unsigned vid = ExtractHex(needle, L"VID_");
  const unsigned pid = ExtractHex(needle, L"PID_");
  return vid != 0 && pid != 0 && vid == bound_vid_ && pid == bound_pid_;
}

MediaAction SessionRemapper::ActionForControlId(const std::wstring& id) const {
  std::lock_guard lock(device_mutex_);
  for (const auto& c : device_.controls) {
    if (c.id == id) return c.action;
  }
  return MediaAction::None;
}

void SessionRemapper::HandleBoundMouse(const RAWMOUSE& mouse) {
  // Movement ignored — LL hook already prevents cursor updates from hardware.
  const USHORT flags = mouse.usButtonFlags;

  auto edge = [&](USHORT down_flag, USHORT up_flag, int bit, const std::wstring& id) {
    if (flags & down_flag) {
      if (!(bound_buttons_ & (1u << bit))) {
        bound_buttons_ |= (1u << bit);
        SendMediaAction(ActionForControlId(id));
      }
    }
    if (flags & up_flag) {
      bound_buttons_ &= ~(1u << bit);
    }
  };

  edge(RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, 0, L"btn:1");
  edge(RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, 1, L"btn:2");
  edge(RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 2, L"btn:3");
  edge(RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, 3, L"btn:4");
  edge(RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, 4, L"btn:5");

  if (flags & RI_MOUSE_WHEEL) {
    const SHORT delta = static_cast<SHORT>(mouse.usButtonData);
    if (delta > 0) {
      const int steps = std::max(1, delta / WHEEL_DELTA);
      for (int i = 0; i < steps; ++i) SendMediaAction(ActionForControlId(L"wheel:up"));
    } else if (delta < 0) {
      const int steps = std::max(1, (-delta) / WHEEL_DELTA);
      for (int i = 0; i < steps; ++i) SendMediaAction(ActionForControlId(L"wheel:down"));
    }
  }
  if (flags & RI_MOUSE_HWHEEL) {
    const SHORT delta = static_cast<SHORT>(mouse.usButtonData);
    if (delta > 0) SendMediaAction(ActionForControlId(L"hwheel:right"));
    else if (delta < 0) SendMediaAction(ActionForControlId(L"hwheel:left"));
  }
}

void SessionRemapper::HandleOtherMouse(const RAWMOUSE& mouse) {
  const bool absolute = (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
  if (absolute) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int x = vx + static_cast<int>((static_cast<__int64>(mouse.lLastX) * vw) / 65535);
    const int y = vy + static_cast<int>((static_cast<__int64>(mouse.lLastY) * vh) / 65535);
    SetCursorPos(x, y);
  } else if (mouse.lLastX != 0 || mouse.lLastY != 0) {
    POINT pt{};
    if (GetCursorPos(&pt)) {
      SetCursorPos(pt.x + mouse.lLastX, pt.y + mouse.lLastY);
    }
  }

  std::vector<INPUT> inputs;
  auto push = [&](DWORD flag, DWORD data = 0) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    in.mi.mouseData = data;
    // Mark as injected so our LL hook lets it through.
    in.mi.dwExtraInfo = 0;
    inputs.push_back(in);
  };

  const USHORT flags = mouse.usButtonFlags;
  if (flags & RI_MOUSE_LEFT_BUTTON_DOWN) push(MOUSEEVENTF_LEFTDOWN);
  if (flags & RI_MOUSE_LEFT_BUTTON_UP) push(MOUSEEVENTF_LEFTUP);
  if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN) push(MOUSEEVENTF_RIGHTDOWN);
  if (flags & RI_MOUSE_RIGHT_BUTTON_UP) push(MOUSEEVENTF_RIGHTUP);
  if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) push(MOUSEEVENTF_MIDDLEDOWN);
  if (flags & RI_MOUSE_MIDDLE_BUTTON_UP) push(MOUSEEVENTF_MIDDLEUP);
  if (flags & RI_MOUSE_BUTTON_4_DOWN) push(MOUSEEVENTF_XDOWN, XBUTTON1);
  if (flags & RI_MOUSE_BUTTON_4_UP) push(MOUSEEVENTF_XUP, XBUTTON1);
  if (flags & RI_MOUSE_BUTTON_5_DOWN) push(MOUSEEVENTF_XDOWN, XBUTTON2);
  if (flags & RI_MOUSE_BUTTON_5_UP) push(MOUSEEVENTF_XUP, XBUTTON2);
  if (flags & RI_MOUSE_WHEEL) {
    push(MOUSEEVENTF_WHEEL, static_cast<DWORD>(static_cast<SHORT>(mouse.usButtonData)));
  }
  if (flags & RI_MOUSE_HWHEEL) {
    push(MOUSEEVENTF_HWHEEL, static_cast<DWORD>(static_cast<SHORT>(mouse.usButtonData)));
  }

  if (!inputs.empty()) {
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  }
}

void SessionRemapper::HandleRaw(HRAWINPUT raw) {
  if (stop_request_.load() || !running_.load()) return;

  UINT size = 0;
  GetRawInputData(raw, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
  if (size == 0 || size > 1024) return;

  BYTE stack_buf[1024];
  BYTE* buf = stack_buf;
  std::vector<BYTE> heap;
  if (size > sizeof(stack_buf)) {
    heap.resize(size);
    buf = heap.data();
  }
  if (GetRawInputData(raw, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
    return;
  }

  auto* input = reinterpret_cast<RAWINPUT*>(buf);
  if (input->header.dwType != RIM_TYPEMOUSE) return;
  if (input->header.hDevice == nullptr) return;

  wchar_t name[512]{};
  UINT name_size = sizeof(name);
  if (GetRawInputDeviceInfoW(input->header.hDevice, RIDI_DEVICENAME, name, &name_size) ==
      static_cast<UINT>(-1)) {
    return;
  }

  if (PathIsBoundDevice(name)) {
    HandleBoundMouse(input->data.mouse);
  } else {
    HandleOtherMouse(input->data.mouse);
  }
}
