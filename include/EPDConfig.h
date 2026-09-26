#pragma once

/**
 * E-Paper panel selection. Define exactly one panel macro.
 * Add new panels by adding an #elif defined(EPD_PANEL_XXX) block.
 *
 * Must match the physical panel. A wrong driver applies a different waveform,
 * scan count, and internal voltage; brief tests usually only look wrong, but
 * leaving a mismatch running can ghost or damage the panel.
 */
#define EPD_PANEL_HINK_E042A13
// #define EPD_PANEL_HINK_E029A01_A1

#include <GxEPD2_BW.h>

#if defined(EPD_PANEL_HINK_E029A01_A1)
#define EPD_WIDTH   296
#define EPD_HEIGHT  128
#define EPD_ROTATION  3
#define EPD_DRIVER_CLASS  GxEPD2_290
using EPaperDisplay = GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT>;

#elif defined(EPD_PANEL_HINK_E042A13)
#include <GxEPD2_420_HinkE042A13Select.h>
#define EPD_WIDTH   400
#define EPD_HEIGHT  300
#define EPD_ROTATION  0
#define EPD_DRIVER_CLASS  GxEPD2_420_HinkE042A13Select
#define EPD_PANEL_SELECTABLE
using EPaperDisplay = GxEPD2_BW<GxEPD2_420_HinkE042A13Select, GxEPD2_420_HinkE042A13Select::HEIGHT>;

#else
#error "EPDConfig.h: define exactly one EPD_PANEL_* macro"
#endif

#define EPD_BUF_SIZE ((EPD_WIDTH * EPD_HEIGHT / 8) + 8)
#define EPD_FULL_REFRESH_INTERVAL 64

#define PREF_EPD_PANEL "epd_panel"
#define TT_EPD_PANEL_A0 "a0"
#define TT_EPD_PANEL_B0 "b0"
#define TT_EPD_PANEL_DEFAULT TT_EPD_PANEL_A0
