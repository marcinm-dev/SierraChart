# DetrendedOscillator

A Sierra Chart ACSIL study: the DiNapoli Detrended Oscillator (close minus a 7-bar simple moving average, shift 0) in its own pane with levels at 0 and +/-100.

## Versions

Versions are git tags: `detrendedoscillator-v1`, `detrendedoscillator-v2`, `detrendedoscillator-v4`, `detrendedoscillator-v5`, `detrendedoscillator-v6`, `detrendedoscillator-v7`, `detrendedoscillator-v8`.

- **V1**: First working version: SMA(7) detrended oscillator with zero and +/-100 level lines.
- **V2**: Level lines drawn across the whole region and SMA warm-up guard.
- **V4**: Internal SMA array, persistent slots 120-122, Control Bar toggle with ghost mode, clean header, CPU and RAM guards, 20% region height.
- **V5**: Fixed region height input applied on every tick (flickering known).
- **V6**: Split into Engine and Hider studies with startup audit, historical lock and 100 ms throttling.
- **V7**: Renamed to Engine and Switch, bar-close bypass of throttling, gap handling restored, names without ShortName.
- **V8**: Oscillator value label shown on the price scale.

## Notes

- There is no V3: the work of the third chat session was saved together with V4, so it has no snapshot of its own.
- The source file is `DetrendedCombo.cpp` in every version. The DLL name and the call `DetrendedCombo.scsf_DO_Engine` depend on it. The author's notes call the early files `DetrendedOscillator.cpp`; here all versions share one file name.
- V4 and V5 still export `scsf_DetrendedOscillator` without a short name, although the notes describe restoring `scsf_DiNapoliDetrendedOscillator` and the short name `DDO` at the end of the V4 session. That fix was never saved in a snapshot, so the versions are kept exactly as saved.
- The commit dates are the dates the code snapshots were saved (US Eastern time). Code comments are in Polish and unchanged.
- V8 is the version the author has tested in Sierra Chart.
