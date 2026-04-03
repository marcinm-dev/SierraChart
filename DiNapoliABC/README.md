# DiNapoliABC

A Sierra Chart ACSIL study that recreates the DiNapoli expansion tool from Trade Navigator. Click three points A, B and C (swing highs and lows): the study marks them with rings and price labels and draws the 0.618, 1.000 and 1.618 levels from point C, with an optional text prefix for each drawing. Has Control Bar buttons to draw, hide and delete.

## Versions

Versions are git tags: `dinapoliabc-v13`, `dinapoliabc-v14`, `dinapoliabc-v15`, `dinapoliabc-v18`.

- **V13**: First saved version: three-click A-B-C expansion tool with 0.618, 1.000 and 1.618 levels, throttled redraw, cached bar indexes and pre-formatted price strings.
- **V14**: Chart range and the Hide Chart Drawings menu ID computed once instead of on every redraw.
- **V15**: 1 px pixel bridge for the 0.618, 1.000 and 1.618 labels so that labels above and below the line sit symmetrically.
- **V18**: Rings drawn as native hollow-circle subgraphs and a price-scale guard that rejects wrong pixels after a restart.
