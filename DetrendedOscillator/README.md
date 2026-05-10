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
