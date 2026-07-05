# Session handoff: Puckie Puck Face

Written 2026-07-04 at the end of the first Claude Code session, for
whichever agent or human picks this up next. The chat context is gone;
this file and the repo are the source of truth.

## State as of this handoff

- Branch: `claude/xiegu-x6200-wireless-knob-vb09um`, PR #2 (draft) against
  `main` at https://github.com/RobertBlitt/personal/pull/2
- CI: green. `.github/workflows/puckie-tests.yml` runs the simulator unit
  tests, the CI-V codec host tests, the codec-vs-simulator integration
  test, and an LVGL syntax check on every push touching this project.
- Two commits: the one-shot build, then a review pass that fixed three
  real bugs (shared PCF8574 expander init conflict, encoder tuning
  double-count, LVGL float printf in the POTA list) and added the
  integration test plus CI.

## What has never happened yet

1. `pio run` for the actual ESP32-S3 target. The remote sandbox could not
   download the xtensa toolchain (network policy). Must be done on a
   normal machine. Expect at most small fixups.
2. Flashing and hardware bring-up on the CrowPanel.
3. Any contact with a real X6200 (Robert does not have the radio yet).

Flashing steps are in the project README. Simulator quick start is in
`simulator/README.md`.

## Known coordination note

Robert also worked on this project locally with Codex on 2026-07-04
(handoff doc on his Mac at
`~/Documents/Codex/2026-07-04/read-the-handoff-for-the-pickie/HANDOFF_CURRENT.md`).
That work was NOT pushed to GitHub as of this handoff, so remote sessions
cannot see it. Before building further, reconcile: either push the Codex
work to a branch or paste its handoff into the session, and diff against
this branch to avoid duplicate or conflicting implementations.

## Architecture in one paragraph

The firmware (PlatformIO, LVGL 8, CrowPanel 2.1" ESP32-S3) never opens
sockets in UI code. One module, `firmware/src/radio_client.cpp`, speaks
raw CI-V over TCP; everything else calls intent functions and reads a
snapshot. The Python simulator (`simulator/x6200_sim.py`) implements the
radio side of the same protocol from the official Radioddity V1.0.6
document, so the knob cannot tell it from the real radio. When the radio
arrives: `socat TCP-LISTEN:7373,fork,reuseaddr FILE:/dev/ttyACM1,b19200,raw`
on any Linux box, repoint `RADIO_HOST` in `firmware/include/config.h`,
done. Full rationale in `docs/architecture.md`; protocol details and the
flagged verify-on-hardware assumptions in `docs/ci-v-notes.md`.

## Deferred decisions (Phase 2, do not relitigate without new facts)

The permanent transport (Pi bridge vs dual-MCU BT Classic board vs rooted
radio) is deliberately undecided, pending three facts: whether stock BT
CAT needs the WF-01 dongle, whether the radio's combo chip supports BLE,
and whether the internal headers expose a CAT-carrying TTL UART (see
github.com/tom-acco/Xiegu-X6200-Research). The long-press gesture is
reserved in `ui.cpp` for the eventual pairing flow.

## Working preferences (Robert)

- Heavily commented code that teaches; explain concepts inline.
- Accuracy over speculation; flag uncertainties explicitly.
- No em dashes in prose.
