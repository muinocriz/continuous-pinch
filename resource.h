#pragma once

// Resource and control IDs, shared by app.rc and the C++ sources.

// Application version: used by the VERSIONINFO resource (app.rc) and shown
// in the tray tooltip. Update together with app.manifest and CHANGELOG.md.
#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 3
#define APP_VERSION_PATCH 0
#define APP_VERSION_STR "1.3.0"

#define IDI_APP_ICON 101

#define IDD_SETTINGS 200

// Settings dialog: group boxes and labels (localized at runtime)
#define IDC_GRP_GENERAL 1001
#define IDC_GRP_ZOOM 1002
#define IDC_GRP_KEYS 1003
#define IDC_GRP_APPS 1004
#define IDC_LBL_LANGUAGE 1011
#define IDC_LBL_SENS 1012
#define IDC_LBL_CURVE 1013
#define IDC_LBL_TARGET 1014
#define IDC_LBL_ZOOM_MOD 1015
#define IDC_LBL_RESET_MOD 1016
#define IDC_LBL_HARDRESET_MOD 1017
#define IDC_LBL_EXIT 1018
#define IDC_LBL_RESET_HINT 1019
#define IDC_LBL_HARDRESET_HINT 1020
#define IDC_LBL_WHITELIST 1021
#define IDC_LBL_CONFIG 1022
#define IDC_STARTUP_HINT 1023
#define IDC_LBL_ZOOM_HINT 1024

// Settings dialog: inputs
#define IDC_ENABLED 1101
#define IDC_LANGUAGE 1102
#define IDC_STARTUP 1103
#define IDC_SENS_SLIDER 1104
#define IDC_SENS_VALUE 1105
#define IDC_CURVE 1106
#define IDC_TARGET_MODE 1107
#define IDC_ZOOM_MOD 1108
#define IDC_RESET_MOD 1109
#define IDC_HARDRESET_MOD 1110
#define IDC_EXIT_HOTKEY 1111
#define IDC_WHITELIST 1112
#define IDC_CONFIG_PATH 1113
#define IDC_OPEN_CONFIG_DIR 1114

// Settings dialog: buttons (IDOK / IDCANCEL are the standard IDs)
#define IDC_DEFAULTS 1201
#define IDC_APPLY 1202
