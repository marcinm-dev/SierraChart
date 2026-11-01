#include "sierrachart.h"

SCDLLName("ChartInfo")

// ChartInfo V6: one header line with date, time and the range (highest high - lowest low) of the visible bars.
// A daily-data chart whose last session is before today (weekend, holiday, a date range, D1BarReplay) shows that
// session's date with 16:59 instead of the current time. Optionally (inputs 11-12) such a chart takes the clock of
// the D1BarReplay master chart while its Bar Replay runs. The ChartInfo on the master (a chart built from intraday
// data) keeps its clock in a persistent SCDateTime, which the receiver reads with GetPersistentSCDateTimeFromChartStudy.
// No subgraphs are used: any subgraph value of a study in the main price region enters the price scale (V5-dev.2
// flattened the price chart that way).
// V6 is a simplification of V5: no own throttle (Sierra calls the study at the chart update interval anyway, and the
// replay clock made a throttle unreliable), no O(1) range path (the loop over the visible bars is a few hundred
// iterations), no separate state for "Show Range" switched off (a changed input causes a full recalculation).
// The development logging was removed in V4; the logged version and the list of the removed logs are
// described in PROJECT_CONTEXT_FOR_AI.md. Keep only one DLL exporting scsf_ChartInfo in the Data folder.

// Time of day shown with the date of a closed or past session (the last minute of the ES session in the chart's time
// zone).
static const int SESSION_END_HOUR = 16;
static const int SESSION_END_MINUTE = 59;

// Line numbers of the two text drawings: the main text and the bar count.
static const int MAIN_LINE_NUMBER = 2026;
static const int BARS_LINE_NUMBER = 2027;

// Draws (or adjusts) one header text at the fixed relative screen position.
static void DrawHeaderText(SCStudyInterfaceRef sc, int LineNumber, const SCString& Text, uint32_t Color,
	int HorizPos, int VertPos, int FontSize, int Bold, int Alignment)
{
	s_UseTool Tool;
	Tool.Clear();
	Tool.DrawingType = DRAWING_TEXT;
	Tool.LineNumber = LineNumber;
	Tool.ChartNumber = sc.ChartNumber;
	Tool.AddMethod = UTAM_ADD_OR_ADJUST;

	Tool.BeginDateTime = (double)HorizPos;
	Tool.BeginValue = (float)VertPos;
	Tool.UseRelativeVerticalValues = true;

	Tool.Color = Color;
	Tool.FontSize = FontSize;
	Tool.FontBold = Bold;
	Tool.Text = Text;
	Tool.TextAlignment = Alignment;

	sc.UseTool(Tool);
}

SCSFExport scsf_ChartInfo(SCStudyInterfaceRef sc)
{
	SCInputRef InHoriz = sc.Input[0];
	SCInputRef InVert = sc.Input[1];
	SCInputRef InColor = sc.Input[2];
	SCInputRef InFontSize = sc.Input[3];
	SCInputRef InBold = sc.Input[4];
	SCInputRef InShowRange = sc.Input[5];
	// Inputs 6 and 7 were "Range Line Offset Below" (removed in V3) and "Log Level" (removed in V4). The indexes
	// stay reserved and unnamed, because saved chartbooks depend on the input indexes.
	SCInputRef InPaintBars = sc.Input[8];
	SCInputRef InBarsThreshold = sc.Input[9];
	SCInputRef InBarsColor = sc.Input[10];
	SCInputRef InSyncMaster = sc.Input[11];
	SCInputRef InMasterChart = sc.Input[12];

	if (sc.SetDefaults)
	{
		sc.GraphName = "ChartInfo";
		sc.AutoLoop = 0;
		sc.GraphRegion = 0;

		// Scrolling and zooming change the visible bars only when the study function is called.
		sc.UpdateAlways = 1;

		// Inputs 6 and 7 are left unnamed on purpose; without this flag Sierra would stop reading inputs at the gap.
		sc.ProcessStudyUnsetInputs = 1;

		InHoriz.Name = "Horizontal Position (1-100)"; InHoriz.SetInt(146);
		InVert.Name = "Vertical Position (1-100)"; InVert.SetInt(100);
		InColor.Name = "Text Color"; InColor.SetColor(RGB(0, 0, 0));
		InFontSize.Name = "Font Size"; InFontSize.SetInt(10);
		InBold.Name = "Use Bold Font"; InBold.SetYesNo(1);
		InShowRange.Name = "Show Range of Visible Bars"; InShowRange.SetYesNo(1);
		InPaintBars.Name = "Color Bar Count When Not Equal To Threshold"; InPaintBars.SetYesNo(1);
		InBarsThreshold.Name = "Bar Count Threshold"; InBarsThreshold.SetInt(105);
		InBarsThreshold.SetIntLimits(1, 100000);
		InBarsColor.Name = "Bar Count Color"; InBarsColor.SetColor(RGB(255, 0, 0));
		InSyncMaster.Name = "Take Time From Master Chart During Its Bar Replay (Daily and higher charts)"; InSyncMaster.SetYesNo(0);
		InMasterChart.Name = "Master Chart (chart with the Bar Replay, needs ChartInfo too)"; InMasterChart.SetChartNumber(0);

		// One line when the study is added or loaded, to confirm which build Sierra runs (see the ghost DLL lesson).
		sc.AddMessageToLog("ChartInfo V6 loaded", 0);
		return;
	}

	// --- CLEANUP ---
	if (sc.LastCallToFunction)
	{
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, MAIN_LINE_NUMBER);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, BARS_LINE_NUMBER);
		return;
	}

	// --- PERSISTENT VARIABLES ---
	float& r_LastBarHigh = sc.GetPersistentFloat(1);
	float& r_LastBarLow = sc.GetPersistentFloat(2);
	int& r_LastMin = sc.GetPersistentInt(1);   // key of the cached time text (the minute, or the date for a fixed date)
	int& r_LastFirst = sc.GetPersistentInt(2); // first and last visible bar of the last calculation
	int& r_LastLast = sc.GetPersistentInt(3);
	int& r_DrawnHighlight = sc.GetPersistentInt(4);  // 1 if the bar count was drawn in the alert color
	SCString& r_CachedTimePart = sc.GetPersistentSCString(1);
	SCString& r_DrawnMain = sc.GetPersistentSCString(2);   // text of the main drawing as last sent to the chart
	SCString& r_DrawnBars = sc.GetPersistentSCString(3);   // text of the bar count drawing as last sent to the chart
	SCDateTime& r_PublishedClock = sc.GetPersistentSCDateTime(1);  // clock of a chart built from intraday data, read by receivers

	const int LastIdx = sc.ArraySize - 1;
	if (LastIdx < 0) return;

	// --- INPUT DATA ---
	SCDateTime dt = sc.LatestDateTimeForLastBar;
	// Charts built from historical daily data (Daily, Weekly, Monthly, Quarterly) carry no time of day: there
	// sc.LatestDateTimeForLastBar holds the date of the last session at 00:00 (the last Friday on a weekend, the end
	// of a date range the chart is cut to; verified in the Message Log of 2026-11-01). When that date is before today,
	// the market is not trading on this chart's last day (weekend, holiday, "Date Range To" in the past, the cut
	// D1BarReplay makes during a Bar Replay) and the header shows that date with 16:59, the end of the session. Otherwise it shows
	// sc.GetCurrentDateTime(), which is already in the chart's time zone (verified 2026-10-30: 10:13 UTC gave 06:13
	// on a 240 Min chart; an extra UTC -> chart conversion gave 02:13, so it must not be applied).
	// While the Bar Replay of the master chart (the sender of D1BarReplay) plays or is paused and the sync input is
	// on, the clock of the master is used instead: this chart is not replaying itself, so its own current time is the
	// system time. A receiver without a ChartInfo on the master, or without data from it, falls back to the rule above.
	bool FixedDate = false;
	if (sc.ChartDataType == DAILY_DATA && dt.GetHour() == 0 && dt.GetMinute() == 0)
	{
		bool Synced = false;
		const int MasterNo = InMasterChart.GetChartNumber();
		if (InSyncMaster.GetYesNo() != 0 && MasterNo > 0 && MasterNo != sc.ChartNumber
			&& sc.GetReplayStatusFromChart(MasterNo) != 0)
		{
			// The ChartInfo on the master keeps its clock in persistent SCDateTime 1 (0 until the master published it).
			const int MasterStudyID = sc.GetStudyIDByName(MasterNo, "ChartInfo", 0);
			if (MasterStudyID > 0)
			{
				const SCDateTime MasterClock = sc.GetPersistentSCDateTimeFromChartStudy(MasterNo, MasterStudyID, 1);
				if (MasterClock.GetDate() > 0)
				{
					dt = MasterClock;
					Synced = true;
				}
			}
		}

		if (!Synced)
		{
			const SCDateTime Now = sc.GetCurrentDateTime();
			if (dt.GetDate() >= Now.GetDate())
				dt = Now;
			else
			{
				FixedDate = true;
				dt.SetTimeHMS(SESSION_END_HOUR, SESSION_END_MINUTE, 0);
			}
		}
	}
	// Key of the cached date/time text: the minute, or the date for a fixed date (the two cannot collide, a date
	// number is far above 59), so a change of mode or of the date also rebuilds the text.
	const int TimeKey = FixedDate ? dt.GetDate() : dt.GetMinute();

	// Publish the clock for the receivers, only from charts built from intraday data (a possible master); a chart
	// built from daily data is a receiver and publishes nothing.
	if (sc.ChartDataType != DAILY_DATA)
		r_PublishedClock = dt;

	// Visible bars: sc.IndexOfFirstVisibleBar / sc.IndexOfLastVisibleBar index the base data arrays.
	// Both are 0 right after the chart loads and Last can point into the fill space after the last bar.
	int FirstVisible = sc.IndexOfFirstVisibleBar;
	int LastVisible = sc.IndexOfLastVisibleBar;
	if (LastVisible > LastIdx) LastVisible = LastIdx;
	if (FirstVisible < 0) FirstVisible = 0;
	const bool VisibleReady = (LastVisible > 0 && FirstVisible <= LastVisible);
	const bool LastBarVisible = (LastVisible >= LastIdx);

	// Switching this input off or on causes a full recalculation, which passes the gate below and redraws everything.
	const bool RangeShown = InShowRange.GetYesNo() != 0 && VisibleReady;

	// --- 1. CHANGE DETECTION ---
	// A plain price tick never causes a redraw: the last bar matters only when it is visible and its high or low
	// moved. The previous values are stored before the early return, otherwise the gate would stay open.
	const float LastBarHigh = sc.High[LastIdx];
	const float LastBarLow = sc.Low[LastIdx];
	const bool lastBarMoved = RangeShown && LastBarVisible && (LastBarHigh != r_LastBarHigh || LastBarLow != r_LastBarLow);
	r_LastBarHigh = LastBarHigh;
	r_LastBarLow = LastBarLow;
	const bool minuteChanged = (TimeKey != r_LastMin);
	const bool viewChanged = RangeShown && (FirstVisible != r_LastFirst || LastVisible != r_LastLast);

	if (!minuteChanged && !viewChanged && !lastBarMoved && !r_CachedTimePart.IsEmpty() && !sc.IsFullRecalculation)
		return;

	// --- 2. TIME TEXT ---
	if (minuteChanged || r_CachedTimePart.IsEmpty() || sc.IsFullRecalculation)
	{
		r_CachedTimePart.Format("%02d/%02d/%04d %02d:%02d",
			dt.GetMonth(), dt.GetDay(), dt.GetYear(),
			dt.GetHour(), dt.GetMinute());
		r_LastMin = TimeKey;
	}

	// --- 3. RANGE OF THE VISIBLE BARS ---
	// The gate above passes a few times per second at most, and the loop is a few hundred iterations.
	SCString FinalText;
	SCString BarsText;
	bool Highlight = false;
	if (RangeShown)
	{
		float HH = -FLT_MAX;
		float LL = FLT_MAX;
		for (int i = FirstVisible; i <= LastVisible; ++i)
		{
			if (sc.High[i] > HH) HH = sc.High[i];
			if (sc.Low[i] < LL) LL = sc.Low[i];
		}
		r_LastFirst = FirstVisible;
		r_LastLast = LastVisible;

		// Round the finished difference to the symbol's tick (the float subtraction can leave e.g. 267.49997).
		const float RangeValue = sc.RoundToTickSize(HH - LL);
		const int BarCount = LastVisible - FirstVisible + 1;
		// Colored when the bar count differs from the threshold (fewer or more bars).
		Highlight = InPaintBars.GetYesNo() != 0 && BarCount != InBarsThreshold.GetInt();

		// One visual line "MM/DD/YYYY HH:MM R=<points> (<bars>B)" made of two text drawings that meet at the
		// horizontal position: the main text is right aligned and ends there, the bar count is left aligned and
		// starts there, so the whole bracket "(<bars>B)" can have its own color.
		FinalText.Format("%s R=%.2f", r_CachedTimePart.GetChars(), RangeValue);
		BarsText.Format(" (%dB)", BarCount);
	}
	else
	{
		// Without a valid range only date and time are shown.
		FinalText.Format("%s", r_CachedTimePart.GetChars());
	}

	// --- 4. DRAWING (RELATIVE POSITIONING 1-100) ---
	// Skip the drawing calls when nothing visible changed since the last draw (UseTool searches the chart's list of
	// drawings). A full recalculation always redraws, because Sierra clears the drawings then and the inputs may
	// have changed.
	const int HighlightFlag = Highlight ? 1 : 0;
	const bool SameAsDrawn = !sc.IsFullRecalculation && !r_DrawnMain.IsEmpty()
		&& r_DrawnMain.Compare(FinalText.GetChars()) == 0
		&& r_DrawnBars.Compare(BarsText.GetChars()) == 0
		&& r_DrawnHighlight == HighlightFlag;
	if (SameAsDrawn)
		return;

	DrawHeaderText(sc, MAIN_LINE_NUMBER, FinalText, InColor.GetColor(),
		InHoriz.GetInt(), InVert.GetInt(), InFontSize.GetInt(), InBold.GetYesNo(), DT_RIGHT);

	if (RangeShown)
	{
		// Bar count "(<bars>B)", alert color when it differs from the threshold.
		DrawHeaderText(sc, BARS_LINE_NUMBER, BarsText, Highlight ? InBarsColor.GetColor() : InColor.GetColor(),
			InHoriz.GetInt(), InVert.GetInt(), InFontSize.GetInt(), InBold.GetYesNo(), DT_LEFT);
	}
	else
	{
		// The range is off or not ready: make sure no old bar count text stays on the chart.
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, BARS_LINE_NUMBER);
	}

	// Remember what is on the chart now.
	r_DrawnMain.Format("%s", FinalText.GetChars());
	r_DrawnBars.Format("%s", BarsText.GetChars());
	r_DrawnHighlight = HighlightFlag;
}
