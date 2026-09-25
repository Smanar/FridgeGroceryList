// ============================================================================
// Audio — ES8311 codec + I2S tone generation + MICROPHONE (I2S RX)
//
// This file talks to the ES8311 directly over I2C (raw register writes),
// instead of pulling in the full esp_codec_dev component. The register
// sequence below has been checked line-by-line against Espressif's own
// driver:
//   esp_codec_dev (component) -> device/es8311/es8311.c
//   https://github.com/waveshareteam/ESP32-S3-ePaper-1.54/tree/main/02_Example/Arduino/08_Audio_Test/src/esp_codec_dev/device/es8311/es8311.c
// which is the exact component vendored inside Waveshare's own
// "08_Audio_Test" example for this board family.
//
// The upstream driver splits codec bring-up into two calls:
//   - es8311_open()  : one-time setup (called once when the codec device is created)
//   - es8311_start() : called every time playback/recording is enabled
// The two phases below (Open / Start) mirror that split so the register
// order stays faithful to the source, even though this file (unlike
// upstream) fully power-cycles the codec rail between sounds instead of
// using the upstream's register-level suspend sequence — see the note
// above audioSuspend()/audioResume() for why.
//
// IMPORTANT: I2S must be started BEFORE ES8311 init so that MCLK is running
// when the codec configures its internal clock tree.
//
// MIC NOTES:
//  - ES8311 outputs 16-bit ADC samples packed into the MSB of a 32-bit I2S
//    slot, same convention as the DAC input, so reading mirrors writing.
//  - Depending on how the mic is wired on the Waveshare board (ADC1L vs
//    ADC1R), you may need to read the RIGHT channel instead of LEFT if the
//    left channel comes back silent — see MIC_CHANNEL below. This wasn't
//    verified against any source; it's a board-wiring fallback to try.
// ============================================================================

// About the power management
// AUDIO_PWR_PIN > is the general power, it turn on/off the ES8311 (the codec)
// PA_EN_PIN > is the amplificator

// The codec need 100ms to be ready, so sounds with a duration < 100ms are not audible

#include "audio.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

#include "hardware.h"
#include "storage.h"

// ---- ES8311 I2C ----
#define ES8311_ADDR       0x18   // 7-bit address (matches ES8311_CODEC_DEFAULT_ADDR 0x30 >> 1 in es8311_codec.h)

// ---- I2S config ----
#define I2S_PORT          I2S_NUM_0
#define SAMPLE_RATE       16000
#define DMA_BUF_COUNT     4
#define DMA_BUF_LEN       256      // samples per DMA buffer
#define TONE_AMPLITUDE    24000    // ~73% of int16 max — loud enough for tiny speaker
                                    // (not sourced from anywhere — chosen empirically for this speaker)

// ---- Mic config ----
#define MIC_CHANNEL          0     // 0 = left slot, 1 = right slot. Flip if silent (unverified against source).
#define MIC_READ_BLOCK      128    // frames per read() batch
#define MIC_SOFTWARE_GAIN    4.0f  // digital gain applied after capture. 1.0 = off.
                                    // Not from the source driver — this file's own post-capture gain,
                                    // separate from the codec's analog PGA gain (REG16, see below).

static bool g_audioInitialized = false;
static bool g_audioEnabled     = false;
static bool g_audioSuspended   = false;
unsigned long g_lastAudioActivity = 0; // Stocke le snapshot du temps (en ms)
const unsigned long AUDIO_TIMEOUT = 60000; // 1 minute en millisecondes

// ---- ES8311 I2C helpers ----
// Mirrors es8311_write_reg()/es8311_read_reg() in the source driver, which
// go through a ctrl_if abstraction; here it's inlined directly over Wire.

static bool es8311_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static uint8_t es8311_read(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

// ---- Register dump for debugging ----
// Register list taken from es8311_reg.h's named registers (0x00-0x45), plus
// the three chip-ID/version registers (0xFD-0xFF) confirmed in the same header.

static void es8311_dump_regs() {
    Serial.println("[Audio] === ES8311 Register Dump ===");
    const uint8_t regs[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x1B, 0x1C,
        0x32, 0x37, 0x44, 0x45, 0xFD, 0xFE, 0xFF
    };
    for (int i = 0; i < (int)(sizeof(regs)/sizeof(regs[0])); i++) {
        Serial.printf("  REG 0x%02X = 0x%02X\n", regs[i], es8311_read(regs[i]));
    }
    Serial.println("[Audio] === End Dump ===");
}

// ---- ES8311 codec init ----
//
// Source: esp_codec_dev component, device/es8311/es8311.c (Espressif,
// Apache-2.0), as vendored in Waveshare's ESP32-S3-ePaper-1.54 repo under
// 02_Example/Arduino/08_Audio_Test/src/esp_codec_dev/.
//
// This board is fixed at: slave mode, external MCLK, analog mic (no DMIC),
// full-duplex (ADC+DAC both enabled), MCLK=4.096MHz / Fs=16kHz / 16-bit.
// The two phases below are the register writes es8311_open() and
// es8311_start() perform for exactly that configuration — verified against
// the driver's actual branches for those flag values, not assumed.
//
// Clock coefficient row used (mclk=4096000, rate=16000), taken verbatim
// from the driver's coeff_div[] table:
//   pre_div=1, pre_multi=x1, adc_div=1, dac_div=1, fs_mode=0(single speed),
//   lrck_h=0x00, lrck_l=0xFF, bclk_div=4, adc_osr=0x10, dac_osr=0x20
//
// Register encoding (from es8311_config_sample() in the source):
//   REG02 = ((pre_div-1)<<5) | (pre_multi_code<<3)         -> 0x00
//   REG05 = ((adc_div-1)<<4) | (dac_div-1)                 -> 0x00
//   REG03 = (fs_mode<<6) | adc_osr                         -> 0x10
//   REG04 = dac_osr                                        -> 0x20
//   REG07 = lrck_h                                         -> 0x00
//   REG08 = lrck_l                                         -> 0xFF
//   REG06 = (bclk_div-1) when bclk_div<19                  -> 0x03

// Prerequisite: MCLK must already be running (I2S started first).

static bool es8311_open_regs() {
    // Enhance I2C noise immunity — the source writes this twice in a row
    // ("occasional failures during the first I2C write"), first thing in open().
    es8311_write(0x44, 0x08);
    es8311_write(0x44, 0x08);

    es8311_write(0x01, 0x30);   // Clock manager: disable most clocks initially
    es8311_write(0x02, 0x00);   // Clock divider defaults
    es8311_write(0x03, 0x10);   // ADC OSR = 0x10 (initial; re-set in config_sample below)
    es8311_write(0x16, 0x06);   // ADC/mic PGA gain — see set_mic_gain() below for why this is 0x06, not a raw bit pattern
    es8311_write(0x04, 0x10);   // DAC OSR initial (re-set in config_sample below)
    es8311_write(0x05, 0x00);   // ADC/DAC clock divider = 1
    es8311_write(0x0B, 0x00);   // System power ref
    es8311_write(0x0C, 0x00);   // System power ref
    es8311_write(0x10, 0x1F);   // Enable analog block
    es8311_write(0x11, 0x7F);   // DAC bias / analog settings

    // RESET_REG00: CSM power-up (bit7=1). Slave mode = bit6 stays 0, so 0x80 is final for us.
    es8311_write(0x00, 0x80);

    // CLK_MANAGER_REG01: clock source select. For use_mclk=true, invert_mclk=false
    // the driver's formula (regv=0x3F; regv&=0x7F if use_mclk; regv|=0x40 if invert)
    // resolves to 0x3F for our fixed config.
    es8311_write(0x01, 0x3F);

    // CLK_MANAGER_REG06: SCLK invert bit. invert_sclk=false for us, so just clear it.
    es8311_write(0x06, 0x00);

    es8311_write(0x13, 0x10);   // VMID reference config
    es8311_write(0x1B, 0x0A);   // ADC HPF stage 1
    es8311_write(0x1C, 0x6A);   // ADC HPF stage 2

    // GPIO_REG44: internal reference signal select. The driver writes 0x58
    // here (ADCL + DACR reference) whenever no_dac_ref == false, which is
    // the default/standard case — NOT 0x08. (0x08 is only correct if
    // no_dac_ref == true, i.e. you deliberately don't want a DAC reference
    // on the right channel.) This was wrong in a previous version of this
    // file and is a plausible cause of a weak/silent mic path.
    es8311_write(0x44, 0x58);

    return true;
}

// ---- ES8311 "start" phase — enables the ADC+DAC signal path ----
//
// Source: es8311_start() in es8311.c, specialized for codec_mode == BOTH
// (this board always runs full-duplex).

static bool es8311_start_regs() {
    // RESET_REG00 again (start() re-asserts it): slave mode -> 0x80.
    es8311_write(0x00, 0x80);
    // CLK_MANAGER_REG01 again, same resolved value as in open().
    es8311_write(0x01, 0x3F);

    // Clock coefficients for MCLK=4.096MHz, Fs=16kHz — see table above.
    es8311_write(0x02, 0x00);
    es8311_write(0x05, 0x00);
    es8311_write(0x03, 0x10);
    es8311_write(0x04, 0x20);
    es8311_write(0x07, 0x00);
    es8311_write(0x08, 0xFF);
    es8311_write(0x06, 0x03);

    // SDPIN_REG09 / SDPOUT_REG0A: word length (16-bit -> OR 0x0C, per
    // es8311_set_bits_per_sample) + I2S normal format (AND 0xFC, per
    // es8311_config_fmt) + ADC/DAC path enabled (bit6 cleared, since this
    // board runs codec_mode == BOTH). Net result for a codec coming out of
    // reset with those high bits at 0: 0x0C for both registers.
    es8311_write(0x09, 0x0C);
    es8311_write(0x0A, 0x0C);

    // ADC/DAC power-up + volume, in the order es8311_start() uses:
    es8311_write(0x17, 0xBF);   // ADC volume/enable
    es8311_write(0x0E, 0x02);   // Power up analog circuitry (mic bias included)
    es8311_write(0x12, 0x00);   // Enable DAC (0x00 = enabled)
    es8311_write(0x14, 0x1A);   // Analog PGA select, DMIC disabled (analog mic path)
    es8311_write(0x0D, 0x01);   // Power up digital core (shared ADC+DAC)
    es8311_write(0x15, 0x40);   // ADC ramp rate
    es8311_write(0x37, 0x08);   // DAC ramp rate (prevents pops)
    es8311_write(0x45, 0x00);   // GP control: normal operation

    // DAC volume (REG32): the source computes this from a dB value via
    // esp_codec_dev_set_out_vol()'s vol_range table (min 0x00=-95.5dB,
    // max 0xFF=+32dB) — it's not a fixed init-time constant in the driver.
    // 0xBF is kept here as an approximate "around 0dB" starting volume;
    // unlike the rest of this function it is NOT a value taken from the
    // source, just a reasonable default.
    es8311_write(0x32, 0xBF);

    // Verify chip is responding
    uint8_t chipId = es8311_read(0xFD);
    Serial.printf("[Audio] ES8311 chip ID: 0x%02X\n", chipId);

    // Dump all registers for debugging
    //es8311_dump_regs();

    return chipId != 0xFF;
}

static bool es8311_init() {
    return es8311_open_regs() && es8311_start_regs();
}

// ---- I2S init (full-duplex: TX for speaker, RX for mic) ----
//
// Not part of the es8311.c source (that driver only talks I2C to the codec
// and expects the caller to already have I2S running) — this section is
// this file's own I2S setup.

static bool i2s_init_output() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);  // full-duplex
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;  // 32-bit slot to match ES8311 SDP config
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = DMA_BUF_COUNT;
    cfg.dma_buf_len = DMA_BUF_LEN;
    cfg.use_apll = true;           // APLL for accurate MCLK generation
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = SAMPLE_RATE * 256;  // MCLK = 4.096 MHz for 16kHz

    if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) {
        Serial.println("[Audio] I2S driver install failed");
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_MCLK_PIN;
    pins.bck_io_num   = I2S_BCLK_PIN;
    pins.ws_io_num    = I2S_WS_PIN;
    pins.data_out_num = I2S_DOUT_PIN;
    pins.data_in_num  = I2S_DIN_PIN;

    if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
        Serial.println("[Audio] I2S pin config failed");
        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    return true;
}

// ---- DMA flush: write silence to push remaining tone data through pipeline --

static void i2s_flush_dma() {
    int32_t silence[DMA_BUF_LEN * 2] = {0};  // one full DMA buffer of stereo silence
    size_t written;
    for (int i = 0; i < DMA_BUF_COUNT; i++) {
        i2s_write(I2S_PORT, silence, sizeof(silence), &written, 200);
    }
}

// ---- Discard any stale RX bytes sitting in the DMA ring before a fresh capture ----

static void i2s_flush_rx() {
    int32_t scratch[DMA_BUF_LEN * 2];
    size_t read;
    do {
        i2s_read(I2S_PORT, scratch, sizeof(scratch), &read, 0);  // non-blocking (timeout=0)
    } while (read > 0);
}

// ============================================================================
// Public API
// ============================================================================

void audioInit(void)
{
    if (g_audioInitialized) return;

    // Power on audio rail
    pinMode(AUDIO_PWR_PIN, OUTPUT);
    digitalWrite(AUDIO_PWR_PIN, LOW);  // active low
    delay(50);

    // PA off initially
    pinMode(PA_EN_PIN, OUTPUT);
    digitalWrite(PA_EN_PIN, LOW);

    // I2C for ES8311 (may already be initialised for other I2C devices)
    I2C_init();

    // *** CRITICAL: Start I2S FIRST so MCLK is running before ES8311 init ***
    // The ES8311 codec needs MCLK to configure its internal clock tree.
    if (!i2s_init_output()) {
        Serial.println("[Audio] I2S init failed — audio disabled");
        return;
    }
    delay(50);  // let MCLK stabilize

    if (!es8311_init()) {
        Serial.println("[Audio] ES8311 init failed — audio disabled");
        return;
    }

    // I2S clocks keep running — ES8311 needs continuous MCLK.
    // Light sleep automatically pauses/resumes I2S clocks.
    // tx_desc_auto_clear ensures silence when no data is written.

    g_audioInitialized = true;
    Serial.println("[Audio] Initialized (I2S full-duplex + ES8311)");

    // Suspend audio hardware to save power — will auto-resume on next tone
    audioSuspend();
}

void audioEnable() {
    if (!g_audioInitialized) return;

    digitalWrite(PA_EN_PIN, HIGH);

    g_audioEnabled = true;
    delay(50);  // let PA + DAC output stabilize, to prevent "pop" sound
}

void audioDisable() {
    if (!g_audioInitialized) return;

    delay(10);  // brief tail after last sample

    digitalWrite(PA_EN_PIN, LOW);
    g_audioEnabled = false;
}

// ---- Tone generation ----
// Own code, not from es8311.c (that driver doesn't generate audio, it only
// configures the codec — sample data comes from the caller).

void audioTone(int freqHz, int durationMs)
{
    if (!g_audioInitialized) return;
    if (g_audioSuspended) audioResume();

    const int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
    const float omega = 2.0f * M_PI * freqHz / SAMPLE_RATE;

    // Generate in small blocks (32-bit samples for ES8311 32-bit slot width)

    const int blockSize = 128;
    int32_t buf[blockSize * 2];  // stereo: L, R interleaved, 32-bit per slot
    size_t written;
    int samplesLeft = totalSamples;

    for (int offset = 0; samplesLeft > 0; ) {
        int count = min(blockSize, samplesLeft);
        for (int i = 0; i < count; i++) {
            int16_t raw = (int16_t)(sinf(omega * (offset + i)) * TONE_AMPLITUDE);
            int32_t sample = (int32_t)raw << 16;  // 16-bit data in MSB of 32-bit slot
            buf[i * 2]     = sample;  // left
            buf[i * 2 + 1] = sample;  // right
        }
        i2s_write(I2S_PORT, buf, count * 8, &written, 200);  // 8 bytes per stereo frame
        offset += count;
        samplesLeft -= count;
    }

    // Flush DMA — push all tone data through the pipeline so it actually plays
    //i2s_flush_dma();  // disabled because it "kill" the first ms of sound

    // Better method than i2s_flush_dma() ?

    vTaskDelay(pdMS_TO_TICKS(durationMs));
}

//***************************************************************

// Auto-suspend helper — called after canned effects to power-gate until next use
static void audioAutoSuspend()
{
    // Small delay so last DMA data clears, then suspend
    delay(20);
    audioSuspend();
}

// ============================================================================
// Microphone (I2S RX ← ES8311 ADC)
// ============================================================================

// ---- Discard stale buffered samples right before starting a live capture ----
//
// Public wrapper — call this once right before a streaming loop (e.g. before
// sending audio to a speech API) so the first chunk isn't leftover audio
// from while the mic was idle.

void audioMicFlush()
{
    if (!g_audioInitialized) return;
    if (g_audioSuspended) audioResume();
    i2s_flush_rx();
}

// ---- Read raw mono PCM16 samples from the mic into a caller-provided buffer ----
//
// Blocking call: fills `outSamples` samples (mono, 16-bit, SAMPLE_RATE Hz).
// PA / speaker stay off — recording doesn't need the amp.
// Reads in MIC_READ_BLOCK-sized batches for efficiency.

bool audioMicRead(int16_t* outBuf, size_t numSamples) {
    if (!g_audioInitialized) return false;
    if (g_audioSuspended) audioResume();

    int32_t frame[MIC_READ_BLOCK * 2];  // stereo 32-bit per block
    size_t bytesRead;
    size_t got = 0;

    while (got < numSamples) {
        size_t count = min((size_t)MIC_READ_BLOCK, numSamples - got);
        size_t bytesToRead = count * 2 * sizeof(int32_t);  // stereo frames

        if (i2s_read(I2S_PORT, frame, bytesToRead, &bytesRead, portMAX_DELAY) != ESP_OK) {
            Serial.println("[Audio] i2s_read failed");
            return false;
        }

        size_t framesRead = bytesRead / (2 * sizeof(int32_t));
        for (size_t i = 0; i < framesRead; i++) {
            int32_t raw = frame[i * 2 + MIC_CHANNEL];
            int16_t sample = (int16_t)(raw >> 16);  // 16-bit mic data lives in MSB

            // Digital gain with clipping protection — do the math in float/int32
            // so we saturate cleanly at INT16 range instead of wrapping around.

            int32_t boosted = (int32_t)(sample * MIC_SOFTWARE_GAIN);
            if (boosted > INT16_MAX) boosted = INT16_MAX;
            if (boosted < INT16_MIN) boosted = INT16_MIN;

            outBuf[got + i] = (int16_t)boosted;
        }
        got += framesRead;
    }
    return true;
}

// ---- Minimal WAV header writer (16-bit mono PCM) ----

static void wavWriteHeader(File &f, uint32_t sampleRate, uint32_t dataBytes) {
    uint32_t chunkSize = 36 + dataBytes;
    uint16_t numChannels = 1;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    uint16_t blockAlign = numChannels * bitsPerSample / 8;

    f.write((const uint8_t*)"RIFF", 4);
    f.write((const uint8_t*)&chunkSize, 4);
    f.write((const uint8_t*)"WAVE", 4);
    f.write((const uint8_t*)"fmt ", 4);
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1;  // PCM
    f.write((const uint8_t*)&subchunk1Size, 4);
    f.write((const uint8_t*)&audioFormat, 2);
    f.write((const uint8_t*)&numChannels, 2);
    f.write((const uint8_t*)&sampleRate, 4);
    f.write((const uint8_t*)&byteRate, 4);
    f.write((const uint8_t*)&blockAlign, 2);
    f.write((const uint8_t*)&bitsPerSample, 2);
    f.write((const uint8_t*)"data", 4);
    f.write((const uint8_t*)&dataBytes, 4);
}

// ---- Sound effects ----

void MakeSound(BasicSound s, int repeats)
{
    if (!g_audioInitialized) return;
    if (g_audioSuspended) audioResume();

    // We turn on the amplifier (including 50 ms for stabilization).
    if (!g_audioEnabled) audioEnable();

    switch (s)
    {
        case SOUND_BEEP:
            audioTone(1000, 200);
        break;
        case SOUND_CLICK:
            audioTone(500, 100);
        break;
        case SOUND_CONFIRM:
            audioTone(1800, 120);
            delay(40);
            audioTone(2400, 120);
        break;
        case SOUND_NOTIFY:
            audioTone(1800, 120);
            delay(40);
            audioTone(2400, 120);
        break;
        case SOUND_ERROR:
            audioTone(400, 300);
        break;
        case SOUND_ATTENTION:
            for (int i = 0; i < repeats; i++)
            {
                audioTone(1000, 200);
                if (i < repeats - 1) delay(300);  // gap between bursts
            }
        break;
        default:
        break;
    }

    // The music has stopped—cut the amp immediately!
    // As for the codec, it remains powered in the background to ensure responsiveness.
    if (g_audioEnabled) audioDisable();
}

//------------------------------------------------------------------------------------------------

void audioShutdown() {
    if (!g_audioInitialized) return;

    audioDisable();
    i2s_stop(I2S_PORT);
    i2s_driver_uninstall(I2S_PORT);
    digitalWrite(AUDIO_PWR_PIN, HIGH);  // power off audio rail (active low)
    g_audioInitialized = false;
    g_audioSuspended   = false;

    Serial.println("[Audio] Shutdown complete");
}

// ---- Power-gating: suspend/resume audio hardware between sounds ----
//
// This is a board-level design choice, not something from es8311.c: this
// board can cut power to the whole codec rail (AUDIO_PWR_PIN), which loses
// all ES8311 register state — so resume just re-runs the full open+start
// sequence rather than the source driver's register-based es8311_suspend()
// (which writes a specific low-power register sequence while keeping the
// rail powered). Both approaches are valid; this file intentionally uses
// the power-rail approach for the extra current savings.

void audioSuspend()
{
    if (!g_audioInitialized || g_audioSuspended) return;

    // You ALWAYS turn off the amplifier first to cut the signal to the speaker.
    if (g_audioEnabled) audioDisable();   // Lead time + cut PA_EN_PIN (total silence)

    i2s_stop(I2S_PORT);          // stop I2S clocks
    digitalWrite(AUDIO_PWR_PIN, HIGH);  // power off codec rail

    g_audioSuspended = true;
    Serial.println("[Audio] Suspend codec (power-gated)");
}

void audioResume()
{
    if (!g_audioInitialized) return;

    // if audio sleeping, wake it
    if (g_audioSuspended)
    {
        digitalWrite(AUDIO_PWR_PIN, LOW);   // power on codec rail
        delay(50);                          // let rail stabilize
        i2s_start(I2S_PORT);                // restart I2S clocks (MCLK needed)
        delay(50);                          // let MCLK stabilize
        es8311_init();                      // re-run open+start (registers were lost with power)

        g_audioSuspended = false;
        Serial.println("[Audio] Resumed");
    }

    g_lastAudioActivity = millis();
}

// Audio timeout Management
void audioloop(void)
{
    if (g_audioInitialized && !g_audioSuspended)
    {
        if (millis() - g_lastAudioActivity >= AUDIO_TIMEOUT) {
            audioSuspend();
        }
    }
}


// ---- Record `durationMs` of mic audio into RAM and play it straight back ----
//
// Memory note: 16kHz * 16-bit mono = 32 KB per second. A few seconds is fine
// on the ESP32-S3's internal RAM; for longer clips, switch the malloc below
// to heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM) if the board has PSRAM.

bool audioRecordAndPlayback(int durationMs)
{
    if (!g_audioInitialized) return false;
    if (g_audioSuspended) audioResume();

    // We turn on the amplifier (including 50 ms for stabilization).
    if (!g_audioEnabled) audioEnable(); 

    const uint32_t totalSamples = (SAMPLE_RATE * (uint32_t)durationMs) / 1000;
    int16_t* buf = (int16_t*)malloc(totalSamples * sizeof(int16_t));
    if (!buf) {
        Serial.println("[Audio] Not enough RAM for recording buffer");
        return false;
    }

    i2s_flush_rx();  // drop stale samples buffered while codec was idle

    Serial.printf("[Audio] Recording %d ms to RAM...\n", durationMs);

    if (!audioMicRead(buf, totalSamples))
    {
        free(buf);
        return false;
    }

    Serial.println("[Audio] Recording done — playing back...");

    // Play it back through the existing TX path (same conversion as audioTone)

    const int blockSize = 128;
    int32_t outBuf[blockSize * 2];
    size_t written;
    uint32_t offset = 0;
    while (offset < totalSamples) {
        uint32_t count = min((uint32_t)blockSize, totalSamples - offset);
        for (uint32_t i = 0; i < count; i++) {
            int32_t sample = (int32_t)buf[offset + i] << 16;  // MSB of 32-bit slot
            outBuf[i * 2]     = sample;
            outBuf[i * 2 + 1] = sample;
        }
        i2s_write(I2S_PORT, outBuf, count * 8, &written, 200);
        offset += count;
    }
    i2s_flush_dma();

    free(buf);
    Serial.println("[Audio] Playback complete");
    audioAutoSuspend();


    // The music has stopped—cut the amp immediately!
    // As for the codec, it remains powered in the background to ensure responsiveness.
    if (g_audioEnabled) audioDisable();

    return true;
}