#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_heap_caps.h"

#include "stt.h"
#include "audio.h"
#include "algorithm.h"
#include "itemlist.h"
#include "simplenote.h"
#include "storage.h"

// https://wit.ai
// Need the server token API

#define WIT_CHUNK_SAMPLES 256   // 256 samples = 512 bytes per chunk sent, TODO : Try with 1024
#define BOOT_BUTTON 0
#define MAX_RECORDING_TIME_MS 8000 // 8 seconds maximum recording time limit
#define WIT_SAMPLE_RATE 16000
#define WIT_MAX_SAMPLES ((size_t)((MAX_RECORDING_TIME_MS / 1000.0) * WIT_SAMPLE_RATE))
#define WIT_CONNECT_RETRIES 2   // total attempts (1 initial + 1 retry) for the backup transmission

//#define FALLBACK_ENABLED

// --- Client-side silence detection ----------------------------------------
// Goal: cut the stream ourselves as soon as the end of the sentence is detected,
// to finish cleanly BEFORE Wit.ai does so on its end (its server-side VAD
// closes the connection abruptly upon detecting silence).
// Winning this race avoids the majority of observed -80 errors.
#define SILENCE_AMPLITUDE_THRESHOLD 400   // to be adjusted according to background noise / microphone gain
#define SILENCE_DURATION_MS 700           // continuous silence, after speaking, before considering the sentence finished

static const char WIT_CONTENT_TYPE[] =
    "audio/raw;encoding=signed-integer;bits=16;rate=16000;endian=little";

// ---------------------------------------------------------------------------
// Robust TLS write: loop until everything is sent or the connection dies.
// Returns immediately if the socket is dead or after 2s without any progress.
// ---------------------------------------------------------------------------
bool robustWrite(WiFiClientSecure &client, const uint8_t *data, size_t len)
{
  size_t sent = 0;
  unsigned long start = millis();

  while (sent < len)
  {
    if (!client || !client.connected())
    {
      return false;
    }

    size_t n = client.write(data + sent, len - sent);
    if (n > 0)
    {
      sent += n;
      start = millis(); // Resets the timeout if progress is made
    }
    else
    {
      if (millis() - start > 2000) return false;
      delay(2); // Allow time for the TCP/TLS stack and avoid flooding
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Average absolute amplitude of an audio chunk — a rough measure, but
// sufficient to distinguish "speech" from "silence".
// ---------------------------------------------------------------------------
static int16_t averageAmplitude(const int16_t *chunk, size_t samples)
{
  uint32_t sum = 0;
  for (size_t i = 0; i < samples; i++)
  {
    sum += (uint32_t)abs(chunk[i]);
  }
  return (int16_t)(sum / samples);
}

// ---------------------------------------------------------------------------
// Reads a Wit.ai HTTP response already available on `client` and extracts the
// recognized text and the HTTP status code (0 if the connection dropped
// before a header was received).
// ---------------------------------------------------------------------------
static String readWitResponse(WiFiClientSecure &client, int &httpStatusCode)
{
  String finalResult = "";
  String rawResponse = "";
  bool headerParsed = false;
  httpStatusCode = 0;

  while (client.connected() || client.available())
  {
    if (client.available())
    {
      String line = client.readStringUntil('\n');
      rawResponse += line + "\n";

      if (!headerParsed && line.startsWith("HTTP/1.1"))
      {
        httpStatusCode = line.substring(9, 12).toInt();
        headerParsed = true;
      }

      // Look for the "text" key inside the Wit.ai JSON stream response chunk
      int textIndex = line.indexOf("\"text\":");
      if (textIndex != -1)
      {
        int start = line.indexOf("\"", textIndex + 7) + 1;
        int end = line.indexOf("\"", start);
        if (end != -1) finalResult = line.substring(start, end);
      }
    }
    yield(); // Avoid triggering ESP32 watchdog during server processing lag
  }

  if (httpStatusCode == 0 && finalResult.length() > 0)
  {
    httpStatusCode = 200;
  }

  Serial.printf("[Wit] HTTP status: %d\n", httpStatusCode);
  Serial.println("[Wit] Raw response ---");
  Serial.println(rawResponse);
  Serial.println("[Wit] --- end raw response");

  return finalResult;
}

#ifdef FALLBACK_ENABLED
// ---------------------------------------------------------------------------
// Fallback: sends the entire recorded buffer in a single short HTTPS
// request (Content-Length, no chunked encoding). Used only when the
// live stream was cut off by Wit.ai prematurely. Retries once, using a
// fresh connection, if the write fails or the response is empty.
// ---------------------------------------------------------------------------
static String sendBufferToWit(const int16_t *buffer, size_t sampleCount)
{
  size_t byteLen = sampleCount * sizeof(int16_t);

  for (int attempt = 1; attempt <= WIT_CONNECT_RETRIES; attempt++)
  {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);

    Serial.printf("[Wit] Fallback : connection (try %d/%d)\n", attempt, WIT_CONNECT_RETRIES);

    if (!client.connect("api.wit.ai", 443))
    {
      Serial.println("[Wit] Fallback : connection failed");
      char error_buf[100];
      client.lastError(error_buf, 100);
      Serial.print("[Wit] Error: ");
      Serial.println(error_buf);
      delay(200);
      continue;
    }

    char header[256];
    snprintf(header, sizeof(header),
             "POST /speech?v=20230215 HTTP/1.1\r\n"
             "Host: api.wit.ai\r\n"
             "Authorization: Bearer %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %u\r\n"
             "Connection: close\r\n\r\n",
             g_witai_api.c_str(), WIT_CONTENT_TYPE, (unsigned int)byteLen);

    bool ok = robustWrite(client, (uint8_t*)header, strlen(header));
    if (ok) ok = robustWrite(client, (uint8_t*)buffer, byteLen);

    if (!ok)
    {
      Serial.println("[Wit] Fallback : write failure, retrying");
      client.stop();
      delay(200);
      continue;
    }

    Serial.println("[Wit] Fallback : processing");

    int httpStatusCode = 0;
    String finalResult = readWitResponse(client, httpStatusCode);
    client.stop();

    if (httpStatusCode == 0)
    {
      Serial.println("[Wit] Fallback : empty response (connection reset), retrying");
      delay(200);
      continue;
    }

    return finalResult; // can be empty if Wit legitimately recognized nothing
  }

  Serial.println("[Wit] Fallback : All attempts failed.");
  return "";
}
#endif



// ---------------------------------------------------------------------------
// Principal function
// ---------------------------------------------------------------------------
void sendAudioToWit()
{
  size_t maxSamples = WIT_MAX_SAMPLES;
  int16_t *audioBuffer = nullptr;

#ifdef FALLBACK_ENABLED
// Mirror buffer for all captured audio, to allow playback via the
// fallback stream if the live feed is interrupted. PSRAM preferred
// (~256 KB for 8s at 16 kHz/16-bit mono).
  audioBuffer = (int16_t*)heap_caps_malloc(maxSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!audioBuffer)
  {
    Serial.println("[Wit] PSRAM allocation failed, falling back to internal RAM.");
    audioBuffer = (int16_t*)malloc(maxSamples * sizeof(int16_t));
  }
  if (!audioBuffer)
  {
    Serial.println("[Wit] Buffer allocation failed; streaming without a safety net.");
  }
#endif

  WiFiClientSecure client;
  client.setInsecure();

  client.setTimeout(20); // In test

  bool streamConnected = client.connect("api.wit.ai", 443);

  if (!streamConnected)
  {
    Serial.println("[Wit] Connection failed");
    char error_buf[100];
    client.lastError(error_buf, 100);
    Serial.print("[Wit] Error: ");
    Serial.println(error_buf);
    // We're proceeding anyway: we can try the fallback option
    // once recording is complete, provided the buffer was successfully allocated.
  }
  else
  {
    Serial.println("[Wit] Connected");

    char header[256];
    snprintf(header, sizeof(header),
             "POST /speech?v=20230215 HTTP/1.1\r\n"
             "Host: api.wit.ai\r\n"
             "Authorization: Bearer %s\r\n"
             "Content-Type: %s\r\n"
             "Transfer-Encoding: chunked\r\n"
             "Connection: close\r\n\r\n",
             g_witai_api.c_str(), WIT_CONTENT_TYPE);

    client.print(header);
  }

  // Flush stale audio samples before starting transcription stream
  audioMicFlush();

  Serial.println("[Wit] Listening");
  MakeSound(SOUND_ATTENTION, 2);

  int16_t chunk[WIT_CHUNK_SAMPLES];
  size_t samplesRecorded = 0;
  bool streamBroken = !streamConnected;
  bool hasSpokenSpeech = false;
  unsigned long silenceStartTime = 0;
  unsigned long startRecordTime = millis();

  while ((digitalRead(BOOT_BUTTON) == HIGH) && ((millis() - startRecordTime) < MAX_RECORDING_TIME_MS))
  {
    if (!audioMicRead(chunk, WIT_CHUNK_SAMPLES)) break;

    size_t byteLen = WIT_CHUNK_SAMPLES * sizeof(int16_t);

#ifdef FALLBACK_ENABLED
    // Mirror to the backup buffer, regardless of whether the stream is healthy or not.
    if (audioBuffer && (samplesRecorded + WIT_CHUNK_SAMPLES <= maxSamples))
    {
      memcpy(audioBuffer + samplesRecorded, chunk, byteLen);
    }
    samplesRecorded += WIT_CHUNK_SAMPLES;
#endif

    // --- Silence detection: attempt to cut off BEFORE Wit.ai --------
    int16_t amplitude = averageAmplitude(chunk, WIT_CHUNK_SAMPLES);
    if (amplitude >= SILENCE_AMPLITUDE_THRESHOLD)
    {
      hasSpokenSpeech = true;
      silenceStartTime = 0; // We're still talking; we're calling off the countdown.
    }
    else if (hasSpokenSpeech)
    {
      if (silenceStartTime == 0) silenceStartTime = millis();
      else if ((millis() - silenceStartTime) >= SILENCE_DURATION_MS)
      {
        Serial.println("[Wit] Fin de parole détectée (silence côté client), on termine proprement");
        break; // we exit the loop ourselves, before Wit.ai
      }
    }

    // Only attempt the live stream while it is healthy.
    if (!streamBroken)
    {
      char chunkSizeStr[16];
      int sizeLen = snprintf(chunkSizeStr, sizeof(chunkSizeStr), "%X\r\n", (unsigned int)byteLen);

      bool success = true;
      success &= robustWrite(client, (uint8_t*)chunkSizeStr, sizeLen);
      success &= robustWrite(client, (uint8_t*)chunk, byteLen);
      success &= robustWrite(client, (const uint8_t*)"\r\n", 2);

      if (!success)
      {
        Serial.println("[Wit] Déconnexion pendant l'écriture (fin de parole détectée côté serveur)");
        streamBroken = true;
        client.stop(); // stop trying; recording continues anyway
      }
    }

    yield();
  }

  MakeSound(SOUND_ATTENTION, 2);

  String finalResult;

  if (!streamBroken)
  {
    // Happy path: finished sending before Wit.ai cuts off.
    if (client.connected())
    {
      robustWrite(client, (const uint8_t*)"0\r\n\r\n", 5);
    }

    Serial.println("[Wit] Processing");

    int httpStatusCode = 0;
    finalResult = readWitResponse(client, httpStatusCode);
    client.stop();

#ifdef FALLBACK_ENABLED
    if (httpStatusCode == 0 && audioBuffer && samplesRecorded > 0)
    {
      // Le flux s'est terminé mais sans réponse exploitable : on récupère
      // via le buffer qu'on a mirroré depuis le début.
      Serial.println("[Wit] Réponse vide malgré une fin de flux propre, on tente le filet de secours");
      finalResult = sendBufferToWit(audioBuffer, samplesRecorded);
    }
#endif

  }

#ifdef FALLBACK_ENABLED
  else if (audioBuffer && samplesRecorded > 0)
  {
    // Wit.ai (ou le réseau) a coupé le flux en premier — on renvoie tout ce
    // qui a été capté jusque-là en une seule requête.
    finalResult = sendBufferToWit(audioBuffer, samplesRecorded);
  }
#endif

  else
  {
    Serial.println("[Wit] Stream cut off and no backup buffer available");

    finalResult = "";
  }

  if (audioBuffer) free(audioBuffer);

  audioSuspend();  // Suspend hardware peripheral to save critical battery capacity

  if (finalResult.length() > 0)
  {
    Serial.printf("[Wit] Final: %s\n", finalResult.c_str());
  }
  else
  {
    Serial.println("[Wit] No speech detected");
    return;
  }

  // Update lists and execute detected intents
  simpleNoteRefresh();
  ParsedCommand cmd = parseIntent(finalResult.c_str());

  if (cmd.intent == Intent::ADD_ITEM)
  {
    if (cmd.itemName.length() > 0)
    {
        if (addItemList(cmd.itemName))
        {
          Serial.println("[Wit] Item added");
          UpdateListToSN();
        }
    }
  }
  else if (cmd.intent == Intent::REMOVE_ITEM)
  {
    if (cmd.itemName.length() > 0)
    {
      if (removeItemList(cmd.itemName))
      {
          Serial.println("[Wit] Item found");
          UpdateListToSN();
      }
    }
  }
  else if (cmd.intent == Intent::CLEAR_LIST)
  {
      ClearList();
      UpdateListToSN();
  }
  else if (cmd.intent == Intent::UPDATE_LIST)
  {
     // Handled inherently by simpleNoteRefresh() call above
  }
  else
  {
    Serial.println("[Wit] Intent not recognized");
  }
}
