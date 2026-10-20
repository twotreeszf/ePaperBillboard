#include "TTUpdatePage.h"
#include "TTOtaNotesPage.h"
#include "../Base/TTFirmwareVersion.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Reader/TTReaderTextView.h"
#include "TTReaderPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTLvglEpdDriver.h"
#include "../Base/TTNavigationBar.h"
#include "../Service/TTOtaService.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <new>

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
    lv_obj_set_scrollable(block, false);

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
    lv_obj_set_scrollable(column, false);
    lv_obj_align(column, LV_ALIGN_TOP_MID, 0, TT_UPDATE_PAD);

    addTextBlock(column, "当前版本", font16, font12, &_versionValue, &_versionTitle);
    lv_label_set_text(_versionValue, TT_FW_VERSION);
    lv_obj_t* notesBlock = addTextBlock(column, "更新说明", font16, font12, &_notesValue);
    lv_label_set_text(_notesValue, "无");
    _notesView = tt_reader_text_view_create(notesBlock);
    lv_obj_set_size(_notesView, TT_UPDATE_BODY_W, notesMaxHeight());
    lv_obj_set_hidden(_notesView, true);

    _notesFooter = lv_label_create(screen);
    lv_obj_set_style_text_font(_notesFooter, font12, 0);
    lv_obj_set_style_text_color(_notesFooter, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_notesFooter, LV_OPA_TRANSP, 0);
    lv_label_set_text(_notesFooter, "");
    lv_obj_set_hidden(_notesFooter, true);

    _checkBtn = TTTextButton::create(screen, "检查更新", font16);
    lv_obj_align(_checkBtn, LV_ALIGN_BOTTOM_MID, 0, -TT_UPDATE_BUTTON_BOTTOM);
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
    lv_obj_set_scrollable(_statusBox, false);
    lv_obj_set_clickable(_statusBox, false);
    lv_obj_align(_statusBox, LV_ALIGN_CENTER, 0, TT_NAV_PAGE_INSET / 2);
    lv_obj_set_hidden(_statusBox, true);

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
    if (_upgradeFromNotes) {
        _upgradeFromNotes = false;
        runOnce(0, [this]() {
            if (_visible) {
                startUpgrade();
            }
        });
        return;
    }
    showInstalled();
    setStatus(_notesFailed ? TT_UPDATE_NOTES_FAIL : "");
    _notesFailed = false;
    setLocked(false);
    showCheckButton(true);
}

void TTUpdatePage::willDestroy() {
    detachNotesFooter();
    if (_notesFooter != nullptr) {
        lv_obj_delete(_notesFooter);
        _notesFooter = nullptr;
    }
    _notesSource.close();
    _notesPage.reset();
    TTScreenPage::willDestroy();
}

void TTUpdatePage::requestUpgradeFromNotes() {
    _upgradeFromNotes = true;
    setLocked(true);
}

void TTUpdatePage::notesUnavailable() {
    _notesFailed = true;
    setLocked(false);
}

void TTUpdatePage::rememberTarget(const TTOtaPayload& payload) {
    strncpy(_targetVersion, payload.version, sizeof(_targetVersion) - 1);
    _targetVersion[sizeof(_targetVersion) - 1] = '\0';
}

void TTUpdatePage::showTarget() {
    if (_versionTitle != nullptr) {
        lv_label_set_text(_versionTitle, "正在更新");
    }
    if (_versionValue != nullptr && _targetVersion[0] != '\0') {
        lv_label_set_text(_versionValue, _targetVersion);
    }
}

void TTUpdatePage::showInstalled() {
    char version[TT_OTA_VER_MAX];
    char notes[TT_OTA_NOTES_MAX];
    notes[0] = '\0';
    TTOtaService& ota = TTInstanceOf<TTOtaService>();
    ota.readLocalVersion(version, sizeof(version));
    if (strcmp(version, TT_FW_VERSION) == 0) {
        ota.readLocalNotes(notes, sizeof(notes));
    } else if (version[0] != '\0') {
        LOG_I("Update page: manifest %s differs from fw %s", version, TT_FW_VERSION);
    }
    if (_versionTitle != nullptr) {
        lv_label_set_text(_versionTitle, "当前版本");
    }
    if (_versionValue != nullptr) {
        lv_label_set_text(_versionValue, TT_FW_VERSION);
    }
    showInstalledNotes(notes);
}

int32_t TTUpdatePage::notesMaxHeight() const {
    TTFontManager& fonts = TTFontManager::instance();
    const lv_font_t* titleFont = fonts.getFont(TT_READER_FONT_SIZE);
    const lv_font_t* versionFont = fonts.getFont(TT_NAV_STATUS_FONT);
    const int32_t titleH = titleFont != nullptr ? titleFont->line_height : TT_READER_FONT_SIZE;
    const int32_t versionH = versionFont != nullptr ? versionFont->line_height : TT_NAV_STATUS_FONT;
    const int32_t above = TT_UPDATE_PAD + titleH + TT_UPDATE_TITLE_GAP + versionH
        + TT_UPDATE_SECTION_GAP + titleH + TT_UPDATE_TITLE_GAP;
    const int32_t below = TT_TEXT_BUTTON_H + TT_UPDATE_BUTTON_BOTTOM + TT_UPDATE_NOTES_GAP;
    const int32_t height = TT_UPDATE_CONTENT_H - above - below;
    return height > 0 ? height : 0;
}

void TTUpdatePage::resetNotesPages() {
    _notesPageCount = 0;
    _notesPageIndex = 0;
}

bool TTUpdatePage::showInstalledNotes(const char* notes) {
    const bool hasNotes = notes != nullptr && notes[0] != '\0';
    resetNotesPages();
    if (_notesValue != nullptr) {
        lv_label_set_text(_notesValue, hasNotes ? "" : "无");
        lv_obj_set_hidden(_notesValue, hasNotes);
    }
    if (_notesView == nullptr) {
        syncNotesChrome();
        return false;
    }
    if (!hasNotes) {
        lv_obj_set_hidden(_notesView, true);
        _notesSource.close();
        syncNotesChrome();
        return true;
    }
    TTFontLoader* textFont = TTFontManager::instance().getLoader(TT_READER_FONT_SIZE);
    TTFontLoader* codeFont = TTFontManager::instance().getLoader(TT_READER_CODE_FONT_SIZE);
    const int32_t maxH = notesMaxHeight();
    if (_notesPage == nullptr) {
        _notesPage.reset(new (std::nothrow) TTReaderPageLayout());
    }
    LOG_I("Update page: notes area h=%d", (int)maxH);
    bool opened = maxH > 0 && textFont != nullptr && _notesPage
        && _notesSource.openMemory((const uint8_t*)notes, (uint32_t)strlen(notes))
        && _notesLayout.begin(textFont, codeFont, TT_UPDATE_BODY_W, (int16_t)maxH, true);
    uint32_t offset = 0;
    while (opened && _notesPageCount < TT_UPDATE_NOTES_PAGES_MAX) {
        if (!_notesLayout.layoutPage(_notesSource, offset, *_notesPage) || _notesPage->lineCount == 0) {
            opened = _notesPageCount > 0;
            break;
        }
        _notesStarts[_notesPageCount++] = _notesPage->start;
        if (_notesPage->atEnd || _notesPage->end <= offset) {
            break;
        }
        offset = _notesPage->end;
    }
    if (!opened || _notesPageCount == 0) {
        LOG_W("Update page: notes markdown failed, show plain text");
        resetNotesPages();
        lv_obj_set_hidden(_notesView, true);
        _notesSource.close();
        if (_notesValue != nullptr) {
            lv_obj_set_height(_notesValue, maxH > 0 ? maxH : LV_SIZE_CONTENT);
            lv_obj_set_hidden(_notesValue, false);
            lv_label_set_text(_notesValue, notes);
        }
        syncNotesChrome();
        return false;
    }
    if (_notesPageCount == TT_UPDATE_NOTES_PAGES_MAX && !_notesPage->atEnd) {
        LOG_W("Update page: notes stopped at page cap %u", (unsigned)TT_UPDATE_NOTES_PAGES_MAX);
    }
    lv_obj_set_size(_notesView, TT_UPDATE_BODY_W, maxH);
    lv_obj_set_hidden(_notesView, false);
    LOG_I("Update page: notes %u bytes, %u pages, h=%d",
          (unsigned)_notesSource.size(), (unsigned)_notesPageCount, (int)maxH);
    showNotesPage(false);
    syncNotesChrome();
    return true;
}

void TTUpdatePage::showNotesPage(bool refresh) {
    if (_notesView == nullptr || !_notesPage || _notesPageCount == 0 || _notesPageIndex >= _notesPageCount) {
        return;
    }
    if (!_notesLayout.layoutPage(_notesSource, _notesStarts[_notesPageIndex], *_notesPage)) {
        LOG_E("Update page: show notes page %u failed", (unsigned)_notesPageIndex);
        return;
    }
    tt_reader_text_view_set_page(_notesView, &_notesLayout, _notesPage.get());
    updateNotesFooter();
    if (refresh) {
        requestRefresh(TT_REFRESH_PARTIAL);
    }
}

void TTUpdatePage::nextNotesPage() {
    if (_notesPageIndex + 1 >= _notesPageCount) {
        TTInstanceOf<TTPopupLayer>().showToast(TT_UPDATE_NOTES_LAST);
        return;
    }
    _notesPageIndex++;
    LOG_I("Update page: notes page %u/%u", (unsigned)(_notesPageIndex + 1), (unsigned)_notesPageCount);
    showNotesPage(true);
}

void TTUpdatePage::prevNotesPage() {
    if (_notesPageIndex == 0) {
        TTInstanceOf<TTPopupLayer>().showToast(TT_UPDATE_NOTES_FIRST);
        return;
    }
    _notesPageIndex--;
    LOG_I("Update page: notes page %u/%u", (unsigned)(_notesPageIndex + 1), (unsigned)_notesPageCount);
    showNotesPage(true);
}

void TTUpdatePage::syncNotesChrome() {
    if (_notesPageCount > 1 && _visible && !_locked && !_loading) {
        attachNotesFooter();
        setDeepRefreshBlocked(true);
        return;
    }
    detachNotesFooter();
    if (!_locked && !_loading) {
        setDeepRefreshBlocked(false);
    }
}

void TTUpdatePage::attachNotesFooter() {
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar == nullptr || _notesFooter == nullptr || _notesPageCount < 2) {
        return;
    }
    bar->setSensorsVisible(false);
    bar->setCenterContainer(_notesFooter);
    updateNotesFooter();
}

void TTUpdatePage::detachNotesFooter() {
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar == nullptr) {
        return;
    }
    if (bar->centerContainer() == _notesFooter) {
        bar->setCenterContainer(nullptr);
    }
    bar->setSensorsVisible(true);
}

void TTUpdatePage::updateNotesFooter() {
    if (_notesFooter == nullptr || _notesPageCount < 2) {
        return;
    }
    char text[16];
    snprintf(text, sizeof(text), "%u / %u", (unsigned)(_notesPageIndex + 1), (unsigned)_notesPageCount);
    lv_label_set_text(_notesFooter, text);
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar != nullptr && bar->centerContainer() == _notesFooter) {
        bar->setCenterContainer(_notesFooter);
    }
}

bool TTUpdatePage::handleKeyAction(TTKeyId key, TTKeyGesture gesture) {
    if (gesture != TT_KEY_CLICK || _notesPageCount < 2 || _locked || _loading) {
        return TTScreenPage::handleKeyAction(key, gesture);
    }
    if (key == TT_KEY_RIGHT) {
        nextNotesPage();
        return true;
    }
    if (key == TT_KEY_LEFT) {
        prevNotesPage();
        return true;
    }
    return TTScreenPage::handleKeyAction(key, gesture);
}

void TTUpdatePage::willDisappear() {
    detachNotesFooter();
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
        lv_obj_set_hidden(_statusBox, false);
        lv_obj_move_foreground(_statusBox);
    } else {
        lv_obj_set_hidden(_statusBox, true);
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
        lv_obj_set_hidden(_checkBtn, false);
    } else {
        lv_obj_set_hidden(_checkBtn, true);
    }
}

void TTUpdatePage::startCheck() {
    if (_loading || _locked) {
        return;
    }
    detachNotesFooter();
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
    detachNotesFooter();
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
        setDeepRefreshBlocked(false);
        setLocked(false);
        showCheckButton(false);
        if (getNavigationController() == nullptr || !tt_file_exists(TT_OTA_NOTES_PATH)) {
            LOG_E("OTA page: notes unavailable");
            setStatus(TT_UPDATE_NOTES_FAIL);
            showCheckButton(true);
            requestRefresh(TT_REFRESH_PARTIAL);
            break;
        }
        LOG_I("OTA page: show notes %s", _targetVersion);
        getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTOtaNotesPage(this)));
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
