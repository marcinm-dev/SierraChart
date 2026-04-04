# DiNapoliRetracement

A Sierra Chart ACSIL study that recreates the DiNapoli retracement tool from Trade Navigator. Click a Focus point (a swing high or low) and then one or more Reaction points: the study draws the 0.382, 0.618 and 1.000 levels as a column to the right of the last bar.

## Versions

Versions are git tags: `dinapoliretracement-v3`, `dinapoliretracement-v5`, `dinapoliretracement-v6`, `dinapoliretracement-v7`, `dinapoliretracement-v8`.

- **V3**: First saved version: two-click Focus and Reaction tool with 0.382, 0.618 and 1.000 levels and incremental trailing focus.
- **V5**: Rings drawn as native subgraphs, GPU price-scale guard and full subgraph wipe on recalculation.
- **V6**: Binary .dat persistence, HIDE synced with DiNapoliABC through a shared slot, time-price re-indexing after an interval change.
- **V7**: Space cancels drawing through a character event instead of key polling.
- **V8**: Level labels anchored with a 1 px pixel bridge and label widths cached in the saved data.
