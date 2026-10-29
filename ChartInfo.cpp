#include "sierrachart.h"

SCDLLName("ChartInfo")

// ChartInfo V3 (in development): one header line with date, time and the range (highest high - lowest low) of the visible bars.
// Development build with diagnostic logging (input "Log Level"):
//   0 = off
//   1 = events only: visible range / extremes changes, readiness changes, full recalculation, minute cache
//   2 = level 1 + a statistics line every 10 seconds (call counts per gate)
//   3 = level 2 + one line per draw (floods the log: use on a single chart for a short time)
// Development logging is still in the code on purpose. The old ChartInfo.dll is replaced by this build (same export name scsf_ChartInfo), so keep only one DLL exporting it.

// Writes one line to the Message Log with the chart number in front.
static void PLog(SCStudyInterfaceRef sc, const SCString& Text)
{
	SCString Msg;
	Msg.Format("ChartInfo[#%d] %s", sc.ChartNumber, Text.GetChars());
	sc.AddMessageToLog(Msg, 0);
}

SCSFExport scsf_ChartInfo(SCStudyInterfaceRef sc)
{
	SCInputRef InHoriz = sc.Input[0];
	SCInputRef InVert = sc.Input[1];
	SCInputRef InColor = sc.Input[2];
	SCInputRef InFontSize = sc.Input[3];
	SCInputRef InBold = sc.Input[4];
	SCInputRef InShowRange = sc.Input[5];
	// Input 6 was "Range Line Offset Below" (second line, removed in V3). The index stays reserved
	// and unnamed, because saved chartbooks depend on the input indexes.
	SCInputRef InLogLevel = sc.Input[7];
	SCInputRef InPaintBars = sc.Input[8];
	SCInputRef InBarsThreshold = sc.Input[9];
	SCInputRef InBarsColor = sc.Input[10];

	if (sc.SetDefaults)
	{
		sc.GraphName = "ChartInfo";
		sc.AutoLoop = 0;
		sc.GraphRegion = 0;

		// Scrolling and zooming change the visible bars only when the study function is called.
		sc.UpdateAlways = 1;

		// Input 6 is left unnamed on purpose; without this flag Sierra would stop reading inputs at the gap.
		sc.ProcessStudyUnsetInputs = 1;

		InHoriz.Name = "Horizontal Position (1-100)"; InHoriz.SetInt(149);
		InVert.Name = "Vertical Position (1-100)"; InVert.SetInt(100);
		InColor.Name = "Text Color"; InColor.SetColor(RGB(0, 0, 0));
		InFontSize.Name = "Font Size"; InFontSize.SetInt(10);
		InBold.Name = "Use Bold Font"; InBold.SetYesNo(1);
		InShowRange.Name = "Show Range of Visible Bars"; InShowRange.SetYesNo(1);
		InLogLevel.Name = "Log Level (0=off 1=events 2=+stats 3=every draw)"; InLogLevel.SetInt(2);
		InLogLevel.SetIntLimits(0, 3);
		InPaintBars.Name = "Color Bar Count Above Threshold"; InPaintBars.SetYesNo(1);
		InBarsThreshold.Name = "Bar Count Threshold"; InBarsThreshold.SetInt(105);
		InBarsThreshold.SetIntLimits(1, 100000);
		InBarsColor.Name = "Bar Count Color"; InBarsColor.SetColor(RGB(255, 0, 0));

		sc.AddMessageToLog("ChartInfo V3-dev.2 loaded (UpdateAlways=1, diagnostic logging)", 0);
		return;
	}

	// --- PERSISTENT VARIABLES ---
	float& r_LastBarHigh = sc.GetPersistentFloat(2);
	float& r_LastBarLow = sc.GetPersistentFloat(3);
	float& r_RangeHigh = sc.GetPersistentFloat(4);
	float& r_RangeLow = sc.GetPersistentFloat(5);
	int& r_LastMin = sc.GetPersistentInt(1);
	int& r_LineNumber = sc.GetPersistentInt(2);
	int& r_LastFirst = sc.GetPersistentInt(3);
	int& r_LastLast = sc.GetPersistentInt(4);
	int& r_RangeValid = sc.GetPersistentInt(5);
	int& r_ReadyState = sc.GetPersistentInt(6);   // 0 unknown, 1 visible range ready, 2 not ready
	int& r_BarsDrawn = sc.GetPersistentInt(7);    // 1 while the separate bar count text exists on the chart
	int& r_StatCalls = sc.GetPersistentInt(10);
	int& r_StatThrottled = sc.GetPersistentInt(11);
	int& r_StatNoChange = sc.GetPersistentInt(12);
	int& r_StatDrawn = sc.GetPersistentInt(13);
	int& r_StatRangeCalcs = sc.GetPersistentInt(14);
	int& r_StatViewChanges = sc.GetPersistentInt(15);
	double& r_LastUpdate = sc.GetPersistentDouble(1);
	double& r_StatStart = sc.GetPersistentDouble(2);
	SCString& r_CachedTimePart = sc.GetPersistentSCString(1);

	if (r_LineNumber == 0) r_LineNumber = 2026;
	const int BarsLineNumber = r_LineNumber + 1;
	const int LogLevel = InLogLevel.GetInt();

	// --- CLEANUP ---
	if (sc.LastCallToFunction)
	{
		if (LogLevel >= 1)
			PLog(sc, "last call: study removed or chart closing, drawings deleted");
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, r_LineNumber);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, BarsLineNumber);
		return;
	}

	double CurrentTime = sc.GetCurrentDateTime().GetAsDouble();

	// --- STATISTICS (level 2): one line every 10 seconds, counters are reset after printing ---
	++r_StatCalls;
	if (r_StatStart == 0)
		r_StatStart = CurrentTime;

	if (LogLevel >= 2 && (CurrentTime - r_StatStart) >= (10.0 / 86400.0))
	{
		SCString L;
		L.Format("stats %.1fs: calls=%d throttled=%d noChange=%d drawn=%d rangeCalcs=%d viewChanges=%d",
			(CurrentTime - r_StatStart) * 86400.0,
			r_StatCalls, r_StatThrottled, r_StatNoChange, r_StatDrawn, r_StatRangeCalcs, r_StatViewChanges);
		PLog(sc, L);

		r_StatCalls = 0; r_StatThrottled = 0; r_StatNoChange = 0;
		r_StatDrawn = 0; r_StatRangeCalcs = 0; r_StatViewChanges = 0;
		r_StatStart = CurrentTime;
	}

	// --- 1. THROTTLING (200ms) ---
	if ((CurrentTime - r_LastUpdate) < (0.2 / 86400.0) && r_LastUpdate != 0 && !sc.IsFullRecalculation)
	{
		++r_StatThrottled;
		return;
	}

	int LastIdx = sc.ArraySize - 1;
	if (LastIdx < 0) return;

	// --- INPUT DATA ---
	SCDateTime dt = sc.LatestDateTimeForLastBar;
	int currentMin = dt.GetMinute();

	// Visible bars: sc.IndexOfFirstVisibleBar / sc.IndexOfLastVisibleBar index the base data arrays.
	// Both are 0 right after the chart loads and Last can point into the fill space after the last bar.
	const int RawFirst = sc.IndexOfFirstVisibleBar;
	const int RawLast = sc.IndexOfLastVisibleBar;
	int FirstVisible = RawFirst;
	int LastVisible = RawLast;
	if (LastVisible > LastIdx) LastVisible = LastIdx;
	if (FirstVisible < 0) FirstVisible = 0;
	const bool VisibleReady = (LastVisible > 0 && FirstVisible <= LastVisible);
	const bool LastBarVisible = (LastVisible >= LastIdx);

	const bool ShowRange = InShowRange.GetYesNo() != 0;

	if (sc.IsFullRecalculation && LogLevel >= 1)
	{
		SCString L;
		L.Format("full recalculation: arraySize=%d rawFirst=%d rawLast=%d showRange=%d",
			sc.ArraySize, RawFirst, RawLast, ShowRange ? 1 : 0);
		PLog(sc, L);
	}

	// Log when the visible range becomes available or unavailable (e.g. right after the chart loads).
	if (ShowRange)
	{
		const int NewReadyState = VisibleReady ? 1 : 2;
		if (NewReadyState != r_ReadyState)
		{
			if (LogLevel >= 1)
			{
				SCString L;
				L.Format("visible range %s: rawFirst=%d rawLast=%d arraySize=%d",
					VisibleReady ? "READY" : "NOT READY", RawFirst, RawLast, sc.ArraySize);
				PLog(sc, L);
			}
			r_ReadyState = NewReadyState;
		}
	}

	// --- 2. CHANGE DETECTION ---
	// The price is no longer shown, so a price tick alone never causes a redraw.
	bool minuteChanged = (currentMin != r_LastMin);
	bool viewChanged = ShowRange && VisibleReady && (FirstVisible != r_LastFirst || LastVisible != r_LastLast);
	bool lastBarChanged = ShowRange && (sc.High[LastIdx] != r_LastBarHigh || sc.Low[LastIdx] != r_LastBarLow);
	// The option was switched on again, or the range was never calculated yet.
	bool rangeMissing = ShowRange && VisibleReady && !r_RangeValid;
	// The option was switched off while the range was still shown in the header text.
	bool rangeRemoved = !ShowRange && r_RangeValid;

	if (!minuteChanged && !viewChanged && !lastBarChanged && !rangeMissing && !rangeRemoved
		&& !r_CachedTimePart.IsEmpty() && !sc.IsFullRecalculation)
	{
		++r_StatNoChange;
		return;
	}

	++r_StatDrawn;
	if (LogLevel >= 3)
	{
		SCString L;
		L.Format("draw: changed[minute=%d view=%d lastBar=%d missing=%d removed=%d full=%d]",
			minuteChanged ? 1 : 0, viewChanged ? 1 : 0, lastBarChanged ? 1 : 0,
			rangeMissing ? 1 : 0, rangeRemoved ? 1 : 0, sc.IsFullRecalculation ? 1 : 0);
		PLog(sc, L);
	}

	// --- 3. TIME CACHING ---
	if (minuteChanged || r_CachedTimePart.IsEmpty() || sc.IsFullRecalculation)
	{
		r_CachedTimePart.Format("%02d/%02d/%04d %02d:%02d",
			dt.GetMonth(), dt.GetDay(), dt.GetYear(),
			dt.GetHour(), currentMin);
		r_LastMin = currentMin;

		if (LogLevel >= 1)
		{
			SCString L;
			L.Format("time cache rebuilt: %s", r_CachedTimePart.GetChars());
			PLog(sc, L);
		}
	}

	r_LastUpdate = CurrentTime;

	// --- 4. RANGE OF THE VISIBLE BARS ---
	// The loop runs only when the view changed, when the last bar changed while it is visible,
	// or when the range is missing; never on a plain price tick.
	if (ShowRange && VisibleReady
		&& (viewChanged || (lastBarChanged && LastBarVisible) || rangeMissing || sc.IsFullRecalculation))
	{
		float HH = -FLT_MAX;
		float LL = FLT_MAX;
		int HHIndex = FirstVisible;
		int LLIndex = FirstVisible;
		for (int i = FirstVisible; i <= LastVisible; ++i)
		{
			if (sc.High[i] > HH) { HH = sc.High[i]; HHIndex = i; }
			if (sc.Low[i] < LL) { LL = sc.Low[i]; LLIndex = i; }
		}

		++r_StatRangeCalcs;
		if (viewChanged)
			++r_StatViewChanges;

		const bool ExtremesChanged = (HH != r_RangeHigh || LL != r_RangeLow);
		if (LogLevel >= 1 && (viewChanged || ExtremesChanged || rangeMissing || sc.IsFullRecalculation))
		{
			SCString L;
			L.Format("calc [view=%d lastBar=%d missing=%d full=%d] first %d->%d last %d->%d (raw %d/%d) bars=%d arraySize=%d lastBarVisible=%d | HH=%.2f@%d LL=%.2f@%d range=%.2f",
				viewChanged ? 1 : 0, lastBarChanged ? 1 : 0, rangeMissing ? 1 : 0, sc.IsFullRecalculation ? 1 : 0,
				r_LastFirst, FirstVisible, r_LastLast, LastVisible, RawFirst, RawLast,
				LastVisible - FirstVisible + 1, sc.ArraySize, LastBarVisible ? 1 : 0,
				HH, HHIndex, LL, LLIndex, HH - LL);
			PLog(sc, L);
		}

		r_RangeHigh = HH;
		r_RangeLow = LL;
		r_LastFirst = FirstVisible;
		r_LastLast = LastVisible;
		r_RangeValid = 1;
	}
	r_LastBarHigh = sc.High[LastIdx];
	r_LastBarLow = sc.Low[LastIdx];

	// --- 5. BUILDING THE TEXT ---
	// One visual line "MM/DD/YYYY HH:MM R=<points> (<bars>B)" made of two text drawings that meet at the
	// horizontal position: the main text is right aligned and ends there, the bar count is left aligned and
	// starts there, so the bar count can have its own color. Without a valid range only date and time are shown.
	const bool RangeShown = ShowRange && r_RangeValid;
	const int BarCount = r_LastLast - r_LastFirst + 1;
	SCString FinalText;
	SCString BarsText;
	if (RangeShown)
	{
		// Round the finished difference to the symbol's tick (the float subtraction can leave e.g. 267.49997).
		const float RangeValue = sc.RoundToTickSize(r_RangeHigh - r_RangeLow);
		FinalText.Format("%s R=%.2f", r_CachedTimePart.GetChars(), RangeValue);
		BarsText.Format(" (%dB)", BarCount);
	}
	else
	{
		FinalText.Format("%s", r_CachedTimePart.GetChars());
	}

	// --- 6. DRAWING (RELATIVE POSITIONING 1-100) ---
	s_UseTool Tool;
	Tool.Clear();
	Tool.DrawingType = DRAWING_TEXT;
	Tool.LineNumber = r_LineNumber;
	Tool.ChartNumber = sc.ChartNumber;
	Tool.AddMethod = UTAM_ADD_OR_ADJUST;

	Tool.BeginDateTime = (double)InHoriz.GetInt();
	Tool.BeginValue = (float)InVert.GetInt();
	Tool.UseRelativeVerticalValues = true;

	Tool.Color = InColor.GetColor();
	Tool.FontSize = InFontSize.GetInt();
	Tool.FontBold = InBold.GetYesNo();
	Tool.Text = FinalText;
	Tool.TextAlignment = DT_RIGHT;

	sc.UseTool(Tool);

	// --- 6b. BAR COUNT (separate drawing, red above the threshold) ---
	if (RangeShown)
	{
		const bool OverThreshold = InPaintBars.GetYesNo() != 0 && BarCount > InBarsThreshold.GetInt();

		s_UseTool BarsTool;
		BarsTool.Clear();
		BarsTool.DrawingType = DRAWING_TEXT;
		BarsTool.LineNumber = BarsLineNumber;
		BarsTool.ChartNumber = sc.ChartNumber;
		BarsTool.AddMethod = UTAM_ADD_OR_ADJUST;

		BarsTool.BeginDateTime = (double)InHoriz.GetInt();
		BarsTool.BeginValue = (float)InVert.GetInt();
		BarsTool.UseRelativeVerticalValues = true;

		BarsTool.Color = OverThreshold ? InBarsColor.GetColor() : InColor.GetColor();
		BarsTool.FontSize = InFontSize.GetInt();
		BarsTool.FontBold = InBold.GetYesNo();
		BarsTool.Text = BarsText;
		BarsTool.TextAlignment = DT_LEFT;

		sc.UseTool(BarsTool);
		r_BarsDrawn = 1;
	}
	else if (r_BarsDrawn)
	{
		// The range is off or not ready: remove the old bar count text once.
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, BarsLineNumber);
		r_BarsDrawn = 0;
	}

	// --- 7. RANGE SWITCHED OFF ---
	// The header was just redrawn without the range, so forget the old range once.
	if (!ShowRange && r_RangeValid)
	{
		r_RangeValid = 0;
		if (LogLevel >= 1)
			PLog(sc, "range removed from the header (option switched off)");
	}
}
