#pragma once

#include "devices.hpp"
#include "hid_caps.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class MouseReader {
public:
  using StatusFn = std::function<void(const std::wstring&)>;

  MouseReader();
  ~MouseReader();

  MouseReader(const MouseReader&) = delete;
  MouseReader& operator=(const MouseReader&) = delete;

  bool Start(MouseDevice device, StatusFn on_status);
  void Stop();
  bool Running() const { return running_.load(); }
  void UpdateDeviceMappings(const MouseDevice& device);

private:
  void ThreadMain();

  std::atomic<bool> running_{false};
  std::thread thread_;
  MouseDevice device_{};
  StatusFn on_status_;
  mutable std::mutex device_mutex_;
};