# ChartInfo

A minimal Sierra Chart ACSIL study that reproduces the Trade Navigator "Simple Header": a line with the date, time and price, pinned to a fixed corner of the chart window, plus a second line with the range of the bars currently visible on the chart.

```
04/19/2026 15:05 = 6615.00
R=267.50 (105B)
```

The first line has the format `MM/DD/YYYY HH:MM = Price`. The second line shows the range (highest high minus lowest low) of the visible bars in price points, and the number of visible bars in brackets (`105B` = 105 bars). The text is meant to act as a watermark on screenshots, so a trade journal entry always shows the exact market moment the picture was taken and what the picture covers.

## Features

- **Fixed screen position.** The text does not move when you scroll the chart or when the price scale changes. It is placed with relative coordinates (1-100 percent of the window), not with chart time and price.
- **Tick-accurate clock.** Time comes from `sc.LatestDateTimeForLastBar`, so the minute advances inside a single bar (for example on a 60-minute chart) and it also works in Bar Replay.
- **Last price.** Price comes from `sc.Close[sc.ArraySize - 1]`.
- **Range of the visible bars.** Calculated from `sc.IndexOfFirstVisibleBar` and `sc.IndexOfLastVisibleBar` and rounded with `sc.RoundToTickSize`, so it is always a multiple of the symbol's tick (for example `267.50` or `267.25` on ES). It follows scrolling, zooming and interval changes.
- **Cheap to run.** Built for 40+ charts open at the same time (see "Performance design").
- **Configurable look.** Position, color, font size and bold are regular study inputs.

## Inputs

| Index | Input | Default | Notes |
|---|---|---|---|
| 0 | Horizontal Position (1-100) | 149 | X position in percent of the window width. The default is outside 1-100 on purpose for the author's layout; lower it if the text is not visible. |
| 1 | Vertical Position (1-100) | 100 | Y position in percent of the window height. 100 is the top of the chart region. |
| 2 | Text Color | black (0, 0, 0) | |
| 3 | Font Size | 10 | |
| 4 | Use Bold Font | Yes | |
| 5 | Show Range of Visible Bars | Yes | Turns the second line on and off. |
| 6 | Range Line Offset Below (percent) | 3 | How far below the first line the second line is drawn. |
| 7 | Log Level | 2 | Development logging to the Message Log: 0 = off, 1 = events, 2 = events plus a statistics line every 10 seconds, 3 = a line for every draw (floods the log). Set it to 0 for daily use. |

Input indexes must not be renumbered: saved chartbooks depend on them. The text is right aligned (`DT_RIGHT`), drawn in chart region 0.

## Build and install

1. Copy `ChartInfo.cpp` to your Sierra Chart `ACS_Source` folder (for example `C:\SierraChart\ACS_Source`).
2. In Sierra Chart choose **Analysis > Build Custom Studies DLL**, select `ChartInfo.cpp` and build the **64-bit** DLL.
3. Add the study **ChartInfo** to a chart through **Analysis > Studies > Add Custom Study**. After loading, the Message Log shows `ChartInfo V2-dev.1 loaded`; use it to confirm which build is running.

Keep only one DLL in `Data\` that exports `scsf_ChartInfo`. If two DLLs export the same function name, Sierra Chart may load the old one and your changes will seem to have no effect (the message log still says the DLL was loaded).

## How it works

`sc.AutoLoop = 0`, so the function runs once per update and only looks at the last bar and at the visible bars. Both lines are drawn with `sc.UseTool` as `DRAWING_TEXT` objects:

- `UseRelativeVerticalValues = true`, `BeginDateTime` holds X and `BeginValue` holds Y (both as 1-100 values).
- `AddMethod = UTAM_ADD_OR_ADJUST` with the fixed line numbers `2026` (header) and `2027` (range), so the same objects are updated instead of new ones being added each time.
- Both drawings are deleted in the `sc.LastCallToFunction` call, when the study is removed or the chart is closed.

`sc.UpdateAlways = 1` is set because scrolling and zooming change the visible bars without any new data, and the study only sees that when it is called. At rest it is called about 2.5 times per second per chart and almost every call returns at once (see below).

### Performance design

Three gates keep the study almost free. None of them should be removed when extending the study.

1. **Throttling (200 ms).** The study does nothing if less than 0.2 s passed since the last draw, so at most 5 redraws per second even at hundreds of ticks per second.
2. **Change detection.** If neither the price, the minute, the visible bars nor the last bar's high and low changed, the study returns immediately.
3. **Text caching.** The date and time part of the string is formatted once per minute and kept in a persistent `SCString`. On a price tick only the price is appended.

The loop over the visible bars runs only when the visible range changed, when the last bar changed while it is visible, when the range is missing, or on a full recalculation; never on a plain price tick.

All gates are bypassed when `sc.IsFullRecalculation` is true. This is the "first breath": after a symbol or interval change, or a chart reload, Sierra clears the drawings. With no incoming ticks (offline, weekend) the gates would otherwise block the redraw and the header would stay missing until the next tick.

## Compatibility notes

The project was developed on Sierra Chart build 2891 and the author now uses 2908 (64-bit). In build 2891 the compiler did not accept several names that appear in older documentation, so the code deliberately avoids them:

- `SCDT_DATE` / `SCDT_TIME` (date and time are built from `GetYear()`, `GetMonth()`, `GetDay()`, `GetHour()` and `GetMinute()` of `SCDateTime`)
- `sc.AddTextToChartHeader`
- `UseScreenCoordinates`, `UseChartRegionCoordinates` and similar flags
- `sc.UpdateFrequencyInSeconds` and `sc.GetSystemTime()` (throttling uses `sc.GetCurrentDateTime()` instead)
- `sc.DateTimeIn` (the working name is `sc.BaseDateTimeIn`)

These names were not re-checked against build 2908. If you want to use one of them, grep `sierrachart.h` in your own `ACS_Source` first.

## Known limits and ideas

- The study has been tested by the author on a chart and the range and bar count matched manual measurements on different dates and intervals. It has no automated tests.
- Not yet measured: behaviour with live ticks over a long time and the CPU load with 40+ charts open.
- Possible extension: symbol name and interval in the header. It must go through the existing text cache and change detection.
- Possible extension: automatic screenshot names in the form `[DATE]_[SYMBOL]_[INTERVAL].png` for journaling. Any file writing from a study should be a deliberate, explicit decision.

## Versions

Versions are git tags: `chartinfo-v1`, `chartinfo-v2`.

- **V1**: fixed-position `MM/DD/YYYY HH:MM = Price` header with throttling, change detection, text caching, the `IsFullRecalculation` bypass and English code comments.
- **V2**: second line with the range of the visible bars (`R=<points> (<bars>B)`, rounded to the tick), `UpdateAlways`, cleanup of the drawings on removal and development logging (input `Log Level`).

## Files

- `ChartInfo.cpp` - the study source.
- `ChartInfo_README.md` - this file.
