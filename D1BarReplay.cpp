#include "sierrachart.h"

SCDLLName("D1BarReplay_HLC_Override_v1")

/*============================================================================
  D1 Bar Replay (HLC Override)  -  studium dla wykresu D1 HISTORICAL (receiver)

  Cel: nadpisac w PAMIECI High/Low/Close jednego bara D1, tak zeby wszystkie
  wskazniki liczyly sie na stanie z chwili wejscia. Plik .dly nie jest dotykany.

  WERSJA: v0.2.x. Zweryfikowane przez uzytkownika (wersja z logami, commit 1e4baa2):
  D1 (E1-E5) oraz Weekly/Monthly/Quarterly (E8) z replayem na senderze, Stop
  replay, restart Sierry z zapisanym obcieciem i synchronizacja bez replay.
  Nie przetestowane: Yearly, wykresy [C] na datach sprzed rollu oraz ta wersja
  po usunieciu logow (tylko usuniete logi i martwe zmienne, logika bez zmian).
  Lista usunietych logow: LOGI_usuniete.md.

  STATUS BUDOWY (patrz plan w D1BarReplay_PLAN.md):
    E1  Silnik nadpisania (reczny)        <- ZAMKNIETY
    E3  Polaczenie zrodlo -> nadpisanie   <- ZWERYFIKOWANE: wykres zrodlowy (Master chart)
                                             => H/L/C z ostatniego bara sendera, gdy replay gra/pauza
    E4  Przeskok dnia                     <- ZWERYFIKOWANE: receiver podaza za DNIEM wykresu zrodlowego
                                             (obciecie + zaczytanie bara).
    E5  Obciecie danych (lookahead)       <- ZWERYFIKOWANE: obciecie do dnia bara (z sendera lub
                                             parametru 'Bar od konca'), ponowne narzucenie po reloadzie
    E8  Wykresy W/M/Q/Y                   <- ZWERYFIKOWANE dla W, M i Q (agregat dni okresu z sendera)

  'Bar od konca' liczony jest wzgledem PELNYCH danych wykresu (bez obciecia):
  0 = ostatni bar pliku, 1 = przedostatni itd. Po obcieciu bar docelowy jest
  ostatnim barem wykresu, ale numeracja nadal odnosi sie do pelnych danych.

  Persistent (klucze):
    Int   0 : Applied        (1 = nadpisanie jest wpisane w dane)
    Int   1 : TargetIndex    (szybka sciezka, prawda jest w TargetDateTime)
    Int   2 : HasTarget
    Int  30 : LastBarsBack
    Int  32 : Initialized    (0 = pierwsze wywolanie: auto-zaczytanie bara)
    Int  34 : liczba ponowien narzucenia zakresu dat po przeladowaniu wykresu
    Int  40 : NeedResolve    (1 = czekamy na pelne dane, potem zaczytaj bar)
    Int  41 : FullSize       (liczba barow pelnych danych)
    Int  42..44 : zapamietane ustawienia zakresu dat uzytkownika (ByRange, Start, End)
    Int  46 : licznik czyszczenia zastanej daty koncowej zakresu dat (D1, max 3)
    Int  49 : CutEnd         (data koncowa narzucona przez studium, 0 = brak obciecia)
    Int  50 : numer rysunku z czerwonym ostrzezeniem (0 = brak), patrz ShowWarning
    Int  51..53 : W/M/Q/Y: ostatnio zalogowany dzien sendera, status replay, stan (log tylko przy zmianie)
    Input 18 : "Daily chart" (W/M/Q/Y), wykres dzienny z danymi dni okresu przed dniem replay
    Float 0..2 : Orig  H, L, C
    Float 3..5 : Applied H, L, C
    Float 23..25 : D1: ostatnio nalozone H, L, C (reakcja tylko na zmiane) |
                   W/M/Q/Y: stan widziany przez wskazniki (nalozone H/L/C albo 0,0,0 = oryginal)
    SCDateTime 0 : TargetDateTime
============================================================================*/

// --- Pomocnicze ------------------------------------------------------------

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
        int ByRange = sc.LoadChartDataByDateRange;
        int StartD  = sc.ChartDataStartDate;
        int EndD    = sc.ChartDataEndDate;

        // Zakres z data koncowa, zastany gdy studium nie trzyma obciecia (CutEnd == 0), to pozostalosc
        // po poprzedniej sesji: chartbook zapisany z aktywnym obcieciem (log 2026-10-26: po restarcie
        // Daily #2 mial dane tylko do 9.10 i po zdjeciu obciecia tam zostawal). Nie traktujemy go jako
        // ustawienia uzytkownika: data koncowa jest odrzucana. Tryb zakresu zostaje wlaczony:
        // wg dokumentacji Sierry pusta data poczatkowa = pierwsza dostepna data pliku, pusta data
        // koncowa = ostatnia dostepna data, czyli pelna historia (log 2026-10-26: wylaczenie trybu
        // zakresu dawalo na Daily #2 tylko 138 barow po zdjeciu obciecia).
        if (ByRange != 0 && EndD != 0)
            EndD = 0;

        sc.GetPersistentInt(42) = ByRange;
        sc.GetPersistentInt(43) = StartD;
        sc.GetPersistentInt(44) = EndD;
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

// --- Ostrzezenie na wykresie ------------------------------------------------
// Czerwony napis na srodku wykresu, gdy wartosci z mastera zostaly odrzucone (niezgodne z Open tego
// wykresu, np. wykres ciagly [C] ze starszym kontraktem sprzed rolla): bar zostaje wtedy z danymi
// z pliku, a nie ze stanem z replay. Code: 0 = usun napis, 1 = niezgodnosc Open (seria kontraktu),
// 2 = brak danych z wykresu Daily (W/M/Q/Y), 3 = nie ustawiono wykresu Daily (W/M/Q/Y). Numer rysunku trzymany w persistent 50.
static void ShowWarning(SCStudyInterfaceRef sc, int Code)
{
    bool Show = (Code != 0);
    int& Line = sc.GetPersistentInt(50);

    // Rysunek mogl zniknac (przeladowanie wykresu): wtedy numer jest nieaktualny
    if (Line != 0 && sc.ChartDrawingExists(sc.ChartNumber, Line) == 0)
        Line = 0;

    if (!Show)
    {
        if (Line != 0)
        {
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, Line);
            Line = 0;
        }
        return;
    }

    s_UseTool Tool;
    Tool.ChartNumber = sc.ChartNumber;
    Tool.DrawingType = DRAWING_TEXT;
    Tool.Region = sc.GraphRegion;
    Tool.AddMethod = UTAM_ADD_OR_ADJUST;
    Tool.BeginDateTime = 75;                    // pozycja pozioma: skala 0-150 (zrzut: 50 dalo ok. 1/3 szerokosci), 75 = srodek
    Tool.BeginValue = 50.0f;                    // pozycja pionowa: 50% wysokosci (srodek)
    Tool.UseRelativeVerticalValues = 1;
    Tool.TextAlignment = DT_CENTER | DT_VCENTER;
    Tool.Color = RGB(255, 255, 255);
    Tool.FontBackColor = RGB(200, 0, 0);
    Tool.TransparentLabelBackground = 0;
    Tool.FontBold = 1;
    Tool.FontSize = 16;
    Tool.FontFace = sc.GetChartTextFontFaceName();
    // Studium dalej podaza za masterem i obcina wykres, ale NIE naklada H/L/C z replay: bar zostaje z pliku.
    if (Code == 3)
        Tool.Text = "D1 Bar Replay - Ustaw Daily chart.\n"
                    "Narzedzie dziala niepoprawnie - dane z replay NIE nalozone";
    else if (Code == 2)
        Tool.Text = "D1 Bar Replay - brak danych z wykresu Daily.\n"
                    "Narzedzie dziala niepoprawnie - dane z replay NIE nalozone";
    else
        Tool.Text = "D1 Bar Replay - niezgodnosc serii kontraktu (Open price).\n"
                    "Narzedzie dziala niepoprawnie - dane z replay NIE nalozone";
    Tool.DrawUnderneathMainGraph = 0;

    if (Line != 0)
        Tool.LineNumber = Line;

    if (sc.UseTool(Tool) > 0)
        Line = Tool.LineNumber;
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
    SCInputRef In_Apply       = sc.Input[0];    // dawniej "TURN ON/OFF"; ukryty i ignorowany (nakladanie wlacza sam Master chart)
    SCInputRef In_Load        = sc.Input[1];
    SCInputRef In_High        = sc.Input[2];
    SCInputRef In_Low         = sc.Input[3];
    SCInputRef In_Close       = sc.Input[4];
    SCInputRef In_Restore     = sc.Input[5];
    SCInputRef In_BarsBack    = sc.Input[6];
    SCInputRef In_SourceChart = sc.Input[7];
    SCInputRef In_Unused8     = sc.Input[8];    // dawniej "Diagnostyka w logu"; slot zostaje, zeby nie przesunac inputow
    SCInputRef In_Cut         = sc.Input[9];
    // Inputy 10-17: pola [RO] (patrz WriteSlot)
    SCInputRef In_DailyChart  = sc.Input[18];   // tylko W/M/Q/Y: wykres Daily z danymi dni okresu (wymagany)
    // W oknie ustawien widoczne sa tylko In_SourceChart ("Master chart") i In_DailyChart ("Daily chart");
    // reszta nie ma nazwy, wiec jest ukryta (patrz SetDefaults).

    if (sc.SetDefaults)
    {
        sc.GraphName = "D1 Bar Replay (HLC Override)";
        sc.StudyDescription = "Nadpisuje H/L/C jednego bara w pamieci wykresu. Nie zapisuje do plikow danych. "
            "Master chart: wybierz wykres z Bar Replay; wybranie wlasnego wykresu = wylaczone. "
            "Daily chart: wymagany na wykresach Weekly/Monthly/Quarterly/Yearly, wykres dzienny z tej samej serii kontraktow.";
        sc.AutoLoop = 0;            // manual looping: jedno wywolanie, nie petla po barach
        sc.UpdateAlways = 1;        // potrzebne do odpytywania wykresu zrodlowego (E2+)
        sc.GraphRegion = 0;
        sc.CalculationPrecedence = STD_PREC_LEVEL;   // kolejnosc: studium ma byc NA GORZE listy studiow
        sc.ProcessStudyUnsetInputs = 1;     // wiele ukrytych (bez nazwy) inputow z rzedu

        // Widoczny dla uzytkownika jest tylko jeden input. Input bez ustawionej nazwy (Name) Sierra
        // nie pokazuje w oknie ustawien studium, ale nadal istnieje i dziala w kodzie.
        // Wylaczenie narzedzia = wybor wlasnego wykresu jako Master chart.
        In_Apply.SetYesNo(1);

        In_SourceChart.Name = "Master chart";
        In_SourceChart.SetChartNumber(0);

        // Tylko dla wykresow Weekly/Monthly/Quarterly/Yearly: wykres Daily (historical, ta sama seria
        // kontraktow) z ktorego studium bierze dni okresu przed dniem replay. Na wykresie D1 ignorowany.
        In_DailyChart.Name = "Daily chart (tylko W/M/Q/Y)";
        In_DailyChart.SetChartNumber(0);

        // Ukryte (bez nazwy): reczny tryb, bar od konca, obciecie, pola [RO]
        In_Load.SetYesNo(0);
        In_High.SetFloat(0.0f);
        In_Low.SetFloat(0.0f);
        In_Close.SetFloat(0.0f);
        In_Restore.SetYesNo(0);
        In_BarsBack.SetInt(0);
        In_BarsBack.SetIntLimits(0, 100000);
        In_Unused8.SetYesNo(0);
        In_Cut.SetYesNo(1);

        // Pola tylko do odczytu (wypelnia je studium): oryginaly zaczytanych barow
        for (int Base = SLOT1_BASE; Base <= SLOT2_BASE; Base += SLOT2_BASE - SLOT1_BASE)
        {
            sc.Input[Base].SetString("");
            sc.Input[Base + 1].SetFloat(0.0f);
            sc.Input[Base + 2].SetFloat(0.0f);
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
            ShowWarning(sc, false);
            ReleaseCut(sc);
            return;
        }

        int HtfSrc = In_SourceChart.GetChartNumber();

        // --- Cofanie daty: wykres wyzszego okresu konczy sie na dniu sendera ---
        // Ostatni bar jest wtedy czesciowym okresem (np. tygodniem do dnia sendera).
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
                // Ustawienie zakresu moze obowiazywac, a dane byc pelne (po "Reload and Recalculate"):
                // sprawdzamy tez, czy ostatni bar nie jest pozniejszy niz dzien obciecia (jak na D1).
                bool InEffect = (sc.LoadChartDataByDateRange == 1 && sc.ChartDataEndDate == WantedEnd
                    && (sc.ArraySize == 0 || sc.BaseDateTimeIn[sc.ArraySize - 1].GetDate() <= WantedEnd));

                if (CutEnd != WantedEnd)
                    RequestCut(sc, WantedEnd);
                else if (!InEffect && HtfReasserts < 3)
                {
                    // Przeladowanie wykresu moglo przywrocic pelne dane: narzuc zakres ponownie
                    HtfReasserts++;
                    RequestCut(sc, WantedEnd);
                }
                else if (InEffect)
                    HtfReasserts = 0;
            }
        }

        // --- Dane sendera i agregat dni biezacego okresu (dla zapisu E8) ---
        // Agregat: High = max High, Low = min Low dni okresu do dnia sendera, Close = biezaca
        // cena sendera. Poczatek okresu liczymy z POLUDNIA dnia handlowego, bo bar sendera dnia
        // handlowego zaczyna sie dzien wczesniej wieczorem (np. 1.10 zaczyna sie 30.09 18:00,
        // co dla okresu miesiecznego wskazaloby poprzedni miesiac).
        // Dni okresu PRZED dniem sendera bierzemy z wykresu Daily (In_DailyChart: dane z pliku, ta sama
        // seria kontraktow co ten wykres), a z sendera tylko dzien replay. Bez wykresu Daily nic nie
        // nakladamy i pokazujemy ostrzezenie (agregat calego okresu z samego sendera dawal na wykresach
        // [C] roznice 50-60 pkt w okresach z rollem, wiec tego trybu juz nie ma).
        bool  HaveAgg = false;
        int   SrcDay = 0, Days = 0;
        float AggH = -1.0e10f, AggL = 1.0e10f, AggC = 0.0f;
        SCDateTime PeriodStart;

        // Lista wykresow nie ma pozycji "brak": wybor WLASNEGO wykresu albo wykresu MASTER traktujemy
        // jak nieustawiony wykres Daily (DailySrc = 0 -> napis "Ustaw Daily chart").
        int   DailySrc = In_DailyChart.GetChartNumber();
        if (DailySrc == sc.ChartNumber || DailySrc == HtfSrc)
            DailySrc = 0;
        bool  DailyOk = false;              // dane dzienne z wykresu Daily uzyte
        bool  DayOpenMismatch = false;      // Open dnia z sendera != Open tego dnia z wykresu Daily
        int   DailyDays = 0, DailyFirstDate = 0;
        float DailyOpenFirst = 0.0f, DailyOpenDay = 0.0f;
        float SrcO = 0.0f, SrcH = 0.0f, SrcL = 0.0f;
        const float OpenTol = sc.TickSize * 0.5f;       // Open ma byc identyczny (pol ticka = tylko zaokraglenie float)
        int   ReplayStatus = (HtfSrc > 0 && HtfSrc != sc.ChartNumber) ? (int)sc.GetReplayStatusFromChart(HtfSrc) : 0;   // 0 = stop, 1 = gra, 2 = pauza
        int   WarnCode = 0;                 // 0 = brak, 1 = niezgodnosc Open, 2 = brak danych z wykresu Daily, 3 = nie ustawiono wykresu Daily

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
                SrcO = HtfData[SC_OPEN][N - 1];
                SrcH = HtfData[SC_HIGH][N - 1];
                SrcL = HtfData[SC_LOW][N - 1];

                if (DailySrc > 0)
                {
                    // Dni okresu z wykresu Daily (musi byc dziennym wykresem historical, siegac dnia sendera)
                    n_ACSIL::s_BarPeriod DailyPeriod;
                    sc.GetBarPeriodParametersForChart(DailySrc, DailyPeriod);
                    SCDateTimeArray DTimes;
                    sc.GetChartDateTimeArray(DailySrc, DTimes);
                    SCGraphData DData;
                    sc.GetChartBaseData(DailySrc, DData);

                    int DN = DTimes.GetArraySize();
                    bool IsDailyChart = (DailyPeriod.ChartDataType == DAILY_DATA
                        && DailyPeriod.HistoricalChartBarPeriodType == HISTORICAL_CHART_PERIOD_DAYS);

                    if (IsDailyChart && DN > 0 && DData[SC_LAST].GetArraySize() >= DN
                        && DTimes[DN - 1].GetDate() >= SrcDay)
                    {
                        int PStart = PeriodStart.GetDate();
                        int FirstIdx = -1, DayIdx = -1;
                        bool ReachedBefore = false;
                        float DH = -1.0e10f, DL = 1.0e10f;

                        for (int i = DN - 1; i >= 0; i--)
                        {
                            int d = DTimes[i].GetDate();
                            if (d > SrcDay)         // wykres Daily jeszcze nie obciety do dnia sendera
                                continue;
                            if (d < PStart)
                            {
                                ReachedBefore = true;
                                break;
                            }
                            if (d == SrcDay)
                                DayIdx = i;         // dzien replay: H/L/C dostarcza sender
                            else
                            {
                                if (DData[SC_HIGH][i] > DH) DH = DData[SC_HIGH][i];
                                if (DData[SC_LOW][i]  < DL) DL = DData[SC_LOW][i];
                                DailyDays++;
                            }
                            FirstIdx = i;
                            DailyFirstDate = d;
                        }

                        bool Covered = ReachedBefore || (DTimes[0].GetDate() - PStart <= 2);
                        if (FirstIdx >= 0 && Covered)
                        {
                            DailyOk = true;
                            DailyOpenFirst = DData[SC_OPEN][FirstIdx];

                            // Zgodnosc serii kontraktow: Open dnia replay z sendera vs Open tego dnia z pliku
                            if (DayIdx >= 0)
                            {
                                DailyOpenDay = DData[SC_OPEN][DayIdx];
                                DayOpenMismatch = (fabs((double)SrcO - (double)DailyOpenDay) > (double)OpenTol);
                            }

                            AggH = (SrcH > DH) ? SrcH : DH;
                            AggL = (SrcL < DL) ? SrcL : DL;
                            Days = DailyDays + 1;
                            HaveAgg = true;
                        }
                    }
                }
            }
        }

        if (DailySrc <= 0 && ReplayStatus != 0)
            WarnCode = 3;       // brak wykresu Daily w ustawieniach: narzedzie nic nie naklada
        else if (DailySrc > 0 && !DailyOk && ReplayStatus != 0)
            WarnCode = 2;
        else if (DayOpenMismatch && ReplayStatus != 0)
            WarnCode = 1;

        // --- E8: nalozenie zagregowanych H/L/C na ostatni bar (czesciowy okres) ---
        // Warunki: wykres obciety do dnia sendera, replay gra lub pauza.
        // Przy zatrzymanym replay bar zostaje oryginalny (Revert wykonany na poczatku bloku).
        if (HaveAgg && CutEnd != 0 && CutEnd == SrcDay && sc.ArraySize > 0)
        {
            int Status = ReplayStatus;

            int Last  = sc.ArraySize - 1;
            int Stamp = sc.BaseDateTimeIn[Last].GetDate();

            // Ostatni bar musi byc biezacym okresem (data znacznika bara w [poczatek okresu, dzien sendera])
            if (Status != 0 && Stamp >= PeriodStart.GetDate() && Stamp <= SrcDay)
            {
                float H = sc.RoundToTickSize(AggH);
                float L = sc.RoundToTickSize(AggL);
                float C = sc.RoundToTickSize(AggC);
                float O = sc.BaseData[SC_OPEN][Last];

                // Z wykresem Daily: Open okresu na tym wykresie musi zgadzac sie z Open pierwszego dnia
                // okresu z wykresu Daily (inaczej to inna seria kontraktow). Open dnia replay sprawdzono wyzej.
                bool PeriodOpenMismatch = (DailyOk && fabs((double)O - (double)DailyOpenFirst) > (double)OpenTol);

                if (!PeriodOpenMismatch && !DayOpenMismatch
                    && H >= O && H >= C && H >= L && L <= O && L <= C)
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
                }
                else
                    WarnCode = 1;
            }
        }

        // --- Log (tylko przy zmianie dnia sendera, statusu replay lub stanu): do sprawdzenia agregatu ---
        {
            int& LogDay    = sc.GetPersistentInt(51);
            int& LogStatus = sc.GetPersistentInt(52);
            int& LogState  = sc.GetPersistentInt(53);
            int State = (Applied == 1) ? 3 : WarnCode;      // 0 = nie nakladam, 1/2 = ostrzezenie, 3 = nalozone

            if (HtfSrc > 0 && HtfSrc != sc.ChartNumber
                && (SrcDay != LogDay || ReplayStatus != LogStatus || State != LogState))
            {
                LogDay = SrcDay; LogStatus = ReplayStatus; LogState = State;

                float FileH = 0.0f, FileL = 0.0f, FileC = 0.0f, BarO = 0.0f;
                if (sc.ArraySize > 0)
                {
                    int Lb = sc.ArraySize - 1;
                    BarO  = sc.BaseData[SC_OPEN][Lb];
                    FileH = (Applied == 1) ? OrigH : sc.BaseData[SC_HIGH][Lb];
                    FileL = (Applied == 1) ? OrigL : sc.BaseData[SC_LOW][Lb];
                    FileC = (Applied == 1) ? OrigC : sc.BaseData[SC_LAST][Lb];
                }

                SCString Msg;
                Msg.Format("[HTF] okres=%d dzien=%d status=%d zrodlo=%s stan=%d | Daily: dni=%d od=%d Open1=%.2f OpenBar=%.2f"
                    " | sender: O=%.2f H=%.2f L=%.2f C=%.2f OpenDailyDnia=%.2f"
                    " | agregat H=%.2f L=%.2f C=%.2f dni=%d | plik H=%.2f L=%.2f C=%.2f",
                    (int)BarPeriod.HistoricalChartBarPeriodType, SrcDay, ReplayStatus,
                    DailyOk ? "Daily" : (DailySrc > 0 ? "Daily-BRAK" : "nie-ustawiono"), State,
                    DailyDays, DailyFirstDate, DailyOpenFirst, BarO,
                    SrcO, SrcH, SrcL, AggC, DailyOpenDay,
                    AggH, AggL, AggC, Days, FileH, FileL, FileC);
                sc.AddMessageToLog(Msg, 0);
            }
        }

        ShowWarning(sc, WarnCode);

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

        return;     // wykresy wyzszego okresu nie przechodza dalej (logika dzienna nie dotyczy)
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

        Applied = 0;
    }

    // --- SPRZATANIE: usuniecie studium / zamkniecie wykresu -------------------
    if (sc.LastCallToFunction)
    {
        ShowWarning(sc, false);
        ReleaseCut(sc);     // przywroc ustawienia zakresu dat uzytkownika
        return;             // Revert wykonany powyzej
    }

    // --- Rozmiar PELNYCH danych (bez obciecia) ---------------------------------
    // Gdy czekamy na przeladowanie po zdjeciu obciecia, nie nadpisuj mniejsza wartoscia.
    if (CutEnd == 0 && (!NeedResolve || sc.ArraySize >= FullSize))
        FullSize = sc.ArraySize;

    // --- Wylaczenie opcji obciecia: zdejmij obciecie ---------------------------
    if (!In_Cut.GetYesNo() && CutEnd != 0)
        ReleaseCut(sc);

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
            }
        }
        else
            Reasserts = 0;
    }

    // --- SPRZEZENIE Z WYKRESEM ZRODLOWYM: dzien receivera podaza za dniem sendera ---
    // Gdy podano wykres zrodlowy, 'Bar od konca' jest ustawiany przez studium (Sierra nie ma
    // inputow read-only, wiec reczna zmiana jest nadpisywana). Dalej dziala zwykla sciezka:
    // zmiana 'Bar od konca' -> zaczytanie bara + obciecie wykresu do jego dnia.
    // Wartosci H/L/C przy wykresie zrodlowym: patrz E3 w sekcji APPLY.
    // Lista wykresow w Sierra nie ma pozycji "brak": wybor WLASNEGO wykresu = wylaczone.
    bool ForceNewBar = false;   // 1 = zaczytaj bar ponownie, nawet gdy 'Bar od konca' sie nie zmienilo
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
                sc.GetPersistentInt(46) = 0;    // licznik czyszczenia zastanej daty koncowej zakresu
                int WantedBarsBack = FullSize - 1 - WantedIdx;
                if (In_BarsBack.GetInt() != WantedBarsBack)
                    In_BarsBack.SetInt(WantedBarsBack);
                else if (HasTarget && TargetDT.GetDate() != FollowDay)
                {
                    // 'Bar od konca' zgadza sie liczbowo, ale zaczytany bar jest z innego dnia niz dzien
                    // zrodla (po restarcie zapisane 'Bar od konca' wskazuje dzien na skroconych danych,
                    // a po zaladowaniu pelnych danych ta sama liczba daje dzien zrodla).
                    ForceNewBar = true;
                }
            }
            else if (CutEnd != 0 && FollowDay > sc.BaseDateTimeIn[sc.ArraySize - 1].GetDate())
            {
                // Dzien zrodla jest pozniej niz obciety zakres: wroc do pelnych danych,
                // a w nastepnych wywolaniach bar zostanie znaleziony.
                ReleaseCut(sc);
            }
            else if (CutEnd == 0 && sc.LoadChartDataByDateRange != 0 && sc.ChartDataEndDate != 0
                && FollowDay > sc.BaseDateTimeIn[sc.ArraySize - 1].GetDate()
                && sc.GetPersistentInt(46) < 3)
            {
                // Wykres zaladowany z zapisanym zakresem dat z data koncowa, a studium go nie narzucilo
                // (CutEnd == 0): pozostalosc po poprzedniej sesji, wiec dane koncza sie przed dniem
                // mastera. Czyscimy date koncowa (pusta = do ostatniej dostepnej daty pliku).
                // Limit prob chroni przed petla przeladowan.
                sc.GetPersistentInt(46)++;
                sc.ChartDataEndDate = 0;
            }
        }
    }

    // --- ZMIANA "BAR OD KONCA": zwolnij stary bar i zaczytaj nowy (reset do oryginalu) ---
    // Revert powyzej juz przywrocil stary bar. Zerujemy cel, a Load nizej wpisze do
    // inputow oryginalne H/L/C nowego bara. Przy wykresie zrodlowym Apply zastosuje je ponownie.
    int& LastBarsBack = sc.GetPersistentInt(30);
    bool BarsBackChanged = (In_BarsBack.GetInt() != LastBarsBack) || ForceNewBar;
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
        }
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
        In_Restore.SetYesNo(0);
        return;
    }

    // --- 3. LOAD / auto-capture -----------------------------------------------
    bool DoLoad = In_Load.GetYesNo() != 0;

    if (FollowChart > 0 && !HasTarget)
        DoLoad = true;      // Master chart bez wczesniejszego Load: wybierz bar sam

    if (ReloadNewBar)
        DoLoad = true;      // po zmianie 'Bar od konca' zaczytaj nowy bar

    if (AutoInit)
        DoLoad = true;      // start studium: zaczytaj bar wskazany przez 'Bar od konca'

    // Po zdjeciu obciecia czekalismy na pelne dane: teraz zaczytaj bar
    if (NeedResolve && CutEnd == 0 && sc.ArraySize >= FullSize)
        DoLoad = true;

    bool CutJustRequested = false;
    bool Warn = false;      // wartosci odrzucone: pokaz ostrzezenie na wykresie

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
            return;
        }

        if (Idx < 0 || Idx >= sc.ArraySize)
        {
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

        // --- OBCIECIE DANYCH DO DNIA BARA (brak lookahead) ---
        if (In_Cut.GetYesNo())
        {
            int EndDate = TargetDT.GetDate();
            bool IsLastOfFull = (CutEnd == 0 && Idx == sc.ArraySize - 1);

            if (!IsLastOfFull && CutEnd != EndDate)
            {
                RequestCut(sc, EndDate);
                CutJustRequested = true;
            }
        }
    }

    // --- 4. APPLY ----------------------------------------------------------------
    // Gdy wlasnie zlecilismy obciecie, wykres zaraz sie przeladuje, wiec Apply
    // wykona sie w nastepnym wywolaniu na swiezych danych.
    if (FollowChart > 0 && HasTarget && !CutJustRequested)
    {
        int Idx = FindTargetIndex(sc, TargetIdx, TargetDT);
        if (Idx >= 0)       // bar docelowy zniknal (zmiana danych): nic nie nakladamy
        {
            TargetIdx = Idx;

            float H = 0.0f, L = 0.0f, C = 0.0f;
            bool ValuesReady = true;

            if (FollowChart > 0)
            {
                // E3: H/L/C z ostatniego bara wykresu zrodlowego (sender), tylko gdy replay
                // trwa lub jest wstrzymany. Gdy zatrzymany: bar zostaje oryginalny (settlement).
                ValuesReady = false;

                int Status = (int)sc.GetReplayStatusFromChart(FollowChart);     // 0 = stop, 1 = gra, 2 = pauza

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
            Warn = (ValuesReady && !Valid);

            // Brak wartosci do nalozenia (replay zatrzymany / inny dzien) albo wartosci niespojne
            // z Open: bar zostaje oryginalny.
            if (ValuesReady && Valid)
            {
                // Bar jest czysty (po Revert) - zapamietaj oryginal i zapisz nowe wartosci
                OrigH = sc.BaseData[SC_HIGH][Idx];
                OrigL = sc.BaseData[SC_LOW][Idx];
                OrigC = sc.BaseData[SC_LAST][Idx];

                WriteBar(sc, Idx, H, L, C);

                AppH = H;
                AppL = L;
                AppC = C;
                Applied = 1;

                // UpdateAlways = 1 wola studium ciagle, wiec reagujemy tylko na zmiane wartosci
                float& LastH = sc.GetPersistentFloat(23);
                float& LastL = sc.GetPersistentFloat(24);
                float& LastC = sc.GetPersistentFloat(25);
                bool Changed = (H != LastH || L != LastL || C != LastC);
                LastH = H; LastL = L; LastC = C;

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
            }
        }
    }

    ShowWarning(sc, Warn);
}
