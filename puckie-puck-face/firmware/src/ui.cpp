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
#include <lvgl.h>
#include <time.h>

#include "civ.h"
#include "config.h"
#include "input.h"
#include "net.h"
#include "radio_client.h"

namespace ui {

namespace {

/* ---- Palette. Dark theme, high contrast, ham-shack-at-night friendly. --- */
constexpr uint32_t kBg = 0x101418;
constexpr uint32_t kPanel = 0x1c2229;
constexpr uint32_t kText = 0xe8eaed;
constexpr uint32_t kDim = 0x8a939e;
constexpr uint32_t kAccent = 0x35c0ed;  /* frequency digits, highlights */
constexpr uint32_t kGood = 0x4cd964;
constexpr uint32_t kWarn = 0xffb300;
constexpr uint32_t kBad = 0xff5252;

/* ---- Screens and the widgets that get updated at runtime ----------------- */
constexpr int kScreenCount = 4;
lv_obj_t *screens[kScreenCount];
int currentScreen = 0;

/* Radio screen */
lv_obj_t *lblFreq;
lv_obj_t *lblMode;
lv_obj_t *lblStep;
lv_obj_t *lblLink;
lv_obj_t *arcSMeter;
lv_obj_t *lblSMeter;

/* Clock screen */
lv_obj_t *lblUtcTime;
lv_obj_t *lblLocalTime;
lv_obj_t *lblDate;

/* Bands screen */
lv_obj_t *lblSolarSummary;
lv_obj_t *lblBandRows[4];

/* POTA screen */
lv_obj_t *lblPotaRows[POTA_MAX_SPOTS];
lv_obj_t *lblPotaStatus;
int potaSelected = 0;

/* Tuning step handling. */
const int32_t kTuneSteps[] = TUNE_STEPS;
int tuneStepIndex = TUNE_STEP_DEFAULT_INDEX;

/* ---- Small helpers ----------------------------------------------------- */

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

/* Render 14074000 as "14.074.000" so MHz/kHz/Hz groups read at a glance,
 * the way radio dials group digits. */
void formatFreq(uint32_t hz, char *out, size_t outSize) {
    const uint32_t mhz = hz / 1000000;
    const uint32_t khz = (hz / 1000) % 1000;
    const uint32_t rest = hz % 1000;
    snprintf(out, outSize, "%lu.%03lu.%03lu",
             (unsigned long)mhz, (unsigned long)khz, (unsigned long)rest);
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

    /* S-meter as a sweeping arc hugging the round bezel. The arc runs
     * along the bottom 270 degrees, like a meter needle unrolled. */
    arcSMeter = lv_arc_create(scr);
    lv_obj_set_size(arcSMeter, 420, 420);
    lv_obj_center(arcSMeter);
    lv_arc_set_rotation(arcSMeter, 135);
    lv_arc_set_bg_angles(arcSMeter, 0, 270);
    lv_arc_set_range(arcSMeter, 0, 255);
    lv_arc_set_value(arcSMeter, 0);
    /* Display only: remove the touch knob so fingers cannot drag it. */
    lv_obj_remove_style(arcSMeter, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arcSMeter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arcSMeter, lv_color_hex(kPanel), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arcSMeter, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arcSMeter, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arcSMeter, 10, LV_PART_INDICATOR);

    lblLink = makeLabel(scr, &lv_font_montserrat_16, kBad, LV_SYMBOL_WIFI " no link");
    lv_obj_align(lblLink, LV_ALIGN_TOP_MID, 0, 56);

    lblMode = makeLabel(scr, &lv_font_montserrat_28, kText, "USB");
    lv_obj_align(lblMode, LV_ALIGN_TOP_MID, 0, 118);

    lblFreq = makeLabel(scr, &lv_font_montserrat_48, kAccent, "--.---.---");
    lv_obj_align(lblFreq, LV_ALIGN_CENTER, 0, -8);

    lblStep = makeLabel(scr, &lv_font_montserrat_16, kDim, "step 1 kHz");
    lv_obj_align(lblStep, LV_ALIGN_CENTER, 0, 40);

    lblSMeter = makeLabel(scr, &lv_font_montserrat_20, kDim, "S 0");
    lv_obj_align(lblSMeter, LV_ALIGN_BOTTOM_MID, 0, -78);

    lv_obj_t *hint = makeLabel(scr, &lv_font_montserrat_12, kDim,
                               "turn: tune   click: step   2x: next screen");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -46);
}

void buildClockScreen() {
    lv_obj_t *scr = screens[1] = makeScreen();

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
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -64);
}

void buildBandsScreen() {
    lv_obj_t *scr = screens[2] = makeScreen();

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
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_BOTTOM_MID, 0, -60);
}

void buildPotaScreen() {
    lv_obj_t *scr = screens[3] = makeScreen();

    makeLabel(scr, &lv_font_montserrat_20, kText, "POTA Spots");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_TOP_MID, 0, 58);

    lblPotaStatus = makeLabel(scr, &lv_font_montserrat_14, kDim, "loading...");
    lv_obj_align(lblPotaStatus, LV_ALIGN_TOP_MID, 0, 90);

    for (int i = 0; i < POTA_MAX_SPOTS; i++) {
        lblPotaRows[i] = makeLabel(scr, &lv_font_montserrat_16, kText, "");
        lv_obj_align(lblPotaRows[i], LV_ALIGN_TOP_MID, 0, 118 + i * 32);
    }

    makeLabel(scr, &lv_font_montserrat_12, kDim, "turn: select   click: tune radio");
    lv_obj_align(lv_obj_get_child(scr, -1), LV_ALIGN_BOTTOM_MID, 0, -56);
}

/* ---- Runtime refresh ------------------------------------------------------- */

void refreshRadio() {
    const radio::Snapshot s = radio::snapshot();

    char buf[24];
    if (s.linkUp) {
        formatFreq(s.freqHz, buf, sizeof(buf));
        lv_label_set_text(lblFreq, buf);
        lv_label_set_text_fmt(lblMode, "%s%s", civ::modeName(s.mode),
                              s.dataMode ? "-D" : "");
        lv_obj_set_style_text_color(lblLink, lv_color_hex(kGood), 0);
        lv_label_set_text(lblLink, LV_SYMBOL_WIFI " linked");
    } else {
        lv_obj_set_style_text_color(lblLink, lv_color_hex(kBad), 0);
        lv_label_set_text(lblLink, LV_SYMBOL_WIFI " no link");
    }

    lv_arc_set_value(arcSMeter, s.linkUp ? s.sMeter : 0);
    /* Rough S-unit mapping for the label: the doc only promises 0-255 as
     * 0-100%, so display S1..S9+ proportionally rather than pretending to
     * calibrated dB. */
    const int sUnits = (s.sMeter * 9) / 255;
    lv_label_set_text_fmt(lblSMeter, "S %d", sUnits);
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

void refreshPota() {
    const net::PotaData p = net::pota();
    if (!p.valid) {
        return;
    }
    lv_label_set_text_fmt(lblPotaStatus, "%d active (newest first)", p.count);
    if (potaSelected >= p.count) {
        potaSelected = p.count - 1;
    }
    for (int i = 0; i < POTA_MAX_SPOTS; i++) {
        if (i < p.count) {
            const net::PotaSpot &s = p.spots[i];
            lv_label_set_text_fmt(lblPotaRows[i], "%s%-10s %7.1fk %s",
                                  i == potaSelected ? LV_SYMBOL_RIGHT " " : "",
                                  s.activator, s.freqHz / 1000.0, s.reference);
            lv_obj_set_style_text_color(
                lblPotaRows[i],
                lv_color_hex(i == potaSelected ? kAccent : kText), 0);
        } else {
            lv_label_set_text(lblPotaRows[i], "");
        }
    }
}

/* ---- Input routing ------------------------------------------------------------ */

void switchScreen(int delta) {
    currentScreen = (currentScreen + delta + kScreenCount) % kScreenCount;
    lv_scr_load_anim(screens[currentScreen], LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, false);
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
            } else if (currentScreen == 3) {
                potaSelected += cw ? 1 : -1;
                if (potaSelected < 0) potaSelected = 0;
                if (potaSelected >= POTA_MAX_SPOTS) potaSelected = POTA_MAX_SPOTS - 1;
                refreshPota();
            }
            break;
        }

        case input::Event::Click:
            if (currentScreen == 0) {
                /* Cycle the tuning step: 10 Hz, 100 Hz, 1 kHz, 10 kHz. */
                tuneStepIndex = (tuneStepIndex + 1) % TUNE_STEP_COUNT;
                const int32_t step = kTuneSteps[tuneStepIndex];
                if (step >= 1000) {
                    lv_label_set_text_fmt(lblStep, "step %ld kHz", (long)(step / 1000));
                } else {
                    lv_label_set_text_fmt(lblStep, "step %ld Hz", (long)step);
                }
            } else if (currentScreen == 3) {
                /* Tune the radio to the selected spot and jump to the
                 * radio screen to watch it land. */
                const net::PotaData p = net::pota();
                if (p.valid && potaSelected < p.count) {
                    const net::PotaSpot &s = p.spots[potaSelected];
                    radio::tuneTo(s.freqHz);
                    /* Pick a sensible mode from the spot listing. */
                    if (strcasecmp(s.modeStr, "CW") == 0) {
                        radio::requestMode((uint8_t)civ::Mode::CW, false);
                    } else if (strcasecmp(s.modeStr, "FT8") == 0 ||
                               strcasecmp(s.modeStr, "FT4") == 0) {
                        radio::requestMode((uint8_t)civ::Mode::USB, true);
                    } else {
                        /* Phone: LSB below 10 MHz by convention. */
                        radio::requestMode(s.freqHz < 10000000
                                               ? (uint8_t)civ::Mode::LSB
                                               : (uint8_t)civ::Mode::USB,
                                           false);
                    }
                    currentScreen = 0;
                    lv_scr_load_anim(screens[0], LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, false);
                }
            }
            break;

        case input::Event::DoubleClick:
            switchScreen(+1);
            break;

        case input::Event::LongPress:
            /* Reserved: this is where the Bluetooth/bridge pairing flow
             * will live. For now it cycles modes as a placeholder so the
             * gesture does something visible. */
            if (currentScreen == 0) {
                radio::cycleMode(true);
            }
            break;
    }
}

} // namespace

/* ---- Public API ------------------------------------------------------------- */

void begin() {
    buildRadioScreen();
    buildClockScreen();
    buildBandsScreen();
    buildPotaScreen();
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
            case 1: refreshClock(); break;
            case 2: refreshBands(); break;
            case 3: refreshPota(); break;
        }
    }
}

} // namespace ui
