# VerticalGrid

A dotted vertical time grid for Sierra Chart drawn with GDI. The grid spacing follows the chart interval, as in TradeNavigator: daily lines for intraday charts below H1, weekly for H1 to H4, monthly for daily charts, yearly for weekly and monthly charts, and every 10 years for yearly charts. Lines are painted at the midpoints between bars, and the study does not change any chart settings.

## Versions

Versions are git tags: `verticalgrid-v4`, `verticalgrid-v7`, `verticalgrid-v8`, `verticalgrid-v9`, `verticalgrid-v11`.

- **V4**: First attempt at an automatic vertical grid using vertical line drawing tools (incomplete, abandoned).
- **V7**: First fully working version, the grid is painted with GDI instead of drawing tools.
- **V8**: Many speed optimisations: grid map calculated once per bar, cached pen, lines at bar midpoints.
- **V9**: More optimisations: PolyPolyline drawing, forward-bar limit, recalculation only on a new bar.
- **V11**: Memory and GDI resource leak fixed (cleanup on last call), attempt at a grid in region 2 dropped.

## Notes

- Versions V1 to V3, V5, V6 and V10 have no code snapshot. V1 to V3 were early chat sessions (research and plans) before the first saved code. V5 and V6 were a specification and an analysis of the functions to use. The work of V10 (the memory leak fix) was saved together with V11, so it has no snapshot of its own.
- V4 is the first attempt: it exports `scsf_HorizontalVerticalGrid` and draws the lines with vertical line drawing tools. It was incomplete and not working, and the author abandoned it. It is kept exactly as saved.
- From V7 on the study function is `scsf_VerticalGridTN`. The source file is `VerticalGrid.cpp` in every version; the author's working file is called `VerticalGridGDI.cpp`.
- The commit dates are the dates the code snapshots were saved (US Eastern time). Code comments are in Polish and unchanged.
- All versions were tested in Sierra Chart by the author.
