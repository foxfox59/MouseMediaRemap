#pragma once

#define IDI_APP                 1
#define IDI_TRAY_IDLE           2
#define IDI_TRAY_ACTIVE         3
#define IDD_MAIN                100
#define IDC_DEVICE_LIST         101
#define IDC_REFRESH             102
#define IDC_BIND                103
#define IDC_START_WITH_WINDOWS  104
#define IDC_STATUS              105
#define IDC_MAPPINGS_HOST       106
#define IDC_STATIC_DEVICE       107
#define IDC_STATIC_MAPPINGS     108
#define IDC_DETECT              109
#define IDC_TOGGLE_REMAP        110

#define IDM_TRAY_SHOW           200
#define IDM_TRAY_EXIT           202
#define IDM_TRAY_TOGGLE_REMAP   203

#define WM_TRAYICON             (WM_APP + 1)
#define WM_APP_READER_STATUS    (WM_APP + 2)
#define WM_APP_CONTROL_EVENT    (WM_APP + 3)
#define WM_APP_DETECT_TIMEOUT   (WM_APP + 4)

#define HOTKEY_STOP_REMAP       1
