// ============================================================================
// Battery monitoring — Waveshare ESP32-S3-ePaper-1.54 V2
//
// ADC1 Channel 3 = GPIO 4, 2:1 resistor divider on the board.
// Uses Arduino analogReadMilliVolts() for calibrated readings.
// ============================================================================

#include <Arduino.h>
#include <driver/gpio.h>
#include "soc/usb_serial_jtag_reg.h"

#include "hardware.h"
#include "battery.h"

// ---------------------------------------------------------------------------
// Board-specific constants
// ---------------------------------------------------------------------------

static constexpr float DIVIDER_RATIO      = 2.0f;    // 2:1 voltage divider
static constexpr float BATT_FULL_V        = 4.10f;   // Charge IC tops out ~4.10-4.15V
static constexpr float BATT_EMPTY_V       = 3.00f;   // LiPo cutoff

static const int       BATTERY_WARN_PCT     = 15;
static const int       BATTERY_SHUTDOWN_PCT = 5;

// Number of samples to average (reduces noise)
static const int       ADC_SAMPLES          = 16;

// ---------------------------------------------------------------------------
void batteryInit()
{
    // Fallback to ADC_11db if ADC_ATTEN_DB_12 is not available in your core version.
    // Both represent the maximum attenuation (~11-12dB) providing a 0 - 3.1V / 3.3V range.
    #ifdef ADC_ATTEN_DB_12
        analogSetAttenuation(ADC_ATTEN_DB_12);
    #else
        analogSetAttenuation(ADC_11db); 
    #endif

    pinMode(BATT_ADC_PIN, INPUT);
    
    // Prime the ADC with a throwaway read
    analogReadMilliVolts(BATT_ADC_PIN);
}

// ---------------------------------------------------------------------------
float batteryReadVoltage()
{
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++)
    {
        sum += analogReadMilliVolts(BATT_ADC_PIN);
        delayMicroseconds(200); // Leave time for ADC noise to settle
    }
    float mV = (float)sum / ADC_SAMPLES;
    float voltage = (mV / 1000.0f) * DIVIDER_RATIO;
    return voltage;
}

// ---------------------------------------------------------------------------
int batteryPercent(float voltage)
{
    if (voltage >= BATT_FULL_V)  return 100;
    if (voltage <= BATT_EMPTY_V) return 0;

    // Piecewise linear approximation for a standard LiPo discharge curve
    if (voltage > 4.00f) return 90 + (int)((voltage - 4.00f) * 10.0f  / 0.10f); // 4.00V to 4.10V
    if (voltage > 3.80f) return 50 + (int)((voltage - 3.80f) * 40.0f  / 0.20f); // 3.80V to 4.00V
    if (voltage > 3.70f) return 15 + (int)((voltage - 3.70f) * 35.0f  / 0.10f); // 3.70V to 3.80V
    if (voltage > 3.50f) return 5  + (int)((voltage - 3.50f) * 10.0f  / 0.20f); // 3.50V to 3.70V
    
    // Voltage drops very quickly below 3.5V
    return (int)((voltage - BATT_EMPTY_V) * 5.0f / (3.50f - BATT_EMPTY_V));
}

// ---------------------------------------------------------------------------
bool batteryOnUSB(float voltage) 
{
    // Primary: voltage >= 4.25V (charge IC overshoot)
    if (voltage >= 4.25f) return true;

    // Secondary: USB SOF frame counter — the USB host sends Start-of-Frame
    // packets every 1ms. If the counter changes across two reads, USB is
    // physically connected and actively enumerated.
    uint32_t sof1 = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
    delay(2);  // wait >1 SOF interval (1 ms)
    uint32_t sof2 = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
    
    if (sof1 != sof2) return true;

    return false;
}

// ----------------------------------------------------------------------------
int GetBatteryState(void)
{
    float voltage = batteryReadVoltage();

    if (batteryOnUSB(voltage)) return STATE_USB;

    int pct = batteryPercent(voltage);

    // Critical level
    if (pct < BATTERY_SHUTDOWN_PCT) return STATE_CRITICAL;

    // Low level
    if (pct < BATTERY_WARN_PCT) return STATE_LOW;

    return STATE_NONE;
}
