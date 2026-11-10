import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import ctypes
import datetime
import itertools
import json
import math
import os
import threading
import traceback
from pathlib import Path

try:
    import winsound  # Windows only; used for the fallback beep
except ImportError:
    winsound = None

# The settings file lives next to the script, whatever directory the program is started from.
# It holds only the interval and the path of the sound file (written by the Save button).
SETTINGS_FILE = Path(__file__).resolve().parent / "timer_settings.json"

# How often (in milliseconds) the time is checked; the alarm is late by at most this much
TICK_MS = 200
# How long (in seconds) the "interval elapsed" / "countdown started" message stays on screen
MESSAGE_SECONDS = 2
# How many seconds before a candle close the IntervalTime alarm rings (time to react)
REACTION_SECONDS = 20
# Minimum lead (in seconds) between the IntervalTime click and the calculated start
MIN_LEAD_SECONDS = 1
# Longest accepted interval (one day); a larger value is almost certainly a typing mistake
MAX_INTERVAL_MINUTES = 1440
# How long (in milliseconds) the Save button shows "Saved!"
SAVED_FEEDBACK_MS = 1500
# Fallback beep used when the sound file is missing or cannot be played
FALLBACK_BEEP_HZ = 1000
FALLBACK_BEEP_MS = 500

# Counter for unique MCI aliases (every playback gets its own)
SOUND_COUNTER = itertools.count(1)

# Look of the window (a flat, light theme)
UI_FONT = "Segoe UI"
MIN_WINDOW_WIDTH = 560
COUNTDOWN_FONT_SIZE = 22         # the countdown and its messages ("Time left", "Start in", ...)
COLOR_BG = "#f4f6f9"
COLOR_TEXT = "#1f2937"
COLOR_MUTED = "#6b7280"          # idle / no countdown
COLOR_BORDER = "#cbd5e1"
COLOR_BUTTON = "#e5e9f0"
COLOR_BUTTON_HOVER = "#d6dde8"
COLOR_BUTTON_PRESSED = "#c5cedc"
COLOR_ACCENT = "#2563eb"         # Start button, focus border, "waiting" text
COLOR_ACCENT_HOVER = "#1d4ed8"
COLOR_ACCENT_PRESSED = "#1e40af"
COLOR_RUNNING = "#16a34a"        # countdown in progress


def format_hms(total_seconds):
    # MM:SS below one hour, H:MM:SS from one hour on (so a long wait never shows "1380:12")
    total_seconds = max(0, int(total_seconds))
    hours, rest = divmod(total_seconds, 3600)
    minutes, seconds = divmod(rest, 60)
    if hours:
        return f"{hours}:{minutes:02}:{seconds:02}"
    return f"{minutes:02}:{seconds:02}"


class IntervalTimerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Interval Timer")

        self.running = False
        self.interval_seconds = 0
        self.start_time = None
        self.sound_path = ""
        self.after_id = None            # id of the scheduled tick, so it can be cancelled
        self.message_until = None       # until when the "interval elapsed" message stays visible
        self.waiting_for_start = False  # True during the "Start in" phase (the sound plays at the start)

        self.load_settings()

        self.build_ui()
        self.update_countdown_label_initial()

    def setup_style(self):
        # The "clam" theme is flat and can be recoloured; the default Windows theme cannot
        style = ttk.Style()
        style.theme_use("clam")
        self.root.configure(bg=COLOR_BG)
        font = (UI_FONT, 10)

        style.configure(".", background=COLOR_BG, foreground=COLOR_TEXT, font=font)
        style.configure("TFrame", background=COLOR_BG)
        style.configure("TLabel", background=COLOR_BG, foreground=COLOR_TEXT, font=font)
        style.configure("Countdown.TLabel", font=(UI_FONT, COUNTDOWN_FONT_SIZE, "bold"))

        # Entries: white field, thin grey border that turns blue when the field has the focus
        style.configure("TEntry", fieldbackground="white", foreground=COLOR_TEXT, insertcolor=COLOR_TEXT,
                        bordercolor=COLOR_BORDER, lightcolor=COLOR_BORDER, darkcolor=COLOR_BORDER,
                        padding=(5, 5))
        style.map("TEntry", bordercolor=[("focus", COLOR_ACCENT)], lightcolor=[("focus", COLOR_ACCENT)],
                  darkcolor=[("focus", COLOR_ACCENT)])

        # Flat buttons without 3D edges: a normal one and a blue accent one (Start)
        for name, base, hover, pressed, text in (
                ("TButton", COLOR_BUTTON, COLOR_BUTTON_HOVER, COLOR_BUTTON_PRESSED, COLOR_TEXT),
                ("Accent.TButton", COLOR_ACCENT, COLOR_ACCENT_HOVER, COLOR_ACCENT_PRESSED, "white")):
            style.configure(name, background=base, foreground=text, font=font, padding=(14, 7), relief="flat",
                            bordercolor=base, lightcolor=base, darkcolor=base, focuscolor=base)
            colors = [("pressed", pressed), ("active", hover)]
            style.map(name, background=colors, bordercolor=colors, lightcolor=colors, darkcolor=colors)

    def build_ui(self):
        self.setup_style()

        # Everything sits in one frame that is centred in the window
        self.root.columnconfigure(0, weight=1)
        content = ttk.Frame(self.root, padding=(24, 18, 24, 18))
        content.grid(row=0, column=0)

        entry_font = (UI_FONT, 10)

        # Start time: HH:MM:SS + buttons that fill it in
        ttk.Label(content, text="Start time:").grid(row=0, column=0, sticky="e", padx=(0, 12), pady=7)
        time_frame = ttk.Frame(content)
        time_frame.grid(row=0, column=1, sticky="w")

        self.hour_entry = ttk.Entry(time_frame, width=3, justify="center", font=entry_font)
        self.hour_entry.grid(row=0, column=0)
        ttk.Label(time_frame, text=":").grid(row=0, column=1, padx=2)
        self.minute_entry = ttk.Entry(time_frame, width=3, justify="center", font=entry_font)
        self.minute_entry.grid(row=0, column=2)
        ttk.Label(time_frame, text=":").grid(row=0, column=3, padx=2)
        self.second_entry = ttk.Entry(time_frame, width=3, justify="center", font=entry_font)
        self.second_entry.grid(row=0, column=4)
        ttk.Button(time_frame, text="Current time", command=self.fill_current_time).grid(row=0, column=5, padx=(14, 0))
        self.closest_button = ttk.Button(time_frame, text="IntervalTime", command=self.closest_interval)
        self.closest_button.grid(row=0, column=6, padx=(6, 0))

        # Interval (minutes)
        ttk.Label(content, text="Interval (minutes):").grid(row=1, column=0, sticky="e", padx=(0, 12), pady=7)
        self.interval_entry = ttk.Entry(content, width=8, justify="center", font=entry_font)
        self.interval_entry.grid(row=1, column=1, sticky="w")

        # Sound (optional; the system beep is used when it is empty)
        ttk.Label(content, text="Sound (optional):").grid(row=2, column=0, sticky="e", padx=(0, 12), pady=7)
        sound_frame = ttk.Frame(content)
        sound_frame.grid(row=2, column=1, sticky="w")
        self.sound_entry = ttk.Entry(sound_frame, width=30, font=entry_font)
        self.sound_entry.grid(row=0, column=0, padx=(0, 6))
        ttk.Button(sound_frame, text="Browse...", command=self.browse_file).grid(row=0, column=1)

        # Countdown label (spans the form, so it is centred under it)
        self.countdown_label = ttk.Label(content, text="Time left: --:--", style="Countdown.TLabel")
        self.countdown_label.grid(row=3, column=0, columnspan=2, pady=(22, 18))

        # Start / Stop / Save buttons: three equal columns across the width of the form, so the buttons
        # are spread evenly and Stop sits exactly under the countdown label.
        # Save writes the interval and the sound file to the settings file.
        button_frame = ttk.Frame(content)
        button_frame.grid(row=4, column=0, columnspan=2, sticky="ew")
        for column in range(3):
            button_frame.columnconfigure(column, weight=1, uniform="buttons")

        self.start_button = ttk.Button(button_frame, text="Start", width=10, style="Accent.TButton",
                                       command=self.start_timer)
        self.start_button.grid(row=0, column=0)

        self.stop_button = ttk.Button(button_frame, text="Stop", width=10, command=self.stop_timer)
        self.stop_button.grid(row=0, column=1)

        self.save_button = ttk.Button(button_frame, text="Save", width=10, command=self.save_clicked)
        self.save_button.grid(row=0, column=2)

        # Size the window to its content (never smaller), a little wider than the form for air
        self.root.update_idletasks()
        needed_width, needed_height = self.root.winfo_reqwidth(), self.root.winfo_reqheight()
        self.root.minsize(needed_width, needed_height)
        self.root.geometry(f"{max(needed_width, MIN_WINDOW_WIDTH)}x{needed_height}")

        # Load the saved interval and sound file into the fields (the start time is never saved)
        if self.settings.get("interval"):
            self.interval_entry.insert(0, str(self.settings["interval"]))
        if self.settings.get("sound_path"):
            self.sound_entry.insert(0, str(self.settings["sound_path"]))

    def set_time_fields(self, moment):
        self.hour_entry.delete(0, tk.END)
        self.minute_entry.delete(0, tk.END)
        self.second_entry.delete(0, tk.END)
        self.hour_entry.insert(0, f"{moment.hour:02}")
        self.minute_entry.insert(0, f"{moment.minute:02}")
        self.second_entry.insert(0, f"{moment.second:02}")

    def fill_current_time(self):
        self.set_time_fields(datetime.datetime.now())

    @staticmethod
    def closest_alarm_time(now, interval_min):
        # Nearest candle close (boundaries counted from midnight: for 3 min they are 13:57, 14:00, 14:03...)
        # minus REACTION_SECONDS. The result is always at least MIN_LEAD_SECONDS in the future, so when
        # the reaction window of the current candle has already passed, the next candle is used.
        interval = datetime.timedelta(minutes=interval_min)
        reaction = datetime.timedelta(seconds=REACTION_SECONDS)
        lead = datetime.timedelta(seconds=MIN_LEAD_SECONDS)
        midnight = now.replace(hour=0, minute=0, second=0, microsecond=0)
        # first boundary number whose (boundary - reaction) is later than now + lead
        n = (now - midnight + reaction + lead) // interval + 1
        return midnight + n * interval - reaction

    def parse_interval(self):
        # Returns the interval in minutes, or None after showing an error message
        try:
            interval_min = int(self.interval_entry.get())
        except ValueError:
            messagebox.showerror("Error", "The interval must be a whole number of minutes.")
            return None
        if not 1 <= interval_min <= MAX_INTERVAL_MINUTES:
            messagebox.showerror("Error", f"The interval must be between 1 and {MAX_INTERVAL_MINUTES} minutes.")
            return None
        return interval_min

    def closest_interval(self):
        interval_min = self.parse_interval()
        if interval_min is None:
            return

        # Enter the calculated time and start exactly as if Start had been clicked
        self.set_time_fields(self.closest_alarm_time(datetime.datetime.now(), interval_min))
        self.start_timer()

    def browse_file(self):
        file_path = filedialog.askopenfilename(filetypes=[("Audio Files", "*.mp3 *.wav")])
        if file_path:
            self.sound_entry.delete(0, tk.END)
            self.sound_entry.insert(0, file_path)

    def save_settings(self, interval_min, sound_path):
        # Only the interval and the sound file are stored; the start time is different every day
        self.settings = {
            "interval": interval_min,
            "sound_path": sound_path
        }
        try:
            with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
                json.dump(self.settings, f)
            return True
        except OSError as e:
            print("Could not save the settings:", e)
            return False

    def save_clicked(self):
        interval_min = self.parse_interval()
        if interval_min is None:
            return
        if self.save_settings(interval_min, self.sound_entry.get().strip()):
            # Short confirmation on the button itself (no extra window to click away)
            self.save_button.config(text="Saved!")
            self.root.after(SAVED_FEEDBACK_MS, lambda: self.save_button.config(text="Save"))
        else:
            messagebox.showerror("Error", f"Could not write the settings file:\n{SETTINGS_FILE}")

    def load_settings(self):
        self.settings = {}
        try:
            with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                self.settings = data
        except (OSError, ValueError):
            # No file or a damaged JSON: start with empty settings
            pass

    def cancel_loop(self):
        # Cancels the scheduled tick, so there is never more than one countdown loop running
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None

    def start_timer(self):
        try:
            start_hour = int(self.hour_entry.get())
            start_minute = int(self.minute_entry.get())
            start_second = int(self.second_entry.get())
        except ValueError:
            messagebox.showerror(
                "Error",
                "The start time (hour, minute, second) must be whole numbers.\n"
                "Use \"Current time\" or \"IntervalTime\" to fill it in.")
            return
        if not (0 <= start_hour <= 23 and 0 <= start_minute <= 59 and 0 <= start_second <= 59):
            messagebox.showerror("Error", "The start time must be between 00:00:00 and 23:59:59.")
            return

        interval_min = self.parse_interval()
        if interval_min is None:
            return

        # A sound file that was set but does not exist is almost certainly a mistake: say so now,
        # before the user walks away from the screen and relies on the alarm
        sound_path = self.sound_entry.get().strip()
        if sound_path and not os.path.exists(sound_path):
            proceed = messagebox.askokcancel(
                "Sound file not found",
                f"This file does not exist:\n{sound_path}\n\n"
                "The timer will use the system beep instead. Start anyway?")
            if not proceed:
                return

        self.sound_path = sound_path

        # Starting again replaces the previous countdown instead of running a second one
        self.cancel_loop()
        self.message_until = None

        self.interval_seconds = interval_min * 60
        # Compare with one-second precision: a time entered by the "Current time" button
        # must count as "now", not as a time that has already passed
        now = datetime.datetime.now().replace(microsecond=0)
        self.start_time = now.replace(hour=start_hour, minute=start_minute, second=start_second)
        if self.start_time < now:
            # The time has already passed today (for example a leftover from yesterday). Keep the grid
            # of intervals that began at that time: move the start to the beginning of the interval
            # that is running now, so the next alarm comes at the next grid point, never a day later
            passed = int((now - self.start_time).total_seconds())
            full_intervals = passed // self.interval_seconds
            self.start_time += datetime.timedelta(seconds=full_intervals * self.interval_seconds)
        # Play the start sound only when there really was a "Start in" phase
        self.waiting_for_start = self.start_time > now

        self.running = True
        self.update_countdown()

    def stop_timer(self):
        self.running = False
        self.waiting_for_start = False
        self.cancel_loop()
        self.message_until = None
        self.update_countdown_label_initial()

    def update_countdown_label_initial(self):
        # Show the interval before the start, in blue
        try:
            interval_min = int(self.interval_entry.get())
            self.countdown_label.config(text=f"Time left: {format_hms(interval_min * 60)}", foreground=COLOR_ACCENT)
        except ValueError:
            self.countdown_label.config(text="Time left: --:--", foreground=COLOR_MUTED)

    def update_countdown(self):
        self.after_id = None
        if not self.running:
            return
        try:
            self.tick()
        except Exception:
            # Report the problem, but never let one failure stop the countdown:
            # the next tick is always scheduled below
            traceback.print_exc()
        finally:
            if self.running:
                self.after_id = self.root.after(TICK_MS, self.update_countdown)

    def tick(self):
        now = datetime.datetime.now()
        if now < self.start_time:
            # The countdown has not started yet: show the time left until the start
            diff = (self.start_time - now).total_seconds()
            self.countdown_label.config(text=f"Start in: {format_hms(math.ceil(diff))}", foreground=COLOR_ACCENT)
            return

        elapsed = (now - self.start_time).total_seconds()

        if self.waiting_for_start:
            # "Start in" reached zero: sound at the start (unless the program slept through a whole
            # interval, then the interval-end sound below plays, so two sounds never overlap)
            self.waiting_for_start = False
            if elapsed < self.interval_seconds:
                self.countdown_label.config(text="Countdown started!", foreground=COLOR_RUNNING)
                self.message_until = now + datetime.timedelta(seconds=MESSAGE_SECONDS)
                self.play_sound()

        if elapsed >= self.interval_seconds:
            # Interval elapsed: one sound, even if the program slept through several intervals
            # (a sleeping computer); start_time moves by all the full intervals at once
            minutes = self.interval_seconds // 60
            missed = int(elapsed // self.interval_seconds)
            self.start_time += datetime.timedelta(seconds=missed * self.interval_seconds)
            self.countdown_label.config(text=f"Interval elapsed: {minutes} min!", foreground=COLOR_RUNNING)
            self.message_until = now + datetime.timedelta(seconds=MESSAGE_SECONDS)
            self.play_sound()
        elif self.message_until is None or now >= self.message_until:
            self.message_until = None
            remaining = self.interval_seconds - elapsed
            self.countdown_label.config(text=f"Time left: {format_hms(math.ceil(remaining))}", foreground=COLOR_RUNNING)

    def play_sound(self):
        # The timer is never silent: without a playable file the system beep is used instead
        if self.sound_path and os.path.exists(self.sound_path):
            # Playback waits for the end of the sound, so it runs in a separate thread
            threading.Thread(target=self.play_sound_in_background, args=(self.sound_path,), daemon=True).start()
        else:
            if self.sound_path:
                print("Sound file not found, using the system beep:", self.sound_path)
            threading.Thread(target=self.beep_fallback, daemon=True).start()

    @staticmethod
    def beep_fallback():
        if winsound is None:
            print("Sound is supported on Windows only.")
            return
        try:
            winsound.Beep(FALLBACK_BEEP_HZ, FALLBACK_BEEP_MS)
        except Exception as e:
            print("Fallback beep failed:", e)

    @staticmethod
    def play_sound_in_background(path):
        # Playback through the Windows MCI player (winmm.dll): no external packages, works for mp3 and wav
        try:
            winmm = ctypes.windll.winmm
        except AttributeError:
            print("Sound is supported on Windows only.")
            return

        def mci(command):
            error = winmm.mciSendStringW(command, None, 0, None)
            if error:
                buffer = ctypes.create_unicode_buffer(256)
                winmm.mciGetErrorStringW(error, buffer, 256)
                raise RuntimeError(buffer.value or f"MCI error code {error}")

        # A unique alias for every playback, so two sounds at once do not clash
        alias = f"timer_sound_{next(SOUND_COUNTER)}"
        device = "waveaudio" if path.lower().endswith(".wav") else "mpegvideo"
        try:
            mci(f'open "{os.path.normpath(path)}" type {device} alias {alias}')
            try:
                mci(f"play {alias} wait")
            finally:
                winmm.mciSendStringW(f"close {alias}", None, 0, None)
        except Exception as e:
            print("Sound playback error:", e)
            # Better a plain beep than silence
            IntervalTimerApp.beep_fallback()


if __name__ == "__main__":
    root = tk.Tk()
    app = IntervalTimerApp(root)
    root.mainloop()
