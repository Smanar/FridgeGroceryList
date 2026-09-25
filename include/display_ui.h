#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include <GxEPD2_BW.h>

#include "storage.h"


// Firmware version
#define FW_VERSION "0.1.000"

#define MAX_ITEM_LIST 7

// ---- Menu system ----
enum MenuItem
{
    MENU_RESTART = 0,
    MENU_DEVICE_INFO,
    MENU_REFRESH,
    MENU_SETTINGS,
    MENU_EXIT,
    MENU_COUNT
};

enum SettingsItem {
    SET_MENU_1 = 0,
    SET_AUDIO,
    SET_UPDATE_INTERVAL,
    SET_BLE_SETUP,
    SET_BACK,
    SET_COUNT
};

enum SleepIndicator
{
    SLEEP_NONE,
    SLEEP_LIGHT,
    SLEEP_DEEP,
    SLEEP_OFF
};

// ---- Screen-drawing functions ----
void drawSplashScreen();  // boot splash
int drawListScreen(int s, bool forced = false);
void drawErrorScreen(const char* title, const char* detail);
void drawShutdownScreen();
void drawLowBatteryScreen(int percent, bool critical);
void drawReBootScreen(void);
void drawMenuScreen(int selected, bool partial = false);
void drawSettingsScreen(int selected, const Settings& settings, bool partial = false);
void drawDeviceInfoScreen(const char* ssid, const char* ip, const char* SNuser, const char* SNPass, float battV, int battPct, bool partial = false);
void drawSleepIndicator(SleepIndicator type);


#endif
