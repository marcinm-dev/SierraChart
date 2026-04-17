# HorizontalGrid

A Sierra Chart ACSIL study that draws a dotted horizontal price grid with GDI and overrides the Y-axis scale increment so that the grid and the price labels always agree. The grid step is chosen automatically from fixed seeds (0.25 ... 1000), tuned for ES.

## Versions

Versions are git tags: `horizontalgrid-v8`.

- **V8**: First working version: dotted horizontal price grid drawn with GDI, step chosen from fixed Micro/Macro seeds with look-ahead upshift, Y-axis scale increment override.
