// ============================================================================
// Display UI
// ============================================================================

#include <Arduino.h>

#include "display_ui.h"
#include "battery.h"
#include "hardware.h"
#include "itemlist.h"
#include "algorithm.h"

// Adafruit-GFX FreeFont headers (bundled with GxEPD2's dependency)
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// URL to the Web Bluetooth setup page.
extern const char* SETUP_URL;

extern String sMainTitle;

//---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Center a string horizontally at the given baseline-y
static void centerText(const char* text, int y) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((200 - w) / 2 - x1, y);
    display.print(text);
}

// Wrap text
static int wrapText(const char* text, int maxWidth, String outLines[], int maxLines)
{
    String word, line;
    int nLines = 0;
    const char* p = text;

    auto flushLine = [&]() {
        if (nLines < maxLines) outLines[nLines++] = line;
        line = "";
    };

    while (*p && nLines < maxLines)
    {
        word = "";
        while (*p && *p != ' ') word += *p++;
        if (*p == ' ') p++;

        String candidate = line.length() ? (line + " " + word) : word;

        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(candidate.c_str(), 0, 0, &x1, &y1, &w, &h);

        if (w > (uint16_t)maxWidth)
        {
            if (line.length() > 0)
            {

                flushLine();
                line = word;
            }
            else
            {

                line = word;
                flushLine();
            }
        }
        else
        {
            line = candidate;
        }
    }
    if (line.length() && nLines < maxLines) flushLine();

    return nLines;
}

// ---------------------------------------------------------------------------
// Battery Icon
// ---------------------------------------------------------------------------

static void drawBatteryIcon(int barX, int barY, int barW, int barH)
{
    float voltage = batteryReadVoltage();
    int   pct     = batteryPercent(voltage);
    bool  usb     = batteryOnUSB(voltage);

    char battStr[24];
    if (usb) {
        snprintf(battStr, sizeof(battStr), "USB  %d%%", pct);
    } else {
        snprintf(battStr, sizeof(battStr), "%.2fV  %d%%", voltage, pct);
    }

    // Battery bar body
    const int tipW = 4, tipH = 8;
    display.drawRect(barX, barY, barW, barH, GxEPD_BLACK); 
    display.drawRect(barX + 1, barY + 1, barW - 2, barH - 2, GxEPD_BLACK); 
    display.fillRect(barX + barW, barY + (barH - tipH) / 2, tipW, tipH, GxEPD_BLACK);  // Tip

    // Fill level
    int fillW = ((barW - 4) * pct) / 100;
    if (fillW > 0) {
        display.fillRect(barX + 2, barY + 2, fillW, barH - 4, GxEPD_BLACK);
    }

    // Battery text to the right of the bar
    display.setFont(NULL);
    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(barX + barW + tipW + 6, barY + 4);
    display.print(battStr);
}

// ---------------------------------------------------------------------------
// Gear icon — represents the BOOT button (has ⚙ symbol on PCB)
// ---------------------------------------------------------------------------
static void drawGearIcon(int cx, int cy, int r, uint16_t fg, uint16_t bg) {
    if (r < 3) return; // Safety check for e-Paper resolution

    const int numTeeth = 6;
    const int toothLength = max(2, r / 3);
    const int outerR = r + toothLength;

    // 1. Draw teeth as small anchor triangles pointing outwards
    for (int i = 0; i < numTeeth; i++) {
        float angle = i * PI / 3.0f;
        
        int tx = cx + (int)(cos(angle) * outerR);
        int ty = cy + (int)(sin(angle) * outerR);

        float angleBase1 = angle - 0.2f;
        float angleBase2 = angle + 0.2f;

        int bx1 = cx + (int)(cos(angleBase1) * (r - 1));
        int by1 = cy + (int)(sin(angleBase1) * (r - 1));
        int bx2 = cx + (int)(cos(angleBase2) * (r - 1));
        int by2 = cy + (int)(sin(angleBase2) * (r - 1));

        display.fillTriangle(tx, ty, bx1, by1, bx2, by2, fg);
    }

    // 2. Central wheel body
    display.fillCircle(cx, cy, r, fg);

    // 3. Center hole optimized for e-Paper contrast
    int holeR = r / 3;
    if (holeR < 1 && r >= 4) holeR = 1; // Force minimum 1px hole
    
    if (holeR > 0) {
        display.fillCircle(cx, cy, holeR, bg);
    } else {
        display.drawPixel(cx, cy, bg); // Single pixel center fallback
    }
}

// ---------------------------------------------------------------------------
// Power icon — represents the POWER button
// ---------------------------------------------------------------------------
static void drawPowerIcon(int cx, int cy, int r, uint16_t fg, uint16_t bg)
{
    const int barH = r;
    const int strokeW = max(2, r / 4);

    display.fillCircle(cx, cy, r, fg);
    display.fillCircle(cx, cy, r - strokeW, bg);

    const int openingW = max(3, (int)(r * 0.9f)); 
    display.fillRect(cx - openingW / 2, cy - r - 1, openingW, strokeW + 2, bg);
    display.fillRect(cx - strokeW / 2, cy - r, strokeW, barH, fg);
}

//---------------------------------------------------------------------------
// Footer
// ---------------------------------------------------------------------------
void drawFooter(const char* text1, const char* text2)
{
    display.setFont(NULL);
    display.setTextSize(2);
    display.setTextColor(GxEPD_BLACK);

    //display.drawLine(10, 172, 190, 172, GxEPD_BLACK);

    drawGearIcon(14, 189, 6, GxEPD_BLACK, GxEPD_WHITE);
    display.setCursor(28, 183);
    display.print(text1);

    drawPowerIcon(135, 189, 6, GxEPD_BLACK, GxEPD_WHITE);
    display.setCursor(150, 183);
    display.print(text2);
}


// ============================================================================
// Splash Screen
// ============================================================================
void drawSplashScreen(void)
{
    char language[16];
    snprintf(language, sizeof(language), "%s", LOCALE_LANGUAGE);

    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);

        // Double border frame
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);
        display.drawRect(2, 2, 196, 196, GxEPD_BLACK);

        // Main Title
        display.setFont(&FreeSansBold18pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("Shopping", 55);
        centerText("List", 88);
        display.drawRoundRect(13, 21, 174, 79, 10, GxEPD_BLACK);

        // Language string
        display.setFont(NULL);
        display.setTextSize(1);
        centerText(language, 120);

        // Battery level rendering
        drawBatteryIcon(60, 145, 40, 16);

        // Footer
        display.setFont(&FreeSansBold9pt7b);
        display.setTextSize(1);
        display.setTextColor(GxEPD_BLACK);
        drawGearIcon(15, 184, 8, GxEPD_BLACK, GxEPD_WHITE);
        display.setCursor(40, 188);
        display.print("Press to start");

    } while (display.nextPage());

    Serial.printf("[UI] Splash: %s\n", language);
}

// ============================================================================
// List Screen
// ============================================================================
int drawListScreen(int selected, bool forced)
{
    const int x0 = 1, y0 = 33, w = 198, h = 200 - 33 - 1;

    // PROTECTION : Init all by defaut
    display.setFont(NULL);
    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);

    display.setTextWrap(false);

    // Dynamic choice: Window strategy to bypass clean visual errors on e-Paper page loops
    if (forced)
    {
        display.setFullWindow();
    }
    else
    {
        display.setPartialWindow(x0, y0, w, h);
    }
    
    
    struct DisplayItem { String lines[3]; int nLines; };
    DisplayItem itemsToShow[MAX_ITEM_LIST];
    int itemCount = 0;
    
    {
        const int lineHeight   = 22;
        const int textX        = 15;
        const int maxTextWidth = w - (textX - x0) - 5;
        int y = 55;
        const int textMargin   = 8;

        display.setFont(&FreeSans9pt7b);

        for (int i = 0; i < MAX_ITEM_LIST; i++)
        {
            const char* t = GetItemsFromList(i + selected);
            if (t == nullptr || t[0] == '\0') continue;
        
            String lines[3];
            int nLines = wrapText(t, maxTextWidth, lines, 3);
        
            if (y + (nLines - 1) * lineHeight + textMargin > y0 + h) break;
        
            for (int l = 0; l < nLines; l++) itemsToShow[itemCount].lines[l] = lines[l];
            itemsToShow[itemCount].nLines = nLines;
            itemCount++;
        
            y += nLines * lineHeight;
        }
    }
    
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        
        // Fixed: If forced full layout requested, redraw the layout components inside the main loop
        if (forced) {
            display.drawRect(0, 0, 200, 200, GxEPD_BLACK);
            display.setFont(&FreeSansBold12pt7b);
            centerText(sMainTitle.c_str(), 25);
            display.drawLine(10, 32, 190, 32, GxEPD_BLACK);
        }

        display.setFont(&FreeSans9pt7b);
        int y = 55;
        const int lineHeight = 22;
        for (int i = 0; i < itemCount; i++)
        {
            display.setCursor(6, y);
            display.print("-");
            for (int l = 0; l < itemsToShow[i].nLines; l++)
            {
                display.setCursor(15, y);
                display.print(itemsToShow[i].lines[l]);
                y += lineHeight;
            }
        }
    } while (display.nextPage());

    Serial.printf("[UI] List page, selected=%d, number item=%d\n", selected, itemCount);

    return itemCount;
}

// ============================================================================
// Error Screen
// ============================================================================
void drawErrorScreen(const char* title, const char* detail)
{
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Warning triangle shape
        display.fillTriangle(100,20, 70,70, 130,70, GxEPD_BLACK);
        display.fillTriangle(100,30, 78,65, 122,65, GxEPD_WHITE);
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(93, 62);
        display.print("!");

        // Title and details
        display.setFont(&FreeSansBold12pt7b);
        centerText(title, 105);

        if (detail && strlen(detail) > 0) {
            display.setFont(&FreeSans9pt7b);
            centerText(detail, 135);
        }

        // Operational recovery hint
        display.setFont(NULL);
        display.setTextSize(1);
        centerText("Hold BOOT 3s to restart", 155);

        drawBatteryIcon(115, 175, 24, 12);

    } while (display.nextPage());

    Serial.printf("[UI] Error: %s — %s\n", title, detail ? detail : "");
}

// ============================================================================
// Shutdown Screen
// ============================================================================
void drawShutdownScreen()
{
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Power icon architecture
        const int cx = 100, cy = 65, cr = 25;
        display.drawCircle(cx, cy, cr, GxEPD_BLACK);
        display.drawCircle(cx, cy, cr - 1, GxEPD_BLACK);
        display.fillRect(cx - 2, cy - cr - 5, 5, 20, GxEPD_WHITE);
        display.fillRect(cx - 1, cy - cr - 3, 3, 18, GxEPD_BLACK);

        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("Powered Off", 125);

        display.setFont(&FreeSans9pt7b);
        centerText("Press PWR to start", 155);

    } while (display.nextPage());

    Serial.println("[UI] Shutdown screen drawn");
}

// ============================================================================
// Low Battery Warning Screen
// ============================================================================
void drawLowBatteryScreen(int percent, bool critical)
{
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Warning triangle
        display.fillTriangle(100, 20, 70, 70, 130, 70, GxEPD_BLACK);
        display.fillTriangle(100, 30, 78, 65, 122, 65, GxEPD_WHITE);
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(93, 62);
        display.print("!");

        display.setFont(&FreeSansBold12pt7b);
        if (critical)
        {
            centerText("SHUTDOWN", 105);
            display.setFont(&FreeSans9pt7b);
            centerText("Battery critical", 130);
        }
        else
        {
            centerText("LOW BATTERY", 105);
        }

        // Percentage display
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", percent);
        display.setFont(&FreeSansBold18pt7b);
        centerText(buf, critical ? 175 : 160);

    } while (display.nextPage());

    Serial.printf("[UI] Low battery: %d%% %s\n", percent, critical ? "CRITICAL" : "warning");
}

// ============================================================================
// Menu Screen
// ============================================================================
void drawMenuScreen(int selected, bool partial)
{
    const char* labels[MENU_COUNT];
    labels[MENU_RESTART]       = "Restart";
    labels[MENU_DEVICE_INFO] = "Device Info";
    labels[MENU_REFRESH]     = "Refresh Now";
    labels[MENU_SETTINGS]    = "Settings >";
    labels[MENU_EXIT]        = "< Exit";

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("MENU", 25);
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        // Standard menu loop architecture
        display.setFont(&FreeSansBold9pt7b);
        for (int i = 0; i < MENU_COUNT; i++) {
            int y = 55 + i * 22;
            if (i == selected) {
                display.fillRect(5, y - 13, 190, 18, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }
            display.setCursor(15, y);
            display.print(labels[i]);
        }

        // Core operational button hints
        drawFooter("Next", "Sel");

    } while (display.nextPage());

    Serial.printf("[UI] Menu drawn, selected=%d\n", selected);
}

// ============================================================================
// Settings Screen (submenu)
// ============================================================================
void drawSettingsScreen(int selected, const Settings& settings, bool partial)
{
    const char* labels[SET_COUNT];
    static char UpdateLabel[24];
    snprintf(UpdateLabel, sizeof(UpdateLabel), "Update: %ds", settings.updateInterval);

    labels[SET_MENU_1]        = "menu 1";
    labels[SET_AUDIO]         = settings.audioAlerts   ? "Audio: ON"   : "Audio: OFF";
    labels[SET_UPDATE_INTERVAL] = UpdateLabel;
    labels[SET_BLE_SETUP]     = "BLE Setup";
    labels[SET_BACK]          = "< Back";

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
        
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("SETTINGS", 25);
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        display.setFont(&FreeSansBold9pt7b);
        for (int i = 0; i < SET_COUNT; i++)
        {
            int y = 52 + i * 20;
            if (i == selected)
            {
                display.fillRect(5, y - 13, 190, 18, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            }
            else
            {
                display.setTextColor(GxEPD_BLACK);
            }
            display.setCursor(15, y);
            display.print(labels[i]);
        }

        drawFooter("Next", "Sel");

    } while (display.nextPage());

    Serial.printf("[UI] Settings drawn, selected=%d\n", selected);
}

// ============================================================================
// Reset Screen
// ============================================================================
void drawReBootScreen(void)
{
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("Reset Device", 25);
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        char verBuf[16];
        snprintf(verBuf, sizeof(verBuf), "v%s", FW_VERSION);
        display.setCursor(6, 100); display.printf("FW:%s", verBuf);

        //Footer
        drawFooter("Cancel", "Ok");

    } while (display.nextPage());

    Serial.println("[UI] Reset screen drawn");
}

// ============================================================================
// Device Info Screen
// ============================================================================
void drawDeviceInfoScreen(const char* ssid, const char* ip, const char* SNUser, const char* SNPass, float battV, int battPct, bool partial)
{
    char clientShort[20], tenantShort[20];
    snprintf(clientShort, sizeof(clientShort), "%.8s...", SNUser);
    snprintf(tenantShort, sizeof(tenantShort), "%.8s...", SNPass);

    char battBuf[16];
    snprintf(battBuf, sizeof(battBuf), "%.2fV  %d%%", battV, battPct);

    char timeBuf[20];
    struct tm t;
    if (getLocalTime(&t, 100))
    {
        snprintf(timeBuf, sizeof(timeBuf), "%02d/%02d %02d:%02d", t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    }
    else
    {
        snprintf(timeBuf, sizeof(timeBuf), "No sync");
    }

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
        
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("DEVICE INFO", 25);
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        display.setFont(NULL);
        display.setTextSize(2);
        display.setTextColor(GxEPD_BLACK);
        int y = 42;
        const int lineH = 19;

        display.setCursor(6, y); display.printf("SSID:%s", ssid);
        y += lineH;
        display.setCursor(6, y); display.printf("IP:%s", ip);
        y += lineH;
        display.setCursor(6, y); display.printf("SN:%s", clientShort);
        y += lineH;
        display.setCursor(6, y); display.printf("USB: %s", batteryOnUSB(batteryReadVoltage()) ? "true" : "false");
        y += lineH;
        display.setCursor(6, y); display.printf("Batt:%s", battBuf);
        y += lineH;

        char verBuf[16];
        snprintf(verBuf, sizeof(verBuf), "v%s", FW_VERSION);
        display.setCursor(6, y); display.printf("FW:%s", verBuf);

        // Footer
        drawFooter("Exit", "Exit");

    } while (display.nextPage());

    Serial.println("[UI] Device info screen drawn");
}

// --------------------------------------------------------------------------
// drawSleepIndicator()
// --------------------------------------------------------------------------
static const int SLEEP_ICON_X = 178;
static const int SLEEP_ICON_Y = 12;
static const int SLEEP_ICON_W = 16;
static const int SLEEP_ICON_H = 16;

void drawSleepIndicator(SleepIndicator type)
{

    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);


    display.setPartialWindow(SLEEP_ICON_X, SLEEP_ICON_Y, SLEEP_ICON_W, SLEEP_ICON_H);
    display.firstPage();

    do {

        display.fillRect(SLEEP_ICON_X, SLEEP_ICON_Y, SLEEP_ICON_W, SLEEP_ICON_H, GxEPD_WHITE);

        if (type != SLEEP_NONE)
        {
            int16_t xCursor = SLEEP_ICON_X + (SLEEP_ICON_W / 4); 
            int16_t yCursor = SLEEP_ICON_Y + (SLEEP_ICON_H * 3 / 4);
            display.setCursor(xCursor, yCursor);

            switch (type) 
            {
                case SLEEP_LIGHT:
                    display.print("L");
                    break;
                case SLEEP_DEEP:
                    display.print("D");
                    break;
                default:
                    display.print("O");
                    break;
            }

        }
    } while (display.nextPage());
}

