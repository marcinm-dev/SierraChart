# ChartInfo

A minimal Sierra Chart ACSIL study that shows a single header line pinned to a fixed corner of the chart window: the date and time of the last tick and the range of the bars currently visible on the chart. It started as a copy of the Trade Navigator "Simple Header".

```
11/25/2025 16:59 R=267.50 (106B)
```

The format is `MM/DD/YYYY HH:MM R=<range> (<bars>B)`:

- `R=267.50` is the range (highest high minus lowest low) of the visible bars in price points, rounded to the symbol's tick.
- `(106B)` is the number of visible bars. It turns red (configurable) when there are more bars than a threshold, so a screenshot with too many bars is easy to spot.

The text is meant to act as a watermark on screenshots, so a trade journal entry always shows the exact market moment the picture was taken and how much of the market it covers. The price is deliberately not shown (it is on the price scale anyway).

## Features

- **Single line, fixed screen position.** The text does not move when you scroll the chart or when the price scale changes. It is placed with relative coordinates (1-100 percent of the window), not with chart time and price.
- **Tick-accurate clock.** Time comes from `sc.LatestDateTimeForLastBar`, so the minute advances inside a single bar (for example on a 60-minute chart) and it also works in Bar Replay.
- **Range of the visible bars.** Calculated from `sc.IndexOfFirstVisibleBar` and `sc.IndexOfLastVisibleBar` and rounded with `sc.RoundToTickSize`, so it is always a multiple of the symbol's tick (for example `267.50` or `267.25` on ES). It follows scrolling, zooming and interval changes.
- **Bar count warning.** The bar count is colored when it exceeds a threshold (default 105, red).
- **Cheap to run.** Built for 40+ charts open at the same time (see "Performance design").
- **Configurable look.** Position, color, font size and bold are regular study inputs.

## Inputs

| Index | Input | Default | Notes |
|---|---|---|---|
| 0 | Horizontal Position (1-100) | 149 | X position in percent of the window width. It is the point where the main text ends and the bar count starts (see "How it works"), so set it to the column you want, for example 93-95 for the right edge. The default 149 is the author's old value from the one-text version and is only a starting point. |
| 1 | Vertical Position (1-100) | 100 | Y position in percent of the window height. 100 is the top of the chart region. |
| 2 | Text Color | black (0, 0, 0) | |
| 3 | Font Size | 10 | |
| 4 | Use Bold Font | Yes | |
| 5 | Show Range of Visible Bars | Yes | Turns the range and the bar count on and off. When off, only the date and time are shown. |
| 6 | (reserved, hidden) | | Was the offset of the old second line. Left unnamed so the indexes do not shift. |
| 7 | Log Level | 2 | Development logging to the Message Log: 0 = off, 1 = events, 2 = events plus a statistics line every 10 seconds, 3 = a line for every draw (floods the log). Set it to 0 for daily use. |
| 8 | Color Bar Count Above Threshold | Yes | Turns the coloring of the bar count on and off. |
| 9 | Bar Count Threshold | 105 | The bar count is colored when the number of visible bars is greater than this value. |
| 10 | Bar Count Color | red (255, 0, 0) | |

Input indexes must not be renumbered: saved chartbooks depend on them.

## Build and install

1. Copy `ChartInfo.cpp` to your Sierra Chart `ACS_Source` folder (for example `C:\SierraChart\ACS_Source`).
2. In Sierra Chart choose **Analysis > Build Custom Studies DLL**, select `ChartInfo.cpp` and build the **64-bit** DLL.
3. Add the study **ChartInfo** to a chart through **Analysis > Studies > Add Custom Study**. After loading, the Message Log shows `ChartInfo V3-dev.2 loaded`; use it to confirm which build is running.
4. Set the horizontal position (input 0) so the line sits where you want it.

Keep only one DLL in `Data\` that exports `scsf_ChartInfo`. If two DLLs export the same function name, Sierra Chart may load the old one and your changes will seem to have no effect (the message log still says the DLL was loaded).

## How it works

`sc.AutoLoop = 0`, so the function runs once per update and only looks at the last bar and at the visible bars. The header is drawn with `sc.UseTool` as two `DRAWING_TEXT` objects, because one drawing can have only one color:

- **Line 2026**, right aligned (`DT_RIGHT`): `MM/DD/YYYY HH:MM R=<range>`. It ends at the horizontal position.
- **Line 2027**, left aligned (`DT_LEFT`): ` (<bars>B)`. It starts at the same horizontal position, so the two texts read as one line, and only this part changes color.
- Both use `UseRelativeVerticalValues = true` (`BeginDateTime` holds X and `BeginValue` holds Y as 1-100 values) and `AddMethod = UTAM_ADD_OR_ADJUST`, so the same objects are updated instead of new ones being added each time.
- Without a valid range (option off, chart just loaded) only line 2026 with the date and time is drawn and line 2027 is deleted.
- Both drawings are deleted in the `sc.LastCallToFunction` call, when the study is removed or the chart is closed.

`sc.UpdateAlways = 1` is set because scrolling and zooming change the visible bars without any new data, and the study only sees that when it is called. At rest it is called about 2.5 times per second per chart and almost every call returns at once (see below).

### Performance design

Three gates keep the study almost free. None of them should be removed when extending the study.

1. **Throttling (200 ms).** The study does nothing if less than 0.2 s passed since the last draw, so at most 5 redraws per second even at hundreds of ticks per second.
2. **Change detection.** The header is redrawn only when the minute changed, the visible bars changed, the last bar's high or low changed, or the range was switched on or off. A plain price tick does not redraw anything, because the price is not shown.
3. **Text caching.** The date and time part of the string is formatted once per minute and kept in a persistent `SCString`.

The loop over the visible bars runs only when the visible range changed, when the last bar changed while it is visible, when the range is missing, or on a full recalculation; never on a plain price tick.

All gates are bypassed when `sc.IsFullRecalculation` is true. This is the "first breath": after a symbol or interval change, or a chart reload, Sierra clears the drawings. With no incoming ticks (offline, weekend) the gates would otherwise block the redraw and the header would stay missing until the next tick. Changing a study input also causes a full recalculation, so a new threshold or color shows at once.

## Compatibility notes

The project was developed on Sierra Chart build 2891 and the author now uses 2908 (64-bit). In build 2891 the compiler did not accept several names that appear in older documentation, so the code deliberately avoids them:

- `SCDT_DATE` / `SCDT_TIME` (date and time are built from `GetYear()`, `GetMonth()`, `GetDay()`, `GetHour()` and `GetMinute()` of `SCDateTime`)
- `sc.AddTextToChartHeader`
- `UseScreenCoordinates`, `UseChartRegionCoordinates` and similar flags
- `sc.UpdateFrequencyInSeconds` and `sc.GetSystemTime()` (throttling uses `sc.GetCurrentDateTime()` instead)
- `sc.DateTimeIn` (the working name is `sc.BaseDateTimeIn`)

These names were not re-checked against build 2908. If you want to use one of them, grep `sierrachart.h` in your own `ACS_Source` first.

## Known limits and ideas

- The study has been tested by the author on a chart: the range and the bar count matched manual measurements on different dates and intervals, and the bar count turned red above the threshold. It has no automated tests.
- Not yet measured: behaviour with live ticks over a long time and the CPU load with 40+ charts open.
- Development logging is still in the code (input `Log Level`); a production version would ship without it.
- The leading space of the bar count text is kept by Sierra in the author's build; if it were ever trimmed, the two texts would touch.
- Possible extension: symbol name and interval in the header. It must go through the existing text cache and change detection.
- Possible extension: automatic screenshot names in the form `[DATE]_[SYMBOL]_[INTERVAL].png` for journaling. Any file writing from a study should be a deliberate, explicit decision.

## Versions

Versions are git tags: `chartinfo-v1`, `chartinfo-v2`, `chartinfo-v3`.

- **V1**: fixed-position `MM/DD/YYYY HH:MM = Price` header with throttling, change detection, text caching, the `IsFullRecalculation` bypass and English code comments.
- **V2**: second line with the range of the visible bars (`R=<points> (<bars>B)`, rounded to the tick), `UpdateAlways`, cleanup of the drawings on removal and development logging (input `Log Level`).
- **V3**: single line `MM/DD/YYYY HH:MM R=<points> (<bars>B)`: the price is removed and the range moved into the first line; the bar count is a separate drawing that turns red above a threshold (inputs 8-10); the second-line offset input (6) is retired.

## Files

- `ChartInfo.cpp` - the study source.
- `ChartInfo_README.md` - this file.
