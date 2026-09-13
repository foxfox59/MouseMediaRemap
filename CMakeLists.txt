cmake_minimum_required(VERSION 3.16)
project(MouseMediaRemap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MouseMediaRemap WIN32
  src/main.cpp
  src/app.cpp
  src/devices.cpp
  src/hid_caps.cpp
  src/media_keys.cpp
  src/settings.cpp
  src/winusb_device.cpp
  src/driver_bind.cpp
  src/session_remap.cpp
  res/app.rc
)

target_include_directories(MouseMediaRemap PRIVATE src res)

target_compile_definitions(MouseMediaRemap PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WIN32_WINNT=0x0A00
  WINVER=0x0A00
)

target_link_libraries(MouseMediaRemap PRIVATE
  setupapi
  cfgmgr32
  hid
  winusb
  newdev
  comctl32
  shell32
  ole32
  uuid
  gdi32
  user32
  advapi32
  shlwapi
)

if(MINGW)
  # Fully static MinGW runtime so the exe runs without libwinpthread/libgcc/libstdc++ DLLs.
  target_link_options(MouseMediaRemap PRIVATE -static -municode -mwindows)
endif()

install(TARGETS MouseMediaRemap RUNTIME DESTINATION bin)
install(FILES driver/MouseMediaRemap_WinUSB.inf DESTINATION share/MouseMediaRemap)
