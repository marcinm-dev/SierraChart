# D1BarReplay

A Sierra Chart ACSIL study that lets daily and higher-timeframe historical charts show the state of a trading day **as of any moment of a Bar Replay**, so indicators on D1/W1/M1 can be screened exactly as they looked at the time of a trade.

Sierra Chart's Bar Replay does not work on daily historical charts (data from `.dly` files). This study works around it by overwriting the High, Low and Close of one bar **in the chart's memory only**. Data files are never written.

> **Status: prototype.** D1 receiver (manual mode, following a source chart, date-range cut, day rollover) is verified in daily use on `ESZ26-CME`. The weekly/monthly path is written but only partially tested. See "Known limitations".

## How it works

```
 SENDER                       RECEIVER (D1 historical)         RECEIVER (W1 / M1 historical)
 D1 chart built from          this study                       this study
 intraday data, Bar Replay    reads the sender's last bar      cuts the chart to the sender's day,
 runs here (no code)          and overwrites H/L/C of the      last (partial) bar gets the period
                              bar of the same trading day      aggregate from the sender's days
```

- **Sender**: an ordinary daily chart built from intraday data ("intraday only"). Bar Replay runs on it, so its last bar always reflects the replay moment.
- **Receiver**: the settlement-based daily historical chart you normally work on. The study follows the sender's trading day, then writes the sender's High/Low/Close into the matching bar (Open and volume are untouched, derived HL/HLC/OHLC averages are updated). Indicators on the receiver, including your own studies, recalculate from the modified bar.
- **No lookahead**: the study sets the chart's date range so that the chart ends on the replayed day. Swing-type indicators (ZigZag, retracements) cannot see later bars.
- **Pause keeps values, Stop restores** the end-of-day settlement values. Removing the study restores the original data and the previous date range.
- **Weekly / monthly charts**: the last bar becomes the partial period up to the replayed day; High is the maximum and Low the minimum of the period's days (taken from the sender), Close is the sender's current price.
- **Intraday charts**: the study does nothing (they have their own Bar Replay).

Each chart keeps its own copy of the data in RAM, so a modification on one chart does not reach other charts or the files on disk.

## Requirements

- Sierra Chart (developed on version 2908, 64-bit) with ACSIL custom study builds enabled.
- A sender chart and a receiver chart for the same symbol **in the same chartbook**.

## Build and set up

1. Copy `D1BarReplay.cpp` into `ACS_Source` and build via *Analysis > Build Custom Studies DLL* (64-bit).
2. Add the study "D1 Bar Replay (HLC Override)" to the receiver chart and **move it to the top of the study list** (it must calculate before indicators that read its changes).
3. In the study inputs (labels are in Polish):
   - *E2: Wykres zrodlowy* (source chart): select the sender chart. Selecting the receiver's own chart disables source mode.
   - *Apply Override*: Yes.
   - *Cofnij wykres do dnia wybranego bara (brak lookahead)* (cut the chart to the replayed day): Yes.
4. Start Bar Replay on the sender. Pause to take a screenshot, Stop to return to the original data.

Inputs also allow a manual mode (without a source chart): choose the bar with *Bar od konca pelnych danych* (bar counted from the end of the full data), edit High/Low/Close, Apply, Restore (*Restore Original And Reset*).

## Safety

- The study contains no file-writing code, and the `.dly` file hash was verified unchanged before and after test sessions.
- Do not run *Download Data* or *Edit Data* on a chart while an override is active; use Restore or remove the study first.
- The date range is stored in the chartbook. If Sierra Chart crashes while the chart is cut, untick *Use Date Range* in the chart settings.

## Known limitations

- Weekly/monthly aggregation has been verified up to the date-range cut; the value override on those charts is still being tested.
- The sender usually holds only a few months of daily history, so quarterly/yearly aggregates may lack data (the study then leaves the bar untouched).
- The native "last price" label on the price scale comes from symbol data and does not follow the override; hide it in chart settings if it gets in the way of screenshots.
- Designed for historical data. Live charts overwrite the last bar with incoming ticks.

## Diagnostics

Set *Diagnostyka w logu* (diagnostics) to Yes to get a line in the Message Log for every input change, bar load, cut, apply, and sender read. It is verbose during replay; turn it off for daily use.
