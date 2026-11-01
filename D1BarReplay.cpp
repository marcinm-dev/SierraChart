#include "sierrachart.h"

SCDLLName("D1BarReplay_HLC_Override_v1")

/*============================================================================
  D1 Bar Replay (HLC Override)  -  study for a D1 HISTORICAL chart (receiver)

  Goal: overwrite High/Low/Close of one D1 bar in MEMORY so that all indicators
  calculate on the state at entry time. The .dly file is never touched.

  VERSION: V1 (git tag d1barreplay-v1; earlier tags v0.1.0-prototype .. v0.3.0). Verified by the user: D1 (E1-E5), Weekly/Monthly/Quarterly
  (E8) with replay on the sender, Stop replay, Sierra restart with a saved cut,
  sync without replay, W/M/Q with period days taken from the Daily chart ("Daily chart" input),
  red error texts (Open mismatch, Daily chart not set).
  Not confirmed in the logs we reviewed: exact Open check on D1, day without a bar in the file
  (text 4), date mismatch text (5), Daily chart text (6), [D1] log, Yearly.
  What is in this version is in the commit messages and the tags.
  List of logs removed from the production version: LOGI_usuniete.md.

  BUILD STATUS (details in CLAUDE.md):
    E1  Override engine (manual)         <- CLOSED
    E3  Source -> override link          <- VERIFIED: source chart (Master chart)
                                             => H/L/C of the sender's last bar while replay plays/pauses
    E4  Day jump                         <- VERIFIED: receiver follows the DAY of the source chart
                                             (cut + bar load).
    E5  Data cut (lookahead)             <- VERIFIED: cut to the bar's day (from the sender or the
                                             'Bar od konca' parameter), re-imposed after a reload
    E8  W/M/Q/Y charts                   <- VERIFIED for W, M and Q (period aggregate: Daily chart days + the sender's replay day)

  'Bar od konca' (bars back) is counted against the FULL chart data (without the cut):
  0 = last bar of the file, 1 = second to last, etc. After the cut the target bar is
  the last bar of the chart, but the numbering still refers to the full data.

  Persistent (keys):
    Int   0 : Applied        (1 = the override is written into the data)
    Int   1 : TargetIndex    (fast path, the truth is in TargetDateTime)
    Int   2 : HasTarget
    Int  30 : LastBarsBack
    Int  32 : Initialized    (0 = first call: auto-load the bar)
    Int  34 : number of retries of re-imposing the date range after a chart reload
    Int  40 : NeedResolve    (1 = waiting for full data, then load the bar)
    Int  41 : FullSize       (number of bars of the full data)
    Int  42..44 : remembered user date range settings (ByRange, Start, End)
    Int  46 : counter of clearing a leftover range end date (D1, max 3)
    Int  49 : CutEnd         (end date imposed by the study, 0 = no cut)
    Int  50 : number of the red warning drawing (0 = none), see ShowWarning
    Int  54 : D1: master day confirmed as missing in the full data (0 = none)
    Int  55, 56 : D1: last logged warning and day (logged only on change)
    Int  51..53 : W/M/Q/Y: last logged sender day, replay status, state (logged only on change)
    Input 18 : "Daily chart" (W/M/Q/Y), daily chart with the period's days before the replay day
    Float 0..2 : Orig  H, L, C
    Float 3..5 : Applied H, L, C
    Float 23..25 : D1: last applied H, L, C (react only on change) |
                   W/M/Q/Y: state seen by indicators (applied H/L/C or 0,0,0 = original)
    SCDateTime 0 : TargetDateTime
============================================================================*/

// --- Helpers ------------------------------------------------------------

static bool SamePrice(SCStudyInterfaceRef sc, float A, float B)
{
    return fabs((double)A - (double)B) < (double)sc.TickSize * 0.5;
}

// Write H/L/C to the base data plus the derived arrays (as in the spec, section 5)
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

// The target bar is looked up by date and time. The index is only a fast path.
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

// --- Chart date range cut (E5, early version) ---------------------
// Changes to sc.LoadChartDataByDateRange / ChartDataEndDate take effect only after the
// study function returns (the chart reloads). The reload discards the override
// in memory, so Apply runs again on the next call.

static void RequestCut(SCStudyInterfaceRef sc, int EndDate)
{
    int& CutEnd = sc.GetPersistentInt(49);

    if (CutEnd == 0)    // first cut: remember the user's settings
    {
        int ByRange = sc.LoadChartDataByDateRange;
        int StartD  = sc.ChartDataStartDate;
        int EndD    = sc.ChartDataEndDate;

        // A range with an end date found while the study is not holding a cut (CutEnd == 0) is a leftover
        // from a previous session: the chartbook was saved with an active cut (log 2026-10-26: after a restart
        // Daily #2 had data only up to 9.10 and stayed there after the cut was released). We do not treat it as
        // a user setting: the end date is discarded. Range mode stays on:
        // per the Sierra docs an empty start date = first available date of the file, an empty
        // end date = last available date, i.e. the full history (log 2026-10-26: switching range mode
        // off gave only 138 bars on Daily #2 after the cut was released).
        if (ByRange != 0 && EndD != 0)
            EndD = 0;

        sc.GetPersistentInt(42) = ByRange;
        sc.GetPersistentInt(43) = StartD;
        sc.GetPersistentInt(44) = EndD;
    }

    if (sc.GetPersistentInt(42) == 0)
        sc.ChartDataStartDate = 0;      // from the start of the file

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

// --- On-chart warning ------------------------------------------------
// Red text in the center of the chart when the master's values were rejected (Open does not match this
// chart's Open, e.g. a continuous [C] chart with an older contract before the roll): the bar then keeps
// the file data instead of the replay state. Code: 0 = remove the text, 1 = Open mismatch (contract series),
// 2 = no data from the Daily chart (W/M/Q/Y), 3 = Daily chart not set (W/M/Q/Y),
// 4 = no bar for the master's day in the file (D1), 5 = date mismatch after a holiday (different split of
// days in the sender and in the file), 6 = Daily chart starts after the period start (W/M/Q/Y). The drawing number is kept in persistent 50.
static void ShowWarning(SCStudyInterfaceRef sc, int Code)
{
    bool Show = (Code != 0);
    int& Line = sc.GetPersistentInt(50);

    // The drawing may have vanished (chart reload): then the number is stale
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
    Tool.BeginDateTime = 75;                    // horizontal position: scale 0-150 (screenshot: 50 gave about 1/3 of the width), 75 = center
    Tool.BeginValue = 50.0f;                    // vertical position: 50% of the height (center)
    Tool.UseRelativeVerticalValues = 1;
    Tool.TextAlignment = DT_CENTER | DT_VCENTER;
    Tool.Color = RGB(255, 255, 255);
    Tool.FontBackColor = RGB(200, 0, 0);
    Tool.TransparentLabelBackground = 0;
    Tool.FontBold = 1;
    Tool.FontSize = 16;
    Tool.FontFace = sc.GetChartTextFontFaceName();
    // The study still follows the master and cuts the chart, but does NOT apply the replay H/L/C: the bar keeps the file values.
    if (Code == 6)
        Tool.Text = "D1 Bar Replay - Daily chart nie siega poczatku okresu (wczytaj dane od 1980).\n"
                    "Narzedzie dziala niepoprawnie - dane z replay NIE nalozone";
    else if (Code == 5)
        Tool.Text ="D1 Bar Replay - niezgodnosc dat (swieto: dni scalone w pliku).\n"
                    "Narzedzie dziala niepoprawnie - dane z replay NIE nalozone";
    else if (Code == 4)
        Tool.Text = "D1 Bar Replay - brak bara dla dnia mastera w pliku (np. swieto).\n"
                    "Wykres pokazuje poprzedni dzien - dane z replay NIE nalozone";
    else if (Code == 3)
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

// --- [RO] fields: originals of the loaded bars ----------------------
// Safety net: if something goes wrong, the bar can be restored by hand.
// Sierra has no read-only inputs, so the study fills them itself and never reads them
// back (user edits have no effect).
// Slot 1 = most recently loaded bar, Slot 2 = previously loaded bar.
// Each slot: date, High, Low, Close (Open never changes, so it is not shown).
static const int SLOT1_BASE = 10;
static const int SLOT2_BASE = 14;

static void WriteSlot(SCStudyInterfaceRef sc, int Base, const char* DateText, float H, float L, float C)
{
    sc.Input[Base].SetString(DateText);
    sc.Input[Base + 1].SetFloat(H);
    sc.Input[Base + 2].SetFloat(L);
    sc.Input[Base + 3].SetFloat(C);
}

// Slot 1 -> Slot 2 (only when loading a DIFFERENT bar than the one saved in slot 1)
static void ShiftSlots(SCStudyInterfaceRef sc)
{
    SCString OldDate = sc.Input[SLOT1_BASE].GetString();    // copy before writing
    WriteSlot(sc, SLOT2_BASE, OldDate.GetChars(),
        sc.Input[SLOT1_BASE + 1].GetFloat(), sc.Input[SLOT1_BASE + 2].GetFloat(),
        sc.Input[SLOT1_BASE + 3].GetFloat());
}

// --- Study ---------------------------------------------------------------

SCSFExport scsf_D1BarReplay(SCStudyInterfaceRef sc)
{
    SCInputRef In_Apply       = sc.Input[0];    // formerly "TURN ON/OFF"; hidden and ignored (selecting Master chart alone enables the override)
    SCInputRef In_Load        = sc.Input[1];
    SCInputRef In_High        = sc.Input[2];
    SCInputRef In_Low         = sc.Input[3];
    SCInputRef In_Close       = sc.Input[4];
    SCInputRef In_Restore     = sc.Input[5];
    SCInputRef In_BarsBack    = sc.Input[6];
    SCInputRef In_SourceChart = sc.Input[7];
    SCInputRef In_Unused8     = sc.Input[8];    // formerly "Diagnostics in log"; the slot stays so that the inputs do not shift
    SCInputRef In_Cut         = sc.Input[9];
    // Inputs 10-17: [RO] fields (see WriteSlot)
    SCInputRef In_DailyChart  = sc.Input[18];   // W/M/Q/Y only: Daily chart with the period's days (required)
    // Only In_SourceChart ("Master chart") and In_DailyChart ("Daily chart") are visible in the settings window;
    // the rest has no name, so it is hidden (see SetDefaults).

    if (sc.SetDefaults)
    {
        sc.GraphName = "D1 Bar Replay (HLC Override)";
        sc.StudyDescription = "Nadpisuje H/L/C jednego bara w pamieci wykresu. Nie zapisuje do plikow danych. "
            "Master chart: wybierz wykres z Bar Replay; wybranie wlasnego wykresu = wylaczone. "
            "Daily chart: wymagany na wykresach Weekly/Monthly/Quarterly/Yearly, wykres dzienny z tej samej serii kontraktow.";
        sc.AutoLoop = 0;            // manual looping: one call, not a loop over bars
        sc.UpdateAlways = 1;        // needed to poll the source chart (E2+)
        sc.GraphRegion = 0;
        sc.CalculationPrecedence = STD_PREC_LEVEL;   // order: the study must be AT THE TOP of the study list
        sc.ProcessStudyUnsetInputs = 1;     // many hidden (unnamed) inputs in a row

        // Only two inputs are visible to the user. Sierra does not show an input without a Name in the study
        // settings window, but it still exists and works in code.
        // Switching the tool off = selecting the chart's own chart as Master chart.
        In_Apply.SetYesNo(1);

        In_SourceChart.Name = "Master chart";
        In_SourceChart.SetChartNumber(0);

        // W/M/Q/Y charts only: the Daily chart (historical, same contract series) from which the study takes
        // the period's days before the replay day. Ignored on a D1 chart.
        In_DailyChart.Name = "Daily chart (tylko W/M/Q/Y)";
        In_DailyChart.SetChartNumber(0);

        // Hidden (unnamed): manual mode, bars back, cut, [RO] fields
        In_Load.SetYesNo(0);
        In_High.SetFloat(0.0f);
        In_Low.SetFloat(0.0f);
        In_Close.SetFloat(0.0f);
        In_Restore.SetYesNo(0);
        In_BarsBack.SetInt(0);
        In_BarsBack.SetIntLimits(0, 100000);
        In_Unused8.SetYesNo(0);
        In_Cut.SetYesNo(1);

        // Read-only fields (filled by the study): originals of the loaded bars
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

    // --- HIGHER-PERIOD CHART (W1/M1/Q1/Y1): cut to the sender's day + period aggregate (E8) ---
    // The daily logic assumes that the receiver's bar is one day with the sender's date.
    // That is not so on a weekly/monthly chart, hence a separate path: the chart ends
    // on the sender's day (last bar = partial period) and its H/L/C is the aggregate of the period's days
    // (High = max, Low = min, Close = the sender's current price).
    n_ACSIL::s_BarPeriod BarPeriod;
    sc.GetBarPeriodParameters(BarPeriod);
    bool IsHTF = (BarPeriod.ChartDataType == DAILY_DATA
        && BarPeriod.HistoricalChartBarPeriodType != HISTORICAL_CHART_PERIOD_DAYS);

    // Safeguard: a gap >= 6 days between consecutive bars (twice in a row) = a period longer
    // than daily (daily bars have gaps of at most ~4 days), in case the detection above fails.
    if (!IsHTF && sc.ArraySize >= 3)
    {
        int L1 = sc.ArraySize - 1;
        if (sc.BaseDateTimeIn[L1].GetDate() - sc.BaseDateTimeIn[L1 - 1].GetDate() >= 6
            && sc.BaseDateTimeIn[L1 - 1].GetDate() - sc.BaseDateTimeIn[L1 - 2].GetDate() >= 6)
            IsHTF = true;
    }

    // Intraday charts (e.g. 240 Min #3): the study does nothing there. Intraday has its own
    // Bar Replay, and applying daily H/L/C to an intraday bar would give wrong values.
    if (BarPeriod.ChartDataType == INTRADAY_DATA)
    {
        if (CutEnd != 0)
            ReleaseCut(sc);
        return;
    }

    if (IsHTF)
    {
        // Revert: a higher-period chart starts every call from clean data
        // (the override is written fresh below, as on D1).
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

        // --- Date cut: the higher-period chart ends on the sender's day ---
        // The last bar is then a partial period (e.g. the week up to the sender's day).
        if (HtfSrc <= 0 || HtfSrc == sc.ChartNumber || !In_Cut.GetYesNo())
        {
            ReleaseCut(sc);     // source off or cut off: full data
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
                // A range setting may be in force while the data is full (after "Reload and Recalculate"):
                // we also check that the last bar is not later than the cut day (as on D1).
                bool InEffect = (sc.LoadChartDataByDateRange == 1 && sc.ChartDataEndDate == WantedEnd
                    && (sc.ArraySize == 0 || sc.BaseDateTimeIn[sc.ArraySize - 1].GetDate() <= WantedEnd));

                if (CutEnd != WantedEnd)
                    RequestCut(sc, WantedEnd);
                else if (!InEffect && HtfReasserts < 3)
                {
                    // A chart reload may have restored the full data: impose the range again
                    HtfReasserts++;
                    RequestCut(sc, WantedEnd);
                }
                else if (InEffect)
                    HtfReasserts = 0;
            }
        }

        // --- Sender data and aggregate of the current period's days (for the E8 write) ---
        // Aggregate: High = max High, Low = min Low of the period's days up to the sender's day, Close = the
        // sender's current price. The period start is computed from NOON of the trading day, because the sender's
        // bar for a trading day starts the evening before (e.g. 1.10 starts 30.09 18:00,
        // which for a monthly period would point to the previous month).
        // The period's days BEFORE the sender's day are taken from the Daily chart (In_DailyChart: file data, same
        // contract series as this chart) and only the replay day from the sender. Without a Daily chart nothing is
        // applied and a warning is shown (the aggregate of the whole period from the sender alone was off by 50-60 pts
        // on [C] charts in periods with a roll, so that mode no longer exists).
        bool  HaveAgg = false;
        int   SrcDay = 0, Days = 0;
        float AggH = -1.0e10f, AggL = 1.0e10f, AggC = 0.0f;
        SCDateTime PeriodStart;

        // The chart list has no "none" entry: selecting the chart's OWN chart or the MASTER chart is treated
        // as an unset Daily chart (DailySrc = 0 -> text "Ustaw Daily chart").
        int   DailySrc = In_DailyChart.GetChartNumber();
        if (DailySrc == sc.ChartNumber || DailySrc == HtfSrc)
            DailySrc = 0;
        bool  DailyOk = false;              // daily data from the Daily chart used
        bool  DailyShort = false;           // the Daily chart has data but starts after the period start
        bool  DayOpenMismatch = false;      // the sender's Open of the day != that day's Open on the Daily chart
        int   DailyDays = 0, DailyFirstDate = 0;
        float DailyOpenFirst = 0.0f, DailyOpenDay = 0.0f;
        float SrcO = 0.0f, SrcH = 0.0f, SrcL = 0.0f;
        int   SrcPrevDay = 0, DailyPrevDate = 0;    // previous sender day / previous Daily chart bar (holiday detection)
        const float OpenTol = sc.TickSize * 0.5f;       // Open must be identical (half a tick = float rounding only)
        int   ReplayStatus = (HtfSrc > 0 && HtfSrc != sc.ChartNumber) ? (int)sc.GetReplayStatusFromChart(HtfSrc) : 0;   // 0 = stop, 1 = play, 2 = pause
        int   WarnCode = 0;                 // 0 = none, 1 = Open mismatch, 2 = no data from the Daily chart, 3 = Daily chart not set, 6 = Daily chart starts after the period start

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
                if (N >= 2)
                    SrcPrevDay = sc.GetTradingDayDateForChartNumber(HtfSrc, HtfTimes[N - 2]);

                if (DailySrc > 0)
                {
                    // The period's days from the Daily chart (must be a daily historical chart reaching the sender's day)
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
                            if (d > SrcDay)         // the Daily chart is not yet cut to the sender's day
                                continue;
                            if (d < PStart)
                            {
                                ReachedBefore = true;
                                break;
                            }
                            if (d == SrcDay)
                                DayIdx = i;         // replay day: H/L/C comes from the sender
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
                        if (FirstIdx >= 0 && !Covered)
                            DailyShort = true;
                        if (FirstIdx >= 0 && Covered)
                        {
                            DailyOk = true;
                            DailyOpenFirst = DData[SC_OPEN][FirstIdx];

                            // Contract series match: the replay day's Open from the sender vs that day's Open from the file
                            if (DayIdx >= 0)
                            {
                                DailyOpenDay = DData[SC_OPEN][DayIdx];
                                DayOpenMismatch = (fabs((double)SrcO - (double)DailyOpenDay) > (double)OpenTol);
                                if (DayIdx >= 1)
                                    DailyPrevDate = DTimes[DayIdx - 1].GetDate();
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
            WarnCode = 3;       // no Daily chart in the settings: the tool applies nothing
        else if (DailySrc > 0 && !DailyOk && ReplayStatus != 0)
            WarnCode = DailyShort ? 6 : 2;
        else if (DayOpenMismatch && ReplayStatus != 0)
        {
            // After a holiday the sender has a separate day while the file merges it with the next one: the previous days differ
            bool DayStructureMismatch = (SrcPrevDay != 0 && DailyPrevDate != 0 && SrcPrevDay != DailyPrevDate);
            WarnCode = DayStructureMismatch ? 5 : 1;
        }

        // --- E8: apply the aggregated H/L/C to the last bar (partial period) ---
        // Conditions: chart cut to the sender's day, replay playing or paused.
        // With the replay stopped the bar stays original (Revert done at the start of the block).
        if (HaveAgg && CutEnd != 0 && CutEnd == SrcDay && sc.ArraySize > 0)
        {
            int Status = ReplayStatus;

            int Last  = sc.ArraySize - 1;
            int Stamp = sc.BaseDateTimeIn[Last].GetDate();

            // The last bar must be the current period (bar stamp date in [period start, sender's day])
            if (Status != 0 && Stamp >= PeriodStart.GetDate() && Stamp <= SrcDay)
            {
                float H = sc.RoundToTickSize(AggH);
                float L = sc.RoundToTickSize(AggL);
                float C = sc.RoundToTickSize(AggC);
                float O = sc.BaseData[SC_OPEN][Last];

                // With a Daily chart: this chart's period Open must match the Open of the period's first day
                // from the Daily chart (otherwise it is a different contract series). The replay day's Open was checked above.
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
                else if (WarnCode == 0)
                    WarnCode = 1;
            }
        }

        // --- Log (only on a change of sender day, replay status or state): for checking the aggregate ---
        {
            int& LogDay    = sc.GetPersistentInt(51);
            int& LogStatus = sc.GetPersistentInt(52);
            int& LogState  = sc.GetPersistentInt(53);
            int State = (Applied == 1) ? 3 : WarnCode;      // 0 = not applying, 1/2 = warning, 3 = applied

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
                    " | agregat H=%.2f L=%.2f C=%.2f dni=%d | plik H=%.2f L=%.2f C=%.2f | poprzedni dzien: sender=%d Daily=%d",
                    (int)BarPeriod.HistoricalChartBarPeriodType, SrcDay, ReplayStatus,
                    DailyOk ? "Daily" : (DailySrc > 0 ? "Daily-BRAK" : "nie-ustawiono"), State,
                    DailyDays, DailyFirstDate, DailyOpenFirst, BarO,
                    SrcO, SrcH, SrcL, AggC, DailyOpenDay,
                    AggH, AggL, AggC, Days, FileH, FileL, FileC, SrcPrevDay, DailyPrevDate);
                sc.AddMessageToLog(Msg, 0);
            }
        }

        ShowWarning(sc, WarnCode);

        // --- State seen by indicators: recalculate when it changed (applied, changed, reverted) ---
        // Revert at the start of the call undoes the data without recalculating, so after Stop replay (Applied = 0)
        // indicators would stay on the old values. The "original" state = (0,0,0).
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

        return;     // higher-period charts go no further (the daily logic does not apply)
    }

    // --- 1. REVERT: always start from clean data -----------------------
    // We restore the original only if the bar still holds the values applied by
    // the study. If not (data reloaded) - the current values are the new original.
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

    // --- CLEANUP: study removed / chart closed -------------------
    if (sc.LastCallToFunction)
    {
        ShowWarning(sc, false);
        ReleaseCut(sc);     // restore the user's date range settings
        return;             // Revert done above
    }

    // --- Size of the FULL data (without the cut) ---------------------------------
    // While waiting for a reload after releasing the cut, do not overwrite with a smaller value.
    if (CutEnd == 0 && (!NeedResolve || sc.ArraySize >= FullSize))
        FullSize = sc.ArraySize;

    // --- Cut option switched off: release the cut ---------------------------
    if (!In_Cut.GetYesNo() && CutEnd != 0)
        ReleaseCut(sc);

    // --- A chart reload (e.g. Reload and Recalculate) may ignore the imposed range ---
    // Log of 2026-10-25 11:10:10: ArraySize 1281 -> 1286 with CutEnd != 0, i.e. the chart went back to
    // the full data while the study still believed it was cut. We then impose the range again.
    // The retry limit protects against an endless reload loop if Sierra does not accept the range.
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

    // --- LINK TO THE SOURCE CHART: the receiver's day follows the sender's day ---
    // When a source chart is given, 'Bar od konca' is set by the study (Sierra has no
    // read-only inputs, so a manual change is overwritten). The usual path continues:
    // a change of 'Bar od konca' -> load the bar + cut the chart to its day.
    // H/L/C values with a source chart: see E3 in the APPLY section.
    // Sierra's chart list has no "none" entry: selecting the chart's OWN chart = off.
    bool ForceNewBar = false;   // 1 = load the bar again even if 'Bar od konca' did not change
    int WarnCode = 0;           // red text: 1 = Open / values mismatch, 4 = no bar for the master's day
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

            // The master's day is not in the data. First rule out that it simply has not been loaded yet
            // (cut data or a range remembered from the previous session). If the day really is not in
            // the file (holiday, missing data), we step back to the last bar BEFORE that day: the chart is
            // cut to the previous day (no lookahead), nothing is applied, with a red text.
            int& MissingDay = sc.GetPersistentInt(54);      // master's day confirmed as missing in the full data
            int  LastDate   = sc.BaseDateTimeIn[sc.ArraySize - 1].GetDate();
            bool NeedFullData = (WantedIdx < 0 && CutEnd != 0 && FollowDay > LastDate && MissingDay != FollowDay);
            bool StaleRange   = (WantedIdx < 0 && CutEnd == 0 && sc.LoadChartDataByDateRange != 0 && sc.ChartDataEndDate != 0
                && FollowDay > LastDate && sc.GetPersistentInt(46) < 3);

            if (WantedIdx < 0 && !NeedFullData && !StaleRange)
            {
                for (int i = sc.ArraySize - 1; i >= 0; i--)
                    if (sc.BaseDateTimeIn[i].GetDate() < FollowDay) { WantedIdx = i; break; }

                if (WantedIdx >= 0)
                {
                    if (CutEnd == 0)
                        MissingDay = FollowDay;
                    if (sc.GetReplayStatusFromChart(FollowChart) != 0)
                    {
                        WarnCode = 4;       // text only during replay (without replay the master's day may just be newer than the file)

                        int& LogWarn = sc.GetPersistentInt(55);
                        int& LogDay  = sc.GetPersistentInt(56);
                        if (LogWarn != 4 || LogDay != FollowDay)
                        {
                            LogWarn = 4; LogDay = FollowDay;
                            SCString Msg;
                            Msg.Format("[D1] ostrzezenie=4 brak bara dla dnia mastera=%d w pliku, uzyty bar=%d (ostatni bar w danych=%d)",
                                FollowDay, (int)sc.BaseDateTimeIn[WantedIdx].GetDate(), LastDate);
                            sc.AddMessageToLog(Msg, 0);
                        }
                    }
                }
            }

            if (WantedIdx >= 0)
            {
                if (sc.BaseDateTimeIn[WantedIdx].GetDate() == FollowDay)
                {
                    sc.GetPersistentInt(46) = 0;    // counter of clearing a leftover range end date
                    MissingDay = 0;
                }
                int WantedBarsBack = FullSize - 1 - WantedIdx;
                if (In_BarsBack.GetInt() != WantedBarsBack)
                    In_BarsBack.SetInt(WantedBarsBack);
                else if (HasTarget && TargetDT.GetDate() != sc.BaseDateTimeIn[WantedIdx].GetDate())
                {
                    // 'Bar od konca' matches numerically, but the loaded bar is from a different day than the
                    // source's day (after a restart the saved 'Bar od konca' points to a day in the shortened data,
                    // and after the full data loads the same number gives the source's day).
                    ForceNewBar = true;
                }
            }
            else if (NeedFullData)
            {
                // The source's day is later than the cut range: return to the full data,
                // and the bar will be found in subsequent calls.
                ReleaseCut(sc);
            }
            else if (StaleRange)
            {
                // The chart was loaded with a saved date range with an end date and the study did not impose it
                // (CutEnd == 0): a leftover from the previous session, so the data ends before the master's
                // day. We clear the end date (empty = up to the last available date of the file).
                // The retry limit protects against a reload loop.
                sc.GetPersistentInt(46)++;
                sc.ChartDataEndDate = 0;
            }
        }
    }

    // --- 'BAR OD KONCA' CHANGE: release the old bar and load the new one (reset to the original) ---
    // Revert above has already restored the old bar. We clear the target, and Load below writes the
    // new bar's original H/L/C to the inputs. With a source chart Apply applies them again.
    int& LastBarsBack = sc.GetPersistentInt(30);
    bool BarsBackChanged = (In_BarsBack.GetInt() != LastBarsBack) || ForceNewBar;
    LastBarsBack = In_BarsBack.GetInt();

    // First call after adding the study: load the bar indicated by 'Bar od konca'
    // (default 0). After Restore we do not load again - only an input change or Load does that.
    int& Initialized = sc.GetPersistentInt(32);
    bool AutoInit = (Initialized == 0 && sc.ArraySize > 0);
    if (AutoInit)
        Initialized = 1;

    // A change of 'Bar od konca' = load a new bar (H/L/C to the inputs + cut the chart),
    // regardless of whether anything was loaded before.
    bool ReloadNewBar = false;
    if (BarsBackChanged && !In_Restore.GetYesNo())
    {
        ReloadNewBar = true;
        if (HasTarget)
        {
            HasTarget = 0;      // Revert above restored the old bar
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
        ReleaseCut(sc);     // the chart returns to the full data
        In_Restore.SetYesNo(0);
        return;
    }

    // --- 3. LOAD / auto-capture -----------------------------------------------
    bool DoLoad = In_Load.GetYesNo() != 0;

    if (FollowChart > 0 && !HasTarget)
        DoLoad = true;      // Master chart without a prior Load: pick the bar by itself

    if (ReloadNewBar)
        DoLoad = true;      // after a change of 'Bar od konca' load the new bar

    if (AutoInit)
        DoLoad = true;      // study start: load the bar indicated by 'Bar od konca'

    // After releasing the cut we waited for full data: now load the bar
    if (NeedResolve && CutEnd == 0 && sc.ArraySize >= FullSize)
        DoLoad = true;

    bool CutJustRequested = false;
    if (DoLoad)
    {
        int Idx = FullSize - 1 - In_BarsBack.GetInt();      // numbering relative to the full data

        if (Idx >= sc.ArraySize && CutEnd != 0)
        {
            // The bar is beyond the currently loaded (cut) data: return to the full data
            // and load the bar after the reload (NeedResolve).
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

        // [RO] fields: the bar is clean (after Revert), so this is the true original.
        // We shift slot 1 -> 2 only when loading a different bar than the one in slot 1.
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

        // --- CUT THE DATA TO THE BAR'S DAY (no lookahead) ---
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
    // When we have just requested the cut, the chart is about to reload, so Apply
    // runs in the next call on fresh data.
    if (FollowChart > 0 && HasTarget && !CutJustRequested)
    {
        int Idx = FindTargetIndex(sc, TargetIdx, TargetDT);
        if (Idx >= 0)       // the target bar vanished (data changed): nothing is applied
        {
            TargetIdx = Idx;

            float H = 0.0f, L = 0.0f, C = 0.0f;
            float SrcOpen = 0.0f;       // the sender's Open of the day (only with a source chart)
            int   SrcPrevDay = 0;       // trading day of the sender's previous bar (for holiday detection)
            float SrcH = 0.0f, SrcL = 0.0f, SrcC = 0.0f;    // for the log on warning only
            int   SrcDayD1 = 0;         // trading day of the sender's bar (for the log only)
            bool ValuesReady = true;

            if (FollowChart > 0)
            {
                // E3: H/L/C of the source chart's (sender's) last bar, only while the replay
                // plays or is paused. When stopped: the bar stays original (settlement).
                ValuesReady = false;

                int Status = (int)sc.GetReplayStatusFromChart(FollowChart);     // 0 = stop, 1 = play, 2 = pause

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

                        // Apply only when the target bar is from the same day as the sender's bar
                        // (on a day jump the receiver is still moving to the new bar)
                        if (TargetDT.GetDate() == SrcDay)
                        {
                            H = sc.RoundToTickSize(SrcData[SC_HIGH][SrcLast]);
                            L = sc.RoundToTickSize(SrcData[SC_LOW][SrcLast]);
                            C = sc.RoundToTickSize(SrcData[SC_LAST][SrcLast]);
                            SrcOpen = SrcData[SC_OPEN][SrcLast];
                            SrcH = H; SrcL = L; SrcC = C; SrcDayD1 = SrcDay;
                            if (SrcN >= 2)
                                SrcPrevDay = sc.GetTradingDayDateForChartNumber(FollowChart, SrcTimes[SrcN - 2]);
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

            // Validation: High >= Open, Close, Low  |  Low <= Open, Close
            bool Valid = (H >= O && H >= C && H >= L && L <= O && L <= C);

            // Contract series match (with a source only): the sender's Open of the day must be identical to the
            // receiver bar's Open (e.g. a [C] chart before the roll has the Open of an older contract). Checking
            // High/Low against Open alone is not enough: a wide day range can contain a shifted Open.
            bool OpenMismatch = (FollowChart > 0 && ValuesReady && !SamePrice(sc, SrcOpen, O));

            if (ValuesReady && (!Valid || OpenMismatch))
            {
                // Open mismatch after a holiday: the sender has a separate day (e.g. Labour Day) while the file merges it with the next one.
                // Then the sender's previous day differs from the receiver's previous bar: a separate message.
                bool DayStructureMismatch = (OpenMismatch && SrcPrevDay != 0 && Idx >= 1
                    && SrcPrevDay != sc.BaseDateTimeIn[Idx - 1].GetDate());
                WarnCode = DayStructureMismatch ? 5 : 1;

                // Log on a change of warning or day (not every tick): for diagnosing a mismatch
                int& LogWarn = sc.GetPersistentInt(55);
                int& LogDay  = sc.GetPersistentInt(56);
                if (LogWarn != WarnCode || LogDay != SrcDayD1)
                {
                    LogWarn = WarnCode; LogDay = SrcDayD1;
                    SCString Msg;
                    Msg.Format("[D1] ostrzezenie=%d dzien sendera=%d bar receivera=%d (poprzedni bar=%d, poprzedni dzien sendera=%d)"
                        " | sender O=%.2f H=%.2f L=%.2f C=%.2f | bar receivera O=%.2f | wartosci poprawne=%d Open zgodny=%d",
                        WarnCode, SrcDayD1, TargetDT.GetDate(), (Idx >= 1) ? (int)sc.BaseDateTimeIn[Idx - 1].GetDate() : 0, SrcPrevDay,
                        SrcOpen, SrcH, SrcL, SrcC, O, Valid ? 1 : 0, OpenMismatch ? 0 : 1);
                    sc.AddMessageToLog(Msg, 0);
                }
            }

            // No values to apply (replay stopped / different day) or values inconsistent
            // with Open: the bar stays original.
            if (ValuesReady && Valid && !OpenMismatch)
            {
                // The bar is clean (after Revert) - remember the original and write the new values
                OrigH = sc.BaseData[SC_HIGH][Idx];
                OrigL = sc.BaseData[SC_LOW][Idx];
                OrigC = sc.BaseData[SC_LAST][Idx];

                WriteBar(sc, Idx, H, L, C);

                AppH = H;
                AppL = L;
                AppC = C;
                Applied = 1;

                // UpdateAlways = 1 calls the study continuously, so we react only to a change of values
                float& LastH = sc.GetPersistentFloat(23);
                float& LastL = sc.GetPersistentFloat(24);
                float& LastC = sc.GetPersistentFloat(25);
                bool Changed = (H != LastH || L != LastL || C != LastC);
                LastH = H; LastL = L; LastC = C;

                // When fed from the sender, other studies on the receiver do not recalculate by themselves
                // (UpdateAlways call), so after a value change we force a chart recalculation.
                if (FollowChart > 0 && Changed)
                {
                    sc.FlagFullRecalculate = 1;

                    // The H/L/C inputs show the currently applied values (in this mode they are not a data source)
                    In_High.SetFloat(H);
                    In_Low.SetFloat(L);
                    In_Close.SetFloat(C);
                }
            }
        }
    }

    ShowWarning(sc, WarnCode);
}
