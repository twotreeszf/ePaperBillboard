#include "TTAboutPage.h"
#include "../Base/TTFirmwareVersion.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTQrCode.h"
#include "../Base/Logger.h"
#include <cstdio>

static void styleText(lv_obj_t* label, lv_font_t* font, lv_text_align_t align) {
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, align, 0);
}

lv_obj_t* TTAboutPage::makePanel(lv_obj_t* parent) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_hidden(panel, true);
    return panel;
}

static int32_t scaledGap(int32_t lineHeight) {
    const int32_t pitch = (lineHeight * TT_ABOUT_LINE_NUM + TT_ABOUT_LINE_DEN / 2) / TT_ABOUT_LINE_DEN;
    const int32_t gap = pitch - lineHeight;
    return gap > 0 ? gap : 0;
}

lv_obj_t* TTAboutPage::addLine(lv_obj_t* parent, const char* title, const char* value,
                               lv_font_t* titleFont, lv_font_t* valueFont, int32_t lineGap) {
    lv_obj_t* block = lv_obj_create(parent);
    lv_obj_set_width(block, lv_pct(100));
    lv_obj_set_height(block, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(block, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(block, 0, 0);
    lv_obj_set_style_pad_all(block, 0, 0);
    lv_obj_set_style_radius(block, 0, 0);
    lv_obj_set_layout(block, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(block, lineGap, 0);
    lv_obj_set_scrollable(block, false);

    lv_obj_t* titleLabel = lv_label_create(block);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_width(titleLabel, lv_pct(100));
    styleText(titleLabel, titleFont, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_line_space(titleLabel, scaledGap(lv_font_get_line_height(titleFont)), 0);

    lv_obj_t* valueLabel = lv_label_create(block);
    lv_label_set_text(valueLabel, value);
    lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(valueLabel, lv_pct(100));
    styleText(valueLabel, valueFont, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_line_space(valueLabel, scaledGap(lv_font_get_line_height(valueFont)), 0);
    return block;
}

void TTAboutPage::placeQr(lv_obj_t* panel, const char* title, const char* caption, const char* payload,
                          lv_font_t* titleFont, lv_font_t* captionFont) {
    lv_obj_t* column = lv_obj_create(panel);
    lv_obj_set_width(column, TT_ABOUT_BODY_W);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(column, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(column, 0, 0);
    lv_obj_set_style_pad_all(column, 0, 0);
    lv_obj_set_style_radius(column, 0, 0);
    lv_obj_set_layout(column, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, TT_ABOUT_QR_GAP, 0);
    lv_obj_set_scrollable(column, false);
    lv_obj_align(column, LV_ALIGN_TOP_MID, 0, TT_ABOUT_QR_TOP);

    lv_obj_t* titleLabel = lv_label_create(column);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_width(titleLabel, lv_pct(100));
    styleText(titleLabel, titleFont, LV_TEXT_ALIGN_CENTER);

    lv_obj_t* qr = TTQrCode::create(column, TT_ABOUT_QR_VERSION);
    if (TTQrCode::setText(qr, payload)) {
        lv_obj_set_hidden(qr, false);
    } else {
        lv_obj_t* failed = lv_label_create(column);
        lv_label_set_text(failed, "二维码生成失败");
        styleText(failed, captionFont, LV_TEXT_ALIGN_CENTER);
        LOG_E("About: QR failed title=%s", title);
    }

    lv_obj_t* captionLabel = lv_label_create(column);
    lv_label_set_text(captionLabel, caption);
    lv_label_set_long_mode(captionLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(captionLabel, lv_pct(100));
    styleText(captionLabel, captionFont, LV_TEXT_ALIGN_CENTER);
}

void TTAboutPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* fontTitle = fm.getFont(TT_ABOUT_TITLE_FONT);
    lv_font_t* fontBody = fm.getFont(TT_ABOUT_BODY_FONT);
    const int32_t titleH = lv_font_get_line_height(fontTitle);
    const int32_t linePitch = (titleH * TT_ABOUT_LINE_NUM + TT_ABOUT_LINE_DEN / 2) / TT_ABOUT_LINE_DEN;
    const int32_t lineGap = linePitch > titleH ? linePitch - titleH : 0;
    const int32_t paraGap = linePitch * TT_ABOUT_PARA_TIMES;
    LOG_I("About: titleH=%d lineGap=%d paraGap=%d", (int)titleH, (int)lineGap, (int)paraGap);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    createGroup();

    _panels[0] = makePanel(screen);
    lv_obj_t* info = lv_obj_create(_panels[0]);
    lv_obj_set_width(info, TT_ABOUT_BODY_W);
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_pad_all(info, 0, 0);
    lv_obj_set_style_radius(info, 0, 0);
    lv_obj_set_layout(info, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(info, paraGap, 0);
    lv_obj_set_scrollable(info, false);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -TT_ABOUT_INDEX_BOTTOM);
    addLine(info, "设备名称", TT_ABOUT_DEVICE_NAME, fontBody, fontTitle, lineGap);
    addLine(info, "系统版本", TT_FW_VERSION, fontBody, fontTitle, lineGap);
    addLine(info, "闲鱼店铺", TT_ABOUT_XIANYU_SHOP, fontBody, fontTitle, lineGap);

    _panels[1] = makePanel(screen);
    placeQr(_panels[1], "使用说明", "扫码查看说明书", TT_ABOUT_MANUAL_URL, fontTitle, fontBody);

    char qqCaption[48];
    snprintf(qqCaption, sizeof(qqCaption), "%s  %s", TT_ABOUT_QQ_NAME, TT_ABOUT_QQ_NUMBER);
    _panels[2] = makePanel(screen);
    placeQr(_panels[2], "QQ群", qqCaption, TT_ABOUT_QQ_URL, fontTitle, fontBody);

    _indexLabel = lv_label_create(screen);
    styleText(_indexLabel, fontBody, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(_indexLabel, LV_ALIGN_BOTTOM_MID, 0, -TT_ABOUT_INDEX_BOTTOM);
    showPage(0);
}

void TTAboutPage::showPage(int index) {
    if (index < 0 || index >= TT_ABOUT_PAGE_COUNT) {
        return;
    }
    _page = index;
    for (int i = 0; i < TT_ABOUT_PAGE_COUNT; i++) {
        if (_panels[i] != nullptr) {
            lv_obj_set_hidden(_panels[i], i != index);
        }
    }
    if (_indexLabel != nullptr) {
        char text[8];
        snprintf(text, sizeof(text), "%d/%d", index + 1, TT_ABOUT_PAGE_COUNT);
        lv_label_set_text(_indexLabel, text);
    }
    LOG_I("About: page %d/%d", index + 1, TT_ABOUT_PAGE_COUNT);
}

bool TTAboutPage::handleKeyAction(TTKeyId key, TTKeyGesture gesture) {
    if (gesture == TT_KEY_CLICK && (key == TT_KEY_LEFT || key == TT_KEY_RIGHT)) {
        const int next = _page + (key == TT_KEY_RIGHT ? 1 : -1);
        if (next >= 0 && next < TT_ABOUT_PAGE_COUNT) {
            showPage(next);
            requestRefresh(TT_REFRESH_PARTIAL);
        }
        return true;
    }
    return TTScreenPage::handleKeyAction(key, gesture);
}
