import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import datetime
import json
import math
import os
import threading
from pathlib import Path

# Plik ustawien lezy obok skryptu, niezaleznie od katalogu, z ktorego program uruchomiono
SETTINGS_FILE = Path(__file__).resolve().parent / "timer_settings.json"

# Co ile milisekund sprawdzamy czas (maly koszt, a alarm spoznia sie najwyzej o ten czas)
TICK_MS = 200
# Jak dlugo (w sekundach) zostaje na ekranie komunikat o koncu interwalu
MESSAGE_SECONDS = 2
# O ile sekund przed zamknieciem swiecy ma zadzwonic alarm przycisku ClosestInterval (czas na reakcje)
REACTION_SECONDS = 20
# Minimalny zapas (w sekundach) miedzy kliknieciem ClosestInterval a wyliczonym startem
MIN_LEAD_SECONDS = 1


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
        self.after_id = None        # id zaplanowanego tiku, zeby moc go anulowac
        self.message_until = None   # do kiedy pokazujemy komunikat o koncu interwalu
        self.waiting_for_start = False  # True, gdy trwa faza "Start za" (dzwiek zagra w chwili startu)

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
        self.sound_entry.grid(row=0, column=0, padx=(0, 5))
        ttk.Button(sound_frame, text="Wybierz plik", command=self.browse_file).grid(row=0, column=1)

        # Etykieta odliczania
        self.countdown_label = ttk.Label(self.root, text="Pozostały czas: --:--", font=("Segoe UI", 16))
        self.countdown_label.grid(row=3, column=0, columnspan=3, pady=20)

        # Przyciski Start / Stop / Reset
        button_frame = ttk.Frame(self.root)
        button_frame.grid(row=4, column=0, columnspan=3, pady=10)

        self.start_button = ttk.Button(button_frame, text="Start", command=self.start_timer)
        self.start_button.grid(row=0, column=0, padx=10)

        self.stop_button = ttk.Button(button_frame, text="Stop", command=self.stop_timer)
        self.stop_button.grid(row=0, column=1, padx=10)

        self.reset_button = ttk.Button(button_frame, text="Reset", command=self.reset_timer)
        self.reset_button.grid(row=0, column=2, padx=10)

        self.closest_button = ttk.Button(button_frame, text="ClosestInterval", command=self.closest_interval)
        self.closest_button.grid(row=0, column=3, padx=10)

        # Wczytaj wartości z ustawień do UI
        if self.settings.get("start_time"):
            try:
                h, m, s = map(int, str(self.settings["start_time"]).split(":"))
                self.hour_entry.insert(0, f"{h:02}")
                self.minute_entry.insert(0, f"{m:02}")
                self.second_entry.insert(0, f"{s:02}")
            except ValueError:
                pass
        if self.settings.get("interval"):
            self.interval_entry.insert(0, str(self.settings["interval"]))
        if self.settings.get("sound_path"):
            self.sound_entry.insert(0, self.settings["sound_path"])

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
        # Najblizsze zamkniecie swiecy (granice liczone od polnocy: dla 3 min 13:57, 14:00, 14:03...)
        # pomniejszone o REACTION_SECONDS. Wynik jest zawsze co najmniej MIN_LEAD_SECONDS w przyszlosci,
        # wiec gdy okno reakcji bierzacej swiecy juz minelo, wybieramy nastepna swiece.
        interval = datetime.timedelta(minutes=interval_min)
        reaction = datetime.timedelta(seconds=REACTION_SECONDS)
        lead = datetime.timedelta(seconds=MIN_LEAD_SECONDS)
        midnight = now.replace(hour=0, minute=0, second=0, microsecond=0)
        # pierwszy numer granicy, dla ktorego (granica - reakcja) jest pozniej niz now + lead
        n = (now - midnight + reaction + lead) // interval + 1
        return midnight + n * interval - reaction

    def closest_interval(self):
        try:
            interval_min = int(self.interval_entry.get())
        except ValueError:
            messagebox.showerror("Błąd", "Interwał musi być liczbą całkowitą (minuty).")
            return
        if interval_min < 1:
            messagebox.showerror("Błąd", "Interwał musi wynosić co najmniej 1 minutę.")
            return

        # Wpisujemy wyliczona godzine i startujemy dokladnie tak, jak po kliknieciu Start
        self.set_time_fields(self.closest_alarm_time(datetime.datetime.now(), interval_min))
        self.start_timer()

    def browse_file(self):
        file_path = filedialog.askopenfilename(filetypes=[("Audio Files", "*.mp3 *.wav")])
        if file_path:
            self.sound_entry.delete(0, tk.END)
            self.sound_entry.insert(0, file_path)

    def save_settings(self, start_hour, start_minute, start_second, interval_min, sound_path):
        self.settings = {
            "start_time": f"{start_hour:02}:{start_minute:02}:{start_second:02}",
            "interval": interval_min,
            "sound_path": sound_path
        }
        try:
            with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
                json.dump(self.settings, f)
        except OSError as e:
            # Brak zapisu ustawien nie moze blokowac timera
            print("Nie udało się zapisać ustawień:", e)

    def load_settings(self):
        self.settings = {}
        try:
            with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                self.settings = data
        except (OSError, ValueError):
            # Brak pliku albo uszkodzony JSON: start z pustymi ustawieniami
            pass

    def cancel_loop(self):
        # Anuluje zaplanowany tik, dzieki czemu nigdy nie dziala wiecej niz jedna petla odliczania
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None

    def start_timer(self):
        try:
            start_hour = int(self.hour_entry.get())
            start_minute = int(self.minute_entry.get())
            start_second = int(self.second_entry.get())
            interval_min = int(self.interval_entry.get())
        except ValueError:
            messagebox.showerror("Błąd", "Godzina, minuta, sekunda i interwał muszą być liczbami całkowitymi.")
            return

        if not (0 <= start_hour <= 23 and 0 <= start_minute <= 59 and 0 <= start_second <= 59):
            messagebox.showerror("Błąd", "Godzina musi być w zakresie od 00:00:00 do 23:59:59.")
            return
        if interval_min < 1:
            messagebox.showerror("Błąd", "Interwał musi wynosić co najmniej 1 minutę.")
            return

        self.sound_path = self.sound_entry.get()
        self.save_settings(start_hour, start_minute, start_second, interval_min, self.sound_path)

        # Ponowny Start zastepuje poprzednie odliczanie zamiast uruchamiac drugie
        self.cancel_loop()
        self.message_until = None

        self.interval_seconds = interval_min * 60
        # Porownujemy z dokladnoscia do sekundy: godzina wpisana przyciskiem "Obecny czas"
        # ma byc traktowana jako "teraz", a nie jako godzina, ktora juz minela
        now = datetime.datetime.now().replace(microsecond=0)
        self.start_time = now.replace(hour=start_hour, minute=start_minute, second=start_second)
        if self.start_time < now:
            self.start_time += datetime.timedelta(days=1)
        # Dzwiek na starcie tylko wtedy, gdy rzeczywiscie byla faza "Start za"
        self.waiting_for_start = self.start_time > now

        self.running = True
        self.update_countdown()

    def stop_timer(self):
        self.running = False
        self.waiting_for_start = False
        self.cancel_loop()
        self.message_until = None
        self.update_countdown_label_initial()

    def reset_timer(self):
        self.running = False
        self.waiting_for_start = False
        self.cancel_loop()
        self.message_until = None
        self.start_time = None
        self.interval_seconds = 0
        self.countdown_label.config(text="Pozostały czas: --:--", foreground="black")

    def update_countdown_label_initial(self):
        # Wyświetl interwał przed startem w niebieskim kolorze
        try:
            interval_min = int(self.interval_entry.get())
            self.countdown_label.config(text=f"Pozostały czas: {interval_min:02}:00", foreground="blue")
        except ValueError:
            self.countdown_label.config(text="Pozostały czas: --:--", foreground="black")

    def update_countdown(self):
        self.after_id = None
        if not self.running:
            return

        now = datetime.datetime.now()
        if now < self.start_time:
            # jeszcze nie czas rozpocząć odliczania - wyświetl czas do startu
            diff = (self.start_time - now).total_seconds()
            mins, secs = divmod(math.ceil(diff), 60)
            self.countdown_label.config(text=f"Start za: {mins:02}:{secs:02}", foreground="blue")
            self.after_id = self.root.after(TICK_MS, self.update_countdown)
            return

        elapsed = (now - self.start_time).total_seconds()

        if self.waiting_for_start:
            # "Start za" doszlo do zera: dzwiek na starcie (chyba ze program "przespal" caly interwal,
            # wtedy zagra juz dzwiek konca interwalu ponizej, zeby nie bylo dwoch naraz)
            self.waiting_for_start = False
            if elapsed < self.interval_seconds:
                self.countdown_label.config(text="Start odliczania!", foreground="green")
                self.message_until = now + datetime.timedelta(seconds=MESSAGE_SECONDS)
                self.play_sound()

        if elapsed >= self.interval_seconds:
            # Interwał minął: jeden dźwięk, nawet gdy program "przespał" kilka interwałów
            # (uśpiony komputer); start_time przesuwamy o wszystkie pełne interwały naraz
            minutes = self.interval_seconds // 60
            missed = int(elapsed // self.interval_seconds)
            self.start_time += datetime.timedelta(seconds=missed * self.interval_seconds)
            self.countdown_label.config(text=f"Minął interwał: {minutes} min!", foreground="green")
            self.message_until = now + datetime.timedelta(seconds=MESSAGE_SECONDS)
            self.play_sound()
        elif self.message_until is None or now >= self.message_until:
            self.message_until = None
            remaining = self.interval_seconds - elapsed
            mins, secs = divmod(math.ceil(remaining), 60)
            self.countdown_label.config(text=f"Pozostały czas: {mins:02}:{secs:02}", foreground="green")

        self.after_id = self.root.after(TICK_MS, self.update_countdown)

    def play_sound(self):
        if self.sound_path and os.path.exists(self.sound_path):
            # playsound jest blokujący, więc odtwarzamy w osobnym wątku
            threading.Thread(target=self.play_sound_in_background, args=(self.sound_path,), daemon=True).start()
        else:
            print("Dźwięk nie jest ustawiony lub plik nie istnieje.")

    @staticmethod
    def play_sound_in_background(path):
        # Import tutaj: brak pakietu playsound nie wylacza calego programu, tylko dzwiek
        try:
            from playsound import playsound
            playsound(path)
        except Exception as e:
            print("Błąd odtwarzania dźwięku:", e)


if __name__ == "__main__":
    root = tk.Tk()
    app = IntervalTimerApp(root)
    root.mainloop()
