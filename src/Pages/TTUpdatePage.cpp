#include "TTUpdatePage.h"
#include "../Base/TTFirmwareVersion.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTTextButton.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTLvglEpdDriver.h"
#include "../Base/TTNavigationBar.h"
#include "../Service/TTOtaService.h"
#include <Arduino.h>
#include <cstring>

static void styleText(lv_obj_t* label, lv_font_t* font) {
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, font, 0);
}

static lv_obj_t* addTextBlock(lv_obj_t* parent, const char* title,
                            lv_font_t* titleFont, lv_font_t* valueFont, lv_obj_t** value,
                            lv_obj_t** titleOut = nullptr) {
    lv_obj_t* block = lv_obj_create(parent);
    lv_obj_set_width(block, lv_pct(100));
    lv_obj_set_height(block, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(block, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(block, 0, 0);
    lv_obj_set_style_pad_all(block, 0, 0);
    lv_obj_set_style_radius(block, 0, 0);
    lv_obj_set_layout(block, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(block, TT_UPDATE_TITLE_GAP, 0);
    lv_obj_remove_flag(block, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(block);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_width(titleLabel, lv_pct(100));
    styleText(titleLabel, titleFont);
    if (titleOut != nullptr) {
        *titleOut = titleLabel;
    }

    *value = lv_label_create(block);
    lv_label_set_long_mode(*value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(*value, lv_pct(100));
    styleText(*value, valueFont);
    return block;
}

void TTUpdatePage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font16 = fm.getFont(16);
    lv_font_t* font12 = fm.getFont(12);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* column = lv_obj_create(screen);
    lv_obj_set_width(column, TT_UPDATE_BODY_W);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(column, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(column, 0, 0);
    lv_obj_set_style_pad_all(column, 0, 0);
    lv_obj_set_style_radius(column, 0, 0);
    lv_obj_set_layout(column, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, TT_UPDATE_SECTION_GAP, 0);
    lv_obj_remove_flag(column, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(column, LV_ALIGN_TOP_MID, 0, TT_UPDATE_PAD);

    addTextBlock(column, "当前版本", font16, font12, &_versionValue, &_versionTitle);
    lv_label_set_text(_versionValue, TT_FW_VERSION);
    addTextBlock(column, "更新说明", font16, font12, &_notesValue);
    lv_label_set_text(_notesValue, "无");

    _checkBtn = TTTextButton::create(screen, "检查更新", font16);
    lv_obj_align(_checkBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(_checkBtn, onCheckEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_checkBtn);

    _statusBox = lv_obj_create(screen);
    lv_obj_set_size(_statusBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(_statusBox, TT_UPDATE_STATUS_MAX_W, 0);
    lv_obj_set_style_bg_color(_statusBox, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_statusBox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_statusBox, lv_color_black(), 0);
    lv_obj_set_style_border_width(_statusBox, TT_UPDATE_STATUS_BORDER, 0);
    lv_obj_set_style_radius(_statusBox, 0, 0);
    lv_obj_set_style_pad_all(_statusBox, TT_UPDATE_STATUS_PAD, 0);
    lv_obj_remove_flag(_statusBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(_statusBox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(_statusBox, LV_ALIGN_CENTER, 0, TT_NAV_PAGE_INSET / 2);
    lv_obj_add_flag(_statusBox, LV_OBJ_FLAG_HIDDEN);

    _statusLabel = lv_label_create(_statusBox);
    lv_label_set_text(_statusLabel, "");
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_statusLabel, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(_statusLabel,
                               TT_UPDATE_STATUS_MAX_W - (TT_UPDATE_STATUS_PAD * 2), 0);
    lv_obj_set_style_text_align(_statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    styleText(_statusLabel, font12);
}

void TTUpdatePage::setup() {
    TTScreenPage::setup();
    subscribe<TTOtaPayload>(TT_NOTIFICATION_OTA, [this](const TTOtaPayload& payload) {
        applyOta(payload);
    });
}

void TTUpdatePage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    showInstalled();
    setStatus("");
    showCheckButton(true);
}

void TTUpdatePage::rememberTarget(const TTOtaPayload& payload) {
    strncpy(_targetVersion, payload.version, sizeof(_targetVersion) - 1);
    _targetVersion[sizeof(_targetVersion) - 1] = '\0';
    _targetNotes[0] = '\0';
    const char* text = payload.message;
    const char* firstBreak = strchr(text, '\n');
    const char* lastBreak = strrchr(text, '\n');
    if (firstBreak == nullptr || lastBreak == nullptr || firstBreak == lastBreak) {
        return;
    }
    size_t notesLen = (size_t)(lastBreak - firstBreak - 1);
    if (notesLen >= sizeof(_targetNotes)) {
        notesLen = sizeof(_targetNotes) - 1;
    }
    memcpy(_targetNotes, firstBreak + 1, notesLen);
    _targetNotes[notesLen] = '\0';
}

void TTUpdatePage::showTarget() {
    if (_versionTitle != nullptr) {
        lv_label_set_text(_versionTitle, "正在更新");
    }
    if (_versionValue != nullptr && _targetVersion[0] != '\0') {
        lv_label_set_text(_versionValue, _targetVersion);
    }
    if (_notesValue != nullptr) {
        lv_label_set_text(_notesValue, _targetNotes[0] != '\0' ? _targetNotes : "无");
    }
}

void TTUpdatePage::showInstalled() {
    char version[TT_OTA_VER_MAX];
    char notes[TT_OTA_MSG_MAX];
    TTOtaService& ota = TTInstanceOf<TTOtaService>();
    ota.readLocalVersion(version, sizeof(version));
    ota.readLocalNotes(notes, sizeof(notes));
    if (_versionTitle != nullptr) {
        lv_label_set_text(_versionTitle, "当前版本");
    }
    if (_versionValue != nullptr) {
        lv_label_set_text(_versionValue, version[0] != '\0' ? version : TT_FW_VERSION);
    }
    if (_notesValue != nullptr) {
        lv_label_set_text(_notesValue, notes[0] != '\0' ? notes : "无");
    }
}

void TTUpdatePage::willDisappear() {
    TTScreenPage::willDisappear();
    _visible = false;
    setDeepRefreshBlocked(false);
    if (_loading) {
        _loading = false;
        TTInstanceOf<TTPopupLayer>().dismissLoading();
    }
}

void TTUpdatePage::setStatus(const char* text) {
    if (_statusLabel == nullptr || _statusBox == nullptr) {
        return;
    }
    const bool show = text != nullptr && text[0] != '\0';
    lv_label_set_text(_statusLabel, show ? text : "");
    lv_obj_update_layout(_statusBox);
    lv_obj_align(_statusBox, LV_ALIGN_CENTER, 0, TT_NAV_PAGE_INSET / 2);
    if (show) {
        lv_obj_remove_flag(_statusBox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_statusBox);
    } else {
        lv_obj_add_flag(_statusBox, LV_OBJ_FLAG_HIDDEN);
    }
}

void TTUpdatePage::setDeepRefreshBlocked(bool blocked) {
    if (_deepBlocked == blocked) {
        return;
    }
    _deepBlocked = blocked;
    TTInstanceOf<TTLvglEpdDriver>().setAutoDeepRefresh(!blocked);
    LOG_I("OTA page: deep refresh %s", blocked ? "blocked" : "allowed");
}

void TTUpdatePage::setLocked(bool locked) {
    _locked = locked;
    ITTNavigationController* nav = getNavigationController();
    if (nav == nullptr || nav->getNavBar() == nullptr) {
        return;
    }
    nav->getNavBar()->show(getName(), !locked && nav->canPop());
}

void TTUpdatePage::showCheckButton(bool show) {
    if (_checkBtn == nullptr) {
        return;
    }
    if (show) {
        lv_obj_remove_flag(_checkBtn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_checkBtn, LV_OBJ_FLAG_HIDDEN);
    }
}

void TTUpdatePage::startCheck() {
    if (_loading || _locked) {
        return;
    }
    _loading = true;
    setDeepRefreshBlocked(true);
    setLocked(true);
    showCheckButton(false);
    setStatus("");
    LOG_I("OTA page: check");
    TTInstanceOf<TTPopupLayer>().showLoading("正在检查");
    TTInstanceOf<TTOtaService>().checkAsync();
}

void TTUpdatePage::startUpgrade() {
    _loading = true;
    _updateStarted = true;
    setDeepRefreshBlocked(true);
    setLocked(true);
    showCheckButton(false);
    showTarget();
    TTFontManager::instance().releaseResFonts();
    ITTNavigationController* nav = getNavigationController();
    if (nav != nullptr && nav->getNavBar() != nullptr) {
        nav->getNavBar()->suspendForUpdate();
    }
    LOG_I("OTA page: upgrade %s", _targetVersion);
    TTInstanceOf<TTPopupLayer>().showLoading(TT_OTA_UPDATING_HINT);
    TTInstanceOf<TTOtaService>().upgradeAsync();
}

void TTUpdatePage::showFinishedDialog(const char* message) {
    _loading = false;
    TTInstanceOf<TTPopupLayer>().dismissLoading();
    const char* text = (message != nullptr && message[0] != '\0') ? message : "更新完成，点击确定重启";
    LOG_I("OTA page: reboot dialog %s", text);
    TTInstanceOf<TTPopupLayer>().showConfirmKeep(text, []() {
        LOG_I("OTA page: reboot");
        TTInstanceOf<TTPopupLayer>().setDialogMessage("正在重启中");
        TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_FULL);
        ESP.restart();
    });
}

void TTUpdatePage::showRebootDialog(const char* message) {
    _loading = false;
    TTInstanceOf<TTPopupLayer>().dismissLoading();
    const char* text = (message != nullptr && message[0] != '\0') ? message : "更新失败";
    LOG_I("OTA page: reboot dialog %s", text);
    TTInstanceOf<TTPopupLayer>().showConfirm(text, []() {
        LOG_I("OTA page: reboot");
        ESP.restart();
    });
}

void TTUpdatePage::onCheckEvent(lv_event_t* e) {
    TTUpdatePage* page = static_cast<TTUpdatePage*>(lv_event_get_user_data(e));
    if (page != nullptr) {
        page->startCheck();
    }
}

void TTUpdatePage::applyOta(const TTOtaPayload& payload) {
    if (!_visible) {
        return;
    }
    switch (payload.phase) {
    case TT_OTA_PHASE_PROGRESS:
        if (!_loading) {
            _loading = true;
            TTInstanceOf<TTPopupLayer>().showLoading(payload.message);
        } else {
            TTInstanceOf<TTPopupLayer>().updateLoading(payload.message);
        }
        break;
    case TT_OTA_PHASE_REBOOT:
        showFinishedDialog(payload.message);
        break;
    case TT_OTA_PHASE_AVAILABLE:
        _loading = false;
        TTInstanceOf<TTPopupLayer>().dismissLoading();
        rememberTarget(payload);
        setStatus("");
        TTInstanceOf<TTPopupLayer>().showDialog(
            payload.message,
            [this]() { startUpgrade(); },
            [this]() {
                setDeepRefreshBlocked(false);
                setLocked(false);
                showCheckButton(true);
                requestRefresh(TT_REFRESH_PARTIAL);
            });
        break;
    case TT_OTA_PHASE_UP_TO_DATE:
    case TT_OTA_PHASE_FAILED:
        if (_updateStarted) {
            showRebootDialog(payload.message);
            break;
        }
        setDeepRefreshBlocked(false);
        _loading = false;
        setLocked(false);
        TTInstanceOf<TTPopupLayer>().dismissLoading();
        showInstalled();
        setStatus(payload.message);
        showCheckButton(true);
        requestRefresh(TT_REFRESH_PARTIAL);
        break;
    }
}
