# IntervalTimer

A small desktop timer written in Python (tkinter). It counts down a fixed interval, plays a sound when the interval ends and then starts the next one automatically, so it works as a repeating reminder (for example every 3 minutes). It is not a Sierra Chart study; it is a helper tool kept in this repository.

## How it works

- **Start time**: hour, minute and second at which counting begins (the *Obecny czas* button fills in the current time and starts immediately). Before that moment the window shows how long is left until the start. If the time has already passed today, the start moves to the same time tomorrow. Values outside 00:00:00-23:59:59 are rejected with a message.
- **Interval**: length of one countdown in whole minutes (at least 1). When it reaches zero the sound plays, a message with the real interval stays on screen for 2 seconds, and the next interval begins at once. After a long pause (for example a sleeping computer) the missed intervals are skipped and the sound plays once.
- **Sound** (optional): an `.mp3` or `.wav` file played in a background thread through `playsound`. A missing `playsound` package only silences the sound, it does not stop the program.
- **Start sound**: when the "Start za" phase reaches zero the sound plays once and the message "Start odliczania!" shows for 2 seconds. A start with no waiting phase (for example *Obecny czas*) plays no start sound.
- **Buttons**: *Start*, *Stop*, *Reset*, *ClosestInterval*. Pressing *Start* again restarts the countdown; it never runs two countdowns at once.
- **ClosestInterval**: reads the interval, finds the nearest candle close of that length (boundaries counted from midnight, so 3 minutes gives 13:57, 14:00, 14:03 and so on), subtracts 20 seconds of reaction time (constant `REACTION_SECONDS`), writes the result into the start time fields and starts the countdown. Example for a 3-minute interval: at 13:59 it enters 13:59:40, at 20:46 it enters 20:47:40. If the reaction window of the current candle has already passed (for example 13:59:50), it takes the next candle (14:02:40). The sound then plays every interval, always 20 seconds before a candle close. Boundaries match the chart for intervals that divide a day evenly (1, 3, 5, 15, 20, 60 minutes); for 240 minutes they match only if the chart's 4-hour bars also start at midnight.
- **Settings**: the last start time, interval and sound path are saved to `timer_settings.json` in the same folder as `timer.py`, and loaded on the next launch (a damaged file is ignored). That file is personal (it holds a local file path), so it is not part of the repository.

The window texts are in Polish.

## Run

```
pip install playsound
python timer.py
```

Python 3 with tkinter is required (it is included in the standard Windows installer).

## Versions

Versions are git tags: `intervaltimer-v1`, `intervaltimer-v2`, `intervaltimer-v3`.

- **V1**: first version, as written.
- **V2**: fixes from a code review. *Obecny czas* followed by Start now starts at once instead of tomorrow; input is validated (hour, minute, second ranges, interval of at least 1 minute); a second Start no longer creates a second countdown loop; the end-of-interval message shows the real interval instead of a fixed "3 minutes"; after a long pause the sound plays once instead of once per second; the check runs every 200 ms instead of every 1000 ms, so the sound is late by at most 0.2 s; the settings file lives next to the script and a damaged file no longer stops the program; `playsound` is imported only when a sound is played; the *Ustawienia* button (it only showed a message) and the bare `except` clauses were removed.

- **V3**: new *ClosestInterval* button (start time set to the nearest candle close minus 20 seconds, countdown starts at once) and a sound when the "Start za" phase ends.

## Known limitations

- If the chosen start time has already passed today, the start moves to tomorrow rather than to the next interval boundary.
- `playsound` is an unmaintained package and may fail on newer Python versions.
- V3 was checked with scripted tests of the time calculation and the countdown logic (including midnight and all standard intervals); the window and the real sound have not been tested by hand.
