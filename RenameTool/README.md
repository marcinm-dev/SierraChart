# RenameTool

A small desktop tool written in Python (tkinter) that renames all files of one folder to a fixed series of names and can add a numeric prefix in front of the names. It is not a Sierra Chart study; it is a helper tool kept in this repository.

## How it works

- **Rename files** (button "Zmien nazwy plikow"): lists the chosen folder, sorts the entries by the number formed from all digits of each name (names without digits go last) and gives them, in that order, the names of the selected series, keeping each file's extension. The number of files must equal the length of the series, otherwise an error is shown and nothing is renamed.
  - 13 files: `0, 0_1, 1_1, 1, 2, 3, 4, 5, 6, 8_3, 8_2, 8_1, 8`
  - 14 files: `0, 0_1, 1_1, 1, 2, 3, 4, 5, 6, 8_4, 8_3, 8_2, 8_1, 8`
- **Prefix**: with the checkbox "Dodaj przedrostek" ticked, a whole number (default 0) is added in front of every name after the rename.
- **Prefix only** (button "Dodaj tylko przedrostek"): adds the number in front of every name without renaming anything else.

The window texts are in Polish; the code comments are in English.

## Run

```
python RenameTool.py
```

Python 3 on Windows with tkinter is required (it is included in the standard Windows installer). There are no other dependencies. Try it on a copy of a folder first: there is no preview and no undo.

## Versions

Versions are git tags: `renametool-v1`.

- **V1**: first version, as written (the file was called `renamev6.py` before it entered the repository).

## Known limitations

- The sort key uses all digits of a name, so a name such as `IMG_2024_01.jpg` is sorted as 202401.
- Subfolders and hidden files in the chosen folder are counted and renamed like files.
- A rename that fails is printed to the console only; the remaining files are still processed.
- If the file count does not match the series, the prefix step of "Zmien nazwy plikow" still runs when the checkbox is ticked.
- Testing: used by hand by the author on Windows; no automated tests.
