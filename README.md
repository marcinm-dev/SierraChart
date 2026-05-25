# SierraChart

Custom studies for [Sierra Chart](https://www.sierrachart.com/) written in ACSIL (C++). Each study lives in its own folder with its source file and a README that describes what it does, how to build it and what it cannot do.

## Studies

| Study | What it does | Latest version |
|---|---|---|
| [ChartInfo](ChartInfo/) | A single header line pinned to a corner of the chart: market date and time, range of the visible bars and a colored bar count. | V6 (`chartinfo-v6`) |
| [D1BarReplay](D1BarReplay/) | Lets daily and higher-timeframe historical charts show the state of a trading day as of any moment of a Bar Replay. Works on chart data in memory only. | V1 (`d1barreplay-v1`) |
| [DiNapoliRetracement](DiNapoliRetracement/) | DiNapoli retracement grids drawn with two clicks: a Focus point and one or more Reaction points give the 0.382, 0.618 and 1.000 levels. Has Control Bar buttons. | V26 (`dinapoliretracement-v26`) |
| [DMA3x3](DMA3x3/) | Displaced Moving Average 3x3 (SMA of the last 3 closes shifted forward by 3 bars) with a Control Bar show/hide button. | V2 (`dma3x3-v2`) |
| [FibonacciGrid](FibonacciGrid/) | Fibonacci grid of up to 10 levels drawn with two clicks between swing points (wick snapping), with four presets, a settings window and Control Bar buttons. | V23 (`fibonaccigrid-v23`) |

## Other tools

| Tool | What it does | Latest version |
|---|---|---|
| [IntervalTimer](IntervalTimer/) | A small Python (tkinter) desktop timer: counts down a set interval, plays a sound at the end and repeats. Not a Sierra Chart study. | V8 (`intervaltimer-v8`) |

## Building a study

1. Copy the study's `.cpp` file into your Sierra Chart `ACS_Source` folder.
2. In Sierra Chart choose **Analysis > Build Custom Studies DLL**, select the file and build the 64-bit DLL.

## Versions

Versions of a study are git tags named `<study>-v<n>` (for example `chartinfo-v5`). Open the tag to see the exact source of that version. The history of a single study is the history of its folder.
