# Highlight Bar

Ten small ACSIL studies that colour a bar on the main price panel when the bar meets one simple condition. They are ports of Trade Navigator (TradeSense) conditions. Each study is its own source file and builds to its own DLL, so each can be added to a chart separately.

This folder is a snapshot of the sources in their original state, saved before further changes. It has no version tag yet.

## Studies

| File | Bar is coloured when | Colour |
|---|---|---|
| `HighlightBar_NewHigherHigh.cpp` | `High > previous High` | red |
| `HighlightBar_NewHigherLow.cpp` | `Low >= previous Low` | red |
| `HighlightBar_NewLowerHigh.cpp` | `High <= previous High` | red |
| `HighlightBar_NewLowerLow.cpp` | `Low < previous Low` | red |
| `HighlightBar_wzrostowe_025notequal.cpp` | `(High - Close) < 0.25 * (High - Low)` | green |
| `HighlightBar_wzrostowe_025equal.cpp` | `(High - Close) <= 0.25 * (High - Low)` | green |
| `HighlightBar_wzrostowe_05notequal.cpp` | `(High - Close) < 0.5 * (High - Low)` | green |
| `HighlightBar_spadkowe_025notequal.cpp` | `(Close - Low) < 0.25 * (High - Low)` | red |
| `HighlightBar_spadkowe_025equal.cpp` | `(Close - Low) <= 0.25 * (High - Low)` | red |
| `HighlightBar_spadkowe_05notequal.cpp` | `(Close - Low) < 0.5 * (High - Low)` | red |

Bars with a zero range are never coloured by the last six studies. Source comments are in Polish for now.

## Building

Copy the `.cpp` files into your Sierra Chart `ACS_Source` folder and build the 64-bit DLL with **Analysis > Build Custom Studies DLL**.
