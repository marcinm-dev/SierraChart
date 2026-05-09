# DiNapoliRetracement

A Sierra Chart ACSIL study that recreates the DiNapoli retracement tool from Trade Navigator. Click a Focus point (a swing high or low) and then one or more Reaction points: the study draws the 0.382, 0.618 and 1.000 levels as a column to the right of the last bar.

## Versions

Versions are git tags: `dinapoliretracement-v3`, `dinapoliretracement-v5`, `dinapoliretracement-v6`, `dinapoliretracement-v7`, `dinapoliretracement-v8`, `dinapoliretracement-v9`, `dinapoliretracement-v10`, `dinapoliretracement-v11`, `dinapoliretracement-v13`, `dinapoliretracement-v16`, `dinapoliretracement-v17`, `dinapoliretracement-v18`, `dinapoliretracement-v20`, `dinapoliretracement-v21`, `dinapoliretracement-v22`, `dinapoliretracement-v23`.

- **V3**: First saved version: two-click Focus and Reaction tool with 0.382, 0.618 and 1.000 levels and incremental trailing focus.
- **V5**: Rings drawn as native subgraphs, GPU price-scale guard and full subgraph wipe on recalculation.
- **V6**: Binary .dat persistence, HIDE synced with DiNapoliABC through a shared slot, time-price re-indexing after an interval change.
- **V7**: Space cancels drawing through a character event instead of key polling.
- **V8**: Level labels anchored with a 1 px pixel bridge and label widths cached in the saved data.
- **V9**: RETR DEL button (atomic reset) and a separate drawing ID range 40000.
- **V10**: Hide the retracements of small swings on higher timeframes (cluster visibility threshold).
- **V11**: Time-price alignment falls back to the nearest bar and the trailing focus updates its time, for lower timeframes.
- **V13**: Black 1.000 line drawn with native GDI starting 4 px from the ring, redraw gate and a pixel correction input.
- **V16**: Bar containment lookup after an interval change, session bypass with a float tolerance and a live preview.
- **V17**: Snapping of the preview and the clicks to the highs and lows (Snapping Range input).
- **V18**: Pyramid Guard: a new reaction point cannot go beyond the previous one (1-tick bumper).
- **V20**: 2 px black line drawn as two 1 px GDI rows, static GDI pen and cached price strings.
- **V21**: HIDE sync with Expansion: one visibility decision shared with the GDI line, conditional predictive inversion.
- **V22**: Rings redraw after an interval change (GPU safe-lock stabilisation frames, offline heartbeat).
- **V23**: Arming RETR switches the cursor to Pointer through the Windows menu command.
