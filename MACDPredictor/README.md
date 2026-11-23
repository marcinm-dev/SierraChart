# MACDPredictor

DiNapoli MACD Predictor for Sierra Chart: draws the price at which MACD would cross its signal line on the next bar, replicating the TradeNavigator formula, with a Control Bar button to show or hide it.

## Versions

Versions are git tags: `macdpredictor-v2`, `macdpredictor-v3`, `macdpredictor-v4`.

- **V2**: Control Bar toggle button with Master State, TradeNavigator-replica MACD predictor line.
- **V3**: Stealth look: clean header, value label on the price scale, thinner line, button 3.
- **V4**: Code comments translated from Polish to English; the code itself is unchanged.

## Notes

- The version numbers are the number of the author's working session. There is no saved code for V1 (an analysis of the TradeNavigator formula before the first saved snapshot; its result is already part of V2).
- Inside the code the study is called `DiNapoli MACD Predictor` (function `scsf_DiNapoliMACDP`, DLL name `DiNapoli_MACDP_Navigator_Replica_v10`; the "v10" in that name is a leftover, not a version); the folder is called `MACDPredictor` after the author's own file name.
- The history was added to this repository later, from the author's own notes. The commit date of each version is the date on which that code snapshot was saved (US Eastern time), not the day it was uploaded. The code of V2 and V3 is copied unchanged, including the Polish comments; V4 translates the comments.
