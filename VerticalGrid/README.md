# VerticalGrid

A dotted vertical time grid for Sierra Chart drawn with GDI. The grid spacing follows the chart interval, as in TradeNavigator: daily lines for intraday charts below H1, weekly for H1 to H4, monthly for daily charts, yearly for weekly and monthly charts, and every 10 years for yearly charts. Lines are painted at the midpoints between bars, and the study does not change any chart settings.

## Versions

Versions are git tags: `verticalgrid-v4`, `verticalgrid-v7`, `verticalgrid-v8`, `verticalgrid-v9`.

- **V4**: First attempt at an automatic vertical grid using vertical line drawing tools (incomplete, abandoned).
- **V7**: First fully working version, the grid is painted with GDI instead of drawing tools.
- **V8**: Many speed optimisations: grid map calculated once per bar, cached pen, lines at bar midpoints.
- **V9**: More optimisations: PolyPolyline drawing, forward-bar limit, recalculation only on a new bar.
