// Panel: HINK-E042A13 current film, 4.2" 400x300 BW. Controller: SSD1683.
// Same flex as A0, different waveform. Full refresh uses the internal-temperature
// OTP. The 110C fast OTP leaves black specks on this film. Border is Hi-Z during
// full refresh so it does not flash with the OTP waveform; during partial it
// follows the white-to-white row.
// Partial writes the 70-byte head over the OTP LUT; bytes past 83 must stay on
// OTP or partials stop drawing. The whole panel scans during a partial, so the
// DC VCOM biases every unchanged pixel. OTP VCOM speckles white; -0.2V fades
// black. Partial VCOM sits between. Full refresh reloads the OTP VCOM.
// Unchanged white pixels and the border get a short VSL pulse, then unchanged
// black pixels get a short VSH1 pulse so they do not fade. The pulses run in
// separate groups, and black goes last so stroke edges end on a black push.
// Do not use 0xFC.

#ifndef _GxEPD2_420_HinkE042A13B0_H_
#define _GxEPD2_420_HinkE042A13B0_H_

#include <GxEPD2_EPD.h>

#define EPD_BORDER_HIZ 0xC0
#define EPD_BORDER_FOLLOW_WW 0x03
#define EPD_UPDATE_FULL 0xF7
#define EPD_UPDATE_PART 0xC7
#define EPD_LUT_PARTIAL_BYTES 70
#define EPD_LUT_ACTIVE_BYTES 84
#define EPD_VCOM_PARTIAL 0x30
#define EPD_LUT_PARTIAL_PHASE_A 0x80
#define EPD_LUT_PARTIAL_PHASE_B 0x40
#define EPD_LUT_PARTIAL_REPEAT_A 0x80
#define EPD_LUT_PARTIAL_REPEAT_B 0x40
#define EPD_LUT_PARTIAL_REPEAT_C 0x08
#define EPD_LUT_BLACK_HOLD_PHASE 0x40
#define EPD_LUT_BLACK_HOLD_FRAMES 0x04
#define EPD_LUT_WHITE_HOLD_PHASE 0x80
#define EPD_LUT_WHITE_HOLD_FRAMES 0x04

class GxEPD2_420_HinkE042A13B0 : public GxEPD2_EPD
{
public:
    static const uint16_t WIDTH = 400;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 300;
    static const GxEPD2::Panel panel = GxEPD2::GDEY042T81;
    static const bool hasColor = false;
    static const bool hasPartialUpdate = true;
    static const bool hasFastPartialUpdate = true;
    static const bool useFastFullUpdate = false;
    static const uint16_t power_on_time = 100;
    static const uint16_t power_off_time = 300;
    static const uint16_t full_refresh_time = 1200;
    static const uint16_t partial_refresh_time = 350;

    GxEPD2_420_HinkE042A13B0(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    void clearScreen(uint8_t value = 0xFF);
    void writeScreenBuffer(uint8_t value = 0xFF);
    void writeScreenBufferAgain(uint8_t value = 0xFF);

    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
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
    void selectFastFullUpdate(bool);

private:
    void _writeScreenBuffer(uint8_t command, uint8_t value);
    void _writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Update_Full();
    void _Update_Part();

private:
    bool _use_fast_update;
};

#endif
