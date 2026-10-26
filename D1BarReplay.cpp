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
    E3  Polaczenie zrodlo -> nadpisanie   <- WCZESNA WERSJA (do testu): Apply = Yes + wykres zrodlowy
                                             => H/L/C z ostatniego bara sendera, gdy replay gra/pauza
    E4  Przeskok dnia                     <- CZESCIOWO: receiver podaza za DNIEM wykresu zrodlowego
                                             (obciecie + zaczytanie bara). Wartosci H/L/C z sendera = E3.
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
    Int  33 : ostatni zalogowany dzien zrodla nieobecny w danych receivera (E4-lite)
    Int  34 : liczba ponowien narzucenia zakresu dat po przeladowaniu wykresu
    Int  35 : ostatni zalogowany status replay sendera (E3)
    Int  36..37 : diagnostyka wykresu wyzszego okresu (ostatni dzien sendera, ArraySize)
    Int  38 : ostatni zalogowany status replay sendera na wykresie W/M/Q/Y (E8)
    Int  39 : komunikat o nieaktywnosci na wykresie intraday zalogowany
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

        In_BarsBack.Name = "Bar od konca pelnych danych (0 = ostatni bar pliku; przy wykresie zrodlowym ustawiany automatycznie)";
        In_BarsBack.SetInt(0);
        In_BarsBack.SetIntLimits(0, 100000);

        In_SourceChart.Name = "E2: Wykres zrodlowy (wybierz WLASNY wykres = wylaczone)";
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

    // --- WYKRES WYZSZEGO OKRESU (W1/M1/Q1/Y1): obciecie do dnia sendera + agregat okresu (E8) ---
    // Logika dzienna opiera sie na tym, ze bar receivera to jeden dzien o dacie sendera.
    // Na wykresie tygodniowym/miesiecznym tak nie jest, wiec tu osobna sciezka: wykres konczy
    // sie na dniu sendera (ostatni bar = czesciowy okres), a jego H/L/C to agregat dni okresu
    // z sendera (High = max, Low = min, Close = biezaca cena sendera).
    n_ACSIL::s_BarPeriod BarPeriod;
    sc.GetBarPeriodParameters(BarPeriod);
    bool IsHTF = (BarPeriod.ChartDataType == DAILY_DATA
        && BarPeriod.HistoricalChartBarPeriodType != HISTORICAL_CHART_PERIOD_DAYS);

    // Zabezpieczenie: odstep >= 6 dni miedzy kolejnymi barami (dwa razy z rzedu) = okres wyzszy
    // niz dzienny (dzienne bary maja co najwyzej ~4 dni przerwy), gdyby powyzsze rozpoznanie zawiodlo.
    if (!IsHTF && sc.ArraySize >= 3)
    {
        int L1 = sc.ArraySize - 1;
        if (sc.BaseDateTimeIn[L1].GetDate() - sc.BaseDateTimeIn[L1 - 1].GetDate() >= 6
            && sc.BaseDateTimeIn[L1 - 1].GetDate() - sc.BaseDateTimeIn[L1 - 2].GetDate() >= 6)
            IsHTF = true;
    }

    // Wykresy intraday (np. 240 Min #3): studium nic tam nie robi. Intraday i tak ma wlasny
    // Bar Replay, a nakladanie dziennych H/L/C na bar intraday dawaloby bledne wartosci.
    if (BarPeriod.ChartDataType == INTRADAY_DATA)
    {
        if (CutEnd != 0)
            ReleaseCut(sc);

        int& IntradayLogged = sc.GetPersistentInt(39);
        if (!IntradayLogged && !sc.LastCallToFunction)
        {
            IntradayLogged = 1;
            LogMsg(sc, "wykres intraday: studium jest nieaktywne (dziala na D1 i W/M/Q/Y).");
        }
        return;
    }

    if (IsHTF)
    {
        // Revert: wykres wyzszego okresu zaczyna kazde wywolanie od czystych danych
        // (nadpisanie zapisujemy swiezo nizej, jak na D1).
        if (Applied == 1)
        {
            int RIdx = FindTargetIndex(sc, TargetIdx, TargetDT);
            if (RIdx >= 0
                && SamePrice(sc, sc.BaseData[SC_HIGH][RIdx], AppH)
                && SamePrice(sc, sc.BaseData[SC_LOW][RIdx],  AppL)
                && SamePrice(sc, sc.BaseData[SC_LAST][RIdx], AppC))
                WriteBar(sc, RIdx, OrigH, OrigL, OrigC);
            Applied = 0;
        }

        if (sc.LastCallToFunction)
        {
            ReleaseCut(sc);
            return;
        }

        int HtfSrc = In_SourceChart.GetChartNumber();

        // --- Cofanie daty: wykres wyzszego okresu konczy sie na dniu sendera ---
        // Ostatni bar jest wtedy czesciowym okresem (np. tygodniem do dnia sendera).
        // Wartosci H/L/C (agregacja dni okresu) dojda w E8 po sprawdzeniu logu [HTF].
        if (HtfSrc <= 0 || HtfSrc == sc.ChartNumber || !In_Cut.GetYesNo())
        {
            ReleaseCut(sc);     // zrodlo wylaczone albo obciecie wylaczone: pelne dane
        }
        else
        {
            SCDateTimeArray CutTimes;
            sc.GetChartDateTimeArray(HtfSrc, CutTimes);
            int CutN = CutTimes.GetArraySize();
            if (CutN > 0)
            {
                int WantedEnd = sc.GetTradingDayDateForChartNumber(HtfSrc, CutTimes[CutN - 1]);
                int& HtfReasserts = sc.GetPersistentInt(34);
                bool InEffect = (sc.LoadChartDataByDateRange == 1 && sc.ChartDataEndDate == WantedEnd);

                if (CutEnd != WantedEnd)
                {
                    RequestCut(sc, WantedEnd);
                    if (In_Diag.GetYesNo())
                    {
                        SCString Msg;
                        Msg.Format("D1BarReplay[HTF]: obciecie zakresu dat do dnia sendera (EndDate=%d), ArraySize=%d", WantedEnd, sc.ArraySize);
                        sc.AddMessageToLog(Msg, 0);
                    }
                }
                else if (!InEffect && HtfReasserts < 3)
                {
                    // Przeladowanie wykresu moglo przywrocic pelne dane: narzuc zakres ponownie
                    HtfReasserts++;
                    RequestCut(sc, WantedEnd);
                    LogMsg(sc, "[HTF] zakres dat nie obowiazuje po przeladowaniu: narzucam ponownie.");
                }
                else if (InEffect)
                    HtfReasserts = 0;
            }
        }

        // --- Dane sendera i agregat dni biezacego okresu (dla zapisu E8 i diagnostyki) ---
        // Agregat: High = max High, Low = min Low dni okresu do dnia sendera, Close = biezaca
        // cena sendera. Poczatek okresu liczymy z POLUDNIA dnia handlowego, bo bar sendera dnia
        // handlowego zaczyna sie dzien wczesniej wieczorem (np. 1.10 zaczyna sie 30.09 18:00,
        // co dla okresu miesiecznego wskazaloby poprzedni miesiac).
        bool  HaveAgg = false;
        int   SrcDay = 0, Days = 0;
        float AggH = -1.0e10f, AggL = 1.0e10f, AggC = 0.0f;
        SCDateTime PeriodStart;

        if (HtfSrc > 0 && HtfSrc != sc.ChartNumber && sc.ArraySize > 0)
        {
            SCDateTimeArray HtfTimes;
            sc.GetChartDateTimeArray(HtfSrc, HtfTimes);
            SCGraphData HtfData;
            sc.GetChartBaseData(HtfSrc, HtfData);

            int N = HtfTimes.GetArraySize();
            if (N > 0 && HtfData[SC_LAST].GetArraySize() >= N)
            {
                SrcDay = sc.GetTradingDayDateForChartNumber(HtfSrc, HtfTimes[N - 1]);

                int Unit = TIME_PERIOD_LENGTH_UNIT_WEEKS;
                if (BarPeriod.HistoricalChartBarPeriodType == HISTORICAL_CHART_PERIOD_MONTHLY)
                    Unit = TIME_PERIOD_LENGTH_UNIT_MONTHS;
                else if (BarPeriod.HistoricalChartBarPeriodType == HISTORICAL_CHART_PERIOD_QUARTERLY)
                    Unit = TIME_PERIOD_LENGTH_UNIT_QUARTERS;
                else if (BarPeriod.HistoricalChartBarPeriodType == HISTORICAL_CHART_PERIOD_YEARLY)
                    Unit = TIME_PERIOD_LENGTH_UNIT_YEARS;

                SCDateTime DayNoon(SrcDay, 12 * 3600);
                PeriodStart = sc.GetStartOfPeriodForDateTime(DayNoon, Unit, 1, 0);

                AggC = HtfData[SC_LAST][N - 1];
                bool ReachedBefore = false;
                for (int i = N - 1; i >= 0; i--)
                {
                    if (sc.GetTradingDayDateForChartNumber(HtfSrc, HtfTimes[i]) < PeriodStart.GetDate())
                    {
                        ReachedBefore = true;
                        break;
                    }
                    if (HtfData[SC_HIGH][i] > AggH) AggH = HtfData[SC_HIGH][i];
                    if (HtfData[SC_LOW][i]  < AggL) AggL = HtfData[SC_LOW][i];
                    Days++;
                }

                // Historia sendera musi siegac poczatku okresu (kwartal/rok moga jej nie miec)
                int OldestDay = sc.GetTradingDayDateForChartNumber(HtfSrc, HtfTimes[0]);
                bool Covered = ReachedBefore || (OldestDay - PeriodStart.GetDate() <= 2);
                HaveAgg = (Days > 0 && Covered);
            }
        }

        // --- E8: nalozenie zagregowanych H/L/C na ostatni bar (czesciowy okres) ---
        // Warunki: Apply = Yes, wykres obciety do dnia sendera, replay gra lub pauza.
        // Przy zatrzymanym replay bar zostaje oryginalny (Revert wykonany na poczatku bloku).
        if (In_Apply.GetYesNo() && HaveAgg && CutEnd != 0 && CutEnd == SrcDay && sc.ArraySize > 0)
        {
            int Status = (int)sc.GetReplayStatusFromChart(HtfSrc);
            int& HtfLastStatus = sc.GetPersistentInt(38);
            if (Status != HtfLastStatus)
            {
                HtfLastStatus = Status;
                if (In_Diag.GetYesNo())
                {
                    SCString Msg;
                    Msg.Format("D1BarReplay[HTF]: status replay sendera ch%d = %d (0=stop, 1=gra, 2=pauza)%s",
                        HtfSrc, Status, Status == 0 ? " - wartosci sendera nie sa nakladane" : "");
                    sc.AddMessageToLog(Msg, 0);
                }
            }

            int Last  = sc.ArraySize - 1;
            int Stamp = sc.BaseDateTimeIn[Last].GetDate();

            // Ostatni bar musi byc biezacym okresem (data znacznika bara w [poczatek okresu, dzien sendera])
            if (Status != 0 && Stamp >= PeriodStart.GetDate() && Stamp <= SrcDay)
            {
                float H = sc.RoundToTickSize(AggH);
                float L = sc.RoundToTickSize(AggL);
                float C = sc.RoundToTickSize(AggC);
                float O = sc.BaseData[SC_OPEN][Last];

                if (H >= O && H >= C && H >= L && L <= O && L <= C)
                {
                    OrigH = sc.BaseData[SC_HIGH][Last];
                    OrigL = sc.BaseData[SC_LOW][Last];
                    OrigC = sc.BaseData[SC_LAST][Last];

                    WriteBar(sc, Last, H, L, C);

                    AppH = H; AppL = L; AppC = C;
                    TargetIdx = Last;
                    TargetDT  = sc.BaseDateTimeIn[Last];
                    HasTarget = 1;
                    Applied   = 1;

                    // Przeliczenie wskaznikow ustawia blok "stan widziany przez wskazniki" nizej.
                    bool Changed = (H != sc.GetPersistentFloat(23)
                        || L != sc.GetPersistentFloat(24)
                        || C != sc.GetPersistentFloat(25));

                    if (Changed)
                    {
                        if (In_Diag.GetYesNo())
                        {
                            SCString Msg;
                            Msg.Format("D1BarReplay[HTF]: APPLY bar=%s (%d dni okresu)  orig H=%.2f L=%.2f C=%.2f -> nowe H=%.2f L=%.2f C=%.2f",
                                sc.FormatDateTime(sc.BaseDateTimeIn[Last]).GetChars(), Days,
                                OrigH, OrigL, OrigC, H, L, C);
                            sc.AddMessageToLog(Msg, 0);
                        }
                    }
                }
                else
                {
                    float& RejH = sc.GetPersistentFloat(26);
                    if (H != RejH)
                    {
                        RejH = H;
                        SCString Msg;
                        Msg.Format("D1BarReplay[HTF]: ODRZUCONO H=%.2f L=%.2f C=%.2f (Open=%.2f) - niespojne wartosci. Bar zostaje oryginalny.", H, L, C, O);
                        sc.AddMessageToLog(Msg, 0);
                    }
                }
            }
        }

        // --- Stan widziany przez wskazniki: przelicz, gdy sie zmienil (nalozono, zmieniono, cofnieto) ---
        // Revert na poczatku wywolania cofa dane bez przeliczenia, wiec po Stop replay (Applied = 0)
        // wskazniki zostalyby na starych wartosciach. Stan "oryginal" = (0,0,0).
        {
            float& SeenH = sc.GetPersistentFloat(23);
            float& SeenL = sc.GetPersistentFloat(24);
            float& SeenC = sc.GetPersistentFloat(25);
            float NowH = (Applied == 1) ? AppH : 0.0f;
            float NowL = (Applied == 1) ? AppL : 0.0f;
            float NowC = (Applied == 1) ? AppC : 0.0f;
            if (NowH != SeenH || NowL != SeenL || NowC != SeenC)
            {
                SeenH = NowH; SeenL = NowL; SeenC = NowC;
                sc.FlagFullRecalculate = 1;
            }
        }

        // --- Diagnostyka [HTF] przy zmianie dnia sendera lub liczby barow ---
        if (In_Diag.GetYesNo() && HaveAgg && sc.ArraySize > 0)
        {
            int& LastDay  = sc.GetPersistentInt(36);
            int& LastSize = sc.GetPersistentInt(37);

            if (SrcDay != LastDay || sc.ArraySize != LastSize)
            {
                LastDay = SrcDay;
                LastSize = sc.ArraySize;

                int Last = sc.ArraySize - 1;
                SCString Msg;
                Msg.Format("D1BarReplay[HTF]: okres=%d (2=W 3=M 4=Q 5=Y) ArraySize=%d | ostatnie bary: [%d]=%s [%d]=%s | dzien sendera=%d, poczatek okresu=%s | agregat z sendera (%d dni): H=%.2f L=%.2f C=%.2f | ostatni bar tego wykresu: O=%.2f H=%.2f L=%.2f C=%.2f",
                    (int)BarPeriod.HistoricalChartBarPeriodType, sc.ArraySize,
                    Last, sc.FormatDateTime(sc.BaseDateTimeIn[Last]).GetChars(),
                    Last > 0 ? Last - 1 : 0, sc.FormatDateTime(sc.BaseDateTimeIn[Last > 0 ? Last - 1 : 0]).GetChars(),
                    SrcDay, sc.FormatDateTime(PeriodStart).GetChars(),
                    Days, AggH, AggL, AggC,
                    sc.BaseData[SC_OPEN][Last], sc.BaseData[SC_HIGH][Last],
                    sc.BaseData[SC_LOW][Last], sc.BaseData[SC_LAST][Last]);
                sc.AddMessageToLog(Msg, 0);
            }
        }

        return;     // wykresy wyzszego okresu nie przechodza dalej (logika dzienna nie dotyczy)
    }

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

        // W trybie z wykresem zrodlowym H/L/C inputow zmieniaja sie co tick (studium je odswieza):
        // nie logujemy tych zmian, zeby nie dublowac linii APPLY.
        int  SnapSrc = In_SourceChart.GetChartNumber();
        bool SrcMode = (SnapSrc > 0 && SnapSrc != sc.ChartNumber);

        if (SnapN == 0 || A != SnapA || L != SnapL || R != SnapR || B != SnapB
            || (!SrcMode && (H != SnapH || Lo != SnapLo || C != SnapC)))
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

    // --- Przeladowanie wykresu (np. Reload and Recalculate) moze zignorowac narzucony zakres ---
    // Log z 2026-10-25 11:10:10: ArraySize 1281 -> 1286 przy CutEnd != 0, czyli wykres wrocil do
    // pelnych danych, a studium dalej uwazalo, ze jest obciete. Wtedy narzucamy zakres ponownie.
    // Limit prob chroni przed nieskonczona petla przeladowan, gdyby Sierra zakresu nie przyjela.
    {
        int& Reasserts = sc.GetPersistentInt(34);
        if (CutEnd != 0 && In_Cut.GetYesNo() && sc.ArraySize > 0
            && sc.BaseDateTimeIn[sc.ArraySize - 1].GetDate() > CutEnd)
        {
            if (Reasserts < 3)
            {
                Reasserts++;
                RequestCut(sc, CutEnd);
                if (In_Diag.GetYesNo())
                    LogMsg(sc, "Wykres wrocil do pelnych danych (przeladowanie): narzucam zakres dat ponownie.");
            }
            else if (Reasserts == 3)
            {
                Reasserts++;
                LogMsg(sc, "Nie udalo sie utrzymac zakresu dat po 3 probach - przerywam ponawianie.");
            }
        }
        else
            Reasserts = 0;
    }

    // --- SPRZEZENIE Z WYKRESEM ZRODLOWYM: dzien receivera podaza za dniem sendera ---
    // Gdy podano wykres zrodlowy, 'Bar od konca' jest ustawiany przez studium (Sierra nie ma
    // inputow read-only, wiec reczna zmiana jest nadpisywana). Dalej dziala zwykla sciezka:
    // zmiana 'Bar od konca' -> zaczytanie bara + obciecie wykresu do jego dnia.
    // Wartosci H/L/C przy wykresie zrodlowym: patrz E3 w sekcji APPLY (wymaga Apply = Yes).
    // Lista wykresow w Sierra nie ma pozycji "brak": wybor WLASNEGO wykresu = wylaczone.
    int FollowChart = In_SourceChart.GetChartNumber();
    if (FollowChart == sc.ChartNumber)
        FollowChart = 0;
    if (FollowChart > 0&& !In_Restore.GetYesNo() && !NeedResolve && sc.ArraySize > 0)
    {
        SCDateTimeArray FollowTimes;
        sc.GetChartDateTimeArray(FollowChart, FollowTimes);

        if (FollowTimes.GetArraySize() > 0)
        {
            int FollowDay = sc.GetTradingDayDateForChartNumber(FollowChart, FollowTimes[FollowTimes.GetArraySize() - 1]);

            int WantedIdx = -1;
            for (int i = sc.ArraySize - 1; i >= 0; i--)
                if (sc.BaseDateTimeIn[i].GetDate() == FollowDay) { WantedIdx = i; break; }

            if (WantedIdx >= 0)
            {
                int WantedBarsBack = FullSize - 1 - WantedIdx;
                if (In_BarsBack.GetInt() != WantedBarsBack)
                {
                    In_BarsBack.SetInt(WantedBarsBack);
                    if (In_Diag.GetYesNo())
                    {
                        SCString Msg;
                        Msg.Format("D1BarReplay[E4]: zrodlo ch%d dzien=%d -> bar receivera idx=%d (Bar od konca=%d)",
                            FollowChart, FollowDay, WantedIdx, WantedBarsBack);
                        sc.AddMessageToLog(Msg, 0);
                    }
                }
            }
            else if (CutEnd != 0 && FollowDay > sc.BaseDateTimeIn[sc.ArraySize - 1].GetDate())
            {
                // Dzien zrodla jest pozniej niz obciety zakres: wroc do pelnych danych,
                // a w nastepnych wywolaniach bar zostanie znaleziony.
                ReleaseCut(sc);
                if (In_Diag.GetYesNo())
                    LogMsg(sc, "Zrodlo: dzien poza obcietym zakresem, zdejmuje obciecie.");
            }
            else
            {
                int& LoggedDay = sc.GetPersistentInt(33);
                if (LoggedDay != FollowDay)
                {
                    LoggedDay = FollowDay;
                    SCString Msg;
                    Msg.Format("D1BarReplay[E4]: dnia zrodla (%d) nie ma w danych receivera - bez zmian.", FollowDay);
                    sc.AddMessageToLog(Msg, 0);
                }
            }
        }
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

            float H = 0.0f, L = 0.0f, C = 0.0f;
            bool ValuesReady = true;

            if (FollowChart > 0)
            {
                // E3: H/L/C z ostatniego bara wykresu zrodlowego (sender), tylko gdy replay
                // trwa lub jest wstrzymany. Gdy zatrzymany: bar zostaje oryginalny (settlement).
                ValuesReady = false;

                int Status = (int)sc.GetReplayStatusFromChart(FollowChart);
                int& LastStatus = sc.GetPersistentInt(35);
                if (Status != LastStatus)
                {
                    LastStatus = Status;
                    if (In_Diag.GetYesNo())
                    {
                        SCString Msg;
                        Msg.Format("D1BarReplay[E3]: status replay sendera ch%d = %d (0=stop, 1=gra, 2=pauza)%s",
                            FollowChart, Status, Status == 0 ? " - wartosci sendera nie sa nakladane" : "");
                        sc.AddMessageToLog(Msg, 0);
                    }
                }

                if (Status != 0)
                {
                    SCGraphData SrcData;
                    sc.GetChartBaseData(FollowChart, SrcData);
                    SCDateTimeArray SrcTimes;
                    sc.GetChartDateTimeArray(FollowChart, SrcTimes);

                    int SrcN = SrcTimes.GetArraySize();
                    if (SrcN > 0 && SrcData[SC_LAST].GetArraySize() >= SrcN)
                    {
                        int SrcLast = SrcN - 1;
                        int SrcDay  = sc.GetTradingDayDateForChartNumber(FollowChart, SrcTimes[SrcLast]);

                        // Nakladaj tylko gdy bar docelowy jest z tego samego dnia co bar sendera
                        // (przy przeskoku dnia receiver jeszcze przechodzi na nowy bar)
                        if (TargetDT.GetDate() == SrcDay)
                        {
                            H = sc.RoundToTickSize(SrcData[SC_HIGH][SrcLast]);
                            L = sc.RoundToTickSize(SrcData[SC_LOW][SrcLast]);
                            C = sc.RoundToTickSize(SrcData[SC_LAST][SrcLast]);
                            ValuesReady = true;
                        }
                    }
                }
            }
            else
            {
                H = sc.RoundToTickSize(In_High.GetFloat());
                L = sc.RoundToTickSize(In_Low.GetFloat());
                C = sc.RoundToTickSize(In_Close.GetFloat());
            }

            float O = sc.BaseData[SC_OPEN][Idx];

            // Walidacja: High >= Open, Close, Low  |  Low <= Open, Close
            bool Valid = (H >= O && H >= C && H >= L && L <= O && L <= C);
            if (!ValuesReady)
            {
                // Brak wartosci do nalozenia (replay zatrzymany / inny dzien): bar zostaje oryginalny
            }
            else if (!Valid)
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

                // Przy zasilaniu z sendera inne studia na receiverze nie przeliczaja sie same
                // (wywolanie z UpdateAlways), wiec po zmianie wartosci wymuszamy przeliczenie wykresu.
                if (FollowChart > 0 && Changed)
                {
                    sc.FlagFullRecalculate = 1;

                    // Inputy H/L/C pokazuja aktualnie nalozone wartosci (w tym trybie nie sa zrodlem danych)
                    In_High.SetFloat(H);
                    In_Low.SetFloat(L);
                    In_Close.SetFloat(C);
                }

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
    if (SrcChart == sc.ChartNumber)
        SrcChart = 0;       // wlasny wykres = zrodlo wylaczone
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
