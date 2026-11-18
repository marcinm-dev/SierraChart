import os
import tkinter as tk
from tkinter import filedialog, messagebox


def rename_files_without_prefix(folder_path, new_names):
    files = sorted(os.listdir(folder_path),
                   key=lambda x: int(''.join(filter(str.isdigit, x))) if any(c.isdigit() for c in x) else float('inf'))

    if len(files) != len(new_names):
        messagebox.showerror("Błąd", "Liczba plików w folderze nie zgadza się z liczbą podanych nazw!")
        return

    for old_name, new_name in zip(files, new_names):
        try:
            old_path = os.path.join(folder_path, old_name)
            new_path = os.path.join(folder_path, new_name + os.path.splitext(old_name)[1])
            os.rename(old_path, new_path)
        except Exception as e:
            print(f'Błąd podczas zmiany nazwy {old_name}: {e}')
            continue


def add_prefix_to_files(folder_path, prefix):
    files = sorted(os.listdir(folder_path),
                   key=lambda x: int(''.join(filter(str.isdigit, x))) if any(c.isdigit() for c in x) else float('inf'))

    for old_name in files:
        try:
            old_path = os.path.join(folder_path, old_name)
            new_name = str(prefix) + old_name
            new_path = os.path.join(folder_path, new_name)
            os.rename(old_path, new_path)
        except Exception as e:
            print(f'Błąd podczas zmiany nazwy {old_name}: {e}')
            continue


def add_prefix_only(folder_path, prefix):
    files = sorted(os.listdir(folder_path),
                   key=lambda x: int(''.join(filter(str.isdigit, x))) if any(c.isdigit() for c in x) else float('inf'))
    for old_name in files:
        try:
            old_path = os.path.join(folder_path, old_name)
            new_name = str(prefix) + old_name
            new_path = os.path.join(folder_path, new_name)
            os.rename(old_path, new_path)
        except Exception as e:
            print(f'Błąd podczas dodawania przedrostka {old_name}: {e}')
            continue


def select_folder():
    folder_path = filedialog.askdirectory()
    if folder_path:
        entry_folder.delete(0, tk.END)
        entry_folder.insert(0, folder_path)


def start_renaming():
    folder_path = entry_folder.get()
    prefix = entry_prefix.get()
    use_prefix = prefix_var.get()
    mode = mode_var.get()

    if not prefix.isdigit():
        messagebox.showerror("Błąd", "Przedrostek musi być liczbą!")
        return
    prefix = int(prefix)

    if not os.path.isdir(folder_path):
        messagebox.showerror("Błąd", "Podana ścieżka folderu jest nieprawidłowa!")
        return

    if mode == "13":
        new_names = ["0", "0_1", "1_1", "1", "2", "3", "4", "5", "6", "8_3", "8_2", "8_1", "8"]
    elif mode == "14":
        new_names = ["0", "0_1", "1_1", "1", "2", "3", "4", "5", "6", "8_4", "8_3", "8_2", "8_1", "8"]
    else:
        messagebox.showerror("Błąd", "Nie wybrano trybu nazewnictwa.")
        return

    rename_files_without_prefix(folder_path, new_names)

    if use_prefix:
        add_prefix_to_files(folder_path, prefix)


def add_prefix_only_from_gui():
    folder_path = entry_folder.get()
    prefix = entry_prefix_only.get()

    if not prefix.isdigit():
        messagebox.showerror("Błąd", "Przedrostek musi być liczbą!")
        return
    prefix = int(prefix)

    if not os.path.isdir(folder_path):
        messagebox.showerror("Błąd", "Podana ścieżka folderu jest nieprawidłowa!")
        return

    add_prefix_only(folder_path, prefix)


# GUI
root = tk.Tk()
root.title("Zmiana nazw plików")
root.geometry("500x420")

# Folder
frame = tk.Frame(root)
frame.pack(pady=10)
entry_folder = tk.Entry(frame, width=40)
entry_folder.pack(side=tk.LEFT, padx=5)
btn_browse = tk.Button(frame, text="Wybierz folder", command=select_folder)
btn_browse.pack(side=tk.LEFT)

# Przedrostek
frame_prefix = tk.Frame(root)
frame_prefix.pack(pady=10)
tk.Label(frame_prefix, text="Przedrostek:").pack(side=tk.LEFT)
entry_prefix = tk.Entry(frame_prefix, width=5)
entry_prefix.pack(side=tk.LEFT, padx=5)
entry_prefix.insert(0, "0")
prefix_var = tk.BooleanVar()
prefix_checkbox = tk.Checkbutton(root, text="Dodaj przedrostek", variable=prefix_var)
prefix_checkbox.pack()

# Tryb + przycisk
frame_mode_and_button = tk.Frame(root)
frame_mode_and_button.pack(pady=20)

# Lewa strona: wybór trybu
frame_mode = tk.Frame(frame_mode_and_button)
frame_mode.pack(side=tk.LEFT, padx=20)
mode_var = tk.StringVar(value="13")
tk.Label(frame_mode, text="Wybierz tryb nazewnictwa:").pack()
tk.Radiobutton(frame_mode, text="13 plików", variable=mode_var, value="13").pack(anchor='w')
tk.Radiobutton(frame_mode, text="14 plików", variable=mode_var, value="14").pack(anchor='w')

# Prawa strona: przycisk zmiany nazw
btn_start = tk.Button(frame_mode_and_button, text="Zmień\nnazwy\nplików", command=start_renaming, width=10, height=4)
btn_start.pack(side=tk.RIGHT)

# Przerwa
tk.Label(root, text="").pack(pady=10)

# Dodanie tylko przedrostka
frame_prefix_only = tk.Frame(root)
frame_prefix_only.pack(pady=10)
tk.Label(frame_prefix_only, text="Przedrostek tylko dodanie:").pack()
entry_prefix_only = tk.Entry(frame_prefix_only, width=5)
entry_prefix_only.pack(pady=5)
entry_prefix_only.insert(0, "0")
btn_add_prefix_only = tk.Button(frame_prefix_only, text="Dodaj tylko przedrostek", command=add_prefix_only_from_gui)
btn_add_prefix_only.pack()

root.mainloop()
