# FibonacciGrid

FibonacciGrid draws a grid of up to 10 Fibonacci levels between two swing points picked with the mouse. Point A is 0 % and point B is 100 %, so bullish and bearish swings use the same formula. Each drawing keeps its own levels (use, ratio, colour and line width), which can be edited in a settings window opened by double-clicking the dotted line between the two points. Four presets are chosen with the FIB 1 to FIB 4 Control Bar buttons, LAST DEL and ALL DEL remove drawings, and the grids can be hidden together with a Master study.

## Versions

Versions are git tags: `fibonaccigrid-v3`, `fibonaccigrid-v4`, `fibonaccigrid-v5`, `fibonaccigrid-v6`, `fibonaccigrid-v7`, `fibonaccigrid-v8`, `fibonaccigrid-v9`, `fibonaccigrid-v10`, `fibonaccigrid-v11`, `fibonaccigrid-v12`, `fibonaccigrid-v13`, `fibonaccigrid-v14`, `fibonaccigrid-v16`, `fibonaccigrid-v17`, `fibonaccigrid-v18`, `fibonaccigrid-v19`, `fibonaccigrid-v20`, `fibonaccigrid-v21`, `fibonaccigrid-v22`.

- **V3**: Base functionality: two-point Fibonacci grid.
- **V4**: Wick snapping, buffer of several drawings and font fix.
- **V5**: Working prototype with a Win32 settings window and .dat persistence.
- **V6**: Settings window fixes and reading the native spine line.
- **V7**: LAST DEL and ALL DEL buttons.
- **V8**: HIDE synchronisation with a Master study (global slot 9999, GPU safe-lock).
- **V9**: Dotted spine line and thicker border levels (0.000 and 1.000).
- **V10**: Four configuration presets (FIB1-FIB4) with a global presets file.
- **V11**: Snapping range as an input parameter.
- **V12**: Passive HIDE bridge: the Slave follows the Master state.
- **V13**: Restore the GDI device context state so the price label does not disappear.
- **V14**: Minimum-bars visibility threshold and price search after a timeframe change.
- **V16**: Stealth header, GPU safe-lock frames input, scroll detection and viewport culling.
- **V17**: Helper functions, one-time window class registration and GDI pen pool.
- **V18**: Cached GDI font and removal of dead code.
- **V19**: Remove the double-drawn spine line.
- **V20**: Settings window: table grid and Reset button.
- **V21**: Settings window: Light Mode, zebra rows and flat buttons.
- **V22**: Per-level line width column in the settings window.
