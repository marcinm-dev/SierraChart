# HorizontalGrid

A Sierra Chart ACSIL study that draws a dotted horizontal price grid with GDI and overrides the Y-axis scale increment so that the grid and the price labels always agree. The grid step is chosen automatically from fixed seeds (0.25 ... 1000), tuned for ES.

## Versions

Versions are git tags: `horizontalgrid-v8`, `horizontalgrid-v9`, `horizontalgrid-v10`.

- **V8**: First working version: dotted horizontal price grid drawn with GDI, step chosen from fixed Micro/Macro seeds with look-ahead upshift, Y-axis scale increment override.
- **V9**: Performance optimisations: 1x10 dot pattern drawn by one ExtCreatePen line, GDI handle cleanup in LastCallToFunction, cached clip region, state gate that recomputes the step only when the visible range changes.
- **V10**: Batch rendering: all grid lines drawn with one PolyPolyline call, stack buffer limited to 512 lines.

## Notes

- There are no V1-V7 here: those chat sessions produced research, measurements and design notes (step selection engine, line-count tuning tables, instrument detection ideas), but no saved code snapshot. The first saved code is V8.
- The numbering follows the author's notes (V8, V9, V10), so the history starts at V8 on purpose.
- The source file is `HorizontalGrid.cpp` here. In the author's working folder it is called `HorizontalGridGDI.cpp`, and the study function is `scsf_HorizontalGridGDI` with `SCDLLName("HorizontalGridGDI")`. A DLL built from this file is named after the source file.
- The study was narrowed to ES (S&P 500) only; the earlier idea of a universal grid for all instrument types was dropped.
- The commit dates are the dates the code snapshots were saved (US Eastern time). Code comments are in Polish and unchanged.
- All three versions were tested in Sierra Chart by the author.
