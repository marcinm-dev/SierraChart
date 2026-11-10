import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from playsound import playsound
import datetime
import platform
import os
import json

SETTINGS_FILE = "timer_settings.json"

class IntervalTimerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Interval Timer")
        self.root.geometry("520x300")
        self.root.configure(bg="#f0f2f5")

        self.running = False
        self.interval_seconds = 0
        self.start_time = None
        self.sound_path = ""

        self.load_settings()

        self.build_ui()
        self.update_countdown_label_initial()

    def build_ui(self):
        style = ttk.Style()
        style.configure("TLabel", background="#f0f2f5", font=("Segoe UI", 10))
        style.configure("TButton", font=("Segoe UI", 10), padding=6)
        style.configure("TEntry", font=("Segoe UI", 10))

        # Godzina rozpoczęcia: HH:MM:SS + przycisk obecny czas
        ttk.Label(self.root, text="Godzina rozpoczęcia:").grid(row=0, column=0, sticky="e", padx=5, pady=5)
        time_frame = ttk.Frame(self.root)
        time_frame.grid(row=0, column=1, sticky="w")

        self.hour_entry = ttk.Entry(time_frame, width=3)
        self.hour_entry.grid(row=0, column=0)
        ttk.Label(time_frame, text=":").grid(row=0, column=1)
        self.minute_entry = ttk.Entry(time_frame, width=3)
        self.minute_entry.grid(row=0, column=2)
        ttk.Label(time_frame, text=":").grid(row=0, column=3)
        self.second_entry = ttk.Entry(time_frame, width=3)
        self.second_entry.grid(row=0, column=4)
        ttk.Button(time_frame, text="Obecny czas", command=self.fill_current_time).grid(row=0, column=5, padx=10)

        # Interwał (minuty)
        ttk.Label(self.root, text="Interwał (minuty):").grid(row=1, column=0, sticky="e", padx=5, pady=5)
        self.interval_entry = ttk.Entry(self.root, width=10)
        self.interval_entry.grid(row=1, column=1, sticky="w", padx=5, pady=5)

        # Dźwięk (opcjonalny)
        ttk.Label(self.root, text="Dźwięk (opcjonalny):").grid(row=2, column=0, sticky="e", padx=5, pady=5)
        sound_frame = ttk.Frame(self.root)
        sound_frame.grid(row=2, column=1, sticky="w", padx=5, pady=5)
        self.sound_entry = ttk.Entry(sound_frame, width=25)
        self.sound_entry.grid(row=0, column=0, padx=(0,5))
        ttk.Button(sound_frame, text="Wybierz plik", command=self.browse_file).grid(row=0, column=1)

        # Etykieta odliczania
        self.countdown_label = ttk.Label(self.root, text="Pozostały czas: --:--", font=("Segoe UI", 16))
        self.countdown_label.grid(row=3, column=0, columnspan=3, pady=20)

        # Przyciski Start / Stop / Reset / Ustawienia
        button_frame = ttk.Frame(self.root)
        button_frame.grid(row=4, column=0, columnspan=3, pady=10)

        self.start_button = ttk.Button(button_frame, text="Start", command=self.start_timer)
        self.start_button.grid(row=0, column=0, padx=10)

        self.stop_button = ttk.Button(button_frame, text="Stop", command=self.stop_timer)
        self.stop_button.grid(row=0, column=1, padx=10)

        self.reset_button = ttk.Button(button_frame, text="Reset", command=self.reset_timer)
        self.reset_button.grid(row=1, column=0, padx=10, pady=5)

        self.settings_button = ttk.Button(button_frame, text="Ustawienia", command=self.edit_settings)
        self.settings_button.grid(row=1, column=1, padx=10, pady=5)

        # Wczytaj wartości z ustawień do UI
        if self.settings.get("start_time"):
            try:
                h, m, s = map(int, self.settings["start_time"].split(":"))
                self.hour_entry.insert(0, f"{h:02}")
                self.minute_entry.insert(0, f"{m:02}")
                self.second_entry.insert(0, f"{s:02}")
            except:
                pass
        if self.settings.get("interval"):
            self.interval_entry.insert(0, str(self.settings["interval"]))
        if self.settings.get("sound_path"):
            self.sound_entry.insert(0, self.settings["sound_path"])

    def fill_current_time(self):
        now = datetime.datetime.now()
        self.hour_entry.delete(0, tk.END)
        self.minute_entry.delete(0, tk.END)
        self.second_entry.delete(0, tk.END)
        self.hour_entry.insert(0, f"{now.hour:02}")
        self.minute_entry.insert(0, f"{now.minute:02}")
        self.second_entry.insert(0, f"{now.second:02}")

    def browse_file(self):
        file_path = filedialog.askopenfilename(filetypes=[("Audio Files", "*.mp3 *.wav")])
        if file_path:
            self.sound_entry.delete(0, tk.END)
            self.sound_entry.insert(0, file_path)

    def save_settings(self):
        start_time_str = f"{int(self.hour_entry.get()):02}:{int(self.minute_entry.get()):02}:{int(self.second_entry.get()):02}"
        interval = int(self.interval_entry.get())
        sound_path = self.sound_entry.get()
        self.settings = {
            "start_time": start_time_str,
            "interval": interval,
            "sound_path": sound_path
        }
        with open(SETTINGS_FILE, "w") as f:
            json.dump(self.settings, f)

    def load_settings(self):
        if os.path.exists(SETTINGS_FILE):
            with open(SETTINGS_FILE, "r") as f:
                self.settings = json.load(f)
        else:
            self.settings = {}

    def edit_settings(self):
        messagebox.showinfo("Ustawienia", "W tej wersji ustawienia edytujesz bezpośrednio w polach i zapisujesz przy rozpoczęciu timera.")

    def start_timer(self):
        try:
            start_hour = int(self.hour_entry.get())
            start_minute = int(self.minute_entry.get())
            start_second = int(self.second_entry.get())
            interval_min = int(self.interval_entry.get())
            self.sound_path = self.sound_entry.get()
        except Exception as e:
            messagebox.showerror("Błąd", f"Nieprawidłowe dane wejściowe: {e}")
            return

        self.save_settings()

        self.interval_seconds = interval_min * 60
        now = datetime.datetime.now()
        self.start_time = now.replace(hour=start_hour, minute=start_minute, second=start_second, microsecond=0)
        if self.start_time < now:
            self.start_time += datetime.timedelta(days=1)

        self.running = True
        self.update_countdown()

    def stop_timer(self):
        self.running = False
        self.update_countdown_label_initial()

    def reset_timer(self):
        self.running = False
        self.start_time = None
        self.interval_seconds = 0
        self.countdown_label.config(text="Pozostały czas: --:--", foreground="black")

    def update_countdown_label_initial(self):
        # Wyświetl interwał przed startem w niebieskim kolorze
        try:
            interval_min = int(self.interval_entry.get())
            self.countdown_label.config(text=f"Pozostały czas: {interval_min:02}:00", foreground="blue")
        except:
            self.countdown_label.config(text="Pozostały czas: --:--", foreground="black")

    def update_countdown(self):
        if not self.running:
            return

        now = datetime.datetime.now()
        if now < self.start_time:
            # jeszcze nie czas rozpocząć odliczania - wyświetl czas do startu
            diff = (self.start_time - now).total_seconds()
            mins, secs = divmod(int(diff), 60)
            self.countdown_label.config(text=f"Start za: {mins:02}:{secs:02}", foreground="blue")
            self.root.after(1000, self.update_countdown)
            return

        elapsed = (now - self.start_time).total_seconds()
        remaining = self.interval_seconds - elapsed

        if remaining <= 0:
            self.countdown_label.config(text="Minęły 3 minuty!", foreground="green")
            self.play_sound()
            # Restartujemy odliczanie (start_time przesuwamy o interwał)
            self.start_time = self.start_time + datetime.timedelta(seconds=self.interval_seconds)
            self.root.after(1000, self.update_countdown)
            return

        mins, secs = divmod(int(remaining), 60)
        self.countdown_label.config(text=f"Pozostały czas: {mins:02}:{secs:02}", foreground="green")

        # Sprawdzaj co 1 sekundę, można ustawić np. 15000 ms (15 sekund) jeśli chcesz mniejsze obciążenie
        self.root.after(1000, self.update_countdown)

    def play_sound(self):
        if self.sound_path and os.path.exists(self.sound_path):
            try:
                # playsound jest blokujący, więc uruchomimy w tle wątku
                import threading
                threading.Thread(target=playsound, args=(self.sound_path,), daemon=True).start()
            except Exception as e:
                print("Błąd odtwarzania dźwięku:", e)
        else:
            print("Dźwięk nie jest ustawiony lub plik nie istnieje.")

if __name__ == "__main__":
    root = tk.Tk()
    app = IntervalTimerApp(root)
    root.mainloop()
