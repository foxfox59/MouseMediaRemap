#include "app.hpp"
#include "driver_bind.hpp"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

constexpr wchar_t kMutexName[] = L"Local\\MouseMediaRemap.SingleInstance";
constexpr wchar_t kMainWindowClass[] = L"MouseMediaRemapDlg";

bool HasArg(int argc, wchar_t** argv, const wchar_t* needle) {
  for (int i = 1; i < argc; ++i) {
    if (!_wcsicmp(argv[i], needle)) return true;
  }
  return false;
}

bool ActivateExistingInstance() {
  HWND existing = FindWindowW(kMainWindowClass, nullptr);
  if (!existing) return false;

  if (IsIconic(existing)) {
    ShowWindow(existing, SW_RESTORE);
  } else {
    ShowWindow(existing, SW_SHOW);
  }
  SetForegroundWindow(existing);
  return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmd_line, int show_cmd) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv && argc >= 2) {
    const std::wstring arg1 = argv[1];
    if (arg1 == L"--elevate-bind" || arg1 == L"--elevate-restore" ||
        arg1 == L"--elevate-restore-all") {
      const int code = App::RunElevatedCommand(argc, argv);
      LocalFree(argv);
      return code;
    }
  }

  const bool minimized = argv && HasArg(argc, argv, L"--minimized");
  if (argv) LocalFree(argv);

  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (!mutex) return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    ActivateExistingInstance();
    CloseHandle(mutex);
    return 0;
  }

  (void)cmd_line;
  App app(instance);
  const int code = app.Run(minimized ? SW_HIDE : show_cmd);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return code;
}
