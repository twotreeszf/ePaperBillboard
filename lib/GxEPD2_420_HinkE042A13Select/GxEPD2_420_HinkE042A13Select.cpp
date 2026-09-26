#include "GxEPD2_420_HinkE042A13Select.h"

#define EPD_HINK_DISPATCH(call) \
  do { if (_variant == EPD_HINK_E042A13_B0) _b0.call; else _a0.call; } while (0)

GxEPD2_420_HinkE042A13Select::GxEPD2_420_HinkE042A13Select(int16_t cs, int16_t dc, int16_t rst, int16_t busy) :
  _a0(cs, dc, rst, busy), _b0(cs, dc, rst, busy), _variant(EPD_HINK_E042A13_A0)
{
}

void GxEPD2_420_HinkE042A13Select::selectPanel(uint8_t variant)
{
  _variant = variant == EPD_HINK_E042A13_B0 ? EPD_HINK_E042A13_B0 : EPD_HINK_E042A13_A0;
}

GxEPD2_EPD& GxEPD2_420_HinkE042A13Select::_active()
{
  if (_variant == EPD_HINK_E042A13_B0) return _b0;
  return _a0;
}

void GxEPD2_420_HinkE042A13Select::init(uint32_t serial_diag_bitrate)
{
  _active().init(serial_diag_bitrate);
}

void GxEPD2_420_HinkE042A13Select::init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration, bool pulldown_rst_mode)
{
  _active().init(serial_diag_bitrate, initial, reset_duration, pulldown_rst_mode);
}

void GxEPD2_420_HinkE042A13Select::end()
{
  _active().end();
}

void GxEPD2_420_HinkE042A13Select::selectSPI(SPIClass& spi, SPISettings spi_settings)
{
  _active().selectSPI(spi, spi_settings);
}

void GxEPD2_420_HinkE042A13Select::clearScreen(uint8_t value)
{
  _active().clearScreen(value);
}

void GxEPD2_420_HinkE042A13Select::writeScreenBuffer(uint8_t value)
{
  _active().writeScreenBuffer(value);
}

void GxEPD2_420_HinkE042A13Select::writeScreenBufferAgain(uint8_t value)
{
  _active().writeScreenBufferAgain(value);
}

void GxEPD2_420_HinkE042A13Select::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _active().writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_420_HinkE042A13Select::writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _active().writeImageForFullRefresh(bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_420_HinkE042A13Select::writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _active().writeImageToPrevious(bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_420_HinkE042A13Select::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _active().writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_420_HinkE042A13Select::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  EPD_HINK_DISPATCH(writeImage(black, color, x, y, w, h, invert, mirror_y, pgm));
}

void GxEPD2_420_HinkE042A13Select::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  EPD_HINK_DISPATCH(writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm));
}

void GxEPD2_420_HinkE042A13Select::writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _active().writeImageAgain(bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_420_HinkE042A13Select::writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _active().writeImagePartAgain(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_420_HinkE042A13Select::writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  EPD_HINK_DISPATCH(writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm));
}

void GxEPD2_420_HinkE042A13Select::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  EPD_HINK_DISPATCH(drawImage(bitmap, x, y, w, h, invert, mirror_y, pgm));
}

void GxEPD2_420_HinkE042A13Select::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  EPD_HINK_DISPATCH(drawImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm));
}

void GxEPD2_420_HinkE042A13Select::drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  EPD_HINK_DISPATCH(drawImage(black, color, x, y, w, h, invert, mirror_y, pgm));
}

void GxEPD2_420_HinkE042A13Select::drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  EPD_HINK_DISPATCH(drawImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm));
}

void GxEPD2_420_HinkE042A13Select::drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  EPD_HINK_DISPATCH(drawNative(data1, data2, x, y, w, h, invert, mirror_y, pgm));
}

void GxEPD2_420_HinkE042A13Select::refresh(bool partial_update_mode)
{
  _active().refresh(partial_update_mode);
}

void GxEPD2_420_HinkE042A13Select::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
  _active().refresh(x, y, w, h);
}

void GxEPD2_420_HinkE042A13Select::powerOff()
{
  _active().powerOff();
}

void GxEPD2_420_HinkE042A13Select::hibernate()
{
  _active().hibernate();
}

void GxEPD2_420_HinkE042A13Select::selectFastFullUpdate(bool ff)
{
  _active().selectFastFullUpdate(ff);
}
