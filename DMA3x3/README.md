# DMA3x3

A Sierra Chart ACSIL study that draws a **Displaced Moving Average (DMA) 3x3**: a simple moving average of the Close of the last **3** bars, shifted forward by **3** bars. The result is rounded to the symbol's tick size, so it can be read directly as a price level.

The study has a button on the Control Bar that shows or hides it with a single click, and it is built to cost almost nothing while hidden.

## How it works

- **Calculation**: `SMA(Close, Length)`, default `Length = 3` (`sc.SimpleMovAvg`). The first value appears on bar index `Length - 1`.
- **Shift**: done by Sierra Chart itself through the subgraph's *Graphical Displacement* (default 3 bars). Change it in the subgraph settings, no recompilation needed. The calculated values are not moved, only the drawing.
- **Rounding**: the finished result goes through `sc.RoundToTickSize()`, which uses the tick size of the current symbol (0.25 on S&P 500 futures), so it adapts to every instrument.
- **Display**: drawn in the main price panel (navy line, width 1). The value is shown as a label on the price scale and in the Tool Values window, not in the chart data line, the spreadsheet or the study summary.

## Control Bar button

- The button (default number **5**, input *Numer Przycisku*) toggles the study. Its caption is "DMA 3x3" and it is lit while the study is visible.
- The button stays in sync with the *Hide Study* option in the study settings: hiding or showing the study from either place updates the other.
- The study keeps its own state in persistent variables instead of fighting Sierra Chart's background refresh of the study settings.

## Hidden state

While the study is hidden:

- every bar except the event bar (`UpdateStartIndex`) and the last bar returns immediately,
- the SMA is not calculated at all,
- the Control Bar button is touched only when its state changes or after a full recalculation.

When the study is shown again it forces a full recalculation, so the history is rebuilt at once.

## Inputs

| Input | Default | Meaning |
|---|---|---|
| Numer Przycisku | 5 | Number of the Control Bar custom study button |
| Okres SMA | 3 | Length of the moving average (1-1000) |

The input names are currently in Polish.

## Build and set up

1. Copy `DMA3x3.cpp` into `ACS_Source` and build via *Analysis > Build Custom Studies DLL* (64-bit).
2. Add the study "DiNapoli DMA 3x3" to the chart and make sure the Control Bar has the custom study button with the number set in the input.

## Versions

Versions are git tags: `dma3x3-v1`, `dma3x3-v2`.

- **V1**: SMA(Close, 3) with displacement 3, tick rounding and the Control Bar button.
- **V2**: optimised hidden state (early return, no calculation while hidden, button calls only on state change) and English code comments.

## Known limitations

- The displacement is a drawing shift only; it is not an input of the study.
