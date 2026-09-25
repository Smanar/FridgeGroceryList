// ============================================================================
// Audio — ES8311 codec + I2S tone generation
// ============================================================================

#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

enum BasicSound
{
    SOUND_BEEP,
    SOUND_CLICK,
    SOUND_CONFIRM,
    SOUND_NOTIFY,
    SOUND_ERROR,
    SOUND_ATTENTION
};

// Initialise I2C to ES8311, configure codec, set up I2S output.
void audioInit(void);

// Enable/disable the power amplifier and audio power rail.
void audioEnable();
void audioDisable();

// Graceful shutdown — uninstall I2S, power off codec.
void audioShutdown();

// Suspend audio hardware to save power (power-gate codec + I2S stop).
// Call audioResume() before the next tone.  Safe to call multiple times.
void audioSuspend();
void audioResume();

// thread loop
void audioloop(void);

// --- Tone primitives ---

// Play a single sine tone at `freqHz` for `durationMs` milliseconds.
void audioTone(int freqHz, int durationMs);

// --- Sound effects ---
void MakeSound(BasicSound s, int repeats = 1);

// For micro to record
bool audioMicRead(int16_t* outBuf, size_t numSamples);
void audioMicFlush();

//For tests
bool audioRecordAndPlayback(int durationMs);

#endif
