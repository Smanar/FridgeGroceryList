// ============================================================================
// Shopping List — main firmware
//
//
// Factory reset: hold BOOT during splash screen to clear NVS.
// ============================================================================


#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <time.h>
#include <driver/rtc_io.h>

#include "hardware.h"
#include "display_ui.h"
#include "battery.h"
#include "storage.h"
#include "audio.h"
#include "itemlist.h"
#include "stt.h"
#include "algorithm.h"
#include "simplenote.h"
#include "input.h"
#include "hardware.h"


// ============================================================================
// Application state
// ============================================================================
enum AppState
{
    STATE_BOOT,
    STATE_SETUP_BLE,
    STATE_CONNECTING_WIFI,
    STATE_RUNNING,
    STATE_ERROR
};

static AppState            g_state              = STATE_BOOT;


// ============================================================================
// other stuff
// ============================================================================

// ---- setting ----
static Settings         g_settings;

// ---- Power management ----
static bool                g_serialDisabled     = false;

// ---- Deep sleep state (RTC memory — survives deep sleep) ----
RTC_DATA_ATTR static bool    rtc_deepSleepActive = false;
RTC_DATA_ATTR static unsigned long rtc_stableSince = 0; // working time

//Never use deepsleep and poweroff in same time
bool FORCE_SLEEP = false;                              // Used for test because on USB mode there is no sleep
static unsigned long POWER_OFF_DELAY =   5*60*1000;    // 5mn
static unsigned long DEEP_SLEEP_DELAY =  0*60*1000;    // disabled
static unsigned long LIGHT_SLEEP_DELAY = 1*60*1000;    // 1 mn
static unsigned long WARN_COOLDOWN_MS =  5*60*1000;    // 5 min
//#define DEEP_SLEEP_DISPLAY // Disabled for the moment, don't work, break the display.


// ---- Feature flags -----------------------------------------------------
static bool g_need_refresh = false;

// Others
static int selPosList;
static int itemsDisplayedList;
bool FullPower(void);
void LowerPower(bool forced = false);



// ============================================================================
// Forward declarations
// ============================================================================
void initializeHardware();
bool connectWiFi(unsigned long timeoutMs = 15000);
void PowerOff(bool displayscreen = true);
void displayMainPage(bool forced = false);
void handleMenu();
void handleSettings();

void checkBattery();
void enterDeepSleep(int intervalSec);
void DoMinimalWorkafterDS(void);
void enterLightSleep(unsigned long nextPoll);
void syncNTP();

void STTAction(void);



// ============================================================================
// Generic menu loop — used by handleMenu(), handleSettings() and
// handleLightAction(). BOOT cycles the selection, PWR invokes onSelect().
// onSelect returns false to exit the menu, true to keep looping.
// ============================================================================
template <typename Redraw, typename OnSelect>
static void runMenu(int itemCount, Redraw redraw, OnSelect onSelect)
{
    int selected = 0;
    redraw(selected, false);  // first draw: full refresh

    while (true) {
        ButtonEvent evt = pollButtons(/*holdMs=*/NO_HOLD_THRESHOLD, /*comboMs=*/NO_HOLD_THRESHOLD);

        if (evt == EVT_BOOT_CLICK)
        {
            selected = (selected + 1) % itemCount;
            redraw(selected, true);  // partial refresh
        }
        else if (evt == EVT_PWR_CLICK)
        {
            if (!onSelect(selected)) return;
            redraw(selected, true);
        }

        delay(50);
    }
}


// ============================================================================
// setup()
// ============================================================================
void setup()
{

    // Release ALL GPIO holds from deep sleep so pins can be driven again
    gpio_hold_dis((gpio_num_t)VBAT_PWR_PIN);
    gpio_deep_sleep_hold_dis();

    // === SECURING THE BATTERY POWER SUPPLY (CRITICAL, NEED TO BE FIRST) ===
    // Force the pin to a HIGH state immediately to prevent the board from shutting down.
    pinMode(VBAT_PWR_PIN, OUTPUT);
    digitalWrite(VBAT_PWR_PIN, HIGH);

    // === SCREEN TRANSISTOR MANAGEMENT ===
    // Release the latch on the display transistor.
    gpio_hold_dis((gpio_num_t)EPD_PWR_PIN);
    
    // Disabled because all this stuff is done in initializeHardware()
    //pinMode(EPD_PWR_PIN, OUTPUT);
    //digitalWrite(EPD_PWR_PIN, LOW); // Reconnect the 3.3V to the screen.
    //delay(50); // Allow time for the screen's power supply to stabilize.


#ifdef DEEP_SLEEP_DISPLAY

    // ===  RELEASING THE DISPLAY SPI BUS ===
    // Release CS (removes hold and RTC mode)
    //rtc_gpio_hold_dis((gpio_num_t)EPD_CS_PIN);
    //rtc_gpio_deinit((gpio_num_t)EPD_CS_PIN);

    // Exiting isolation mode for MOSI and SCK
    rtc_gpio_deinit((gpio_num_t)EPD_MOSI_PIN);
    rtc_gpio_deinit((gpio_num_t)EPD_SCK_PIN);

    //gpio_reset_pin((gpio_num_t)EPD_CS_PIN);
    gpio_reset_pin((gpio_num_t)EPD_MOSI_PIN);
    gpio_reset_pin((gpio_num_t)EPD_SCK_PIN);
#endif

#if 0
    // === SCREEN HARDWARE RESET ===
    // Recommended, as the screen has just regained its 3.3V power supply
    pinMode(EPD_RST_PIN, OUTPUT);
    digitalWrite(EPD_RST_PIN, LOW);
    delay(20);
    digitalWrite(EPD_RST_PIN, HIGH);
    delay(20);
#endif

    Serial.begin(115200);
    delay(50); // use 50 instead of 1000, even we can skip somme output

    Serial.printf("\n=== Shopping List v%s ===\n\n", FW_VERSION);

    // Log reset reason for diagnostics
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reasonStr = "UNKNOWN";

    switch (reason)
    {
        case ESP_RST_POWERON:  reasonStr = "POWER_ON";    break;
        case ESP_RST_SW:       reasonStr = "SOFTWARE";    break;
        case ESP_RST_PANIC:    reasonStr = "PANIC/CRASH"; break;
        case ESP_RST_INT_WDT:  reasonStr = "INT_WDT";     break;
        case ESP_RST_TASK_WDT: reasonStr = "TASK_WDT";    break;
        case ESP_RST_WDT:      reasonStr = "OTHER_WDT";   break;
        case ESP_RST_DEEPSLEEP:reasonStr = "DEEP_SLEEP";  break;
        case ESP_RST_BROWNOUT: reasonStr = "BROWNOUT";    break;
        default: break;
    }
    Serial.printf("[Main] Reset reason: %s (%d)\n", reasonStr, (int)reason);

    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    pinMode(PWR_BUTTON,  INPUT_PULLUP);

    rtc_stableSince = millis();

    // Return from DeepSlepp ?
    if (reason == ESP_RST_DEEPSLEEP && rtc_deepSleepActive)
    {

        rtc_deepSleepActive = false;

        esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();

        if (wakeup == ESP_SLEEP_WAKEUP_TIMER)
        {
            // Periodic wake up do we need to do something ?
            Serial.println("[DeepSleep] Timer wake — Minimal stuff");

            //If USB is used, normal boot
            if (!batteryOnUSB(batteryReadVoltage()) || FORCE_SLEEP)
            {

                DoMinimalWorkafterDS();

                Serial.println("[DeepSleep] return back to deep sleep");
            
                //Deep sleep enabled for 24h
                enterDeepSleep(24*60 *60);
        
                return; // Useless because previous fonction never return

            }

        }
        else
        {
            // Button wake from deep sleep — full boot
            Serial.println("[DeepSleep] Button wake — full boot");
            // Fall through to normalBoot
        }
    }

    // Normal boot

    g_state = STATE_BOOT;

    initializeHardware();

    //Turn Green LED on
    GreenLEDState(true);

    InitStorage();
    loadSettings(g_settings); // Use NVS

    // Audio init (ES8311)
    audioInit();

    //It's a wake up after Deepsleep
    bool fastboot = (reason == ESP_RST_DEEPSLEEP);
    //It's a wake up after a RTC power off
    if (ReadRTCMemory()) fastboot = true;

    //Wait for buttons release if the user are pressing them to power the device
    waitForRelease(BOOT_BUTTON);
    waitForRelease(PWR_BUTTON);

    // --- Splash gate: wait for BOOT press (short = continue, hold 3s = reset)
    if (!fastboot)
    {
        Serial.println("[Audio] Playing startup test tone...");
        MakeSound(SOUND_BEEP);
        Serial.println("[Audio] Test tone complete");

        drawSplashScreen();

        Serial.println("[Main] Splash — press BOOT to continue, hold 3s for reset");

        while (true)
        {
            ButtonEvent evt = pollButtons(/*holdMs=*/3000, /*comboMs=*/NO_HOLD_THRESHOLD);

            if (evt == EVT_BOOT_HELD)
            {
                Serial.println("[Main] BOOT held 3s — factory reset");
                drawErrorScreen("Factory Reset", "Clearing all data...");
                clearStoredCredentials();
                delay(2000);
                ESP.restart();
            }

            if (evt == EVT_BOOT_CLICK)
            {
                // Short press — continue boot
                Serial.println("[Main] BOOT pressed — continuing");
                MakeSound(SOUND_BEEP);
                //audioAttention(1);
                break;
            }

            delay(50);
        }
    }
    else
    {
        Serial.println("[Main] Use fast boot");
    }

    // Initialize NVS first
    initializeNVS();

#if 0
    // --- Credential check ---
    if (!hasStoredCredentials())
    {
        Serial.println("[Main] No credentials — Setup Mode");

        // BLE always initialised ?
        initializeBLE();

        g_state = STATE_SETUP_BLE;
        startBLEAdvertising();
        drawSetupScreen();

        return;
    }
#endif

    // BLE no longer needed — free ~60 KB of RAM
    //deinitBLE(); // Useless because we have just restarted

    loadCredentialsFromNVS();

    Serial.printf("[Main] SSID: %s  SN ID: %s\n", g_ssid.c_str(), g_SN_id.c_str());

    if (!FullPower()) // Set full power at start and enable wifi
    {
        return; // Stop here and display error.
    }

#if 0
    //no need this part as this project don't need time

    // Sync timezone according to settings
    if (g_timezone.length() > 0) g_settings.timezone = g_timezone;
    //TODO : remove this patch
    if (g_settings.timezone.length() == 0) g_settings.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    // --- NTP time sync ---
    syncNTP();
#endif

    //Retrieve Data
    Serial.println("[Main] Retrieving List");
    initItemList(); 
    if (!simpleNoteInit())
    {
        g_state = STATE_ERROR;
        drawErrorScreen("Simple Note","Simple Note Auth failed");
        return; // Stop here and display error.
    }

#if 0
    //Print list if there is no problem and if it's the first launch
    g_need_refresh = true; // The next call to displayMainPage() will be a full refresh
    if (!fastboot)
    {
        selPosList = 0;
        displayMainPage(true);
    }
    else
    {
        selPosList = 0;
        g_state = STATE_RUNNING;   // we don't update the screen but still going to running state.
    }
#else
    selPosList = 0;
    displayMainPage(true);
#endif

    // To debug
    //listLittleFS("/audio");

    //Turn Green LED off
    GreenLEDState(false);

    LowerPower(); // Lower power usage

    Serial.println("[Main] Device Ready");

}

// ============================================================================
// loop()
// ============================================================================
void loop()
{

    // Audio stuff
    audioloop();

    bool onUSB = batteryOnUSB(batteryReadVoltage());

    // Check battery status every 10s
    static unsigned long lastLedCheck = 0;
    if (millis() - lastLedCheck >= 10000)
    {
        lastLedCheck = millis();

        checkBattery();
    }

    switch (g_state)
    {
        // --- Connecting wifi ----------------------------
        case STATE_CONNECTING_WIFI:
            break;

        // ---- BLE setup: just wait for callbacks ----------------------------
        case STATE_SETUP_BLE:
#if 0
            if (bleSaveRebootPending())
            {
                Serial.println("  → Storing credentials to NVS...");
                saveCredentialsToNVS();
        
                Serial.println("  → Credentials saved. Rebooting in 2s...");
                delay(2000);
                Serial.println("  → Rebooting now!");
                Serial.flush();
                ESP.restart();
            }
#endif
            break;

        // ---- Normal operation --------------------------------
        case STATE_RUNNING:
        {

            // --- Charging = WOT: full speed, serial on, no sleep ---
            if (onUSB)
            {
                if (g_serialDisabled)
                {
                    Serial.begin(115200);
                    g_serialDisabled = false;
                    Serial.println("[Power] USB — full-power mode");
                }
            }
            else
            {
                // On battery: disable Serial to save ~2-3 mA (one-shot)
                if (!g_serialDisabled)
                {
                    Serial.println("[Power] Battery — disabling Serial");
                    Serial.flush();
                    Serial.end();
                    g_serialDisabled = true;
                }
            }

            //Button management
            ButtonEvent evt = pollButtons(/*holdMs=*/3000, /*comboMs=*/2000);

            if (evt > EVT_NONE) rtc_stableSince = millis();

            switch (evt)
            {
                case EVT_COMBO_HELD:

                    Serial.println("[Main] BOOT+PWR long — Menu");
                    if (g_settings.audioAlerts) MakeSound(SOUND_CLICK);

                    // Menu
                    handleMenu();

                    //Return to main page
                    displayMainPage(true);

                    break;
        
                case EVT_BOOT_CLICK:
                    Serial.println("[Main] BOOT short — STT");

                    STTAction();

                    break;
        
                case EVT_PWR_HELD:
                    Serial.println("[Main] PWR long — Power off");
                    PowerOff();

                    break;
        
                case EVT_PWR_CLICK:
                    Serial.println("[Main] PWR short — Scroll");

                    if (g_settings.audioAlerts) MakeSound(SOUND_CLICK);

                    selPosList += itemsDisplayedList;
                    if (selPosList >= GetTotalItem()) selPosList = 0;
                    displayMainPage();

                    break;
        
                default:
                    break;
            }

            // --- Power management ---
            if (onUSB && !FORCE_SLEEP)
            {
                //Nothing to do, full power
            }
            else if (POWER_OFF_DELAY && (millis() - rtc_stableSince >= POWER_OFF_DELAY))
            {
                // Stable long enough — power off
                Serial.printf("[Power] %d s working time — power off\n", rtc_stableSince / 1000);

                // Use RTC for programmed wake up ?
                //RTC_shutdown();

                PowerOff(false);

            }
            else if (DEEP_SLEEP_DELAY && (millis() - rtc_stableSince >= DEEP_SLEEP_DELAY))
            {
                // Stable long enough — enter deep sleep
                Serial.printf("[Power] %d s working time — deep sleep\n", rtc_stableSince / 1000);

                //Deep sleep enabled for 24h
                enterDeepSleep(24*60*60);
                // Never returns
            }
            else if (LIGHT_SLEEP_DELAY && (millis() - rtc_stableSince >= LIGHT_SLEEP_DELAY))
            {
                // Stable long enough — enter light sleep
                Serial.printf("[Power] %d s working time — light sleep\n", rtc_stableSince / 1000);

                enterLightSleep(millis() + ((DEEP_SLEEP_DELAY > 0) ? DEEP_SLEEP_DELAY : POWER_OFF_DELAY)); // Light sleep for Deep sleep delay

            }

            break;
        }

        // ---- Error: hold BOOT 3 s to restart --------------------------------
        case STATE_ERROR:
        {
            ButtonEvent evt = pollButtons(/*holdMs=*/3000, /*comboMs=*/NO_HOLD_THRESHOLD);
            if (evt == EVT_BOOT_HELD) ESP.restart();
            break;
        }

        default:
            break;
    }

    delay(100);
    //vTaskDelay(pdMS_TO_TICKS(100)); // delay(100) is better here

}


// ============================================================================
// WiFi
// ============================================================================
bool connectWiFi(unsigned long timeoutMs)
{

    Serial.printf("[WiFi] Connecting to %s", g_ssid.c_str());

    // Does not rewrite configurations to Flash memory at every startup (saves cycles and time).
    WiFi.persistent(false);

    // Station mode
    WiFi.mode(WIFI_STA);

#if 0
    // STATIC IP OPTIMIZATION (Optional but highly recommended)
    // Avoids waiting for DHCP negotiation, which often takes 2 to 4 seconds. 
    // Configure these addresses according to your home network:

    IPAddress local_IP(192, 168, 1, 69);  // Set your IP
    IPAddress gateway(192, 168, 1, 254);  // Gateway IP
    IPAddress subnet(255, 255, 255, 0);
    IPAddress dns(1, 1, 1, 1);            // DNS Cloudflare
    if (!WiFi.config(local_IP, gateway, subnet))
    {
        Serial.println("[Wi-Fi] Static IP configuration failed");
    }
#endif

    // Connect
    WiFi.begin(g_ssid.c_str(), g_password.c_str());

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("[WiFi] ✓ IP %s\n", WiFi.localIP().toString().c_str());

        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // modem sleep between polls
        Serial.println("[WiFi] Modem sleep enabled");

        return true;
    }
    
    Serial.println("[WiFi] ✗ Failed");
    return false;
}

// ============================================================================
// display Main page
// ============================================================================
void displayMainPage(bool forced)
{

    if (g_need_refresh)
    {
        g_need_refresh = false;
        forced = true;
    }

    if (forced)
    {
        Serial.println("[Main] Main page [FULL]");
    }
    else
    {
        Serial.println("[Main] Main page [UPDATE]");
    }

    itemsDisplayedList = drawListScreen(selPosList, forced);

    g_state = STATE_RUNNING;
}

// ============================================================================
// NTP
// ============================================================================
void syncNTP()
{
    if (g_settings.timezone.length() == 0)
    {
        Serial.println("[NTP] No timezone set");
        return;
    }
    Serial.printf("[NTP] Syncing with TZ: %s\n", g_settings.timezone.c_str());

    configTzTime(g_settings.timezone.c_str(), "pool.ntp.org", "time.nist.gov");

    struct tm t;
    if (getLocalTime(&t, 5000))
    {
        Serial.printf("[NTP] Time: %04d-%02d-%02d %02d:%02d:%02d (wday=%d)\n", t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, t.tm_wday);
    }
    else
    {
        Serial.println("[NTP] Failed to get time");
    }
}

// ============================================================================
// Battery check — warn at low %, auto-shutdown at critical %
// ============================================================================
void checkBattery()
{

    switch (GetBatteryState())
    {
    case STATE_USB:
        break;
    case STATE_LOW:
        {

            Serial.printf("[Power] Low battery\n");

            // Make audio warning
            static unsigned long lastWarnBeep = 0;

            if (millis() - lastWarnBeep < WARN_COOLDOWN_MS) return;
            lastWarnBeep = millis();

            MakeSound(SOUND_ATTENTION, 1);

            break;
        }
    case STATE_CRITICAL:
        {
            Serial.printf("[Power] CRITICAL — shutdown\n");

            //Make graphical warning
            initializeHardware();
            drawLowBatteryScreen(batteryPercent(batteryReadVoltage()), true);

            delay(3000);
            PowerOff(false);

            break;
        }
    default:
        break;
    }

}

// ============================================================================
// Power off — graceful shutdown, release power latch
// ============================================================================
void PowerOff(bool displayscreen)
{
    Serial.println("[Main] Powering off...");

    // Graceful cleanup — turn off peripherals before power cut
    GreenLEDState(false);
    audioShutdown();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    if (displayscreen)
    {
        drawShutdownScreen();
        delay(1000);  // let user see the screen
    }
    else
    {
        //drawSleepIndicator(SLEEP_OFF);

        // Writing the marker to the RTC RAM
        writeRtcStorage(0xAA);
    }

    // Wait for button release
    waitForRelease(PWR_BUTTON);

    //Close storage stuff
    EndStorage();

    // Release GPIO holds from deep sleep before driving pin LOW
    gpio_hold_dis((gpio_num_t)VBAT_PWR_PIN);
    gpio_deep_sleep_hold_dis();

    // Release power latch — device will lose power
    digitalWrite(VBAT_PWR_PIN, LOW);

    // Hardware shutdown (The MOSFET opens, the power supply cuts off)
    Serial.println("[Main] Hardware shutdown imminent (Power consumption -> ~15µA).");
    Serial.flush();
    digitalWrite(VBAT_PWR_PIN, LOW);
 
    if (batteryOnUSB(batteryReadVoltage()))
    {
        // If still running (USB powered), enter deep sleep with no wake sources
        delay(100);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_deep_sleep_start();
    }
    else
    {
        // Boucle infinie pendant la décharge des condensateurs
        while (1)
        {
            delay(1000);
        }
    }
 
     // Never reached, previous code is locking

}

// ============================================================================
// enterDeepSleep — hold power latch, set wake sources, sleep
//
// Check https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/sleep_modes.html
// ============================================================================
void enterDeepSleep(int intervalSec)
{

    Serial.printf("[DeepSleep] Sleeping %d s\n", intervalSec);
    Serial.flush();

    //drawSleepIndicator(SLEEP_DEEP);

    LowerPower(true);

    rtc_deepSleepActive = true;

    // Turn off LED before sleeping
    GreenLEDState(false);

    // Disable totaly audio to save power
    audioShutdown();

    // Turn off screen power
    display.powerOff();

    //Close storage stuff
    EndStorage();
    
    //turn off display transistor
    digitalWrite(EPD_PWR_PIN, HIGH);
    gpio_hold_en((gpio_num_t)EPD_PWR_PIN);

    // Turn off all radio stuff
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    btStop();

    // Turn off RTC stuff
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
    // Fast RTC memory clearing (not required for standard RTC_DATA_ATTR variables)
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);

    //shtc3.sleep(true); // Forces the SHTC3 into ultra-low power mode (0.5 µA)

    // Hold power latch HIGH during deep sleep
    gpio_hold_en((gpio_num_t)VBAT_PWR_PIN);

    // APPLIQUER LE VERROUILLAGE GÉNÉRAL DES GPIO JUSTE AVANT DE DORMIR
    gpio_deep_sleep_hold_en();


#ifdef DEEP_SLEEP_DISPLAY
    // 2. SÉCURISATION DU BUS SPI DE L'ÉCRAN
    // Initialize the RTC mode on the CS pin.
    //rtc_gpio_init((gpio_num_t)EPD_CS_PIN);
    //rtc_gpio_set_direction((gpio_num_t)EPD_CS_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    // Force CS to 1 (HIGH) so that the screen ignores spurious noise.
    //rtc_gpio_set_level((gpio_num_t)EPD_CS_PIN, 1);
    // This HIGH state is latched for the entire duration of Deep Sleep.
    //rtc_gpio_hold_en((gpio_num_t)EPD_CS_PIN);

    // These lines are completely cut to eliminate current leakage.
    rtc_gpio_isolate((gpio_num_t)EPD_MOSI_PIN);
    rtc_gpio_isolate((gpio_num_t)EPD_SCK_PIN);
#endif

    // Wake on either button (ext1 — ANY_LOW)
    uint64_t buttonMask = (1ULL << BOOT_BUTTON) | (1ULL << PWR_BUTTON);
    esp_sleep_enable_ext1_wakeup(buttonMask, ESP_EXT1_WAKEUP_ANY_LOW);

    // Wake on timer
    esp_sleep_enable_timer_wakeup((uint64_t)intervalSec * 1000000ULL);

    esp_deep_sleep_start();
    // Never returns
}



// ============================================================================
// DoMinimalWorkafterDS
// ============================================================================
void DoMinimalWorkafterDS(void)
{
    // --- Battery check first (may shutdown before spending power) ---
    batteryInit();
    checkBattery();

    return;

}


// ============================================================================
// Reset screen (opened from the main menu)
// ============================================================================
static void showReBootScreen()
{
    drawReBootScreen();

    // BOOT = close, PWR = reboot
    while (true) {
        ButtonEvent evt = pollButtons(/*holdMs=*/NO_HOLD_THRESHOLD, /*comboMs=*/NO_HOLD_THRESHOLD);

        if (evt == EVT_BOOT_CLICK) return;

        if (evt == EVT_PWR_CLICK)
        {
            Serial.println("[Menu] Rebooting...");
            ESP.restart();
        }

        delay(50);
    }
}


// ============================================================================
// Device info (opened from the main menu)
// ============================================================================
static void showDeviceInfoScreen()
{
    float bv = batteryReadVoltage();
    int bp = batteryPercent(bv);
    String ip = WiFi.localIP().toString();
    drawDeviceInfoScreen(g_ssid.c_str(), ip.c_str(), g_SN_id.c_str(), g_SN_pass.c_str(), bv, bp, true);

    // BOOT = close, PWR = reboot
    while (true)
    {
        ButtonEvent evt = pollButtons(/*holdMs=*/NO_HOLD_THRESHOLD, /*comboMs=*/NO_HOLD_THRESHOLD);

        if (evt == EVT_BOOT_CLICK) return;
        if (evt == EVT_PWR_CLICK) return;

        delay(50);
    }
}



// ============================================================================
// Menu handler — blocking loop while user navigates
//   BOOT (⚙) = next item
//   PWR       = select / toggle / enter
// ============================================================================
void handleMenu()
{
    Serial.println("[Menu] Entering menu");
    // Wait for PWR release from the press that opened the menu
    waitForRelease(PWR_BUTTON);

    runMenu(
        MENU_COUNT,
        [](int sel, bool partial) { drawMenuScreen(sel, partial); },
        [](int sel) -> bool {
            switch (sel) {
            case MENU_DEVICE_INFO:
                showDeviceInfoScreen();
                return true;

            case MENU_RESTART:
                showReBootScreen();
                return true;

            case MENU_SETTINGS:
                handleSettings();
                return true;

            case MENU_REFRESH:
                Serial.println("[Menu] Refresh selected — exiting menu");

                if (!FullPower()) // Set full power at start and enable wifi
                {
                    return false; // Stop here and display error.
                }

                simpleNoteInit();
                displayMainPage(true);
                LowerPower(); // Lower power usage

                return false;

            case MENU_EXIT:
                Serial.println("[Menu] Exit");
                return false;
            }
            return true;
        });
}

// ============================================================================
// handleSettings() — Settings submenu
// ============================================================================
void handleSettings()
{
    Serial.println("[Settings] Entering settings submenu");

    static const int UPDATE_INTERVALS[] = {6, 12, 24, 48};
    constexpr int UPDATE_INTERVAL_COUNT = sizeof(UPDATE_INTERVALS) / sizeof(UPDATE_INTERVALS[0]);

    runMenu(
        SET_COUNT,
        [](int sel, bool partial) { drawSettingsScreen(sel, g_settings, partial); },
        [](int sel) -> bool {
            switch (sel)
            {
            case SET_MENU_1:
                Serial.println("[Settings] Menu 1");
                delay(200);
                break;

            case SET_AUDIO:
                g_settings.audioAlerts = !g_settings.audioAlerts;
                saveSettings(g_settings);
                if (g_settings.audioAlerts) MakeSound(SOUND_BEEP);  // confirm audio is now ON
                break;

            case SET_UPDATE_INTERVAL: {
                int idx = 0;
                for (int i = 0; i < UPDATE_INTERVAL_COUNT; i++)
                {
                    if (g_settings.updateInterval == UPDATE_INTERVALS[i]) { idx = i; break; }
                }
                g_settings.updateInterval = UPDATE_INTERVALS[(idx + 1) % UPDATE_INTERVAL_COUNT];
                saveSettings(g_settings);
                Serial.printf("[Settings] Update interval → %ds\n", g_settings.updateInterval);
                break;
            }
#if 0
            case SET_BLE_SETUP:
                Serial.println("[Settings] Starting BLE setup");
                initializeBLE();           // re-init after deinitBLE()
                loadCredentialsFromNVS();  // populate globals for onRead
                drawSetupScreen();
                startBLEAdvertising();
                waitForAnyButton();
                stopBLEAdvertising();
                deinitBLE();               // free RAM again
                loadSettings(g_settings);
                break;
#endif
            case SET_BACK:
                Serial.println("[Settings] Back to main menu");
                return false;

            default:
                return false;
            }
            return true;
        });
}


// ============================================================================
// ===    Light sleep   ===
// ============================================================================

void enterLightSleep(unsigned long nextPoll)
{
    unsigned long now = millis();

    if (nextPoll > now + 1000)
    {
        // print an icon
        //drawSleepIndicator(SLEEP_LIGHT);

        unsigned long sleepMs = nextPoll - now - 500;

        LowerPower(true);

        // Configure GPIO wake for button presses
        gpio_wakeup_enable((gpio_num_t)BOOT_BUTTON, GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable((gpio_num_t)PWR_BUTTON,  GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        esp_sleep_enable_timer_wakeup((uint64_t)sleepMs * 1000ULL);

        Serial.printf("[Power] Light sleep %lu ms\n", sleepMs);
        Serial.flush();
        esp_light_sleep_start();

        // --- Woke up ---

//Disabled because not working
#if 0
        // Reconfigure Serial, useless, but usefull in debug mode (with FORCE_SLEEP enabled)
        if (FORCE_SLEEP)
        {
            Serial.end();
            delay(10);
            Serial.begin(115200);
            delay(10);
        }
#endif
        //drawSleepIndicator(SLEEP_NONE);
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        Serial.printf("[Power] Woke: %s\n", cause == ESP_SLEEP_WAKEUP_GPIO ? "button" : "timer");

        if (cause == ESP_SLEEP_WAKEUP_GPIO)
        {
            //Restart timer else we just return back immediatly in light sleep
            rtc_stableSince = millis();
        }

        delay(50);  // debounce
    }
    else
    {
        delay(100);
    }
}

// ============================================================================
// ===    STT stuff   ===
// ============================================================================

void STTAction(void)
{

    FullPower();

    Serial.println("[Main] Recording");

#if 0
    //Only for tests
    audioMicFlush();  
    audioRecordAndPlayback(3000);
#else
    sendAudioToWit();

    //Update page
    displayMainPage(true);

#endif

    LowerPower();

}


// ============================================================================
// ===    Full power   ===
// ============================================================================
bool FullPower(void)
{
    // WiFi connect (need 240 MHz for radio)
    setCpuFrequencyMhz(240);

    if (!connectWiFi(5000))
    {
        g_state = STATE_ERROR;
        drawErrorScreen("WiFi Failed", "Check SSID / password");
        return false;
    }
    else
    {
        Serial.println("[Main] Connected to wifi");
    }

    //Enable audio
    //audioResume(); // Disabled, activated during sound generation

    return true;
}

// ============================================================================
// ===    Low  power   ===
// ============================================================================
void LowerPower(bool forced)
{

    bool onUSB = batteryOnUSB(batteryReadVoltage());

    //Only if we are not un USB or forced
    if (onUSB && !forced) return;

    // Turn off WiFi
    if (WiFi.getMode() != WIFI_OFF)
    {
        WiFi.disconnect(false); // Faux = ne pas effacer les identifiants stockés
        WiFi.mode(WIFI_OFF);

        // Forces a complete shutdown of the radio physical layer to maximize energy savings.
        esp_wifi_stop(); 
    }

    // if forced  = true, it mean it's light sleep or deep sleep
    // on Deep sleep, this line is useless
    // on light sleep, this line need to be avoided, it consomme more power
    if (!forced)
    {
        // Drop to 80 MHz on battery for idle power savings
        if (getCpuFrequencyMhz() != 80) 
        {
            setCpuFrequencyMhz(80);
        }
    }

    //Turn off audio
    audioSuspend();
    
}
