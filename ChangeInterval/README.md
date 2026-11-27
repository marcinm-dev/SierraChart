# ChangeInterval

Two tiny Sierra Chart ACSIL studies that add a Control Bar button changing the interval of the chart to a fixed number of minutes with one click: **20M** (`ChangeIntervalToM20.cpp`) and **3M** (`ChangeIntervalToM3.cpp`). Each file is its own study and builds to its own DLL, so each button can be added to a chart separately.

## How it works

The studies do not change the bar period from code (`sc.SetBarPeriodParameters`). Instead, on a button click they post the characters of Sierra Chart's own interval command to the chart window with `PostMessage` and `WM_CHAR` (`2`, `0`, `m`, Enter for 20 minutes; `3`, `m`, Enter for 3 minutes), as if the command had been typed. The chart is then rebuilt once, natively, and keeps its Right Fill Space.

- The unit is a lowercase `m` (minutes), so it does not trigger a keyboard shortcut bound to the uppercase `M` (Monthly).
- The command always contains the unit, so the result does not depend on the interval the chart currently has.
- `sc.DoNotRedrawChartAfterStudyReturns = 1` skips drawing the old interval, `sc.AutoLoop = 0` and `sc.HideStudy = 1` keep the studies out of the way: they react only to the button event.
- The characters go to the window handle of the chart whose button was clicked (`sc.ChartWindowHandle`), not to whichever window has the focus.

## Control Bar buttons

| File | Study name | Button number | Caption |
|---|---|---|---|
| `ChangeIntervalToM20.cpp` | Change Interval to M20 | 50 | `20M` |
| `ChangeIntervalToM3.cpp` | Change Interval to M3 | 51 | `3M` |

The button numbers are fixed in the source. Each button number must be unique among all custom study buttons on the same chart. The numbers are not inputs, so to use another number change `ButtonID` in the source.

## Build and set up

1. Copy the `.cpp` files you need into your Sierra Chart `ACS_Source` folder and build the 64-bit DLL with **Analysis > Build Custom Studies DLL**.
2. Add the study to the chart and add the matching custom study button (50 or 51) to the Control Bar.

## Versions

Versions are git tags: `changeinterval-v1`.

- **V1**: the first working version of both buttons (20M and 3M).

## Known limitations

- The result depends on Sierra Chart's own interval command line accepting the typed characters in the chart window.
- Each study sets the interval in minutes only. A new interval needs a new copy of the file with its own button number and characters.
