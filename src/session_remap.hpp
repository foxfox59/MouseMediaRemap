#pragma once

#include "devices.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

// While remapping:
// - WH_MOUSE_LL swallows hardware mouse events (so the bound mouse cannot move/click)
// - Raw Input still sees per-device data (media keys / other-mouse cursor)
// - Injected events (LLMHF_INJECTED) are allowed through so our SendInput clicks work
class SessionRemapper {
public:
  using StatusFn = std::function<void(const std::wstring&)>;

  SessionRemapper() = default;
  ~SessionRemapper();

  SessionRemapper(const SessionRemapper&) = delete;
  SessionRemapper& operator=(const SessionRemapper&) = delete;

  bool Start(HWND ui_hwnd, MouseDevice device, StatusFn on_status);
  void Stop();
  bool Running() const { return running_.load(); }
  void UpdateDeviceMappings(const MouseDevice& device);

private:
  static LRESULT CALLBACK MouseLlProc(int code, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK RawWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  void ThreadMain();
  void HandleRaw(HRAWINPUT raw);
  bool PathIsBoundDevice(const std::wstring& path) const;
  void HandleBoundMouse(const RAWMOUSE& mouse);
  void HandleOtherMouse(const RAWMOUSE& mouse);
  MediaAction ActionForControlId(const std::wstring& id) const;
  void PostStatus(const std::wstring& text);

  static SessionRemapper* instance_;

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_request_{false};
  HWND ui_hwnd_ = nullptr;
  HWND raw_hwnd_ = nullptr;
  HHOOK mouse_hook_ = nullptr;
  std::thread thread_;

  MouseDevice device_{};
  StatusFn on_status_;
  mutable std::mutex device_mutex_;
  std::uint32_t bound_buttons_ = 0;

  std::uint16_t bound_vid_ = 0;
  std::uint16_t bound_pid_ = 0;
  std::wstring bound_hid_norm_;
};
