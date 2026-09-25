#ifndef HARDWARE_H
#define HARDWARE_H

#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>

#include "GxEPD2_154_WS_V2_custom.h"


// The display is 200x200 px or 1.54''
// 2 colors support
// Xtensa 32-bit LX7 dual-core processor, up to 240MHz main frequency
// Built-in 512KB Static RAM, 384KB ROM, with integrated 8MB Flash and 8MB PSRAM
// Reserved 2 × 6 2.54mm pitch pin header for convenient external expansion
// Onboard PCF85063 RTC chip and SHTC3 temperature & humidity sensor for accurate RTC management and environmental monitoring

//Example on github https://github.com/waveshareteam/ESP32-S3-ePaper-1.54G

// ============================================================================
// Hardware pins  (Waveshare ESP32-S3-ePaper-1.54 V2)
// ============================================================================

//Display
#define DISPLAY_WIDTH  200
#define DISPLAY_HEIGHT 200
#define EPD_DC_PIN     10
#define EPD_CS_PIN     11
#define EPD_SCK_PIN    12
#define EPD_MOSI_PIN   13
#define EPD_RST_PIN    9
#define EPD_BUSY_PIN   8
#define EPD_PWR_PIN    6    // ACTIVE LOW — LOW = on

//Battery
#define   VBAT_PWR_PIN  17   // Battery power latch — HIGH = stay on
#define   BATT_ADC_PIN  4    // GPIO 4 = ADC1_CH3

//RTC
#define   RTC_INT_PIN   5       //RTC pin
#define   RAM_REG_ADDR  0x03    // Registre RAM libre du PCF85063

//https://github.com/waveshareteam/Waveshare-ESP32-components/tree/d081959d3841e0b370c2957c122bf8604ab42bc8/sensor/pcf85063a

//Input
#define BOOT_BUTTON   0
#define PWR_BUTTON    18

//LED
#define GREEN_LED_PIN 3  //Green LED
// the Red LED can't be managed, it's just power on when USB is plugged

//audio
#define AUDIO_PWR_PIN     42    // Audio power rail — ACTIVE LOW
#define PA_EN_PIN         46    // Power amplifier enable — ACTIVE HIGH
#define I2S_MCLK_PIN      14    // I2S output
#define I2S_BCLK_PIN      15    // I2S output
#define I2S_WS_PIN        38    // I2S output
#define I2S_DOUT_PIN      45    // I2S output
#define I2S_DIN_PIN       16    // I2S output

//ESP32 I2C Init
#define I2C_SDA_PIN            47    // ES8311 codec via I2C
#define I2C_SCL_PIN            48    // ES8311 codec via I2C
#define PCF85063_I2C_ADDR      0x51  // Adresse I2C standard du PCF85063
#define I2C_SHTC3_DEV_ADD      0x70 

//SD card
#define SD_CLK_PIN 36
#define SD_CMD_PIN 35
#define SD_DATA0_PIN 37

//Low-power wake-up
#define EXT_WAKE_UP_PIN 0

// ============================================================================
//  Fonction
// ============================================================================

void GreenLEDInit(void);
void GreenLEDState(bool state);
void initializeHardware();

// Display object
extern GxEPD2_BW<GxEPD2_154_WS_V2, GxEPD2_154_WS_V2::HEIGHT> display;


//RTC
void RTC_shutdown(void);
bool ReadRTCMemory(void);
void writeRtcStorage(uint8_t value);

//I2C
void I2C_init(void);

#endif