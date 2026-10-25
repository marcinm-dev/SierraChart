# D1BarReplay

Studium ACSIL do Sierra Chart. Nadpisuje w pamięci High/Low/Close jednego bara na wykresie D1 historical, tak żeby wskaźniki pokazywały stan z chwili wejścia w trade (Bar Replay nie działa na wykresach daily historical). Plik `.dly` ani inne dane na dysku nie są dotykane.

**Status: prototyp v0.1.0 (etap E1 zamknięty).** Działa ręczny panel i wczesna wersja obcinania wykresu do dnia bara. Odczyt z wykresu D1 intraday-only w Bar Replay (E2 i dalej) jest dopiero planowany.

## Zawartość

- `D1BarReplay.cpp` - studium (v0.1.0-prototype)
- `D1BarReplay_PLAN.md` - architektura, etapy budowy i kryteria zaliczenia
- `BarHLCOverride_SPEC.md` - pierwotna specyfikacja projektu

## Co działa (zweryfikowane ręcznie na wykresie daily `ESZ26-CME`)

- Load bara do inputów i Apply z walidacją (High ≥ Open, Close, Low; Low ≤ Open, Close)
- Restore i usunięcie studium przywracają oryginał
- "Bar od końca" (względem pełnych danych) zaczytuje bar automatycznie i obcina wykres do dnia bara (brak lookahead)
- Pola `[RO]` z oryginalnymi wartościami dwóch ostatnio zaczytanych barów
- Plik `.dly` bez zmian (porównanie hasha przed i po)

## Plan

Szczegóły w `D1BarReplay_PLAN.md`: E2 odczyt sendera (D1 intraday-only w Bar Replay), E3 połączenie sender → nadpisanie, E4 przeskok dnia, E6 zabezpieczenia, E7 integracja z modułem screenowania.

## Budowanie

Skopiuj `D1BarReplay.cpp` do `ACS_Source` Sierra Chart i zbuduj przez Analysis >> Build Custom Studies DLL (64-bit). Używane wyłącznie API z nagłówków `sierrachart.h`.

## Uwaga

Narzędzie służy do analizy historycznej i screenowania. Zmienia wyświetlane dane tylko w pamięci wykresu.
