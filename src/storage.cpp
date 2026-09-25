#include <Arduino.h>
#include <Preferences.h>

#include "storage.h"

// Global credential storage
String g_ssid = "";
String g_password = "";
String g_SN_id = "";
String g_SN_pass = "";
String g_witai_api = "";
String g_timezone = "";

static const char* SETTINGS_NS = "App_settings";


void InitStorage(void)
{
    if (!LittleFS.begin(true))
    {
        Serial.println("[LittleFS] Init fail");
        return;
    }
    Serial.println("[LittleFS] Initialised");
}

void EndStorage(void)
{
    LittleFS.end();
    //SD_MMC.close()
}

void listLittleFS(const char *path)
{
    Serial.println("[LittleFS] SCAN LittleFS");

    File root = LittleFS.open(path);
    if (!root || !root.isDirectory())
    {
        Serial.println("[LittleFS] Error opening the folder");
        return;
    }
    File file = root.openNextFile();
    while (file) {
        Serial.printf("  %s  (%d octets)\n", file.name(), file.size());
        file = root.openNextFile();
    }

    Serial.println("[LittleFS] End SCAN LittleFS");
}

//------------------------------------------------
// Load credentials from NVS
//------------------------------------------------
void loadCredentialsFromNVS()
{
  Serial.println("[NVS] Loading credentials...");

// Use personal_settings.h ?
#if __has_include("personal_settings.h")
    Serial.println("[NVS] Use personal_settings.h");
    #include "personal_settings.h"

    g_password = String(WIFIPASSWORD);
    g_ssid = String(WIFISSID);
    g_SN_id = String(SN_ID);
    g_SN_pass = String(SN_PASS);
    g_witai_api = String(WITAI_API);

#else
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) // open read-only
    {
        g_ssid = prefs.getString(NVS_KEY_SSID, "");
        g_password = prefs.getString(NVS_KEY_PASSWORD, "");
        g_SN_id = prefs.getString(NVS_KEY_SN_ID, "");
        g_SN_pass = prefs.getString(NVS_KEY_SN_PASS, "");
        g_witai_api = prefs.getString(NVS_KEY_WITAI_API, "");
        prefs.end();
    }
#endif

  Serial.printf("  SSID: %s\n", g_ssid.c_str());
  Serial.printf("  SN_ID: %s\n", g_SN_id.c_str());
  Serial.printf("  SN_PASS: %s\n", g_SN_pass.c_str());
  Serial.printf("  WIT.AI API: %s\n", g_witai_api.c_str());
}

//------------------------------------------------
// Save credentials to NVS
//------------------------------------------------
void saveCredentialsToNVS()
{
    Serial.println("[NVS] Saving credentials...");

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) // open read-write
    {
        prefs.putString(NVS_KEY_SSID, g_ssid);
        prefs.putString(NVS_KEY_PASSWORD, g_password);
        prefs.putString(NVS_KEY_SN_ID, g_SN_id);
        prefs.putString(NVS_KEY_SN_PASS, g_SN_pass);
        prefs.putString(NVS_KEY_WITAI_API, g_witai_api);
        prefs.end();
        Serial.println("[NVS] ✓ Credentials saved");
    }
    else
    {
        Serial.println("[NVS] ✗ Failed to open NVS for writing");
    }
}

//------------------------------------------------
// Check if valid credentials exist in NVS
//------------------------------------------------
bool hasStoredCredentials()
{
    Preferences prefs;
    bool has_creds = false;
    
    if (prefs.begin(NVS_NAMESPACE, true)) // Read-only
    {
        // Check SSID only — a provisioned device at minimum has WiFi credentials.
        has_creds = !prefs.getString(NVS_KEY_SSID, "").isEmpty();
        prefs.end();
    }
    return has_creds;
}

//------------------------------------------------
// Clear all stored credentials from NVS (factory reset)
//------------------------------------------------
void clearStoredCredentials()
{
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false))
    {
        prefs.clear();
        prefs.end();
    }
    
    g_ssid = "";
    g_password = "";
    g_SN_id = "";
    g_SN_pass = "";
    g_witai_api = "";

    Serial.println("[NVS] All credentials cleared");
}

//------------------------------------------------
// Initialize the NVS (Non-Volatile Storage)
//------------------------------------------------
void initializeNVS()
{
  Serial.println("[NVS] Initializing...");
  Preferences prefs;
  
  // Quick open/close to verify NVS partition is accessible
  if (prefs.begin(NVS_NAMESPACE, true))
  {
    prefs.end();
    Serial.println("[NVS] ✓ Ready");
  }
  else
  {
    Serial.println("[NVS] ✗ Failed");
  }
}

void loadSettings(Settings& s)
{
    Preferences prefs;

    // Open in read-only (true)
    if (prefs.begin(SETTINGS_NS, true))
    {
        s.audioAlerts      = prefs.getBool("audio",      s.audioAlerts);
        s.updateInterval = prefs.getInt("interval",      s.updateInterval);
        s.fullRefreshEvery = prefs.getInt("fullEvery"  , s.fullRefreshEvery);
        s.timezone         = prefs.getString("timezone", s.timezone);
        prefs.end();
        Serial.println("[Settings] Loaded from NVS");
    }
    else
    {
        Serial.println("[Settings] First boot — structure defaults kept");
    }
}

void saveSettings(const Settings& s)
{
    Preferences prefs;
    if (prefs.begin(SETTINGS_NS, false)) // Ouverture en écriture
    {
        prefs.putBool("audio",      s.audioAlerts);
        prefs.putInt("interval",    s.updateInterval);
        prefs.putInt("fullEvery",   s.fullRefreshEvery);
        prefs.putString("timezone", s.timezone);
        prefs.end();
        Serial.println("[Settings] ✓ Settings saved to NVS");
    }
    else
    {
        Serial.println("[Settings] ✗ Failed to open NVS for saving settings");
    }

    Serial.printf("[Settings] Saved: audio=%d interval=%d fullEvery=%d\n", s.audioAlerts, s.updateInterval, s.fullRefreshEvery);
}

//------------------------------------------------------------------------------------------------

File loadFileMP3(const char* path)
{
#ifdef SDCARD
    File f = SD_MMC.open(path, FILE_READ);
    if (!f)
    {
        Serial.printf("[FILE] MP3 not found on SD: %s\n", path);
        return File();
    }
#else
    File f = LittleFS.open(path, "r");
    if (!f)
    {
        Serial.printf("[FILE] MP3 not found in LittleFS: %s\n", path);
        return File();
    }
#endif

    return f;
}
