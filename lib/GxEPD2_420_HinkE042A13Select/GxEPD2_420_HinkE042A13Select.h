// HINK-E042A13 4.2" 400x300 BW with a runtime waveform choice.
// A0 and B0 share the flex marking but need different drivers; the choice must
// be made before init() and stays fixed until the next boot.

#ifndef _GxEPD2_420_HinkE042A13Select_H_
#define _GxEPD2_420_HinkE042A13Select_H_

#include <GxEPD2_420_HinkE042A13.h>
#include <GxEPD2_420_HinkE042A13B0.h>

#define EPD_HINK_E042A13_A0 0
#define EPD_HINK_E042A13_B0 1

class GxEPD2_420_HinkE042A13Select
{
public:
    static const uint16_t WIDTH = GxEPD2_420_HinkE042A13::WIDTH;
    static const uint16_t WIDTH_VISIBLE = GxEPD2_420_HinkE042A13::WIDTH_VISIBLE;
    static const uint16_t HEIGHT = GxEPD2_420_HinkE042A13::HEIGHT;
    static const GxEPD2::Panel panel = GxEPD2_420_HinkE042A13::panel;
    static const bool hasColor = GxEPD2_420_HinkE042A13::hasColor;
    static const bool hasPartialUpdate = GxEPD2_420_HinkE042A13::hasPartialUpdate;
    static const bool hasFastPartialUpdate = GxEPD2_420_HinkE042A13::hasFastPartialUpdate;

    GxEPD2_420_HinkE042A13Select(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    void selectPanel(uint8_t variant);
    uint8_t selectedPanel() const { return _variant; }

    void init(uint32_t serial_diag_bitrate = 0);
    void init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration = 10, bool pulldown_rst_mode = false);
    void end();
    void selectSPI(SPIClass& spi, SPISettings spi_settings);

    void clearScreen(uint8_t value = 0xFF);
    void writeScreenBuffer(uint8_t value = 0xFF);
    void writeScreenBufferAgain(uint8_t value = 0xFF);

    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                             int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);

    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);

    void refresh(bool partial_update_mode = false);
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h);
    void powerOff();
    void hibernate();
    void selectFastFullUpdate(bool ff);

private:
    GxEPD2_EPD& _active();

    GxEPD2_420_HinkE042A13 _a0;
    GxEPD2_420_HinkE042A13B0 _b0;
    uint8_t _variant;
};

#endif
