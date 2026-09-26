#pragma once

#include "TTFile.h"
#include "TTStreamImage.h"
#include <stdint.h>

#define TT_PIC_URL_BASE          "https://epaper-board.tos-cn-beijing.volces.com/Comics/"
#define TT_PIC_MANIFEST_URL      TT_PIC_URL_BASE "Manifest.json"
#define TT_PIC_MANIFEST_MAX      (8 * 1024)
#define TT_PIC_IMAGE_MAX         (32 * 1024)
#define TT_PIC_HTTP_PATH         TT_FS_TMP_DIR "/pic_http"
#define TT_PIC_SERIES_MAX        24
#define TT_PIC_NAME_MAX          48
#define TT_PIC_PINYIN_MAX        32
#define TT_PIC_MSG_MAX           48
#define TT_PIC_DAY_SWITCH_MS     0
#define PREF_PIC_PINYIN          "pic_pinyin"
#define PREF_PIC_INDEX           "pic_index"
#define PREF_PIC_DAY             "pic_day"
#define PREF_PIC_PATH            "pic_path"
#define PREF_PIC_NAME            "pic_name"

enum TTPicState {
    TT_PIC_FETCHING = 0,
    TT_PIC_OK,
    TT_PIC_FAILED,
};

struct TTPicSeries {
    char name[TT_PIC_NAME_MAX];
    char pinyin[TT_PIC_PINYIN_MAX];
    uint16_t count;
};

struct TTPicPayload {
    TTPicState state;
    char message[TT_PIC_MSG_MAX];
    char path[TT_STREAM_IMAGE_PATH_MAX];
    char name[TT_PIC_NAME_MAX];
};
