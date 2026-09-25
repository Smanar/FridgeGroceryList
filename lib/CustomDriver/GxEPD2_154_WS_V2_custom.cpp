// Display Library for SPI e-paper panels from Dalian Good Display and boards from Waveshare.
// Requires HW SPI and Adafruit_GFX. Caution: the e-paper panels require 3.3V supply AND data lines!
//
// Modified variant of GxEPD2_154_D67 (Author: Jean-Marc Zingg, https://github.com/ZinggJM/GxEPD2)
// adapted with the Waveshare-correct init sequence, custom waveform LUT, driving voltages,
// and update commands, ported from the official Waveshare Arduino demo
// (epd1in54_V2.cpp/h, MIT licensed, https://github.com/waveshare/e-Paper).
//
// Key differences from stock GxEPD2_154_D67:
//   - _InitDisplay(): data entry mode 0x01->0x03 write explicit, border 0x05->0x01,
//     loads temperature+waveform (0x22=0xB1) before loading the custom full LUT
//   - _Update_Full(): command 0xC7 (use loaded LUT) instead of 0xF7 (built-in LUT)
//   - _Update_Part(): command 0xCF (use loaded LUT) instead of 0xFC (built-in LUT)
//   - New _InitPartial()/_InitFullFromPartial(): load the matching waveform LUT
//     (WF_Full_1IN54 / WF_PARTIAL_1IN54_0) and border waveform before each refresh
//     mode switch, matching Waveshare's SetFrameMemoryPartial()/HDirInit() behavior

// file made with changes from https://github.com/waveshareteam/e-Paper/blob/master/Arduino/epd1in54_V2/epd1in54_V2.cpp
// and added to https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_154_D67.cpp
// To make the custom driver


#include "GxEPD2_154_WS_V2_custom.h"

// Waveform LUTs from the official Waveshare demo (epd1in54_V2.cpp), MIT licensed.
// Bytes 0-152: waveform phases | 153: gate voltage | 154: source voltage
// 155-157: VCOM register | 158: VCOM voltage
static const uint8_t WF_Full_1IN54[159] PROGMEM =
{
  0x80, 0x48, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x40, 0x48, 0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x80, 0x48, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x40, 0x48, 0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0xA,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x8,  0x1,  0x0,  0x8,  0x1,  0x0,  0x2,
  0xA,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0,  0x0,  0x0,
  0x22, 0x17, 0x41, 0x0,  0x32, 0x20
};

static const uint8_t WF_PARTIAL_1IN54_0[159] PROGMEM =
{
  0x0,  0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x80, 0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x40, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0xF,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x1,  0x1,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0,  0x0,  0x0,
  0x02, 0x17, 0x41, 0xB0, 0x32, 0x28
};

GxEPD2_154_WS_V2::GxEPD2_154_WS_V2(int16_t cs, int16_t dc, int16_t rst, int16_t busy) :
  GxEPD2_EPD(cs, dc, rst, busy, HIGH, 10000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate)
{
}

void GxEPD2_154_WS_V2::clearScreen(uint8_t value)
{
  _writeScreenBuffer(0x26, value); // set previous
  _writeScreenBuffer(0x24, value); // set current
  refresh(false); // full refresh
  _initial_write = false;
}

void GxEPD2_154_WS_V2::writeScreenBuffer(uint8_t value)
{
  if (_initial_write) return clearScreen(value);
  _writeScreenBuffer(0x24, value);
}

void GxEPD2_154_WS_V2::writeScreenBufferAgain(uint8_t value)
{
  _writeScreenBuffer(0x24, value);
  _writeScreenBuffer(0x26, value);
}

void GxEPD2_154_WS_V2::_writeScreenBuffer(uint8_t command, uint8_t value)
{
  if (!_init_display_done) _InitDisplay();
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(command);
  _startTransfer();
  for (uint32_t i = 0; i < uint32_t(WIDTH) * uint32_t(HEIGHT) / 8; i++)
  {
    _transfer(value);
  }
  _endTransfer();
}

void GxEPD2_154_WS_V2::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x26, bitmap, x, y, w, h, invert, mirror_y, pgm);
  _writeImage(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x26, bitmap, x, y, w, h, invert, mirror_y, pgm);
  _writeImage(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x26, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::_writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  delay(1);
  int16_t wb = (w + 7) / 8;
  x -= x % 8;
  w = wb * 8;
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  if (!_init_display_done) _InitDisplay();
  if (_initial_write) writeScreenBuffer();
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      int16_t idx = mirror_y ? j + dx / 8 + ((h - 1 - (i + dy))) * wb : j + dx / 8 + (i + dy) * wb;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  delay(1);
}

void GxEPD2_154_WS_V2::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x24, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x26, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  _writeImagePart(0x24, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::writeImagePartToPrevious(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x26, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::_writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                     int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  delay(1);
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return;
  if ((x_part < 0) || (x_part >= w_bitmap)) return;
  if ((y_part < 0) || (y_part >= h_bitmap)) return;
  int16_t wb_bitmap = (w_bitmap + 7) / 8;
  x_part -= x_part % 8;
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w;
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h;
  x -= x % 8;
  w = 8 * ((w + 7) / 8);
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  if (!_init_display_done) _InitDisplay();
  if (_initial_write) writeScreenBuffer();
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      int16_t idx = mirror_y ? x_part / 8 + j + dx / 8 + ((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap : x_part / 8 + j + dx / 8 + (y_part + i + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  delay(1);
}

void GxEPD2_154_WS_V2::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) writeImage(black, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) writeImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (data1) writeImage(data1, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
  writeImageAgain(bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                   int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
  writeImagePartAgain(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) drawImage(black, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                   int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) drawImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_WS_V2::drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (data1) drawImage(data1, x, y, w, h, invert, mirror_y, pgm);
}

// ============================================================================
// Refresh — now switches the loaded waveform LUT depending on full vs partial
// ============================================================================

void GxEPD2_154_WS_V2::refresh(bool partial_update_mode)
{
  if (partial_update_mode) refresh(0, 0, WIDTH, HEIGHT);
  else
  {
    if (_using_partial_mode) _InitFullFromPartial();
    _Update_Full();
    _initial_refresh = false;
  }
}

void GxEPD2_154_WS_V2::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
  if (_initial_refresh) return refresh(false); // first refresh must be full
  if (!_using_partial_mode) _InitPartial();
  int16_t w1 = x < 0 ? w + x : w;
  int16_t h1 = y < 0 ? h + y : h;
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  w1 = x1 + w1 < int16_t(WIDTH) ? w1 : int16_t(WIDTH) - x1;
  h1 = y1 + h1 < int16_t(HEIGHT) ? h1 : int16_t(HEIGHT) - y1;
  if ((w1 <= 0) || (h1 <= 0)) return;
  w1 += x1 % 8;
  if (w1 % 8 > 0) w1 += 8 - w1 % 8;
  x1 -= x1 % 8;
  _setPartialRamArea(x1, y1, w1, h1);
  _Update_Part();
}

void GxEPD2_154_WS_V2::powerOff()
{
  _PowerOff();
}

void GxEPD2_154_WS_V2::hibernate()
{
  _PowerOff();
  if (_rst >= 0)
  {
    _writeCommand(0x10); // deep sleep mode
    _writeData(0x1);
    _hibernating = true;
    _init_display_done = false;
  }
}

void GxEPD2_154_WS_V2::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  _writeCommand(0x11); // set ram entry mode
  _writeData(0x03);    // x increase, y increase (matches Waveshare LDirInit / GxEPD2 orientation)
  _writeCommand(0x44);
  _writeData(x / 8);
  _writeData((x + w - 1) / 8);
  _writeCommand(0x45);
  _writeData(y % 256);
  _writeData(y / 256);
  _writeData((y + h - 1) % 256);
  _writeData((y + h - 1) / 256);
  _writeCommand(0x4e);
  _writeData(x / 8);
  _writeCommand(0x4f);
  _writeData(y % 256);
  _writeData(y / 256);
}

void GxEPD2_154_WS_V2::_PowerOn()
{
  if (!_power_is_on)
  {
    _writeCommand(0x22);
    _writeData(0xe0);
    _writeCommand(0x20);
    _waitWhileBusy("_PowerOn", power_on_time);
  }
  _power_is_on = true;
}

void GxEPD2_154_WS_V2::_PowerOff()
{
  if (_power_is_on)
  {
    _writeCommand(0x22);
    _writeData(0x83);
    _writeCommand(0x20);
    _waitWhileBusy("_PowerOff", power_off_time);
  }
  _power_is_on = false;
  _using_partial_mode = false;
}

// ============================================================================
// Init — ported from Waveshare epd1in54_V2.cpp LDirInit()
// ============================================================================

void GxEPD2_154_WS_V2::_InitDisplay()
{
  if (_hibernating) _reset();
  else _reset();
  _waitWhileBusy("_reset", 200);

  _writeCommand(0x12); // SWRESET
  _waitWhileBusy("_swreset", 200);

  _writeCommand(0x01); // driver output control
  _writeData(0xC7);
  _writeData(0x00);
  _writeData(0x00); // GD=0, matches GxEPD2 top-left origin

  _writeCommand(0x11); // data entry mode
  _writeData(0x03);    // x increase, y increase

  _setPartialRamArea(0, 0, WIDTH, HEIGHT);

  _writeCommand(0x3C); // border waveform (Waveshare value, was 0x05 in stock driver)
  _writeData(0x01);

  _writeCommand(0x18); // temperature sensor: internal
  _writeData(0x80);

  _writeCommand(0x22); // load temperature value + built-in waveform, needed before first LUT load
  _writeData(0xB1);
  _writeCommand(0x20);
  _waitWhileBusy("_InitDisplay_loadTemp", 200);

  _setPartialRamArea(0, 0, WIDTH, HEIGHT); // reset RAM pointer to (0,0) after the trigger above

  _SetLut(WF_Full_1IN54); // load Waveshare's full-refresh waveform + driving voltages

  _using_partial_mode = false;
  _init_display_done = true;
}

// ============================================================================
// Waveform LUT loading — ported from Waveshare Lut()/SetLut()
// ============================================================================

void GxEPD2_154_WS_V2::_writeLut(const uint8_t* lut)
{
  _writeCommand(0x32);
  for (uint8_t i = 0; i < 153; i++)
  {
    _writeData(pgm_read_byte(&lut[i]));
  }
  _waitWhileBusy("_writeLut", 200);
}

void GxEPD2_154_WS_V2::_SetLut(const uint8_t* lut)
{
  _writeLut(lut);

  _writeCommand(0x3F); // gate driving voltage
  _writeData(pgm_read_byte(&lut[153]));

  _writeCommand(0x03); // source driving voltage
  _writeData(pgm_read_byte(&lut[154]));

  _writeCommand(0x04); // VCOM register
  _writeData(pgm_read_byte(&lut[155]));
  _writeData(pgm_read_byte(&lut[156]));
  _writeData(pgm_read_byte(&lut[157]));

  _writeCommand(0x2C); // VCOM voltage
  _writeData(pgm_read_byte(&lut[158]));
}

// ============================================================================
// Partial <-> full waveform mode switching — ported from Waveshare
// SetFrameMemoryPartial() (partial) and HDirInit()/LDirInit() (full)
// ============================================================================

void GxEPD2_154_WS_V2::_InitPartial()
{
  _SetLut(WF_PARTIAL_1IN54_0);

  _writeCommand(0x37); // display update control (VBD register), Waveshare partial-mode value
  _writeData(0x00);
  _writeData(0x00);
  _writeData(0x00);
  _writeData(0x00);
  _writeData(0x00);
  _writeData(0x40);
  _writeData(0x00);
  _writeData(0x00);
  _writeData(0x00);
  _writeData(0x00);

  _writeCommand(0x3C); // border waveform for partial mode
  _writeData(0x80);

  _writeCommand(0x22); // apply the loaded LUT/VBD settings
  _writeData(0xC0);
  _writeCommand(0x20);
  _waitWhileBusy("_InitPartial", 200);

  _using_partial_mode = true;
}

void GxEPD2_154_WS_V2::_InitFullFromPartial()
{
  _SetLut(WF_Full_1IN54);

  _writeCommand(0x3C); // border waveform back to full-refresh value
  _writeData(0x01);

  _using_partial_mode = false;
}

// ============================================================================
// Display update — Waveshare DisplayFrame() / DisplayPartFrame()
// ============================================================================

void GxEPD2_154_WS_V2::_Update_Full()
{
  _writeCommand(0x22);
  _writeData(0xC7); // use loaded LUT (was 0xF7 = built-in LUT in stock driver)
  _writeCommand(0x20);
  _waitWhileBusy("_Update_Full", full_refresh_time);
  _power_is_on = false;
}

void GxEPD2_154_WS_V2::_Update_Part()
{
  _writeCommand(0x22);
  _writeData(0xCF); // use loaded LUT (was 0xFC = built-in LUT in stock driver)
  _writeCommand(0x20);
  _waitWhileBusy("_Update_Part", partial_refresh_time);
  _power_is_on = true;
}
