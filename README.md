# D1BarReplay

A Sierra Chart ACSIL study that lets daily and higher-timeframe historical charts show the state of a trading day **as of any moment of a Bar Replay**, so indicators on D1/W1/M1 can be screened exactly as they looked at the time of a trade.

Sierra Chart's Bar Replay does not work on daily historical charts (data from `.dly` files). This study works around it by overwriting the High, Low and Close of one bar **in the chart's memory only**. Data files are never written.

> **Status: production version for occasional use (screening trades).** Verified by the author on `ESZ26-CME`: D1, Weekly, Monthly and Quarterly receivers, pause/play/stop of the replay, day and week/month rollover, restart of Sierra Chart with the chartbook saved while the chart was cut. See "Known limitations" for what is untested.

## How it works

```
 SENDER                       RECEIVER (D1 historical)         RECEIVER (W1 / M1 historical)
 D1 chart built from          this study                       this study
 intraday data, Bar Replay    reads the sender's last bar      cuts the chart to the sender's day,
 runs here (no code)          and overwrites H/L/C of the      last (partial) bar gets the period
                              bar of the same trading day      aggregate from the sender's days
```

- **Sender**: an ordinary daily chart built from intraday data ("intraday only"). Bar Replay runs on it, so its last bar always reflects the replay moment.
- **Receiver**: the settlement-based daily historical chart you normally work on. The study follows the sender's trading day automatically, then writes the sender's High/Low/Close into the matching bar (Open and volume are untouched, derived HL/HLC/OHLC averages are updated). Indicators on the receiver, including your own studies, recalculate from the modified bar.
- **No lookahead**: the study sets the chart's date range so that the chart ends on the replayed day. Swing-type indicators (ZigZag, retracements) cannot see later bars.
- **Pause keeps values, Stop restores** the end-of-day settlement values. Removing the study restores the original data and the date range.
- **Weekly / monthly / quarterly / yearly charts**: the last bar becomes the partial period up to the replayed day; High is the maximum and Low the minimum of the period's days, Close is the sender's current price. The days before the replayed day come from a Daily chart (input *Daily chart*, required); the replayed day comes from the sender.
- **Intraday charts**: the study does nothing (they have their own Bar Replay).

Each chart keeps its own copy of the data in RAM, so a modification on one chart does not reach other charts or the files on disk.

## Requirements

- Sierra Chart (developed on version 2908, 64-bit) with ACSIL custom study builds enabled.
- A sender chart and a receiver chart for the same symbol **in the same chartbook**.

## Build and set up

1. Copy `D1BarReplay.cpp` into `ACS_Source` and build via *Analysis > Build Custom Studies DLL* (64-bit).
2. Add the study "D1 Bar Replay (HLC Override)" to the receiver chart and **move it to the top of the study list** (it must calculate before indicators that read its changes).
3. Visible inputs:
   - **Master chart**: select the sender chart. Selecting the receiver's own chart switches the study off (no cutting, no overwriting).
   - **Daily chart (only W/M/Q/Y)**: on weekly/monthly/quarterly/yearly charts select a daily historical chart of the same symbol and contract series (continuous `[C]` weekly/monthly needs a continuous daily chart). The study takes the days of the period before the replayed day from it (file data) and only the replayed day from the sender. Required on those charts, ignored on D1 charts. The chart list has no "none" entry, so selecting this chart itself or the master chart counts as "not set". While the replay plays or is paused and the field is not set, the study overwrites nothing and shows a red text "Ustaw Daily chart" (the former mode that aggregated the whole period from the sender was removed: on `[C]` charts it was off by 50-60 points in periods containing a roll).
4. Start Bar Replay on the sender. Pause to take a screenshot, Stop to return to the original data.

With a Daily chart selected the study checks the contract series: the sender's Open of the replayed day must match that day's Open on the Daily chart, and the period Open on this chart must match the first day's Open on the Daily chart (the Opens must be identical, half a tick of tolerance only for float rounding). If not, a red error text appears in the center of the chart and nothing is overwritten. Another error text appears when the selected chart is not a daily historical chart or has no data for the period.

All other inputs have no name, so Sierra Chart hides them. They still exist (indexes 0-17 are kept so that saved chartbooks do not shift) but the user cannot change them. The former "TURN ON/OFF" input (index 0) is hidden and ignored. An already saved study instance may keep the old visible names until you reset it with *Defaults* or re-add the study.

## Safety

- The study contains no file-writing code, and the `.dly` file hash was verified unchanged before and after test sessions.
- Do not run *Download Data* or *Edit Data* on a chart while an override is active; remove the study first.
- Sierra Chart saves the chartbook with the study's date-range cut active. After a restart the study sets the range again and follows the sender; if the chart still ends too early, untick *Use Date Range* in the chart settings.
- The study leaves *Use Date Range* switched on after it releases the cut (blank start and end date = full history). It does not restore "off" after a restart.

## Known limitations

- Not tested: yearly charts, continuous (`[C]`) charts on dates before the contract roll (the older contract's prices differ from the sender's by tens of points, so the study rejects the values, shows a red error text in the center of the chart and the bar keeps its settlement values), a replayed day that has no bar in the `.dly` file.
- The sender usually holds only a few months of daily history, so quarterly/yearly aggregates may lack data (the study then leaves the bar untouched).
- The native "last price" label on the price scale comes from symbol data and does not follow the override; hide it in chart settings if it gets in the way of screenshots.
- Designed for historical data. Live charts overwrite the last bar with incoming ticks.
- Each day change in the replay reloads the receiver chart (once or twice); a full chartbook with many receivers has not been measured for performance.

## Diagnostics

Most diagnostic log lines were removed from the production version (list in `LOGI_usuniete.md`, version with logs = commit `1e4baa2`). On W/M/Q/Y charts one `[HTF]` line is written to the Message Log whenever the sender's day, the replay status or the study state changes (not on every tick): period type, day, status, data source (Daily / sender), number of days taken from the Daily chart and the first one, Opens, the sender's day, the aggregate H/L/C and the bar's own H/L/C from the file.
