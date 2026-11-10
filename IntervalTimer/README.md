# IntervalTimer

A small desktop timer written in Python (tkinter). It counts down a fixed interval, plays a sound when the interval ends and then starts the next one automatically, so it works as a repeating reminder (for example every 3 minutes). It is not a Sierra Chart study; it is a helper tool kept in this repository.

## How it works

- **Start time**: hour, minute and second at which counting begins (the *Obecny czas* button fills in the current time). Before that moment the window shows how long is left until the start. If the time has already passed today, the start moves to the same time tomorrow.
- **Interval**: length of one countdown in whole minutes. When it reaches zero the sound plays and the next interval begins at once.
- **Sound** (optional): an `.mp3` or `.wav` file played in a background thread through `playsound`.
- **Buttons**: *Start*, *Stop*, *Reset*. *Ustawienia* only shows an information message.
- **Settings**: the last start time, interval and sound path are saved to `timer_settings.json` next to where the program is run, and loaded on the next launch. That file is personal (it holds a local file path), so it is not part of the repository.

The window texts are in Polish.

## Run

```
pip install playsound
python timer.py
```

Python 3 with tkinter is required (it is included in the standard Windows installer).

## Versions

Versions are git tags: `intervaltimer-v1`.

- **V1**: first version, as written. Known limitations are listed below.

## Known limitations

- The "Obecny czas" button followed by Start moves the start to tomorrow, because the current second has already passed by the time Start is pressed.
- There is no range check: an interval of 0 plays the sound every second, and an hour above 23 or a minute above 59 shows no message.
- Pressing Start twice starts two countdown loops.
- The message shown when an interval ends always says "3 minutes", whatever the interval is.
- After a long pause (for example a sleeping computer) the sound repeats once per second until the missed intervals are caught up.
- `playsound` is an unmaintained package and may fail on newer Python versions.
