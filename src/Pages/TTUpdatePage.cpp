#include "TTUpdatePage.h"
#include "../Base/TTFirmwareVersion.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTTextButton.h"
#include "../Base/Logger.h"
#include "../Service/TTOtaService.h"

void TTUpdatePage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font16 = fm.getFont(16);
    lv_font_t* font12 = fm.getFont(12);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto styleLabel = [](lv_obj_t* label, lv_font_t* font) {
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_set_style_text_font(label, font, 0);
    };

    lv_obj_t* row = lv_obj_create(screen);
    lv_obj_set_size(row, lv_pct(88), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, TT_UPDATE_STATUS_LEFT, TT_UPDATE_STATUS_TOP);

    lv_obj_t* versionTitle = lv_label_create(row);
    lv_label_set_text(versionTitle, "版本");
    lv_obj_set_width(versionTitle, TT_UPDATE_TITLE_W);
    styleLabel(versionTitle, font16);

    _versionValue = lv_label_create(row);
    lv_label_set_text(_versionValue, TT_FW_VERSION);
    styleLabel(_versionValue, font16);
    lv_obj_set_flex_grow(_versionValue, 1);

    lv_obj_t* notesRow = lv_obj_create(screen);
    lv_obj_set_size(notesRow, lv_pct(88), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(notesRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(notesRow, 0, 0);
    lv_obj_set_style_pad_all(notesRow, 0, 0);
    lv_obj_set_layout(notesRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(notesRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(notesRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(notesRow, 8, 0);
    lv_obj_align_to(notesRow, row, LV_ALIGN_OUT_BOTTOM_LEFT, 0, TT_UPDATE_ROW_GAP);

    lv_obj_t* notesTitle = lv_label_create(notesRow);
    lv_label_set_text(notesTitle, "说明");
    lv_obj_set_width(notesTitle, TT_UPDATE_TITLE_W);
    styleLabel(notesTitle, font16);

    _notesValue = lv_label_create(notesRow);
    lv_label_set_text(_notesValue, "无");
    lv_label_set_long_mode(_notesValue, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_notesValue, TT_UPDATE_NOTES_W);
    styleLabel(_notesValue, font12);

    _statusLabel = lv_label_create(screen);
    lv_label_set_text(_statusLabel, "");
    lv_obj_set_width(_statusLabel, lv_pct(88));
    styleLabel(_statusLabel, font12);
    lv_obj_align_to(_statusLabel, notesRow, LV_ALIGN_OUT_BOTTOM_LEFT, 0, TT_UPDATE_ROW_GAP);

    _checkBtn = TTTextButton::create(screen, "检查更新", font16);
    lv_obj_align(_checkBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(_checkBtn, onCheckEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_checkBtn);
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

void TTUpdatePage::showInstalled() {
    char version[TT_OTA_VER_MAX];
    char notes[TT_OTA_MSG_MAX];
    TTOtaService& ota = TTInstanceOf<TTOtaService>();
    ota.readLocalVersion(version, sizeof(version));
    ota.readLocalNotes(notes, sizeof(notes));
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
    if (_loading) {
        _loading = false;
        TTInstanceOf<TTPopupLayer>().dismissLoading();
    }
}

void TTUpdatePage::setStatus(const char* text) {
    if (_statusLabel == nullptr) {
        return;
    }
    lv_label_set_text(_statusLabel, text != nullptr ? text : "");
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
    setLocked(true);
    showCheckButton(false);
    setStatus("");
    LOG_I("OTA page: check");
    TTInstanceOf<TTPopupLayer>().showLoading("正在检查");
    TTInstanceOf<TTOtaService>().checkAsync();
}

void TTUpdatePage::startUpgrade() {
    _loading = true;
    setLocked(true);
    showCheckButton(false);
    LOG_I("OTA page: upgrade");
    TTInstanceOf<TTPopupLayer>().showLoading("正在更新");
    TTInstanceOf<TTOtaService>().upgradeAsync();
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
    case TT_OTA_PHASE_REBOOT:
        if (!_loading) {
            _loading = true;
            TTInstanceOf<TTPopupLayer>().showLoading(payload.message);
        } else {
            TTInstanceOf<TTPopupLayer>().updateLoading(payload.message);
        }
        break;
    case TT_OTA_PHASE_AVAILABLE:
        _loading = false;
        TTInstanceOf<TTPopupLayer>().dismissLoading();
        setStatus(payload.message);
        requestRefresh(TT_REFRESH_PARTIAL);
        TTInstanceOf<TTPopupLayer>().showDialog(
            payload.message,
            [this]() { startUpgrade(); },
            [this]() {
                setLocked(false);
                showCheckButton(true);
                requestRefresh(TT_REFRESH_PARTIAL);
            });
        break;
    case TT_OTA_PHASE_UP_TO_DATE:
    case TT_OTA_PHASE_FAILED:
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
