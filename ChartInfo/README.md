# ChartInfo

A minimal Sierra Chart ACSIL study that shows a single header line pinned to a fixed corner of the chart window: the date and time of the market and the range of the bars currently visible on the chart. It started as a copy of the Trade Navigator "Simple Header".

```
11/25/2025 16:59 R=267.50 (106B)
```

The format is `MM/DD/YYYY HH:MM R=<range> (<bars>B)`:

- `R=267.50` is the range (highest high minus lowest low) of the visible bars in price points, rounded to the symbol's tick.
- `(106B)` is the number of visible bars. It is drawn in an alert color (red by default) when the number of bars differs from a threshold (default 105), so a screenshot with a different number of bars than your standard one is easy to spot.

The text is meant to act as a watermark on screenshots, so a trade journal entry always shows the exact market moment the picture was taken and how much of the market it covers. The price is deliberately not shown (it is on the price scale anyway).

## Features

- **Single line, fixed screen position.** The text does not move when you scroll the chart or when the price scale changes. It is placed with window-relative coordinates, not with chart time and price.
- **Market clock in the chart's time zone.** On charts built from intraday data the time is the time of the last tick (`sc.LatestDateTimeForLastBar`), so the minute advances inside a single bar and it also works in Bar Replay. Charts built from historical daily data (Daily, Weekly, Monthly, Quarterly) have no time of day: `sc.LatestDateTimeForLastBar` is the date of the last session at 00:00. When that session is today, the current time (replay time while replaying) is used. When it is earlier (weekend, holiday, a "Date Range To" in the past, the cut that D1BarReplay makes during a Bar Replay), the header shows the date of the last session with `00:00`. Both are shown in the time zone of the chart.
- **Clock of the D1BarReplay master (optional).** A Daily or higher chart that is not replaying itself (the receiver of D1BarReplay) can take the date and time of the master chart (the chart with the Bar Replay) while that replay plays or is paused. It needs ChartInfo on the master too: the study on a chart built from intraday data keeps its clock in a persistent `SCDateTime` and the receiver reads it with `GetPersistentSCDateTimeFromChartStudy`. Without ChartInfo on the master, or when the replay is stopped, the receiver uses the rule above. The option works only on charts built from historical daily data; on intraday charts it is ignored.
- **Range of the visible bars.** Calculated from `sc.IndexOfFirstVisibleBar` and `sc.IndexOfLastVisibleBar` and rounded with `sc.RoundToTickSize`, so it is always a multiple of the symbol's tick (for example `267.50` or `267.25` on ES). It follows scrolling, zooming and interval changes.
- **Bar count warning.** The bar count is colored when the number of visible bars is not equal to the threshold (fewer or more).
- **Cheap to run.** Built for 40+ charts open at the same time (see "Performance design").
- **Configurable look.** Position, color, font size and bold are regular study inputs.

## Inputs

| Index | Input | Default | Notes |
|---|---|---|---|
| 0 | Horizontal Position (1-100) | 146 | Horizontal position in the window. It is the point where the main text ends and the bar count starts (see "How it works"). The default 146 is outside 1-100 on purpose: in the author's build values above 100 are accepted and 146 puts the line next to the right edge of the chart region, leaving room for the bar count. Lower it if the text is not visible. |
| 1 | Vertical Position (1-100) | 100 | Vertical position in the window. 100 is the top of the chart region. |
| 2 | Text Color | black (0, 0, 0) | |
| 3 | Font Size | 10 | |
| 4 | Use Bold Font | Yes | |
| 5 | Show Range of Visible Bars | Yes | Turns the range and the bar count on and off. When off, only the date and time are shown. |
| 6 | (reserved, hidden) | | Was the offset of an old second line (V2). |
| 7 | (reserved, hidden) | | Was the development `Log Level` (removed in V4). |
| 8 | Color Bar Count When Not Equal To Threshold | Yes | Turns the coloring of the bar count on and off. |
| 9 | Bar Count Threshold | 105 | The bar count is colored when the number of visible bars is different from this value. |
| 10 | Bar Count Color | red (255, 0, 0) | |
| 11 | Take Time From Master Chart During Its Bar Replay | No | Only for charts built from historical daily data. When on, and the Bar Replay of the master chart plays or is paused, the header shows the date and time of the master. |
| 12 | Master Chart | none | The chart with the Bar Replay (the sender of D1BarReplay). It needs ChartInfo on it. |

Input indexes must not be renumbered: saved chartbooks depend on them. Reserved indexes are left unnamed and `sc.ProcessStudyUnsetInputs = 1` makes Sierra read the inputs after the gap.

## Build and install

1. Copy `ChartInfo.cpp` to your Sierra Chart `ACS_Source` folder (for example `C:\SierraChart\ACS_Source`).
2. In Sierra Chart choose **Analysis > Build Custom Studies DLL**, select `ChartInfo.cpp` and build the **64-bit** DLL.
3. Add the study **ChartInfo** to a chart through **Analysis > Studies > Add Custom Study**. When the study is added or loaded, the Message Log shows `ChartInfo V5 loaded`; use it to confirm which build is running.
4. Set the horizontal position (input 0) so the line sits where you want it.

Keep only one DLL in `Data\` that exports `scsf_ChartInfo`. If two DLLs export the same function name, Sierra Chart may load the old one and your changes will seem to have no effect (the message log still says the DLL was loaded).

## How it works

`sc.AutoLoop = 0`, so the function runs once per update and only looks at the last bar and at the visible bars. The header is drawn with `sc.UseTool` as two `DRAWING_TEXT` objects, because one drawing can have only one color:

- **Line 2026**, right aligned (`DT_RIGHT`): `MM/DD/YYYY HH:MM R=<range>`. It ends at the horizontal position.
- **Line 2027**, left aligned (`DT_LEFT`): ` (<bars>B)`. It starts at the same horizontal position, so the two texts read as one line, and only this part changes color.
- Both use `UseRelativeVerticalValues = true` (`BeginDateTime` holds X and `BeginValue` holds Y) and `AddMethod = UTAM_ADD_OR_ADJUST`, so the same objects are updated instead of new ones being added each time.
- Without a valid range (option off, chart just loaded) only line 2026 with the date and time is drawn and line 2027 is deleted.
- Both drawings are deleted in the `sc.LastCallToFunction` call, when the study is removed or the chart is closed.

`sc.UpdateAlways = 1` is set because scrolling and zooming change the visible bars without any new data, and the study only sees that when it is called. It is called as often as the chart updates (every 200 ms on a live chart) and almost every call returns at once (see below).

### Performance design

Four gates keep the study almost free. None of them should be removed when extending the study.

1. **Throttling (200 ms).** The study does nothing if less than 0.2 s passed since the last draw, so at most 5 redraws per second even at hundreds of ticks per second.
2. **Change detection.** Something is done only when the minute changed, the visible bars changed, the last bar went beyond a cached extreme of the range, or the range was switched on or off. A plain price tick does nothing, because the price is not shown.
3. **Cached range and O(1) updates.** The extremes of the visible bars are cached. The loop over the visible bars runs only when the view changes (scroll, zoom, a new bar), on a full recalculation, when the range is missing, or when the last bar shrank (its high fell or its low rose, for example when Bar Replay steps back). While a bar is forming its high only rises and its low only falls, so a last bar that goes beyond a cached extreme just widens it (`HH = max(HH, High)`, `LL = min(LL, Low)`), and a last bar that stays inside the range does nothing. The last bar's high and low are compared, not its close, so a spike between two calls is not missed.
4. **Text caches.** The date and time part is formatted once per minute. The texts and the color flag last sent to the chart are remembered, and the drawing calls are skipped when nothing visible changed.

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

- Tested by the author on charts of several types and intervals, live and in Bar Replay: the range and the bar count matched manual measurements, the bar count was colored as configured, and the time matched on intraday and daily charts. There are no automated tests.
- On a Daily or higher chart there is no time of the last tick: during a session the clock is the current time, and on a closed market or on a chart that ends in the past the header shows the last session's date with `00:00`. On intraday charts the minute advances with the first tick of a new minute, so it can lag by a few seconds on a quiet market.
- Not yet measured: the CPU load with 40+ charts open. By design nearly every call returns at once.
- Development logging (statistics, events) was removed in V4. It is still in the V2 and V3 tags.
- Possible extension: symbol name and interval in the header. It must go through the existing text caches and change detection.
- Possible extension: automatic screenshot names in the form `[DATE]_[SYMBOL]_[INTERVAL].png` for journaling. Any file writing from a study should be a deliberate, explicit decision.

## Versions

Versions are git tags: `chartinfo-v1`, `chartinfo-v2`, `chartinfo-v3`, `chartinfo-v4`, `chartinfo-v5`.

- **V1**: fixed-position `MM/DD/YYYY HH:MM = Price` header with throttling, change detection, text caching, the `IsFullRecalculation` bypass and English code comments.
- **V2**: second line with the range of the visible bars (`R=<points> (<bars>B)`, rounded to the tick), `UpdateAlways`, cleanup of the drawings on removal and development logging (input `Log Level`).
- **V3**: single line `MM/DD/YYYY HH:MM R=<points> (<bars>B)`: the price is removed and the range moved into the first line; the bar count is a separate drawing that turns red above a threshold (inputs 8-10); the second-line offset input (6) is retired.
- **V4**: the bar count is colored when it differs from the threshold (fewer or more), not only above it; the clock works on Daily and higher charts; cheaper updates (cached range with O(1) updates for the last bar, cache of the drawn text); development logging and its input (7) removed; default horizontal position 146.
- **V5**: Daily and higher charts show the date of the last session with `00:00` when that session is before today (weekend, holiday, "Date Range To" in the past, the cut D1BarReplay makes in a Bar Replay), and the current date and time otherwise. V4 showed the current date and time always, which was wrong on a weekend and on a chart cut to the past. New inputs 11-12: a Daily or higher chart can take the date and time of the D1BarReplay master chart while its Bar Replay runs (ChartInfo is needed on the master).

## Files

- `ChartInfo.cpp` - the study source.
- `README.md` - this file.
