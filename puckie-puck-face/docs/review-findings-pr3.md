# Fixes needed on `codex/x6200-puck-core-controls`

Review findings from PR #3, verified against the actual code. Ordered by
severity — items 1-3 are correctness bugs worth fixing before more real-radio
testing; 4-8 are cleanup that can land in a follow-up pass.

## 1. Re-arm pending flags on control-command failure

`firmware/src/radio_client.cpp`, ~lines 197-219

The preamp/attenuator/AGC/tuner blocks in `flushIntents()` do this on failure:

```cpp
if (preampValid) {
    civ::Reply reply;
    if (!transact(civ::makeSetFunction(0x02, preamp ? 1 : 0), reply)) {
        return false;
    }
}
```

Compare to the frequency/mode blocks just above them, which re-arm their
pending flag so the intent survives a reconnect:

```cpp
if (!transact(civ::makeSetFrequency(freqToSend), reply)) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    pendingFreqSend = true;
    xSemaphoreGive(stateMutex);
    return false;
}
```

**Fix:** give each of the four new blocks (preamp, attenuator, AGC, tuner) the
same re-arm treatment — set `pendingPreampValid = true` (and the paired value
field) under the mutex before returning false, mirroring the freq/mode
pattern. Otherwise a transient link error permanently drops the command while
the UI still shows it as applied.

## 2. Control/meter poll failures don't gate link health

`firmware/src/radio_client.cpp`, `controlPollPhase` switch + the
`if (ok) {...} else { markLinkDown(); }` block

`controlOk` is computed per case in the `controlPollPhase` switch
(attenuator/preamp/AGC/tuner/RF/SWR/voltage) but never checked — only `ok`
(from the separate freq/mode/S-meter `pollPhase` switch) affects link status.
If the radio NAKs or times out on one of these registers, that field freezes
forever with no error surfaced.

**Fix:** decide the desired behavior and implement it explicitly — either
(a) fold `controlOk` into the same link-health check so persistent
control-read failures mark the link down and trigger reconnect, or (b) track
per-field staleness (e.g. a `lastGoodControlMs` per field or a
`bool controlsHealthy` flag) and have the UI show something other than
silent stale values when a control hasn't updated in N cycles. Don't leave
it silently ignored.

## 3. `state.filter` not updated after a successful filter change

`firmware/src/radio_client.cpp`, mode-set success path in `flushIntents()`

```cpp
if (modeValid) {
    ...
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    state.mode = mode;
    state.dataMode = dataMode;
    xSemaphoreGive(stateMutex);
}
```

`filter` was sent as part of the same `makeSetMode()` frame but never written
to `state.filter` here, so the Controls screen shows the pre-change filter
until the next periodic mode poll (~1.5s later).

**Fix:** add `state.filter = filter;` alongside the other two assignments in
that block.

## 4. Dead POTA fetch code still runs

`firmware/src/net.cpp`

The POTA screen is gone from `ui.cpp` (confirmed: no remaining references to
`net::pota()` or `PotaData`), but `net.cpp` still runs `refreshPota()` on a
timer every `POTA_REFRESH_MS`, parsing JSON into a 32KB buffer for nothing.

**Fix:** remove `refreshPota()`, `potaData`, `PotaData`/`PotaSpot`,
`net::pota()`, and the `POTA_*` constants from `config.h`, plus the stale
POTA comment in `radio_client.h`. If POTA is coming back later as a
different screen, leave a one-line note in the handoff doc instead of dead
code in the build.

## 5. Duplicate band-edge table

`firmware/src/ui.cpp`, `bandNameForFreq()`, ~line 264

`bandNameForFreq()` hardcodes its own copy of the X6200 band edges, while
`radio_profile.cpp`'s `kX6200Bands` is the actual source of truth (and is
already used two lines below via `profile::bandIndexForFrequency()`).

**Fix:** delete `bandNameForFreq()` and call
`profile::active().bands[profile::bandIndexForFrequency(hz)].name` (or expose
a small `profile::bandNameForFrequency(hz)` helper) instead, so there's one
table for band edges/names.

## 6. Simulator connection eviction changes dev workflow, doesn't fix the real problem

`simulator/x6200_sim.py`, ~line 599

The new one-client-per-source-IP eviction + 15s idle timeout kills a second
local connection from the same IP (e.g. a manual `nc` debug session next to
the running firmware) and drops any session idle >15s. It's also
simulator-only — it doesn't address the actual real-hardware "stale
connection after reflash" problem it was written for, since a real
X6200/bridge has no equivalent eviction.

**Fix:** either (a) make the eviction/timeout opt-in via a CLI flag so
default manual testing isn't affected, or (b) move the actual fix to the
firmware side — have `radio_client.cpp` close/reset its own socket cleanly on
boot/reconnect rather than relying on the simulator to guess and evict. If
the simulator-side fix is kept as a convenience, document in
`simulator/README.md` that it's test-only behavior, not a substitute for a
firmware-side fix.

## 7. Full tick restyle every 200ms

`firmware/src/ui.cpp`, `refreshRadio()`, ~line 601

```cpp
for (int i = 0; i < kTuningTickCount; i++) {
    ...
    lv_obj_set_style_line_color(tuningTicks[i], ...);
    lv_obj_set_style_line_width(tuningTicks[i], ...);
}
```

This restyles all 81 tick objects 5 times a second even though only the
previously-active and newly-active tick actually change.

**Fix:** track the last active tick index in a static/member variable; on
each refresh, only restyle the old index (back to inactive) and the new
index (to active) if it changed.

## 8. Grayline buffer lands in internal SRAM, not PSRAM

`firmware/src/ui.cpp`, ~line 111

```cpp
uint8_t grayCanvasBuf[sizeof(lv_color32_t) * 256 + LAND_MASK_W * LAND_MASK_H];
```

This is a plain global array (~87KB), which the toolchain places in internal
SRAM/.bss, not PSRAM — on a chip already at 42.7% RAM usage, for a buffer
that's only touched once a minute.

**Fix:** allocate it dynamically from PSRAM at startup
(`heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`) and use a pointer instead of a
fixed array, or add `EXT_RAM_BSS_ATTR` if the board support package provides
it.

---

Priority order if time is short: **1-3 before any more real-radio testing**
(silent command loss and frozen indicators are the kind of bug that's
invisible until you're on the air wondering why the attenuator won't turn
off). 4-8 are safe to batch into a follow-up cleanup pass.
