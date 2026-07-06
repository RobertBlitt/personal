# Puckie GUI exploration

This folder records the GUI direction discussed on 2026-07-05. It is design
material only. No firmware behavior is changed by these files.

Open `dial-directions.html` in a desktop browser to compare three lightweight
metallic dial treatments.

## Confirmed decisions

- Double-click selects a top-level main screen.
- Back buttons appear only on child screens and return to that group's main
  screen. Quick Memories and Band Select return to Radio. Clock returns to
  Grayline. Top-level main screens have no back button.
- Live and Demo are explicit data providers. Demo Mode is global and visibly,
  but discreetly, marked `DEMO`. The application must never silently show demo
  data after a live-data failure.
- Spectrum and POTA displays use real data paths in Live Mode and controlled
  sample providers in Demo Mode. The current randomized S-meter pseudo-spectrum
  is not acceptable as live data.
- The dial visual direction is brushed aluminum with a notched coin edge.
- Dial behavior should expose a future haptics hook so visual detents, encoder
  detents, and actuator feedback can eventually share one control model.
- Tuning, RF gain, power, squelch, volume, and filter may open a large
  full-screen dial operated by the physical encoder or touch.
- User display settings, including POTA visibility and Demo Mode, should persist
  across restarts.

## Still to select

Choose the preferred visual direction in `dial-directions.html`:

1. Instrument ring
2. Center knob
3. Hybrid system

The current recommendation is the hybrid: a restrained metal tuning ring on
the main radio screen and a large full-screen dial for selected controls.
