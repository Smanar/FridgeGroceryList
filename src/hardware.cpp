#include <Arduino.h>
#include <time.h>
#include <Wire.h>



#include "hardware.h"
#include "battery.h"

#include "SensorPCF85063.hpp"
SensorPCF85063 rtc;
static constexpr uint8_t PCF85063_NO_ALARM = 0xFF;

static bool g_I2CInitialized = false;

// ============================================================================
// Display object
// ============================================================================

GxEPD2_BW<GxEPD2_154_WS_V2, GxEPD2_154_WS_V2::HEIGHT>
    display(GxEPD2_154_WS_V2(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN)
);

// ============================================================================
//                   LED Stuff
// ============================================================================

// Init Green LED
void GreenLEDInit(void)
{
    // Green charge LED on GPIO 3
    // JTAG pin too (3) — release from JTAG first
    gpio_reset_pin(GPIO_NUM_3);

    // Configure it
    pinMode(GREEN_LED_PIN, OUTPUT);
    digitalWrite(GREEN_LED_PIN, HIGH);
}

// Turn ON/off the green led
void GreenLEDState(bool state)
{
    digitalWrite(GREEN_LED_PIN, state ? LOW : HIGH);
}

// ============================================================================
// Hardware init
// ============================================================================
void initializeHardware()
{
    if (psramFound())
        Serial.printf("[HW] PSRAM: %d MB\n", ESP.getPsramSize() / (1024 * 1024));

    // E-Paper power — ACTIVE LOW
    pinMode(EPD_PWR_PIN, OUTPUT); // Reconnect the 3.3V to the screen.
    digitalWrite(EPD_PWR_PIN, LOW); // Reconnect the 3.3V to the screen
    delay(200);

    // Battery ADC + charge LED
    batteryInit();

    // GxEPD2 display
    display.init(115200, true, 20, false, SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));
    SPI.end();
    SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, -1);
    Serial.printf("[HW] Display: %dx%d\n", display.width(), display.height());

    //Enable Green LED
    GreenLEDInit();

    //Enable I2C
    I2C_init();
}

// ============================================================================
// RTC
// ============================================================================

// Function to write to the RTC's free-use register
void writeRtcStorage(uint8_t value) {
    Wire.beginTransmission(PCF85063_I2C_ADDR); // Adresse I2C du PCF85063
    Wire.write(RAM_REG_ADDR);
    Wire.write(value);
    Wire.endTransmission();
}

// Function to read the RTC's free register
uint8_t readRtcStorage() {
    Wire.beginTransmission(PCF85063_I2C_ADDR);
    Wire.write(RAM_REG_ADDR);
    Wire.endTransmission(false);
    Wire.requestFrom(PCF85063_I2C_ADDR, 1);
    return Wire.available() ? Wire.read() : 0;
}

void I2C_init(void)
{
    if (g_I2CInitialized) return;

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);

    g_I2CInitialized = true;

}

void I2C_close(void)
{
    if (!g_I2CInitialized) return;

    Wire.end();

    g_I2CInitialized = false;;
}

void RTC_shutdown(void)
{
   Serial.println("[RTC] Programming the RTC to wake up in 24 hours...");

   // I2C Initialisation
   I2C_init(); 

   if (!rtc.begin(Wire, PCF85063_SLAVE_ADDRESS))
   {
       Serial.println("[RTC] Impossible to communicate with the RTC PCF85063!");
       return;
   }

   rtc.resetAlarm(); 
   rtc.disableAlarm(); 

   // Retrieving the current time from the RTC module
   RTC_DateTime now = rtc.getDateTime();

   // Extraction of current variables
   uint8_t alrmHour    = (uint8_t)now.getHour();
   uint8_t alrmMinute  = (uint8_t)now.getMinute();
   uint8_t alrmSecond  = 0;
   uint16_t alrmYear   = (uint16_t)now.getYear();
   uint8_t alrmMonth   = (uint8_t)now.getMonth();
   uint8_t alrmDay     = (uint8_t)now.getDay() + 1; // On vise demain (+24h)

   // DAILY CORRECTION ALGORITHM FOR END-OF-MONTH PERIODS
   // Table containing the maximum number of days per month (January = index 1)
   uint8_t daysInMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

   // Leap year handling for February (divisible by 4)
   if ((alrmYear % 4 == 0 && alrmYear % 100 != 100) || (alrmYear % 400 == 0)) {
       daysInMonth[2] = 29;
   }

   // If adding one day causes us to exceed the maximum length of the current month:
   if (alrmDay > daysInMonth[alrmMonth]) {
       alrmDay = 1; // We move to the 1st of the following month.
       alrmMonth++; // We increment the month.
       
       if (alrmMonth > 12) { // If we go past December, we move on to the following year.
           alrmMonth = 1;
           alrmYear++;
       }
   }

   // Programming the alarm on the RTC
   // The PCF85063 primarily requires the hour, minute, second, and day of the month.
   uint8_t alrmWeekday = PCF85063_NO_ALARM; 
   rtc.setAlarm(alrmHour, alrmMinute, alrmSecond, alrmDay, alrmWeekday);

   rtc.enableAlarm();

}

bool isWokenByRTC= false;
bool ReadRTCMemory(void)
{
        uint8_t bootMarker = readRtcStorage();

        if (bootMarker == 0xAA)
        {
            isWokenByRTC = true;
            Serial.println("[RTC] Button wake — short boot");
        }
        else
        {
            isWokenByRTC = false;
        }

        writeRtcStorage(0x00);

        return isWokenByRTC;
}