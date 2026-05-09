# DiNapoliABC

A Sierra Chart ACSIL study that recreates the DiNapoli expansion tool from Trade Navigator. Click three points A, B and C (swing highs and lows): the study marks them with rings and price labels and draws the 0.618, 1.000 and 1.618 levels from point C, with an optional text prefix for each drawing. Has Control Bar buttons to draw, hide and delete.

## Versions

Versions are git tags: `dinapoliabc-v13`, `dinapoliabc-v14`, `dinapoliabc-v15`, `dinapoliabc-v18`, `dinapoliabc-v19`, `dinapoliabc-v19b`, `dinapoliabc-v20`, `dinapoliabc-v21`, `dinapoliabc-v22`, `dinapoliabc-v23`, `dinapoliabc-v23b`, `dinapoliabc-v24`, `dinapoliabc-v26`.

- **V13**: First saved version: three-click A-B-C expansion tool with 0.618, 1.000 and 1.618 levels, throttled redraw, cached bar indexes and pre-formatted price strings.
- **V14**: Chart range and the Hide Chart Drawings menu ID computed once instead of on every redraw.
- **V15**: 1 px pixel bridge for the 0.618, 1.000 and 1.618 labels so that labels above and below the line sit symmetrically.
- **V18**: Rings drawn as native hollow-circle subgraphs and a price-scale guard that rejects wrong pixels after a restart.
- **V19**: Time-windowed bar-based price snap (Search Range Bars) keeps points A, B and C in place after an interval change.
- **V19b**: Live pixel positions for rings and labels, and expansions whose A-C span is too short on higher timeframes are hidden.
- **V20**: Binary-safe saved data (packed structs, fixed strings), Min Bars Visibility Threshold input and cached menu ID.
- **V21**: HIDE impulse is no longer consumed, so the Retracement study reacts to it at once (master-slave sync).
- **V22**: Black reaction lines drawn with native GDI, offset from the ring (GDI offset input), pen created once.
- **V23**: GPU safe-lock: rings redraw after an interval change (stabilisation frames input, forced recalculation) and a price guard for low-priced instruments.
- **V23b**: Ring and subgraph names and values hidden from the data line, the Chart Values window and the spreadsheet.
- **V24**: Arming the ABC button switches the cursor to Pointer through the Windows menu command.
- **V26**: Custom tagging: a window for a text prefix after point C, prefix drawn left of the level prices, points committed on mouse release, study name and input values hidden.

## Notes

- The version numbers are the number of the author's working session. There is no saved code for V1-V12 (they came before the first saved snapshot; their results are already part of V13), for V16 (research on the look of the rings, the result is visible in V15), for V17 (a first subgraph attempt that V18 replaced) and for V25 (a report on the prefix window; the code first appears in V26). V19b is the second report that was numbered 19, and V23b is a snapshot from 12 April that has no report of its own; both numbers were added here.
- Inside the code the study is called `DiNapoli Expansion` (function `scsf_DiNapoliExpansion`, DLL name `DiNapoliExpansion`); the folder is called `DiNapoliABC` after the author's own file name.
- The history was added to this repository later, from the author's own notes. The commit date of each version is the date on which that code snapshot was saved (US Eastern time), not the day it was uploaded. The code is copied unchanged, including the Polish comments.
