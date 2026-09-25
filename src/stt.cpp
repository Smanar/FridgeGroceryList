#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "stt.h"
#include "audio.h"
#include "algorithm.h"
#include "itemlist.h"
#include "simplenote.h"
#include "storage.h"

// https://wit.ai

#define WIT_CHUNK_SAMPLES 256   // 256 samples = 512 bytes per chunk sent
#define BOOT_BUTTON 0
#define MAX_RECORDING_TIME_MS 8000 // 8 seconds maximum recording time limit

void sendAudioToWit()
{
  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("api.wit.ai", 443))
  {
    Serial.println("[Wit] Connection failed");

    char error_buf[100];
    client.lastError(error_buf, 100);
    Serial.print("[Wit] Error: ");
    Serial.println(error_buf);
    
    audioSuspend(); // Crucial: Suspend audio peripheral even on early exit to save battery
    return;
  }
  else
  {
    Serial.println("[Wit] Connected");
  }

  char header[256];
  snprintf(header, sizeof(header),
           "POST /speech?v=20230215 HTTP/1.1\r\n"
           "Host: api.wit.ai\r\n"
           "Authorization: Bearer %s\r\n"
           "Content-Type: audio/raw;encoding=signed-integer;bits=16;rate=16000;endian=little\r\n"
           "Transfer-Encoding: chunked\r\n"
           "Connection: close\r\n\r\n",
           g_witai_api.c_str());

  client.print(header);

  // Flush stale audio samples before starting transcription stream
  audioMicFlush();  

  Serial.println("[Wit] Listening");
  MakeSound(SOUND_ATTENTION, 2);

  int16_t chunk[WIT_CHUNK_SAMPLES];
  unsigned long startRecordTime = millis();

  // Fixed: Loop condition now correctly tracks actual elapsed milliseconds instead of loose loop iterations
  while ((digitalRead(BOOT_BUTTON) == HIGH) && ((millis() - startRecordTime) < MAX_RECORDING_TIME_MS))
  {
    if (!audioMicRead(chunk, WIT_CHUNK_SAMPLES)) break;

    size_t byteLen = WIT_CHUNK_SAMPLES * sizeof(int16_t);
    client.printf("%X\r\n", (unsigned int)byteLen);
    client.write((uint8_t*)chunk, byteLen);
    client.print("\r\n");
    
    yield(); // Feed the hardware watchdog timer
  }

  // Terminate HTTP Chunked stream
  client.print("0\r\n\r\n");

  MakeSound(SOUND_ATTENTION, 2);
  
  Serial.println("[Wit] Processing");

  String finalResult = "";

  while (client.connected() || client.available()) 
  {
    if (client.available())
    {
      String line = client.readStringUntil('\n');

      // Look for the "text" key inside the Wit.ai JSON stream response chunk
      int textIndex = line.indexOf("\"text\": \"");
      if (textIndex != -1)
      {
        int start = textIndex + 9;
        int end = line.indexOf("\"", start);
        if (end != -1) {
            finalResult = line.substring(start, end);
        }
      }
    }
    yield(); // Avoid triggering ESP32 watchdog during server processing lag
  }
  
  client.stop();
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
