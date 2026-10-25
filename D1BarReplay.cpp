#include "sierrachart.h"

SCDLLName("D1BarReplay_HLC_Override_v1")

/*============================================================================
  D1 Bar Replay (HLC Override)  -  studium dla wykresu D1 HISTORICAL (receiver)

  Cel: nadpisac w PAMIECI High/Low/Close jednego bara D1, tak zeby wszystkie
  wskazniki liczyly sie na stanie z chwili wejscia. Plik .dly nie jest dotykany.

  WERSJA: v0.1.0-prototype  (E1 zamkniety i zweryfikowany recznie w Sierra Chart;
  E5 w wczesnej wersji tez zweryfikowany: obciecie/zdjecie obciecia, Restore,
  usuniecie studium).

  STATUS BUDOWY (patrz plan w D1BarReplay_PLAN.md):
    E1  Silnik nadpisania (reczny)        <- ZAMKNIETY (zaliczone w tescie)
    E2  Odczyt wykresu zrodlowego (diag.) <- ZAIMPLEMENTOWANE (tylko log, bez zapisu)
    E3  Polaczenie zrodlo -> nadpisanie   <- TODO
    E4  Przeskok dnia                     <- TODO
    E5  Obciecie danych (lookahead)       <- WCZESNA WERSJA: obciecie do dnia bara wybranego
                                             parametrem 'Bar od konca' (do przetestowania)
    E6  Zabezpieczenia                    <- TODO

  'Bar od konca' liczony jest wzgledem PELNYCH danych wykresu (bez obciecia):
  0 = ostatni bar pliku, 1 = przedostatni itd. Po obcieciu bar docelowy jest
  ostatnim barem wykresu, ale numeracja nadal odnosi sie do pelnych danych.

  Persistent (klucze):
    Int   0 : Applied        (1 = nadpisanie jest wpisane w dane)
    Int   1 : TargetIndex    (szybka sciezka, prawda jest w TargetDateTime)
    Int   2 : HasTarget
    Int  10 : Src_LastN      (E2)
    Int  20..24 / Float 20..22 : migawka inputow (diagnostyka E1)
    Int  30 : LastBarsBack
    Int  32 : Initialized    (0 = pierwsze wywolanie: auto-zaczytanie bara)
    Int  40 : NeedResolve    (1 = czekamy na pelne dane, potem zaczytaj bar)
    Int  41 : FullSize       (liczba barow pelnych danych)
    Int  42..44 : zapamietane ustawienia zakresu dat uzytkownika (ByRange, Start, End)
    Int  49 : CutEnd         (data koncowa narzucona przez studium, 0 = brak obciecia)
    Float 0..2 : Orig  H, L, C
    Float 3..5 : Applied H, L, C
    Float 10..13 : Src_LastO/H/L/C (E2)
    Float 23..25 : ostatnio zalogowany Apply | 26..28 : ostatnio odrzucone H, L, C
    SCDateTime 0 : TargetDateTime
============================================================================*/

// --- Pomocnicze ------------------------------------------------------------

static void LogMsg(SCStudyInterfaceRef sc, const char* Text)
{
    SCString Msg;
    Msg.Format("D1BarReplay: %s", Text);
    sc.AddMessageToLog(Msg, 0);
}

static bool SamePrice(SCStudyInterfaceRef sc, float A, float B)
{
    return fabs((double)A - (double)B) < (double)sc.TickSize * 0.5;
}

// Zapis H/L/C do danych bazowych + tablice pochodne (jak w specyfikacji sekcja 5)
static void WriteBar(SCStudyInterfaceRef sc, int Idx, float H, float L, float C)
{
    float O = sc.BaseData[SC_OPEN][Idx];

    sc.BaseData[SC_HIGH][Idx]     = H;
    sc.BaseData[SC_LOW][Idx]      = L;
    sc.BaseData[SC_LAST][Idx]     = C;
    sc.BaseData[SC_HL_AVG][Idx]   = (H + L) / 2.0f;
    sc.BaseData[SC_HLC_AVG][Idx]  = (H + L + C) / 3.0f;
    sc.BaseData[SC_OHLC_AVG][Idx] = (O + H + L + C) / 4.0f;
}

// Bar docelowy szukany po dacie i czasie. Indeks tylko jako szybka sciezka.
static int FindTargetIndex(SCStudyInterfaceRef sc, int HintIdx, const SCDateTime& Target)
{
    if (HintIdx >= 0 && HintIdx < sc.ArraySize
        && sc.BaseDateTimeIn[HintIdx].GetAsDouble() == Target.GetAsDouble())
        return HintIdx;

    for (int i = sc.ArraySize - 1; i >= 0; i--)
        if (sc.BaseDateTimeIn[i].GetAsDouble() == Target.GetAsDouble())
            return i;

    return -1;
}

// --- Obciecie zakresu dat wykresu (E5, wczesna wersja) ---------------------
// Zmiany sc.LoadChartDataByDateRange / ChartDataEndDate wchodza w zycie dopiero po
// powrocie funkcji studium (wykres sie przeladowuje). Przeladowanie kasuje nadpisanie
// w pamieci, wiec Apply wykona sie ponownie w nastepnym wywolaniu.

static void RequestCut(SCStudyInterfaceRef sc, int EndDate)
{
    int& CutEnd = sc.GetPersistentInt(49);

    if (CutEnd == 0)    // pierwsze obciecie: zapamietaj ustawienia uzytkownika
    {
        sc.GetPersistentInt(42) = sc.LoadChartDataByDateRange;
        sc.GetPersistentInt(43) = sc.ChartDataStartDate;
        sc.GetPersistentInt(44) = sc.ChartDataEndDate;
    }

    if (sc.GetPersistentInt(42) == 0)
        sc.ChartDataStartDate = 0;      // od poczatku pliku

    sc.LoadChartDataByDateRange = 1;
    sc.ChartDataEndDate = EndDate;
    CutEnd = EndDate;
}

static void ReleaseCut(SCStudyInterfaceRef sc)
{
    int& CutEnd = sc.GetPersistentInt(49);
    if (CutEnd == 0)
        return;

    sc.LoadChartDataByDateRange = sc.GetPersistentInt(42);
    sc.ChartDataStartDate       = sc.GetPersistentInt(43);
    sc.ChartDataEndDate         = sc.GetPersistentInt(44);
    CutEnd = 0;
}

// --- Pola [RO]: oryginaly zaczytanych barow --------------------------------
// Siatka bezpieczenstwa: gdyby cos poszlo nie tak, mozna recznie przywrocic bar.
// Sierra nie ma inputow read-only, wiec studium samo je wypelnia i nie czyta ich
// z powrotem (edycja przez uzytkownika nie ma wplywu).
// Slot 1 = ostatnio zaczytany bar, Slot 2 = poprzednio zaczytany bar.
// Kazdy slot: data, High, Low, Close (Open jest niezmienny, wiec go nie pokazujemy).
static const int SLOT1_BASE = 10;
static const int SLOT2_BASE = 14;

static void WriteSlot(SCStudyInterfaceRef sc, int Base, const char* DateText, float H, float L, float C)
{
    sc.Input[Base].SetString(DateText);
    sc.Input[Base + 1].SetFloat(H);
    sc.Input[Base + 2].SetFloat(L);
    sc.Input[Base + 3].SetFloat(C);
}

// Slot 1 -> Slot 2 (tylko gdy zaczytujemy INNY bar niz zapisany w slocie 1)
static void ShiftSlots(SCStudyInterfaceRef sc)
{
    SCString OldDate = sc.Input[SLOT1_BASE].GetString();    // kopia przed zapisem
    WriteSlot(sc, SLOT2_BASE, OldDate.GetChars(),
        sc.Input[SLOT1_BASE + 1].GetFloat(), sc.Input[SLOT1_BASE + 2].GetFloat(),
        sc.Input[SLOT1_BASE + 3].GetFloat());
}

// --- Studium ---------------------------------------------------------------

SCSFExport scsf_D1BarReplay(SCStudyInterfaceRef sc)
{
    SCInputRef In_Apply       = sc.Input[0];
    SCInputRef In_Load        = sc.Input[1];
    SCInputRef In_High        = sc.Input[2];
    SCInputRef In_Low         = sc.Input[3];
    SCInputRef In_Close       = sc.Input[4];
    SCInputRef In_Restore     = sc.Input[5];
    SCInputRef In_BarsBack    = sc.Input[6];
    SCInputRef In_SourceChart = sc.Input[7];
    SCInputRef In_Diag        = sc.Input[8];
    SCInputRef In_Cut         = sc.Input[9];
    // Inputy 10-17: pola [RO] (patrz WriteSlot)

    if (sc.SetDefaults)
    {
        sc.GraphName = "D1 Bar Replay (HLC Override)";
        sc.StudyDescription = "Nadpisuje H/L/C jednego bara w pamieci wykresu. Nie zapisuje do plikow danych.";
        sc.AutoLoop = 0;            // manual looping: jedno wywolanie, nie petla po barach
        sc.UpdateAlways = 1;        // potrzebne do odpytywania wykresu zrodlowego (E2+)
        sc.GraphRegion = 0;
        sc.CalculationPrecedence = STD_PREC_LEVEL;   // kolejnosc: studium ma byc NA GORZE listy studiow

        In_Apply.Name = "Apply Override";
        In_Apply.SetYesNo(0);

        In_Load.Name = "Load Bar Into Inputs";
        In_Load.SetYesNo(0);

        In_High.Name = "High";
        In_High.SetFloat(0.0f);

        In_Low.Name = "Low";
        In_Low.SetFloat(0.0f);

        In_Close.Name = "Close";
        In_Close.SetFloat(0.0f);

        In_Restore.Name = "Restore Original And Reset";
        In_Restore.SetYesNo(0);

        In_BarsBack.Name = "Bar od konca pelnych danych (0 = ostatni bar pliku)";
        In_BarsBack.SetInt(0);
        In_BarsBack.SetIntLimits(0, 100000);

        In_SourceChart.Name = "E2: Numer wykresu zrodlowego (0 = wylaczone)";
        In_SourceChart.SetChartNumber(0);

        In_Diag.Name = "Diagnostyka w logu";
        In_Diag.SetYesNo(1);

        In_Cut.Name = "Cofnij wykres do dnia wybranego bara (brak lookahead)";
        In_Cut.SetYesNo(1);

        // Pola tylko do odczytu (wypelnia je studium): oryginaly zaczytanych barow
        const char* SlotNames[2] = { "[RO] Zaczytany #1 (ostatni)", "[RO] Zaczytany #2 (poprzedni)" };
        for (int s = 0; s < 2; s++)
        {
            int Base = (s == 0) ? SLOT1_BASE : SLOT2_BASE;

            sc.Input[Base].Name.Format("%s data", SlotNames[s]);
            sc.Input[Base].SetString("");
            sc.Input[Base + 1].Name.Format("%s High", SlotNames[s]);
            sc.Input[Base + 1].SetFloat(0.0f);
            sc.Input[Base + 2].Name.Format("%s Low", SlotNames[s]);
            sc.Input[Base + 2].SetFloat(0.0f);
            sc.Input[Base + 3].Name.Format("%s Close", SlotNames[s]);
            sc.Input[Base + 3].SetFloat(0.0f);
        }

        return;
    }

    int&   Applied     = sc.GetPersistentInt(0);
    int&   TargetIdx   = sc.GetPersistentInt(1);
    int&   HasTarget   = sc.GetPersistentInt(2);
    int&   NeedResolve = sc.GetPersistentInt(40);
    int&   FullSize    = sc.GetPersistentInt(41);
    int&   CutEnd      = sc.GetPersistentInt(49);
    float& OrigH       = sc.GetPersistentFloat(0);
    float& OrigL       = sc.GetPersistentFloat(1);
    float& OrigC       = sc.GetPersistentFloat(2);
    float& AppH        = sc.GetPersistentFloat(3);
    float& AppL        = sc.GetPersistentFloat(4);
    float& AppC        = sc.GetPersistentFloat(5);
    SCDateTime& TargetDT = sc.GetPersistentSCDateTime(0);

    // --- DIAGNOSTYKA E1: log przy kazdej zmianie inputow widzianych przez studium ---
    if (In_Diag.GetYesNo() && !sc.LastCallToFunction)
    {
        int&   SnapN = sc.GetPersistentInt(20);   // 0 = pierwsze wywolanie
        int&   SnapA = sc.GetPersistentInt(21);
        int&   SnapL = sc.GetPersistentInt(22);
        int&   SnapR = sc.GetPersistentInt(23);
        int&   SnapB = sc.GetPersistentInt(24);
        float& SnapH = sc.GetPersistentFloat(20);
        float& SnapLo = sc.GetPersistentFloat(21);
        float& SnapC = sc.GetPersistentFloat(22);

        int   A = In_Apply.GetYesNo(),  L = In_Load.GetYesNo(), R = In_Restore.GetYesNo();
        int   B = In_BarsBack.GetInt();
        float H = In_High.GetFloat(), Lo = In_Low.GetFloat(), C = In_Close.GetFloat();

        if (SnapN == 0 || A != SnapA || L != SnapL || R != SnapR || B != SnapB
            || H != SnapH || Lo != SnapLo || C != SnapC)
        {
            SnapN = 1; SnapA = A; SnapL = L; SnapR = R; SnapB = B;
            SnapH = H; SnapLo = Lo; SnapC = C;

            SCString Msg;
            Msg.Format("D1BarReplay[E1]: inputy: Apply=%d Load=%d Restore=%d BarOdKonca=%d H=%.2f L=%.2f C=%.2f | Applied=%d HasTarget=%d ArraySize=%d FullSize=%d CutEnd=%d fullRecalc=%d",
                A, L, R, B, H, Lo, C, Applied, HasTarget, sc.ArraySize, FullSize, CutEnd, (int)sc.IsFullRecalculation);
            sc.AddMessageToLog(Msg, 0);
        }

        // Zmiana liczby barow = przeladowanie wykresu (np. po obcieciu zakresu dat)
        int& LastSize = sc.GetPersistentInt(25);
        if (sc.ArraySize != LastSize)
        {
            SCString Msg;
            Msg.Format("D1BarReplay[E5]: ArraySize %d -> %d (CutEnd=%d, ostatni bar=%s)",
                LastSize, sc.ArraySize, CutEnd,
                sc.ArraySize > 0 ? sc.FormatDateTime(sc.BaseDateTimeIn[sc.ArraySize - 1]).GetChars() : "-");
            sc.AddMessageToLog(Msg, 0);
            LastSize = sc.ArraySize;
        }
    }

    // --- 1. REVERT: zawsze zaczynamy od czystych danych -----------------------
    // Przywracamy oryginal tylko jesli bar nadal zawiera wartosci zastosowane przez
    // studium. Jesli nie (dane przeladowane) - biezace wartosci sa nowym oryginalem.
    if (Applied == 1)
    {
        int Idx = FindTargetIndex(sc, TargetIdx, TargetDT);
        if (Idx >= 0
            && SamePrice(sc, sc.BaseData[SC_HIGH][Idx], AppH)
            && SamePrice(sc, sc.BaseData[SC_LOW][Idx],  AppL)
            && SamePrice(sc, sc.BaseData[SC_LAST][Idx], AppC))
        {
            WriteBar(sc, Idx, OrigH, OrigL, OrigC);
        }
        else if (In_Diag.GetYesNo())
            LogMsg(sc, "Revert pominiety: dane przeladowane lub bar nie znaleziony.");

        Applied = 0;
    }

    // --- SPRZATANIE: usuniecie studium / zamkniecie wykresu -------------------
    if (sc.LastCallToFunction)
    {
        ReleaseCut(sc);     // przywroc ustawienia zakresu dat uzytkownika
        return;             // Revert wykonany powyzej
    }

    // --- Rozmiar PELNYCH danych (bez obciecia) ---------------------------------
    // Gdy czekamy na przeladowanie po zdjeciu obciecia, nie nadpisuj mniejsza wartoscia.
    if (CutEnd == 0 && (!NeedResolve || sc.ArraySize >= FullSize))
        FullSize = sc.ArraySize;

    // --- Wylaczenie opcji obciecia: zdejmij obciecie ---------------------------
    if (!In_Cut.GetYesNo() && CutEnd != 0)
    {
        ReleaseCut(sc);
        LogMsg(sc, "Obciecie wylaczone: przywrocono zakres dat wykresu.");
    }

    // --- ZMIANA "BAR OD KONCA": zwolnij stary bar i zaczytaj nowy (reset do oryginalu) ---
    // Revert powyzej juz przywrocil stary bar. Zerujemy cel, a Load nizej wpisze do
    // inputow oryginalne H/L/C nowego bara. Jesli Apply = Yes, zastosuje je ponownie (bez zmian).
    int& LastBarsBack = sc.GetPersistentInt(30);
    bool BarsBackChanged = (In_BarsBack.GetInt() != LastBarsBack);
    LastBarsBack = In_BarsBack.GetInt();

    // Pierwsze wywolanie po dodaniu studium: zaczytaj bar wskazany przez 'Bar od konca'
    // (domyslnie 0). Po Restore nie zaczytujemy ponownie - to robi dopiero zmiana inputu lub Load.
    int& Initialized = sc.GetPersistentInt(32);
    bool AutoInit = (Initialized == 0 && sc.ArraySize > 0);
    if (AutoInit)
        Initialized = 1;

    // Zmiana 'Bar od konca' = zaczytaj nowy bar (H/L/C do inputow + obciecie wykresu),
    // niezaleznie od tego, czy wczesniej cos zaczytano.
    bool ReloadNewBar = false;
    if (BarsBackChanged && !In_Restore.GetYesNo())
    {
        ReloadNewBar = true;
        if (HasTarget)
        {
            HasTarget = 0;      // Revert powyzej przywrocil stary bar
            TargetIdx = -1;
            if (In_Diag.GetYesNo())
                LogMsg(sc, "Zmiana 'Bar od konca': stary bar przywrocony, zaczytuje nowy.");
        }
        else if (In_Diag.GetYesNo())
            LogMsg(sc, "Zmiana 'Bar od konca': zaczytuje nowy bar.");
    }

    // --- 2. RESTORE ------------------------------------------------------------
    if (In_Restore.GetYesNo())
    {
        if (HasTarget)
        {
            int Idx = FindTargetIndex(sc, TargetIdx, TargetDT);
            if (Idx >= 0)
            {
                In_High.SetFloat(sc.BaseData[SC_HIGH][Idx]);
                In_Low.SetFloat(sc.BaseData[SC_LOW][Idx]);
                In_Close.SetFloat(sc.BaseData[SC_LAST][Idx]);
            }
        }
        HasTarget = 0;
        TargetIdx = -1;
        NeedResolve = 0;
        ReleaseCut(sc);     // wykres wraca do pelnych danych
        In_Apply.SetYesNo(0);
        In_Restore.SetYesNo(0);
        LogMsg(sc, "Restore: oryginal przywrocony, bar zwolniony, zakres dat przywrocony.");
        return;
    }

    // --- 3. LOAD / auto-capture -----------------------------------------------
    bool DoLoad = In_Load.GetYesNo() != 0;

    if (In_Apply.GetYesNo() && !HasTarget)
        DoLoad = true;      // Apply bez wczesniejszego Load: wybierz bar sam

    if (ReloadNewBar)
        DoLoad = true;      // po zmianie 'Bar od konca' zaczytaj nowy bar

    if (AutoInit)
    {
        DoLoad = true;      // start studium: zaczytaj bar wskazany przez 'Bar od konca'
        if (In_Diag.GetYesNo())
            LogMsg(sc, "Start: automatyczne zaczytanie bara.");
    }

    // Po zdjeciu obciecia czekalismy na pelne dane: teraz zaczytaj bar
    if (NeedResolve && CutEnd == 0 && sc.ArraySize >= FullSize)
        DoLoad = true;

    bool CutJustRequested = false;

    if (DoLoad)
    {
        int Idx = FullSize - 1 - In_BarsBack.GetInt();      // numeracja wzgledem pelnych danych

        if (Idx >= sc.ArraySize && CutEnd != 0)
        {
            // Bar jest dalej niz obecnie zaladowane (obciete) dane: wroc do pelnych danych
            // i zaczytaj bar po przeladowaniu (NeedResolve).
            ReleaseCut(sc);
            NeedResolve = 1;
            In_Load.SetYesNo(0);
            LogMsg(sc, "Bar poza obcietym zakresem: zdejmuje obciecie, zaczytam bar po przeladowaniu.");
            return;
        }

        if (Idx < 0 || Idx >= sc.ArraySize)
        {
            LogMsg(sc, "Load: brak bara o podanym przesunieciu.");
            In_Load.SetYesNo(0);
            NeedResolve = 0;
            return;
        }

        NeedResolve = 0;

        TargetIdx = Idx;
        TargetDT  = sc.BaseDateTimeIn[Idx];
        HasTarget = 1;

        // Pola [RO]: bar jest czysty (po Revert), wiec to prawdziwy oryginal.
        // Przesuwamy slot 1 -> 2 tylko gdy zaczytujemy inny bar niz w slocie 1.
        SCString NewDate = sc.FormatDateTime(TargetDT);
        if (sc.Input[SLOT1_BASE].GetString()[0] != '\0'
            && strcmp(sc.Input[SLOT1_BASE].GetString(), NewDate.GetChars()) != 0)
            ShiftSlots(sc);

        WriteSlot(sc, SLOT1_BASE, NewDate.GetChars(),
            sc.BaseData[SC_HIGH][Idx], sc.BaseData[SC_LOW][Idx], sc.BaseData[SC_LAST][Idx]);

        In_High.SetFloat(sc.BaseData[SC_HIGH][Idx]);
        In_Low.SetFloat(sc.BaseData[SC_LOW][Idx]);
        In_Close.SetFloat(sc.BaseData[SC_LAST][Idx]);
        In_Load.SetYesNo(0);

        if (In_Diag.GetYesNo())
        {
            SCString Msg;
            Msg.Format("D1BarReplay: Load bar idx=%d data=%s O=%.2f H=%.2f L=%.2f C=%.2f",
                Idx, NewDate.GetChars(),
                sc.BaseData[SC_OPEN][Idx], sc.BaseData[SC_HIGH][Idx],
                sc.BaseData[SC_LOW][Idx],  sc.BaseData[SC_LAST][Idx]);
            sc.AddMessageToLog(Msg, 0);
        }

        // --- OBCIECIE DANYCH DO DNIA BARA (brak lookahead) ---
        if (In_Cut.GetYesNo())
        {
            int EndDate = TargetDT.GetDate();
            bool IsLastOfFull = (CutEnd == 0 && Idx == sc.ArraySize - 1);

            if (!IsLastOfFull && CutEnd != EndDate)
            {
                RequestCut(sc, EndDate);
                CutJustRequested = true;

                if (In_Diag.GetYesNo())
                {
                    SCString Msg;
                    Msg.Format("D1BarReplay[E5]: obciecie zakresu dat do %s (EndDate=%d), ArraySize=%d FullSize=%d",
                        NewDate.GetChars(), EndDate, sc.ArraySize, FullSize);
                    sc.AddMessageToLog(Msg, 0);
                }
            }
        }
    }

    // --- 4. APPLY ----------------------------------------------------------------
    // Gdy wlasnie zlecilismy obciecie, wykres zaraz sie przeladuje, wiec Apply
    // wykona sie w nastepnym wywolaniu na swiezych danych.
    if (In_Apply.GetYesNo() && HasTarget && !CutJustRequested)
    {
        int Idx = FindTargetIndex(sc, TargetIdx, TargetDT);
        if (Idx < 0)
        {
            LogMsg(sc, "Apply: nie znaleziono bara docelowego (zmiana danych?).");
        }
        else
        {
            TargetIdx = Idx;

            float H = sc.RoundToTickSize(In_High.GetFloat());
            float L = sc.RoundToTickSize(In_Low.GetFloat());
            float C = sc.RoundToTickSize(In_Close.GetFloat());
            float O = sc.BaseData[SC_OPEN][Idx];

            // Walidacja: High >= Open, Close, Low  |  Low <= Open, Close
            bool Valid = (H >= O && H >= C && H >= L && L <= O && L <= C);
            if (!Valid)
            {
                // UpdateAlways wola studium ciagle: komunikat tylko przy zmianie wartosci
                float& RejH = sc.GetPersistentFloat(26);
                float& RejL = sc.GetPersistentFloat(27);
                float& RejC = sc.GetPersistentFloat(28);
                if (H != RejH || L != RejL || C != RejC)
                {
                    RejH = H; RejL = L; RejC = C;
                    SCString Msg;
                    Msg.Format("D1BarReplay: ODRZUCONO H=%.2f L=%.2f C=%.2f (Open=%.2f) - niespojne wartosci. Bar zostaje oryginalny.", H, L, C, O);
                    sc.AddMessageToLog(Msg, 1);
                }
            }
            else
            {
                sc.GetPersistentFloat(26) = 0.0f;   // kolejne odrzucenie ma sie znowu zalogowac

                // Bar jest czysty (po Revert) - zapamietaj oryginal i zapisz nowe wartosci
                OrigH = sc.BaseData[SC_HIGH][Idx];
                OrigL = sc.BaseData[SC_LOW][Idx];
                OrigC = sc.BaseData[SC_LAST][Idx];

                WriteBar(sc, Idx, H, L, C);

                AppH = H;
                AppL = L;
                AppC = C;
                Applied = 1;

                // UpdateAlways = 1 wola studium ciagle, wiec loguj tylko przy zmianie wartosci
                float& LogH = sc.GetPersistentFloat(23);
                float& LogL = sc.GetPersistentFloat(24);
                float& LogC = sc.GetPersistentFloat(25);
                bool Changed = (H != LogH || L != LogL || C != LogC);
                LogH = H; LogL = L; LogC = C;

                if (In_Diag.GetYesNo() && Changed)
                {
                    SCString Msg;
                    Msg.Format("D1BarReplay[E1]: APPLY idx=%d  orig H=%.2f L=%.2f C=%.2f -> nowe H=%.2f L=%.2f C=%.2f | odczyt z danych: H=%.2f L=%.2f C=%.2f",
                        Idx, OrigH, OrigL, OrigC, H, L, C,
                        sc.BaseData[SC_HIGH][Idx], sc.BaseData[SC_LOW][Idx], sc.BaseData[SC_LAST][Idx]);
                    sc.AddMessageToLog(Msg, 0);
                }
            }
        }
    }

    // --- E2: ODCZYT WYKRESU ZRODLOWEGO (TYLKO DIAGNOSTYKA, BEZ ZAPISU) ----------
    int SrcChart = In_SourceChart.GetChartNumber();
    if (SrcChart > 0 && In_Diag.GetYesNo())
    {
        SCGraphData SrcData;
        sc.GetChartBaseData(SrcChart, SrcData);

        SCDateTimeArray SrcTimes;
        sc.GetChartDateTimeArray(SrcChart, SrcTimes);

        int SrcSize = SrcTimes.GetArraySize();
        if (SrcSize == 0 || SrcData[SC_LAST].GetArraySize() == 0)
            return;     // dane zrodla jeszcze sie laduja

        int Last = SrcSize - 1;
        float SO = SrcData[SC_OPEN][Last];
        float SH = SrcData[SC_HIGH][Last];
        float SL = SrcData[SC_LOW][Last];
        float SClose = SrcData[SC_LAST][Last];

        float& LastO = sc.GetPersistentFloat(10);
        float& LastH = sc.GetPersistentFloat(11);
        float& LastL = sc.GetPersistentFloat(12);
        float& LastC = sc.GetPersistentFloat(13);
        int&   LastN = sc.GetPersistentInt(10);

        // Loguj tylko przy zmianie (replay generuje duzo wywolan)
        if (SO != LastO || SH != LastH || SL != LastL || SClose != LastC || SrcSize != LastN)
        {
            LastO = SO; LastH = SH; LastL = SL; LastC = SClose; LastN = SrcSize;

            int SrcDay = sc.GetTradingDayDateForChartNumber(SrcChart, SrcTimes[Last]);

            // Szukamy bara receivera o tej dacie (tylko do porownania w logu)
            int RecvIdx = -1;
            for (int i = sc.ArraySize - 1; i >= 0; i--)
                if (sc.BaseDateTimeIn[i].GetDate() == SrcDay) { RecvIdx = i; break; }

            SCString Msg;
            Msg.Format("D1BarReplay[E2]: zrodlo ch%d replay=%d bars=%d start=%s tradingDay=%d O=%.2f H=%.2f L=%.2f C=%.2f | receiver idx=%d",
                SrcChart, (int)sc.GetReplayStatusFromChart(SrcChart), SrcSize,
                sc.FormatDateTime(SrcTimes[Last]).GetChars(), SrcDay, SO, SH, SL, SClose, RecvIdx);
            sc.AddMessageToLog(Msg, 0);
        }
    }
}
