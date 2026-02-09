/**
 * LVGL Configuration for E-Paper Display
 * 
 * Optimized for:
 * - 2.9" E-Paper (296x128, monochrome)
 * - Manual refresh control (no auto-refresh timer)
 * - Minimal memory footprint
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 1 for monochrome E-Paper */
#define LV_COLOR_DEPTH 1

/* Swap the 2 bytes of RGB565 color. Not used for 1-bit */
#define LV_COLOR_16_SWAP 0

/*====================
   MEMORY SETTINGS
 *====================*/

/* Size of the memory available for `lv_malloc()` in bytes (>= 2kB) */
#define LV_MEM_SIZE (32 * 1024U)

/* Use the standard `malloc` and `free` from C library */
#define LV_STDLIB_INCLUDE <stdlib.h>
#define LV_MALLOC       malloc
#define LV_REALLOC      realloc
#define LV_FREE         free
#define LV_MEMSET       memset
#define LV_MEMCPY       memcpy

/* Use the standard `memcpy` and `memset` instead of LVGL's own functions */
#define LV_MEMCPY_MEMSET_STD 1

/*====================
   HAL SETTINGS
 *====================*/

/* Default display refresh, input device read and animation step period */
#define LV_DEF_REFR_PERIOD 33

/* Default Dot Per Inch. Used to initialize default sizes such as widgets sized, style paddings */
#define LV_DPI_DEF 130

/* Default image cache size. 0 to disable caching */
#define LV_CACHE_DEF_SIZE 0

/* Default image header cache count. 0 to disable caching */
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 0

/*====================
 * FEATURE CONFIGURATION
 *====================*/

/*-------------
 * Logging
 *-----------*/

/* Enable the log module */
#define LV_USE_LOG 1
#if LV_USE_LOG
    /* Log level: LV_LOG_LEVEL_TRACE/INFO/WARN/ERROR/USER/NONE */
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    
    /* Print log with 'printf' */
    #define LV_LOG_PRINTF 1
    
    /* Set callback to print logs */
    #define LV_LOG_USE_TIMESTAMP 1
    #define LV_LOG_USE_FILE_LINE 1
#endif

/*-------------
 * Asserts
 *-----------*/

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/*-------------
 * Debug
 *-----------*/

#define LV_USE_REFR_DEBUG       0
#define LV_USE_LAYER_DEBUG      0
#define LV_USE_PARALLEL_DRAW_DEBUG 0

/*-------------
 * Others
 *-----------*/

/* Enable float type support */
#define LV_USE_FLOAT 0

/* Enable matrix support */
#define LV_USE_MATRIX 0

/*====================
 * FONT USAGE
 *====================*/

/* Montserrat fonts with ASCII range and some symbols */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* Monochrome fonts - perfect for 1bpp E-Paper displays */
#define LV_FONT_UNSCII_8  1
#define LV_FONT_UNSCII_16 1

/* Default font for text - use monochrome font for E-Paper */
#define LV_FONT_DEFAULT &lv_font_unscii_16

/* Enable binary font support for loading external fonts */
#define LV_USE_BINFONT 1

/* Enable FreeType support. Requires FreeType library */
#define LV_USE_FREETYPE 0

/*====================
 * TEXT SETTINGS
 *====================*/

/* Enable bidirectional text support */
#define LV_USE_BIDI 0

/* Enable Arabic/Persian processing */
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*====================
 * WIDGET USAGE
 *====================*/

/* Enable basic widgets - only what we need for clock */
#define LV_USE_ANIMIMG    0
#define LV_USE_ARC        0
#define LV_USE_BAR        0
#define LV_USE_BUTTON     1
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     0
#define LV_USE_CHART      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGE      0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD   0
#define LV_USE_LABEL      1  /* Required for text display */
#define LV_USE_LED        0
#define LV_USE_LINE       0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_OBJID_BUILTIN 0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_SWITCH     0
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TEXTAREA   0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/*====================
 * THEME USAGE
 *====================*/

/* Enable basic theme - mono style for E-Paper */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE  1
#define LV_USE_THEME_MONO    1

/*====================
 * LAYOUT
 *====================*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/*====================
 * ANIMATION
 *====================*/

/* Disable animations for E-Paper - they don't make sense */
#define LV_USE_ANIM 0

/*====================
 * FILE SYSTEM
 *====================*/

/* Enable file system support for loading fonts */
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_MEMFS 0

/* We'll implement custom file system callbacks for LittleFS */
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0

/*====================
 * DRAW ENGINES
 *====================*/

/* Enable software renderer */
#define LV_USE_DRAW_SW 1

// /* Disable ARM-specific optimizations (not for ESP32/Xtensa) */
// #define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE
// #define LV_USE_NATIVE_HELIUM_ASM    0

/* Enable vector graphics */
#define LV_USE_DRAW_VECTOR 0

/* Disable GPU acceleration */
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_SWM341_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0

/*====================
 * OTHER COMPONENTS
 *====================*/

/* Disable image decoders - not needed for text clock */
#define LV_USE_PNG  0
#define LV_USE_BMP  0
#define LV_USE_SJPG 0
#define LV_USE_GIF  0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0

/* File explorer */
#define LV_USE_FILE_EXPLORER 0

/* IME - Input Method Engine */
#define LV_USE_IME_PINYIN 0

/* Monkey test */
#define LV_USE_MONKEY 0

/* Snapshot */
#define LV_USE_SNAPSHOT 0

/* Observer */
#define LV_USE_OBSERVER 0

/* Grid navigation */
#define LV_USE_GRIDNAV 0

/* Fragment */
#define LV_USE_FRAGMENT 0

/* Profiler */
#define LV_USE_PROFILER 0

/* System monitor */
#define LV_USE_SYSMON 0

/* Performance monitor */
#define LV_USE_PERF_MONITOR 0

/* Memory monitor */
#define LV_USE_MEM_MONITOR 0

/*====================
 * COMPILER SETTINGS
 *====================*/

/* Big endian system */
#define LV_BIG_ENDIAN_SYSTEM 0

/* Attribute for large constant arrays */
#define LV_ATTRIBUTE_LARGE_CONST

/* Compiler prefix for a large array declaration in RAM */
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY

/* Place performance critical functions into a faster memory (e.g RAM) */
#define LV_ATTRIBUTE_FAST_MEM

/* Export integer constant to binding */
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning

/* Use DMA to speed up operations */
#define LV_ATTRIBUTE_DMA

#endif /* LV_CONF_H */
