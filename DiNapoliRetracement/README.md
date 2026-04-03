# DiNapoliRetracement

A Sierra Chart ACSIL study that recreates the DiNapoli retracement tool from Trade Navigator. Click a Focus point (a swing high or low) and then one or more Reaction points: the study draws the 0.382, 0.618 and 1.000 levels as a column to the right of the last bar.

## Versions

Versions are git tags: `dinapoliretracement-v3`, `dinapoliretracement-v5`.

- **V3**: First saved version: two-click Focus and Reaction tool with 0.382, 0.618 and 1.000 levels and incremental trailing focus.
- **V5**: Rings drawn as native subgraphs, GPU price-scale guard and full subgraph wipe on recalculation.
