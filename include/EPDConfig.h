#pragma once

/**
 * E-Paper panel selection. Define exactly one of EPD_PANEL_HINK_E029A01_A1, EPD_PANEL_HINK_E042A13, etc.
 * Add new panels by adding an #elif defined(EPD_PANEL_XXX) block.
 */
// #define EPD_PANEL_HINK_E042A13_A0
#define EPD_PANEL_HINK_E029A01_A1

#include <GxEPD2_BW.h>

#if defined(EPD_PANEL_HINK_E029A01_A1)
#define EPD_WIDTH   296
#define EPD_HEIGHT  128
#define EPD_ROTATION  3
#define EPD_DRIVER_CLASS  GxEPD2_290
using EPaperDisplay = GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT>;

#elif defined(EPD_PANEL_HINK_E042A13_A0)
#include <GxEPD2_420_HinkE042A13.h>
#define EPD_WIDTH   400
#define EPD_HEIGHT  300
#define EPD_ROTATION  0
#define EPD_DRIVER_CLASS  GxEPD2_420_HinkE042A13
using EPaperDisplay = GxEPD2_BW<GxEPD2_420_HinkE042A13, GxEPD2_420_HinkE042A13::HEIGHT>;

#else
#error "EPDConfig.h: define exactly one of EPD_PANEL_290, EPD_PANEL_HINK_E042A13"
#endif

#define EPD_BUF_SIZE ((EPD_WIDTH * EPD_HEIGHT / 8) + 8)
#define EPD_FULL_REFRESH_INTERVAL 32
