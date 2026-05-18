# FibonacciGrid

FibonacciGrid draws a grid of up to 10 Fibonacci levels between two swing points picked with the mouse. Point A is 0 % and point B is 100 %, so bullish and bearish swings use the same formula. Each drawing keeps its own levels (use, ratio, colour and line width), which can be edited in a settings window opened by double-clicking the dotted line between the two points. Four presets are chosen with the FIB 1 to FIB 4 Control Bar buttons, LAST DEL and ALL DEL remove drawings, and the grids can be hidden together with a Master study.

## Versions

Versions are git tags: `fibonaccigrid-v3`, `fibonaccigrid-v4`, `fibonaccigrid-v5`, `fibonaccigrid-v6`, `fibonaccigrid-v7`, `fibonaccigrid-v8`, `fibonaccigrid-v9`.

- **V3**: Base functionality: two-point Fibonacci grid.
- **V4**: Wick snapping, buffer of several drawings and font fix.
- **V5**: Working prototype with a Win32 settings window and .dat persistence.
- **V6**: Settings window fixes and reading the native spine line.
- **V7**: LAST DEL and ALL DEL buttons.
- **V8**: HIDE synchronisation with a Master study (global slot 9999, GPU safe-lock).
- **V9**: Dotted spine line and thicker border levels (0.000 and 1.000).
