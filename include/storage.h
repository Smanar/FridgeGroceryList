#ifndef STORAGE_H
#define STORAGE_H

#include "LittleFS.h"


//#define SDCARD
#ifdef SDCARD
#include <SD_MMC.h>
#else
#include <FS.h>
#endif

// NVS Storage Keys
#define NVS_NAMESPACE           "puck_creds"
#define NVS_KEY_SSID            "ssid"
#define NVS_KEY_PASSWORD        "password"
#define NVS_KEY_SN_ID           "sn_id"
#define NVS_KEY_SN_PASS         "sn_pass"
#define NVS_KEY_WITAI_API       "witai_api"

// Credential storage
extern String g_ssid;
extern String g_password;
extern String g_SN_id;
extern String g_SN_pass;
extern String g_witai_api;
extern String g_timezone;

struct Settings
{
    bool audioAlerts      = false;   // false = silent
    int  updateInterval = 120;     // seconds between presence polls
    int  fullRefreshEvery = 10;      // full refresh every N partial updates
    String  timezone           = "";
};

// Load settings: tries SD card first, then NVS fallback.
void loadSettings(Settings& s);

// Save settings: writes to SD card (if available) and NVS.
void saveSettings(const Settings& s);

void InitStorage(void);
void EndStorage(void);
void listLittleFS(const char *path = "/");

void initializeNVS();
void loadCredentialsFromNVS();
void saveCredentialsToNVS();
bool hasStoredCredentials();
void clearStoredCredentials();

File loadFileMP3(const char* path);

#endif