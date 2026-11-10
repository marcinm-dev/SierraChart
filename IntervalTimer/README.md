# IntervalTimer

A small desktop timer written in Python (tkinter). It counts down a fixed interval, plays a sound when the interval ends and then starts the next one automatically, so it works as a repeating reminder (for example every 3 minutes). It is meant to be started and left alone: its job is to make a sound when it is time to look at the chart. It is not a Sierra Chart study; it is a helper tool kept in this repository.

## How it works

- **Start time**: hour, minute and second at which counting begins (the *Current time* button fills in the current time and starts immediately). Before that moment the window shows how long is left until the start ("Start in", as H:MM:SS when longer than an hour). The start time is never saved: the fields are empty when the program starts.
- **Start time that has already passed**: the timer keeps the grid of intervals that began at that time. Example: start time 14:13:40, interval 2 minutes, and it is now 16:00:05; the next alarm comes at 16:01:40, then 16:03:40 and so on. It never waits until tomorrow.
- **Interval**: length of one countdown in whole minutes, from 1 to 1440 (anything else is rejected with a message). When it reaches zero the sound plays, a message with the real interval stays on screen for 2 seconds, and the next interval begins at once. After a long pause (for example a sleeping computer) the missed intervals are skipped and the sound plays once.
- **Sound** (optional): an `.mp3` or `.wav` file played in a background thread through the Windows system player (MCI, `winmm.dll`), so no extra package is needed. The timer is never silent: if the field is empty, the file is missing or it cannot be played, a system beep (1000 Hz, 0.5 s) sounds instead. When a file is set but does not exist, *Start* asks for confirmation before starting, so the mistake is found before you walk away from the screen.
- **Start sound**: when the "Start in" phase reaches zero the sound plays once and the message "Countdown started!" shows for 2 seconds. A start with no waiting phase (for example *Current time*) plays no start sound.
- **Buttons**: *Start*, *Stop* and *SAVE* in one row under the countdown, spread evenly (*Stop* sits exactly under the countdown text), plus *Current time* and *ClosestInterval* next to the start time fields. *Stop* stops the countdown and shows the interval again.
- **ClosestInterval**: reads the interval, finds the nearest candle close of that length (boundaries counted from midnight, so 3 minutes gives 13:57, 14:00, 14:03 and so on), subtracts 20 seconds of reaction time (constant `REACTION_SECONDS`), writes the result into the start time fields and starts the countdown. Example for a 3-minute interval: at 13:59 it enters 13:59:40, at 20:46 it enters 20:47:40. If the reaction window of the current candle has already passed (for example 13:59:50), it takes the next candle (14:02:40). The sound then plays every interval, always 20 seconds before a candle close. Boundaries match the chart for intervals that divide a day evenly (1, 3, 5, 15, 20, 60 minutes); for 240 minutes they match only if the chart's 4-hour bars also start at midnight.
- **Settings**: the *SAVE* button writes the interval and the path of the sound file (and nothing else) to `timer_settings.json` in the same folder as `timer.py`; the button shows "Saved!" for a moment. The file is read when the program starts (a damaged file is ignored). *Start* does not save anything. The file is personal (it holds a local file path), so it is not part of the repository.
- **Reliability**: the countdown loop survives an error inside a single check (the error is printed in the console and the next check still runs), so a failure cannot silently stop the timer.

The window texts, messages and code comments are in English.

## Run

```
python timer.py
```

Python 3 on Windows with tkinter is required (it is included in the standard Windows installer). There are no other dependencies.

## Versions

Versions are git tags: `intervaltimer-v1`, `intervaltimer-v2`, `intervaltimer-v3`, `intervaltimer-v4`, `intervaltimer-v5`, `intervaltimer-v6`.

- **V1**: first version, as written.
- **V2**: fixes from a code review. *Current time* followed by Start now starts at once instead of tomorrow; input is validated (hour, minute, second ranges, interval of at least 1 minute); a second Start no longer creates a second countdown loop; the end-of-interval message shows the real interval instead of a fixed "3 minutes"; after a long pause the sound plays once instead of once per second; the check runs every 200 ms instead of every 1000 ms, so the sound is late by at most 0.2 s; the settings file lives next to the script and a damaged file no longer stops the program; `playsound` is imported only when a sound is played; the settings button (it only showed a message) and the bare `except` clauses were removed.
- **V3**: new *ClosestInterval* button (start time set to the nearest candle close minus 20 seconds, countdown starts at once), a sound when the "Start in" phase ends, and the button placed next to *Current time*.
- **V4**: the `playsound` package is gone; the sound is played directly through the Windows MCI player. Reason: a program started from a virtual environment (for example `.venv` in PyCharm) failed with "No module named 'playsound'".
- **V5**: the whole tool is in English (window texts, messages, comments). Sound safety: *Start* warns when the chosen sound file does not exist, and a system beep sounds whenever the sound file is missing, empty or unplayable, so a broken sound setting can no longer make the timer silent. The *Reset* button is removed (it only differed from *Stop* in the text it left in the window), and *Start* and *Stop* are spread evenly under the countdown.
- **V6**: fixes from a second code review and a new settings rule. A start time that has already passed (for example one left over from yesterday) no longer postpones the first alarm by a whole day: the interval grid from that time is kept. An error inside one countdown check no longer stops the countdown for good. The interval is limited to 1-1440 minutes (a huge value used to end in an unhandled error). Long waits and intervals show as H:MM:SS instead of "1380:12". New *SAVE* button, to the right of *Stop*; the settings file now holds only the interval and the sound file path, *Start* no longer writes it, and the start time fields are empty at launch (a `start_time` left in an older settings file is ignored).

## Known limitations

- The timer follows the PC clock. A clock change during a countdown (daylight saving time or a manual change) shifts the alarms accordingly.
- The timer does not stop by itself: it keeps ringing every interval until *Stop* is pressed.
- The sound works on Windows only (it uses the system MCI player and `winsound`).
- Testing: V3 and V4 were also tried by hand on Windows (ClosestInterval, the start sound and the `ding` mp3 work). V6 was checked with scripted tests (about 70 checks: time calculation, past start times, the loop surviving an error, interval limits, SAVE and loading, the sound warning and the fallback beep); the SAVE button and the window layout have not been looked at by hand yet.
