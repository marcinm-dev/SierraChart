# Highlight Bar

Ten small ACSIL studies that colour a bar on the main price panel when the bar meets one simple condition. They are ports of Trade Navigator (TradeSense) conditions. Each study is its own source file and builds to its own DLL, so each can be added to a chart separately.

Each study has a Control Bar button that shows or hides it. The button logic is the "Master State" pattern from MACDPredictor and DMA3x3. This state of the sources has not been built or tested in Sierra Chart yet and has no version tag.

## Studies

| File | Bar is coloured when | Colour | Button | Caption |
|---|---|---|---|---|
| `HighlightBar_NewLowerLow.cpp` | `Low < previous Low` | red | 60 | `NEW LOWER LOW` -> `LL` |
| `HighlightBar_NewHigherHigh.cpp` | `High > previous High` | red | 61 | `NEW HIGHER HIGH` -> `HH` |
| `HighlightBar_NewHigherLow.cpp` | `Low >= previous Low` | red | 62 | `NEW HIGHER LOW` -> `HL` |
| `HighlightBar_NewLowerHigh.cpp` | `High <= previous High` | red | 63 | `NEW LOWER HIGH` -> `LH` |
| `HighlightBar_UP_025notequal.cpp` | `(High - Close) < 0.25 * (High - Low)` | green | 64 | `UP25` |
| `HighlightBar_UP_025equal.cpp` | `(High - Close) <= round(0.25 * Range)`: bars passing the strict `<` test green, borderline bars (only `<=`) blue | green + blue | 65 | `UP25e` |
| `HighlightBar_UP_05notequal.cpp` | `(High - Close) < 0.5 * (High - Low)` | green | 66 | `UP50` |
| `HighlightBar_DOWN_025notequal.cpp` | `(Close - Low) < 0.25 * (High - Low)` | red | 67 | `DOWN25` |
| `HighlightBar_DOWN_025equal.cpp` | `(Close - Low) <= round(0.25 * Range)`: bars passing the strict `<` test red, borderline bars (only `<=`) blue | red + blue | 68 | `DOWN25e` |
| `HighlightBar_DOWN_05notequal.cpp` | `(Close - Low) < 0.5 * (High - Low)` | red | 69 | `DOWN50` |

Bars with a zero range are never coloured by the last six studies. The two `equal` studies (`UP25e`, `DOWN25e`) use two subgraphs: bars that pass the strict `<` test are painted green (`UP25e`) or red (`DOWN25e`), and the borderline bars, the ones that sit exactly on the rounded 25% line and pass only the `<=` test, are painted blue. An `equal` study therefore lights everything the `<=` test lights, so the separate strict studies (`UP25`, `DOWN25`) are redundant when an `equal` study is on the chart; if both are shown, the strict bars get the same colour from both and nothing conflicts. Borderline bars are rare (about one per week on M20 for bars of 8+ points). Like Trade Navigator, these six round the threshold (`0.25 * range` or `0.5 * range`) to the tick size with `sc.RoundToTickSize` (to the nearest tick, a half tick goes up) and only then compare. Source comments are in English; the names visible in Sierra Chart (study and subgraph names) are still Polish for the `UP` and `DOWN` studies.

## Control Bar buttons

The button number is an input (`Numer Przycisku`) and must match the Custom Study button number added to the Control Bar. The captions of the first four studies are shortened in the source (`LL`, `HH`, `HL`, `LH`); the table lists the names they replaced. The state is kept in persistent variables 10-12 of each study, so the studies do not interfere with each other. Clicking the button, changing **Hide Study** in the study settings and restarting Sierra Chart are meant to keep button and study in step; showing a hidden study forces a full recalculation. A hidden study skips all bars except the click/F6 bar and the last bar, so it costs almost nothing on a full recalculation. The button does not touch the draw style of the subgraph, so the style chosen in the study settings is kept.

## Building

Copy the `.cpp` files into your Sierra Chart `ACS_Source` folder and build the 64-bit DLL with **Analysis > Build Custom Studies DLL**.
