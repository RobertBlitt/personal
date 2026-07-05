/**
 * ui.cpp: LVGL screens for a 480x480 ROUND display.
 *
 * Layout advice that shaped everything here: on a circular panel the
 * corners of the 480x480 canvas do not exist physically, so important
 * content stays inside a centered circle of roughly 440 px diameter.
 * Everything is centered and stacked vertically for that reason.
 *
 * LVGL style philosophy in this file: build objects once at startup, keep
 * pointers to the labels that change, and only touch them when the value
 * they show actually changed. Rebuilding widgets every frame is the
 * classic LVGL performance mistake and it burns the frame budget fast at
 * 480x480.
 */

#include "ui.h"

#include <Arduino.h>
#include <ctype.h>
#include <lvgl.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "civ.h"
#include "config.h"
#include "input.h"
#include "land_mask.h"
#include "net.h"
#include "radio_client.h"
#include "radio_profile.h"

namespace ui {

namespace {

/* Lightweight themes are color tables, not duplicate styles or assets. The
 * final build can keep several of these in flash and select one from NVS. */
struct Theme {
    uint32_t bg;
    uint32_t panel;
    uint32_t text;
    uint32_t dim;
    uint32_t accent;
    uint32_t good;
    uint32_t warn;
    uint32_t bad;
    uint32_t dialTick;
    uint32_t dialTickMajor;
};

constexpr Theme kDefaultTheme = {
    0x101418, 0x1c2229, 0xe8eaed, 0x8a939e,
    0x35c0ed, 0x4cd964, 0xffb300, 0xff5252,
    0x46515b, 0x7d8994,
};

constexpr uint32_t kBg = kDefaultTheme.bg;
constexpr uint32_t kPanel = kDefaultTheme.panel;
constexpr uint32_t kText = kDefaultTheme.text;
constexpr uint32_t kDim = kDefaultTheme.dim;
constexpr uint32_t kAccent = kDefaultTheme.accent;
constexpr uint32_t kGood = kDefaultTheme.good;
constexpr uint32_t kWarn = kDefaultTheme.warn;
constexpr uint32_t kBad = kDefaultTheme.bad;
constexpr uint32_t kDialTick = kDefaultTheme.dialTick;
constexpr uint32_t kDialTickMajor = kDefaultTheme.dialTickMajor;

/* ---- Screens and the widgets that get updated at runtime ----------------- */
constexpr int kScreenCount = 8;
constexpr int kModeCount = 5;
lv_obj_t *screens[kScreenCount];
int currentScreen = 0;
const int kModeStart[kModeCount] = {0, 3, 5, 6, 7};
const int kModeEnd[kModeCount] = {2, 4, 5, 6, 7};
const char *const kModeNames[kModeCount] = {
    "Radio", "Time", "Conditions", "Controls", "System",
};

/* Radio screen */
constexpr int kRadioMiniScopeBars = 29;
constexpr int kTuningTickCount = 81;
lv_obj_t *lblFreq;
lv_obj_t *lblMode;
lv_obj_t *lblStep;
lv_obj_t *lblLink;
lv_obj_t *lblVfo;
lv_obj_t *lblBand;
lv_obj_t *radioMiniBars[kRadioMiniScopeBars];
lv_obj_t *tuningTicks[kTuningTickCount];
lv_point_t tuningTickPoints[kTuningTickCount][2];
lv_obj_t *lblSMeter;

/* Profile quick-memory screen. */
constexpr int kMaxProfileMemories = 12;
lv_obj_t *lblMemoryChoices[kMaxProfileMemories];
lv_obj_t *lblMemoryTarget;
int selectedMemory = 0;

/* Band selector screen. Profile band counts are intentionally kept small. */
constexpr int kMaxProfileBands = 16;
lv_obj_t *lblBandChoices[kMaxProfileBands];
lv_obj_t *lblBandTarget;
int selectedBand = 0;
uint32_t bandLastFreq[kMaxProfileBands];
uint8_t bandLastMode[kMaxProfileBands];
bool bandLastDataMode[kMaxProfileBands];

/* Grayline screen */
lv_obj_t *grayCanvas;
uint8_t grayCanvasBuf[sizeof(lv_color32_t) * 256 + LAND_MASK_W * LAND_MASK_H];
lv_obj_t *lblGrayUtc;
lv_obj_t *lblGrayLocal;
lv_obj_t *lblGraySun;
int lastGraylineMinute = -1;

/* Clock screen */
lv_obj_t *lblUtcTime;
lv_obj_t *lblLocalTime;
lv_obj_t *lblDate;

/* Bands screen */
lv_obj_t *lblSolarSummary;
lv_obj_t *lblBandRows[4];

/* Radio controls screen */
constexpr int kControlCount = 8;
lv_obj_t *lblControlRows[kControlCount];
lv_obj_t *lblControlMeters;
lv_obj_t *lblControlLink;
int selectedControl = 0;

/* Status screen */
lv_obj_t *lblStatusProfile;
lv_obj_t *lblStatusWifi;
lv_obj_t *lblStatusIp;
lv_obj_t *lblStatusRadio;
lv_obj_t *lblStatusLink;
lv_obj_t *lblStatusHint;

/* Tuning step handling. */
const int32_t kTuneSteps[] = TUNE_STEPS;
int tuneStepIndex = TUNE_STEP_DEFAULT_INDEX;

constexpr double kPi = 3.14159265358979323846;

/* ---- Small helpers ----------------------------------------------------- */

void cycleTuneStep();

void makeTouchTarget(lv_obj_t *obj, lv_event_cb_t callback, lv_coord_t extra = 12) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(obj, extra);
    lv_obj_set_style_text_color(obj, lv_color_hex(kAccent), LV_STATE_PRESSED);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_ALL, nullptr);
}

void openScreenFromTouch(int screen) {
    currentScreen = screen;
    lv_scr_load_anim(screens[currentScreen], LV_SCR_LOAD_ANIM_FADE_ON,
                     120, 0, false);
}

void modeTouch(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SHORT_CLICKED) {
        radio::cycleMode(true);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        const radio::Snapshot s = radio::snapshot();
        radio::requestModeFilter(s.mode, !s.dataMode, s.filter);
    }
}

void frequencyTouch(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        openScreenFromTouch(1); /* Quick Memories */
    }
}

void bandTouch(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        const int band = profile::bandIndexForFrequency(radio::snapshot().freqHz);
        if (band >= 0) selectedBand = band;
        openScreenFromTouch(2); /* Band Select */
    }
}

void stepTouch(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        cycleTuneStep();
    }
}

void signalTouch(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        openScreenFromTouch(6); /* Radio Controls and meters */
    }
}

void linkTouch(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        openScreenFromTouch(7); /* Status */
    }
}

void homeTouch(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        openScreenFromTouch(0);
    }
}

lv_obj_t *makeScreen() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return scr;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                    const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

void makeHomeButton(lv_obj_t *scr) {
    lv_obj_t *back = lv_btn_create(scr);
    lv_obj_set_size(back, 48, 48);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(kAccent), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(kDialTickMajor), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, homeTouch, LV_EVENT_ALL, nullptr);

    lv_obj_t *icon = makeLabel(back, &lv_font_montserrat_20, kText,
                               LV_SYMBOL_LEFT);
    lv_obj_center(icon);
}

void makeNavLabel(lv_obj_t *scr, int modeIndex, int pageIndex) {
    const int pages = kModeEnd[modeIndex] - kModeStart[modeIndex] + 1;
    char text[32];
    snprintf(text, sizeof(text), "%s %d/%d",
             kModeNames[modeIndex], pageIndex + 1, pages);
    lv_obj_t *lbl = makeLabel(scr, &lv_font_montserrat_12, kDim, text);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 26);
}

/* Render 14074000 as "14.074.000" so MHz/kHz/Hz groups read at a glance,
 * the way radio dials group digits. */
void formatFreq(uint32_t hz, char *out, size_t outSize) {
    const uint32_t mhz = hz / 1000000;
    const uint32_t khz = (hz / 1000) % 1000;
    const uint32_t rest = hz % 1000;
    snprintf(out, outSize, "%lu.%03lu.%03lu",
             (unsigned long)mhz, (unsigned long)khz, (unsigned long)rest);
}

const char *bandNameForFreq(uint32_t hz) {
    if (hz >= 1800000 && hz <= 2000000) return "160m";
    if (hz >= 3500000 && hz <= 4000000) return "80m";
    if (hz >= 5330000 && hz <= 5407000) return "60m";
    if (hz >= 7000000 && hz <= 7300000) return "40m";
    if (hz >= 10100000 && hz <= 10150000) return "30m";
    if (hz >= 14000000 && hz <= 14350000) return "20m";
    if (hz >= 18068000 && hz <= 18168000) return "17m";
    if (hz >= 21000000 && hz <= 21450000) return "15m";
    if (hz >= 24890000 && hz <= 24990000) return "12m";
    if (hz >= 28000000 && hz <= 29700000) return "10m";
    if (hz >= 50000000 && hz <= 54000000) return "6m";
    return "GEN";
}

uint32_t levelColor(uint8_t level) {
    if (level < 75) return kAccent;
    if (level < 155) return kGood;
    if (level < 220) return kWarn;
    return kBad;
}

/* Map a rating string from hamqsl to a status color. */
uint32_t ratingColor(const char *rating) {
    if (strcasecmp(rating, "Good") == 0) return kGood;
    if (strcasecmp(rating, "Fair") == 0) return kWarn;
    if (strcasecmp(rating, "Poor") == 0) return kBad;
    return kDim;
}

/* ---- Screen builders ------------------------------------------------------ */

void buildRadioScreen() {
    lv_obj_t *scr = screens[0] = makeScreen();

    /* Radial dial marks represent tuning position, not signal strength. */
    constexpr int center = 240;
    constexpr int outerRadius = 218;
    for (int i = 0; i < kTuningTickCount; i++) {
        const bool major = (i % 10) == 0;
        const bool medium = !major && (i % 5) == 0;
        const int innerRadius = major ? 196 : medium ? 203 : 208;
        const double angle = (135.0 + i * 270.0 / (kTuningTickCount - 1)) *
                             kPi / 180.0;
        tuningTickPoints[i][0] = {
            static_cast<lv_coord_t>(center + innerRadius * cos(angle)),
            static_cast<lv_coord_t>(center + innerRadius * sin(angle)),
        };
        tuningTickPoints[i][1] = {
            static_cast<lv_coord_t>(center + outerRadius * cos(angle)),
            static_cast<lv_coord_t>(center + outerRadius * sin(angle)),
        };
        tuningTicks[i] = lv_line_create(scr);
        lv_line_set_points(tuningTicks[i], tuningTickPoints[i], 2);
        lv_obj_set_style_line_color(tuningTicks[i],
                                    lv_color_hex(major ? kDialTickMajor : kDialTick), 0);
        lv_obj_set_style_line_width(tuningTicks[i], major ? 2 : medium ? 2 : 1, 0);
        lv_obj_set_style_line_rounded(tuningTicks[i], true, 0);
    }

    lblLink = makeLabel(scr, &lv_font_montserrat_14, kBad, LV_SYMBOL_WIFI " no link");
    lv_obj_align(lblLink, LV_ALIGN_TOP_MID, 0, 58);
    makeTouchTarget(lblLink, linkTouch);

    lblVfo = makeLabel(scr, &lv_font_montserrat_16, kDim, "VFO A");
    lv_obj_align(lblVfo, LV_ALIGN_TOP_MID, -92, 108);

    lblMode = makeLabel(scr, &lv_font_montserrat_28, kText, "USB");
    lv_obj_align(lblMode, LV_ALIGN_TOP_MID, 0, 98);
    makeTouchTarget(lblMode, modeTouch, 18);

    lblBand = makeLabel(scr, &lv_font_montserrat_16, kDim, "--");
    lv_obj_align(lblBand, LV_ALIGN_TOP_MID, 96, 108);
    makeTouchTarget(lblBand, bandTouch, 16);

    lblFreq = makeLabel(scr, &lv_font_montserrat_48, kAccent, "--.---.---");
    lv_obj_align(lblFreq, LV_ALIGN_CENTER, 0, 30);
    makeTouchTarget(lblFreq, frequencyTouch, 12);

    lblStep = makeLabel(scr, &lv_font_montserrat_16, kDim, "step 1 kHz");
    lv_obj_align(lblStep, LV_ALIGN_CENTER, 0, 72);
    makeTouchTarget(lblStep, stepTouch, 14);

    constexpr int barW = 8;
    constexpr int gap = 3;
    constexpr int gridW = kRadioMiniScopeBars * barW + (kRadioMiniScopeBars - 1) * gap;
    constexpr int startX = (480 - gridW) / 2;
    constexpr int baseY = 235;
    for (int i = 0; i < kRadioMiniScopeBars; i++) {
        radioMiniBars[i] = lv_obj_create(scr);
        lv_obj_set_size(radioMiniBars[i], barW, 4);
        lv_obj_set_pos(radioMiniBars[i], startX + i * (barW + gap), baseY - 4);
        lv_obj_set_style_radius(radioMiniBars[i], 2, 0);
        lv_obj_set_style_border_width(radioMiniBars[i], 0, 0);
        lv_obj_set_style_bg_opa(radioMiniBars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(radioMiniBars[i], lv_color_hex(0x17232d), 0);
    }

    lblSMeter = makeLabel(scr, &lv_font_montserrat_20, kDim, "S 0");
    lv_obj_align(lblSMeter, LV_ALIGN_BOTTOM_MID, 0, -76);
    makeTouchTarget(lblSMeter, signalTouch, 18);

    lv_obj_t *memoryButton = makeLabel(scr, &lv_font_montserrat_24, kDim,
                                       LV_SYMBOL_LIST);
    lv_obj_align(memoryButton, LV_ALIGN_BOTTOM_MID, -88, -72);
    makeTouchTarget(memoryButton, frequencyTouch, 18);

}

void buildClockScreen() {
    lv_obj_t *scr = screens[4] = makeScreen();
    makeNavLabel(scr, 1, 1);
    makeHomeButton(scr);

    lv_obj_t *utcCaption = makeLabel(scr, &lv_font_montserrat_16, kDim, "UTC");
    lv_obj_align(utcCaption, LV_ALIGN_TOP_MID, 0, 84);

    lblUtcTime = makeLabel(scr, &lv_font_montserrat_48, kAccent, "--:--:--");
    lv_obj_align(lblUtcTime, LV_ALIGN_TOP_MID, 0, 112);

    makeLabel(scr, &lv_font_montserrat_16, kDim, "local");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_CENTER, 0, -6);

    lblLocalTime = makeLabel(scr, &lv_font_montserrat_40, kText, "--:--:--");
    lv_obj_align(lblLocalTime, LV_ALIGN_CENTER, 0, 30);

    lblDate = makeLabel(scr, &lv_font_montserrat_20, kDim, "");
    lv_obj_align(lblDate, LV_ALIGN_BOTTOM_MID, 0, -104);

    lv_obj_t *grid = makeLabel(scr, &lv_font_montserrat_24, kGood, STATION_GRID);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -102);
}

void buildBandsScreen() {
    lv_obj_t *scr = screens[5] = makeScreen();
    makeNavLabel(scr, 2, 0);
    makeHomeButton(scr);

    makeLabel(scr, &lv_font_montserrat_20, kText, "Band Conditions");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_TOP_MID, 0, 62);

    lblSolarSummary = makeLabel(scr, &lv_font_montserrat_16, kDim, "waiting for data...");
    lv_obj_align(lblSolarSummary, LV_ALIGN_TOP_MID, 0, 96);

    /* Column headers. */
    lv_obj_t *hdr = makeLabel(scr, &lv_font_montserrat_14, kDim, "band          day     night");
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 140);

    for (int i = 0; i < 4; i++) {
        lblBandRows[i] = makeLabel(scr, &lv_font_montserrat_20, kText, "-");
        lv_obj_align(lblBandRows[i], LV_ALIGN_TOP_MID, 0, 172 + i * 40);
    }

    makeLabel(scr, &lv_font_montserrat_12, kDim, "data: N0NBH / hamqsl.com");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_BOTTOM_MID, 0, -104);
}

void buildControlsScreen() {
    lv_obj_t *scr = screens[6] = makeScreen();
    makeNavLabel(scr, 3, 0);
    makeHomeButton(scr);

    makeLabel(scr, &lv_font_montserrat_20, kText, "Radio Controls");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_TOP_MID, 0, 58);

    lblControlLink = makeLabel(scr, &lv_font_montserrat_14, kDim, "radio --");
    lv_obj_align(lblControlLink, LV_ALIGN_TOP_MID, 0, 88);

    lblControlMeters = makeLabel(scr, &lv_font_montserrat_14, kDim,
                                 "S 000   RF 000   SWR 000   V 000");
    lv_obj_align(lblControlMeters, LV_ALIGN_TOP_MID, 0, 112);

    for (int i = 0; i < kControlCount; i++) {
        lblControlRows[i] = makeLabel(scr, &lv_font_montserrat_16, kDim, "--");
        lv_obj_align(lblControlRows[i], LV_ALIGN_TOP_MID,
                     (i % 2 == 0) ? -105 : 105, 154 + (i / 2) * 48);
    }

    makeLabel(scr, &lv_font_montserrat_12, kDim,
              "turn: select   click: change / start");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_BOTTOM_MID, 0, -104);
}

void buildStatusScreen() {
    lv_obj_t *scr = screens[7] = makeScreen();
    makeNavLabel(scr, 4, 0);
    makeHomeButton(scr);

    makeLabel(scr, &lv_font_montserrat_20, kText, "Status");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_TOP_MID, 0, 58);

    lblStatusProfile = makeLabel(scr, &lv_font_montserrat_24, kAccent,
                                 profile::active().name);
    lv_obj_align(lblStatusProfile, LV_ALIGN_TOP_MID, 0, 96);

    lblStatusWifi = makeLabel(scr, &lv_font_montserrat_20, kText, "WiFi --");
    lv_obj_align(lblStatusWifi, LV_ALIGN_TOP_MID, 0, 146);

    lblStatusIp = makeLabel(scr, &lv_font_montserrat_20, kText, "IP --");
    lv_obj_align(lblStatusIp, LV_ALIGN_TOP_MID, 0, 184);

    lblStatusRadio = makeLabel(scr, &lv_font_montserrat_16, kText, "radio --");
    lv_obj_align(lblStatusRadio, LV_ALIGN_TOP_MID, 0, 222);

    lblStatusLink = makeLabel(scr, &lv_font_montserrat_20, kText, "link --");
    lv_obj_align(lblStatusLink, LV_ALIGN_TOP_MID, 0, 260);

    lblStatusHint = makeLabel(scr, &lv_font_montserrat_12, kDim,
                              "profiles: X6200 now, IC-7300 next");
    lv_obj_align(lblStatusHint, LV_ALIGN_BOTTOM_MID, 0, -104);
}

void buildMemoryScreen() {
    lv_obj_t *scr = screens[1] = makeScreen();
    makeNavLabel(scr, 0, 1);
    makeHomeButton(scr);

    makeLabel(scr, &lv_font_montserrat_20, kText, "Quick Memories");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_TOP_MID, 0, 56);

    lblMemoryTarget = makeLabel(scr, &lv_font_montserrat_24, kAccent, "--");
    lv_obj_align(lblMemoryTarget, LV_ALIGN_TOP_MID, 0, 88);

    const profile::RadioProfile &p = profile::active();
    const int count = p.memoryCount < kMaxProfileMemories
                          ? static_cast<int>(p.memoryCount)
                          : kMaxProfileMemories;
    for (int i = 0; i < count; i++) {
        lblMemoryChoices[i] = makeLabel(scr, &lv_font_montserrat_20, kDim,
                                        p.memories[i].name);
        lv_obj_align(lblMemoryChoices[i], LV_ALIGN_TOP_MID,
                     (i % 2 == 0) ? -104 : 104, 140 + (i / 2) * 48);
    }

    makeLabel(scr, &lv_font_montserrat_12, kDim,
              "turn: choose   click: tune   hold: bands");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_BOTTOM_MID, 0, -96);
}

void buildBandSelectScreen() {
    lv_obj_t *scr = screens[2] = makeScreen();
    makeNavLabel(scr, 0, 2);
    makeHomeButton(scr);

    makeLabel(scr, &lv_font_montserrat_20, kText, "Band Select");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_TOP_MID, 0, 54);

    lblBandTarget = makeLabel(scr, &lv_font_montserrat_24, kAccent, "--");
    lv_obj_align(lblBandTarget, LV_ALIGN_TOP_MID, 0, 82);

    const profile::RadioProfile &p = profile::active();
    const int count = p.bandCount < kMaxProfileBands
                          ? static_cast<int>(p.bandCount)
                          : kMaxProfileBands;
    constexpr int cols = 4;
    constexpr int xPos[cols] = {-126, -42, 42, 126};
    for (int i = 0; i < count; i++) {
        lblBandChoices[i] = makeLabel(scr, &lv_font_montserrat_20, kDim,
                                      p.bands[i].name);
        lv_obj_align(lblBandChoices[i], LV_ALIGN_TOP_MID,
                     xPos[i % cols], 132 + (i / cols) * 48);
    }

    makeLabel(scr, &lv_font_montserrat_12, kDim,
              "turn: choose   click: change band");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_BOTTOM_MID, 0, -96);
}

void buildGraylineScreen() {
    lv_obj_t *scr = screens[3] = makeScreen();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    makeNavLabel(scr, 1, 0);
    makeHomeButton(scr);

    makeLabel(scr, &lv_font_montserrat_20, kText, "Grayline");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_TOP_MID, 0, 54);

    lblGrayUtc = makeLabel(scr, &lv_font_montserrat_28, kText, "UTC --:--");
    lv_obj_align(lblGrayUtc, LV_ALIGN_TOP_MID, 0, 86);

    lblGrayLocal = makeLabel(scr, &lv_font_montserrat_16, kDim, "local --:--");
    lv_obj_align(lblGrayLocal, LV_ALIGN_TOP_MID, 0, 118);

    grayCanvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(grayCanvas, grayCanvasBuf, LAND_MASK_W, LAND_MASK_H,
                         LV_IMG_CF_INDEXED_8BIT);
    lv_canvas_set_palette(grayCanvas, 0, lv_color_black());       /* ocean day */
    lv_canvas_set_palette(grayCanvas, 1, lv_color_hex(0xf2f2f2)); /* land day */
    lv_canvas_set_palette(grayCanvas, 2, lv_color_black());       /* ocean grayline */
    lv_canvas_set_palette(grayCanvas, 3, lv_color_hex(0xb8b8b8)); /* land grayline */
    lv_canvas_set_palette(grayCanvas, 4, lv_color_black());       /* ocean twilight */
    lv_canvas_set_palette(grayCanvas, 5, lv_color_hex(0x686868)); /* land twilight */
    lv_canvas_set_palette(grayCanvas, 6, lv_color_black());       /* ocean night */
    lv_canvas_set_palette(grayCanvas, 7, lv_color_hex(0x242424)); /* land night */
    lv_canvas_set_palette(grayCanvas, 8, lv_color_hex(kAccent));  /* station */
    uint8_t *initialPixels = grayCanvasBuf + sizeof(lv_color32_t) * 256;
    for (int y = 0; y < LAND_MASK_H; y++) {
        for (int x = 0; x < LAND_MASK_W; x++) {
            const int bit = y * LAND_MASK_W + x;
            initialPixels[bit] =
                (LAND_MASK[bit / 8] & (1 << (bit % 8))) ? 1 : 0;
        }
    }
    lv_obj_align(grayCanvas, LV_ALIGN_TOP_MID, 0, 128);

    lblGraySun = makeLabel(scr, &lv_font_montserrat_14, kDim, "sun --");
    lv_obj_align(lblGraySun, LV_ALIGN_BOTTOM_MID, 0, -94);

}

/* ---- Runtime refresh ------------------------------------------------------- */

void refreshRadio() {
    const radio::Snapshot s = radio::snapshot();

    char buf[24];
    if (s.linkUp) {
        formatFreq(s.freqHz, buf, sizeof(buf));
        lv_label_set_text(lblFreq, buf);
        lv_label_set_text_fmt(lblMode, "%s%s", profile::modeName(s.mode),
                              s.dataMode ? "-D" : "");
        lv_label_set_text(lblBand, bandNameForFreq(s.freqHz));
        lv_obj_set_style_text_color(lblLink, lv_color_hex(kGood), 0);
        lv_label_set_text(lblLink, LV_SYMBOL_WIFI " linked");
        const int band = profile::bandIndexForFrequency(s.freqHz);
        if (band >= 0 && band < kMaxProfileBands) {
            bandLastFreq[band] = s.freqHz;
            bandLastMode[band] = s.mode;
            bandLastDataMode[band] = s.dataMode;
        }
    } else {
        lv_label_set_text(lblBand, "--");
        lv_obj_set_style_text_color(lblLink, lv_color_hex(kBad), 0);
        lv_label_set_text(lblLink, LV_SYMBOL_WIFI " no link");
    }

    const uint32_t tuningPosition =
        s.linkUp ? (s.freqHz / kTuneSteps[tuneStepIndex]) % kTuningTickCount
                 : kTuningTickCount;
    for (int i = 0; i < kTuningTickCount; i++) {
        const bool major = (i % 10) == 0;
        const bool medium = !major && (i % 5) == 0;
        const bool active = static_cast<uint32_t>(i) == tuningPosition;
        lv_obj_set_style_line_color(
            tuningTicks[i],
            lv_color_hex(active ? kAccent : major ? kDialTickMajor : kDialTick), 0);
        lv_obj_set_style_line_width(tuningTicks[i],
                                    active ? 3 : major ? 2 : medium ? 2 : 1, 0);
    }
    /* Rough S-unit mapping for the label: the doc only promises 0-255 as
     * 0-100%, so display S1..S9+ proportionally rather than pretending to
     * calibrated dB. */
    const int sUnits = (s.sMeter * 9) / 255;
    lv_label_set_text_fmt(lblSMeter, "S %d", sUnits);
    lv_obj_set_style_text_color(lblSMeter,
                                lv_color_hex(s.linkUp ? levelColor(s.sMeter) : kDim),
                                0);

    constexpr int barW = 8;
    constexpr int gap = 3;
    constexpr int gridW = kRadioMiniScopeBars * barW + (kRadioMiniScopeBars - 1) * gap;
    constexpr int startX = (480 - gridW) / 2;
    constexpr int baseY = 235;
    const int center = kRadioMiniScopeBars / 2;
    for (int i = 0; i < kRadioMiniScopeBars; i++) {
        const int distance = abs(i - center);
        const int noise = static_cast<int>(random(-18, 28));
        int level = s.linkUp ? static_cast<int>(s.sMeter) - distance * 8 + noise : 0;
        if (level < 0) level = 0;
        if (level > 255) level = 255;
        const int h = 4 + (level * 46) / 255;
        lv_obj_set_size(radioMiniBars[i], barW, h);
        lv_obj_set_pos(radioMiniBars[i], startX + i * (barW + gap), baseY - h);
        lv_obj_set_style_bg_color(radioMiniBars[i],
                                  lv_color_hex(s.linkUp ? levelColor(level) : 0x17232d),
                                  0);
    }
}

void refreshClock() {
    if (!net::timeSynced()) {
        return; /* leave the --:--:-- placeholders until NTP lands */
    }
    time_t now = time(nullptr);

    struct tm utc;
    gmtime_r(&now, &utc);
    lv_label_set_text_fmt(lblUtcTime, "%02d:%02d:%02d",
                          utc.tm_hour, utc.tm_min, utc.tm_sec);

    struct tm local;
    localtime_r(&now, &local);
    lv_label_set_text_fmt(lblLocalTime, "%02d:%02d:%02d",
                          local.tm_hour, local.tm_min, local.tm_sec);

    static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    lv_label_set_text_fmt(lblDate, "%s %s %d, %d", kDays[local.tm_wday],
                          kMonths[local.tm_mon], local.tm_mday,
                          local.tm_year + 1900);
}

bool landAt(int x, int y) {
    const int bit = y * LAND_MASK_W + x;
    return (LAND_MASK[bit / 8] & (1 << (bit % 8))) != 0;
}

uint8_t *grayPixels() {
    return grayCanvasBuf + sizeof(lv_color32_t) * 256;
}

bool gridToLonLat(const char *grid, double &lon, double &lat) {
    if (!grid || strlen(grid) < 4) {
        return false;
    }
    const char a = toupper(grid[0]);
    const char b = toupper(grid[1]);
    const char c = grid[2];
    const char d = grid[3];
    if (a < 'A' || a > 'R' || b < 'A' || b > 'R' ||
        c < '0' || c > '9' || d < '0' || d > '9') {
        return false;
    }

    lon = -180.0 + (a - 'A') * 20.0 + (c - '0') * 2.0 + 1.0;
    lat = -90.0 + (b - 'A') * 10.0 + (d - '0') * 1.0 + 0.5;

    if (strlen(grid) >= 6) {
        const char e = tolower(grid[4]);
        const char f = tolower(grid[5]);
        if (e >= 'a' && e <= 'x' && f >= 'a' && f <= 'x') {
            lon += (e - 'a') * (2.0 / 24.0) - 1.0 + (1.0 / 24.0);
            lat += (f - 'a') * (1.0 / 24.0) - 0.5 + (0.5 / 24.0);
        }
    }
    return true;
}

void drawStationMarker() {
    double lon, lat;
    if (!gridToLonLat(STATION_GRID, lon, lat)) {
        return;
    }
    const int x = static_cast<int>((lon + 180.0) * LAND_MASK_W / 360.0);
    const int y = static_cast<int>((90.0 - lat) * LAND_MASK_H / 180.0);
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            const int px = x + dx;
            const int py = y + dy;
            if (px < 0 || px >= LAND_MASK_W || py < 0 || py >= LAND_MASK_H) {
                continue;
            }
            if (abs(dx) + abs(dy) <= 2) {
                grayPixels()[py * LAND_MASK_W + px] = 8;
            }
        }
    }
}

void refreshMemorySelect() {
    const profile::RadioProfile &p = profile::active();
    const int count = p.memoryCount < kMaxProfileMemories
                          ? static_cast<int>(p.memoryCount)
                          : kMaxProfileMemories;
    if (count <= 0) return;
    if (selectedMemory < 0) selectedMemory = count - 1;
    if (selectedMemory >= count) selectedMemory = 0;

    for (int i = 0; i < count; i++) {
        lv_obj_set_style_text_color(
            lblMemoryChoices[i],
            lv_color_hex(i == selectedMemory ? kAccent : kDim), 0);
    }

    const profile::MemoryInfo &memory = p.memories[selectedMemory];
    char freq[24];
    formatFreq(memory.freqHz, freq, sizeof(freq));
    lv_label_set_text_fmt(lblMemoryTarget, "%s  %s%s",
                          freq, profile::modeName(memory.mode),
                          memory.dataMode ? "-D" : "");
}

void refreshBandSelect() {
    const profile::RadioProfile &p = profile::active();
    const int count = p.bandCount < kMaxProfileBands
                          ? static_cast<int>(p.bandCount)
                          : kMaxProfileBands;
    if (count <= 0) return;
    if (selectedBand < 0) selectedBand = count - 1;
    if (selectedBand >= count) selectedBand = 0;

    for (int i = 0; i < count; i++) {
        lv_obj_set_style_text_color(
            lblBandChoices[i], lv_color_hex(i == selectedBand ? kAccent : kDim), 0);
    }

    const profile::BandInfo &band = p.bands[selectedBand];
    const uint32_t target = bandLastFreq[selectedBand] != 0
                                ? bandLastFreq[selectedBand]
                                : band.landingHz;
    char freq[24];
    formatFreq(target, freq, sizeof(freq));
    lv_label_set_text_fmt(lblBandTarget, "%s  %s", band.name, freq);
}

void refreshGrayline() {
    if (!net::timeSynced()) {
        return;
    }

    time_t now = time(nullptr);
    struct tm utc;
    gmtime_r(&now, &utc);
    struct tm local;
    localtime_r(&now, &local);

    lv_label_set_text_fmt(lblGrayUtc, "UTC %02d:%02d", utc.tm_hour, utc.tm_min);
    lv_label_set_text_fmt(lblGrayLocal, "local %02d:%02d   %s",
                          local.tm_hour, local.tm_min, STATION_GRID);

    const int minuteOfDay = utc.tm_hour * 60 + utc.tm_min;
    if (minuteOfDay == lastGraylineMinute) {
        return;
    }
    lastGraylineMinute = minuteOfDay;

    const int dayOfYear = utc.tm_yday + 1;
    const double utcHours = utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0;
    const double gamma = 2.0 * kPi / 365.0 *
                         (dayOfYear - 1 + (utcHours - 12.0) / 24.0);
    const double decl =
        0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma) -
        0.006758 * cos(2.0 * gamma) + 0.000907 * sin(2.0 * gamma) -
        0.002697 * cos(3.0 * gamma) + 0.00148 * sin(3.0 * gamma);
    const double eqTime =
        229.18 * (0.000075 + 0.001868 * cos(gamma) -
                  0.032077 * sin(gamma) - 0.014615 * cos(2.0 * gamma) -
                  0.040849 * sin(2.0 * gamma));
    double subsolarLon = -15.0 * (utcHours - 12.0 + eqTime / 60.0);
    while (subsolarLon < -180.0) subsolarLon += 360.0;
    while (subsolarLon > 180.0) subsolarLon -= 360.0;

    /* Longitude only changes by column, so calculate its expensive cosine
     * 420 times rather than once for every one of the 88,200 pixels. */
    float cosHourAngle[LAND_MASK_W];
    for (int col = 0; col < LAND_MASK_W; col++) {
        const double lon = -180.0 + (col + 0.5) * 360.0 / LAND_MASK_W;
        cosHourAngle[col] = static_cast<float>(
            cos((lon - subsolarLon) * kPi / 180.0));
    }

    for (int row = 0; row < LAND_MASK_H; row++) {
        const double lat = 90.0 - (row + 0.5) * 180.0 / LAND_MASK_H;
        const double latRad = lat * kPi / 180.0;
        const double sinDeclLat = sin(latRad) * sin(decl);
        const double cosDeclLat = cos(latRad) * cos(decl);
        for (int col = 0; col < LAND_MASK_W; col++) {
            const double cosZenith =
                sinDeclLat + cosDeclLat * cosHourAngle[col];
            const bool land = landAt(col, row);
            uint8_t color = land ? 1 : 0;
            if (cosZenith < -0.25) {
                color = land ? 7 : 6;
            } else if (cosZenith < -0.06) {
                color = land ? 5 : 4;
            } else if (cosZenith < 0.10) {
                color = land ? 3 : 2;
            }
            grayPixels()[row * LAND_MASK_W + col] = color;
        }
    }
    drawStationMarker();
    lv_obj_invalidate(grayCanvas);

    lv_label_set_text_fmt(lblGraySun, "sun lon %ld  dec %ld",
                          (long)subsolarLon, (long)(decl * 180.0 / kPi));
}

void refreshBands() {
    const net::SolarData s = net::solar();
    if (!s.valid) {
        return;
    }
    lv_label_set_text_fmt(lblSolarSummary, "SFI %s   A %s   K %s   SSN %s",
                          s.solarFlux, s.aIndex, s.kIndex, s.sunspots);
    for (int i = 0; i < 4; i++) {
        lv_label_set_text_fmt(lblBandRows[i], "%-9s %-7s %s",
                              net::kBandNames[i], s.bandDay[i], s.bandNight[i]);
        /* Color the whole row by the day rating; a per-word color needs
         * spans and is not worth the complexity here. */
        lv_obj_set_style_text_color(lblBandRows[i],
                                    lv_color_hex(ratingColor(s.bandDay[i])), 0);
    }
}

const char *agcName(uint8_t agc) {
    switch (agc) {
        case 0: return "OFF";
        case 1: return "FAST";
        case 2: return "SLOW";
        default: return "AUTO";
    }
}

void refreshControls() {
    const radio::Snapshot s = radio::snapshot();
    lv_label_set_text(lblControlLink, s.linkUp ? "linked" : "no link");
    lv_obj_set_style_text_color(lblControlLink,
                                lv_color_hex(s.linkUp ? kGood : kBad), 0);
    lv_label_set_text_fmt(lblControlMeters, "S %03u  RF %03u  SWR %03u  V %03u",
                          s.sMeter, s.rfMeter, s.swrMeter, s.voltageMeter);

    char values[kControlCount][24];
    snprintf(values[0], sizeof(values[0]), "MODE  %s", profile::modeName(s.mode));
    snprintf(values[1], sizeof(values[1]), "DATA  %s", s.dataMode ? "ON" : "OFF");
    snprintf(values[2], sizeof(values[2]), "FILTER  %u", s.filter);
    snprintf(values[3], sizeof(values[3]), "PREAMP  %s", s.preamp ? "ON" : "OFF");
    snprintf(values[4], sizeof(values[4]), "ATT  %s", s.attenuator ? "ON" : "OFF");
    snprintf(values[5], sizeof(values[5]), "AGC  %s", agcName(s.agc));
    snprintf(values[6], sizeof(values[6]), "ATU  %s", s.tuner ? "ON" : "OFF");
    snprintf(values[7], sizeof(values[7]), "TUNE  START");

    for (int i = 0; i < kControlCount; i++) {
        lv_label_set_text_fmt(lblControlRows[i], "%s%s",
                              i == selectedControl ? LV_SYMBOL_RIGHT " " : "",
                              values[i]);
        lv_obj_set_style_text_color(
            lblControlRows[i],
            lv_color_hex(i == selectedControl ? kAccent : kDim), 0);
    }
}

void refreshStatus() {
    const net::Status n = net::status();
    const radio::Snapshot r = radio::snapshot();

    lv_label_set_text(lblStatusProfile, profile::active().name);
    lv_label_set_text_fmt(lblStatusWifi, "%s  %s  %ld dBm",
                          n.wifiUp ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE,
                          n.ssid,
                          (long)n.rssi);
    lv_obj_set_style_text_color(lblStatusWifi,
                                lv_color_hex(n.wifiUp ? kGood : kBad), 0);

    lv_label_set_text_fmt(lblStatusIp, "IP %s", n.ip);
    lv_label_set_text_fmt(lblStatusRadio, "radio %s:%d", RADIO_HOST, RADIO_PORT);
    lv_label_set_text_fmt(lblStatusLink, "%s  %s  stale %lus",
                          r.linkUp ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE,
                          r.linkUp ? "linked" : "no link",
                          (unsigned long)(r.staleMs / 1000));
    lv_obj_set_style_text_color(lblStatusLink,
                                lv_color_hex(r.linkUp ? kGood : kBad), 0);
}

/* ---- Input routing ------------------------------------------------------------ */

void switchScreen(int delta) {
    currentScreen = (currentScreen + delta + kScreenCount) % kScreenCount;
    lv_scr_load_anim(screens[currentScreen], LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, false);
}

int currentMode() {
    for (int i = 0; i < kModeCount; i++) {
        if (currentScreen >= kModeStart[i] && currentScreen <= kModeEnd[i]) {
            return i;
        }
    }
    return 0;
}

void switchMode(int delta) {
    const int nextMode = (currentMode() + delta + kModeCount) % kModeCount;
    currentScreen = kModeStart[nextMode];
    lv_scr_load_anim(screens[currentScreen], LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, false);
}

void switchModeScreen() {
    const int mode = currentMode();
    if (kModeStart[mode] == kModeEnd[mode]) {
        return;
    }
    currentScreen++;
    if (currentScreen > kModeEnd[mode]) {
        currentScreen = kModeStart[mode];
    }
    if (currentScreen == 1) {
        refreshMemorySelect();
    } else if (currentScreen == 2) {
        const int currentBand = profile::bandIndexForFrequency(radio::snapshot().freqHz);
        if (currentBand >= 0) selectedBand = currentBand;
        refreshBandSelect();
    }
    lv_scr_load_anim(screens[currentScreen], LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, false);
}

void cycleTuneStep() {
    tuneStepIndex = (tuneStepIndex + 1) % TUNE_STEP_COUNT;
    const int32_t step = kTuneSteps[tuneStepIndex];
    if (step >= 1000) {
        lv_label_set_text_fmt(lblStep, "step %ld kHz", (long)(step / 1000));
    } else {
        lv_label_set_text_fmt(lblStep, "step %ld Hz", (long)step);
    }
}

void handleEvent(input::Event e) {
    switch (e) {
        case input::Event::RotateCw:
        case input::Event::RotateCcw: {
            const bool cw = (e == input::Event::RotateCw);
            if (currentScreen == 0) {
                radio::tuneBy(cw ? kTuneSteps[tuneStepIndex]
                                 : -kTuneSteps[tuneStepIndex]);
                refreshRadio(); /* instant dial feedback */
            } else if (currentScreen == 1) {
                selectedMemory += cw ? 1 : -1;
                refreshMemorySelect();
            } else if (currentScreen == 2) {
                selectedBand += cw ? 1 : -1;
                refreshBandSelect();
            } else if (currentScreen == 6) {
                selectedControl += cw ? 1 : -1;
                if (selectedControl < 0) selectedControl = kControlCount - 1;
                if (selectedControl >= kControlCount) selectedControl = 0;
                refreshControls();
            }
            break;
        }

        case input::Event::Click:
            if (currentScreen == 0) {
                switchModeScreen();
            } else if (currentScreen == 1) {
                const profile::RadioProfile &p = profile::active();
                if (selectedMemory >= 0 &&
                    selectedMemory < static_cast<int>(p.memoryCount)) {
                    const profile::MemoryInfo &memory = p.memories[selectedMemory];
                    radio::tuneTo(memory.freqHz);
                    radio::requestMode(memory.mode, memory.dataMode);
                    currentScreen = 0;
                    lv_scr_load_anim(screens[0], LV_SCR_LOAD_ANIM_FADE_ON,
                                     150, 0, false);
                }
            } else if (currentScreen == 2) {
                const profile::RadioProfile &p = profile::active();
                if (selectedBand >= 0 && selectedBand < static_cast<int>(p.bandCount)) {
                    const profile::BandInfo &band = p.bands[selectedBand];
                    const bool hasLast = bandLastFreq[selectedBand] != 0;
                    radio::tuneTo(hasLast ? bandLastFreq[selectedBand] : band.landingHz);
                    radio::requestMode(hasLast ? bandLastMode[selectedBand]
                                               : band.defaultMode,
                                       hasLast ? bandLastDataMode[selectedBand]
                                               : band.dataMode);
                    currentScreen = 0;
                    lv_scr_load_anim(screens[0], LV_SCR_LOAD_ANIM_FADE_ON,
                                     150, 0, false);
                }
            } else if (currentScreen == 6) {
                const radio::Snapshot s = radio::snapshot();
                switch (selectedControl) {
                    case 0: radio::cycleMode(true); break;
                    case 1:
                        radio::requestModeFilter(s.mode, !s.dataMode, s.filter);
                        break;
                    case 2:
                        radio::requestModeFilter(s.mode, s.dataMode,
                                                 (s.filter % 3) + 1);
                        break;
                    case 3: radio::setPreamp(!s.preamp); break;
                    case 4: radio::setAttenuator(!s.attenuator); break;
                    case 5: {
                        const uint8_t nextAgc = s.agc == 3 ? 1
                                                : s.agc == 1 ? 2
                                                : s.agc == 2 ? 0 : 3;
                        radio::setAgc(nextAgc);
                        break;
                    }
                    case 6: radio::setTuner(!s.tuner); break;
                    case 7: radio::startTune(); break;
                }
                refreshControls();
            } else {
                switchModeScreen();
            }
            break;

        case input::Event::DoubleClick:
            switchMode(+1);
            break;

        case input::Event::LongPress:
            if (currentScreen == 0) {
                cycleTuneStep();
            } else if (currentScreen == 1 || currentScreen == 2) {
                switchModeScreen();
            }
            break;
    }
}

} // namespace

/* ---- Public API ------------------------------------------------------------- */

void begin() {
    buildRadioScreen();
    buildMemoryScreen();
    buildBandSelectScreen();
    buildGraylineScreen();
    buildClockScreen();
    buildBandsScreen();
    buildControlsScreen();
    buildStatusScreen();
    lv_scr_load(screens[0]);
}

void tick() {
    /* Drain every waiting input event first so the knob feels immediate. */
    input::Event e;
    while (input::poll(e)) {
        handleEvent(e);
    }

    /* Periodic refresh of whatever screen is visible. 5 Hz is plenty for
     * text, and it keeps LVGL invalidation work off most frames. */
    static uint32_t lastRefreshMs = 0;
    const uint32_t now = millis();
    if (now - lastRefreshMs >= 200) {
        lastRefreshMs = now;
        switch (currentScreen) {
            case 0: refreshRadio(); break;
            case 1: refreshMemorySelect(); break;
            case 2: refreshBandSelect(); break;
            case 3: refreshGrayline(); break;
            case 4: refreshClock(); break;
            case 5: refreshBands(); break;
            case 6: refreshControls(); break;
            case 7: refreshStatus(); break;
        }
    }
}

} // namespace ui
