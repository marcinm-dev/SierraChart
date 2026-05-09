# DiNapoliABC

A Sierra Chart ACSIL study that recreates the DiNapoli expansion tool from Trade Navigator. Click three points A, B and C (swing highs and lows): the study marks them with rings and price labels and draws the 0.618, 1.000 and 1.618 levels from point C, with an optional text prefix for each drawing. Has Control Bar buttons to draw, hide and delete.

## Versions

Versions are git tags: `dinapoliabc-v13`, `dinapoliabc-v14`, `dinapoliabc-v15`, `dinapoliabc-v18`, `dinapoliabc-v19`, `dinapoliabc-v19b`, `dinapoliabc-v20`, `dinapoliabc-v21`, `dinapoliabc-v22`, `dinapoliabc-v23`, `dinapoliabc-v23b`, `dinapoliabc-v24`.

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
