#include "sierrachart.h"

SCDLLName("ChartInfo")

// ChartInfo V4: one header line with date, time and the range (highest high - lowest low) of the visible bars.
// The development logging was removed in this build; the logged version and the list of the removed logs are
// described in PROJECT_CONTEXT_FOR_AI.md. Keep only one DLL exporting scsf_ChartInfo in the Data folder.

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

		// One line when the study is added or loaded, to confirm which build Sierra runs (see the ghost DLL lesson).
		sc.AddMessageToLog("ChartInfo V4 loaded", 0);
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
	int& r_BarsDrawn = sc.GetPersistentInt(7);    // 1 while the separate bar count text exists on the chart
	int& r_DrawnValid = sc.GetPersistentInt(9);   // 1 once the texts below hold what is on the chart
	int& r_DrawnHighlight = sc.GetPersistentInt(16);  // 1 if the bar count was drawn in the alert color
	double& r_LastUpdate = sc.GetPersistentDouble(1);
	SCString& r_CachedTimePart = sc.GetPersistentSCString(1);
	SCString& r_DrawnMain = sc.GetPersistentSCString(2);   // text of the main drawing as last sent to the chart
	SCString& r_DrawnBars = sc.GetPersistentSCString(3);   // text of the bar count drawing as last sent to the chart

	if (r_LineNumber == 0) r_LineNumber = 2026;
	const int BarsLineNumber = r_LineNumber + 1;

	// --- CLEANUP ---
	if (sc.LastCallToFunction)
	{
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, r_LineNumber);
		sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, BarsLineNumber);
		return;
	}

	double CurrentTime = sc.GetCurrentDateTime().GetAsDouble();

	// --- 1. THROTTLING (200ms) ---
	if ((CurrentTime - r_LastUpdate) < (0.2 / 86400.0) && r_LastUpdate != 0 && !sc.IsFullRecalculation)
		return;

	int LastIdx = sc.ArraySize - 1;
	if (LastIdx < 0) return;

	// --- INPUT DATA ---
	SCDateTime dt = sc.LatestDateTimeForLastBar;
	// Charts built from historical daily data (Daily, Weekly, Monthly, Quarterly) carry no time of day in the
	// last bar (it stays 00:00), so use the current time instead: the replay time while replaying, otherwise the
	// system time. sc.GetCurrentDateTime() is already in the chart's time zone (verified in the Message Log of
	// 2026-10-30: 10:13 UTC gave 06:13 on a 240 Min chart; an extra UTC -> chart conversion gave 02:13, so it
	// must not be applied).
	if (sc.ChartDataType == DAILY_DATA && dt.GetHour() == 0 && dt.GetMinute() == 0)
		dt = sc.GetCurrentDateTime();
	int currentMin = dt.GetMinute();

	// Visible bars: sc.IndexOfFirstVisibleBar / sc.IndexOfLastVisibleBar index the base data arrays.
	// Both are 0 right after the chart loads and Last can point into the fill space after the last bar.
	int FirstVisible = sc.IndexOfFirstVisibleBar;
	int LastVisible = sc.IndexOfLastVisibleBar;
	if (LastVisible > LastIdx) LastVisible = LastIdx;
	if (FirstVisible < 0) FirstVisible = 0;
	const bool VisibleReady = (LastVisible > 0 && FirstVisible <= LastVisible);
	const bool LastBarVisible = (LastVisible >= LastIdx);

	const bool ShowRange = InShowRange.GetYesNo() != 0;

	// --- 2. CHANGE DETECTION ---
	// The price is no longer shown, so a price tick alone never causes a redraw.
	bool minuteChanged = (currentMin != r_LastMin);
	bool viewChanged = ShowRange && VisibleReady && (FirstVisible != r_LastFirst || LastVisible != r_LastLast);
	// While a bar is forming its high only rises and its low only falls, so a change of the last bar matters
	// only when the last bar is visible, and then only if it goes beyond a cached extreme of the range.
	const float LastBarHigh = sc.High[LastIdx];
	const float LastBarLow = sc.Low[LastIdx];
	const bool lastBarChanged = (LastBarHigh != r_LastBarHigh || LastBarLow != r_LastBarLow);
	bool lastBarShrank = false;   // high fell or low rose (e.g. Bar Replay stepped back): needs a full loop
	bool lastBarExtends = false;  // high or low went beyond the cached extremes: O(1) update
	if (lastBarChanged && ShowRange && LastBarVisible && VisibleReady && r_RangeValid && !viewChanged)
	{
		if (LastBarHigh < r_LastBarHigh || LastBarLow > r_LastBarLow)
			lastBarShrank = true;
		else if (LastBarHigh > r_RangeHigh || LastBarLow < r_RangeLow)
			lastBarExtends = true;
	}
	r_LastBarHigh = LastBarHigh;
	r_LastBarLow = LastBarLow;
	// The option was switched on again, or the range was never calculated yet.
	bool rangeMissing = ShowRange && VisibleReady && !r_RangeValid;
	// The option was switched off while the range was still shown in the header text.
	bool rangeRemoved = !ShowRange && r_RangeValid;

	if (!minuteChanged && !viewChanged && !lastBarShrank && !lastBarExtends && !rangeMissing && !rangeRemoved
		&& !r_CachedTimePart.IsEmpty() && !sc.IsFullRecalculation)
		return;

	// --- 3. TIME CACHING ---
	if (minuteChanged || r_CachedTimePart.IsEmpty() || sc.IsFullRecalculation)
	{
		r_CachedTimePart.Format("%02d/%02d/%04d %02d:%02d",
			dt.GetMonth(), dt.GetDay(), dt.GetYear(),
			dt.GetHour(), currentMin);
		r_LastMin = currentMin;
	}

	r_LastUpdate = CurrentTime;

	// --- 4. RANGE OF THE VISIBLE BARS ---
	// The loop runs only when the view changed, when the last bar shrank, when the range is missing or on a
	// full recalculation. A last bar that goes beyond a cached extreme is handled in O(1) below; a plain price
	// tick does nothing at all.
	if (ShowRange && VisibleReady
		&& (viewChanged || lastBarShrank || rangeMissing || sc.IsFullRecalculation))
	{
		float HH = -FLT_MAX;
		float LL = FLT_MAX;
		for (int i = FirstVisible; i <= LastVisible; ++i)
		{
			if (sc.High[i] > HH) HH = sc.High[i];
			if (sc.Low[i] < LL) LL = sc.Low[i];
		}

		r_RangeHigh = HH;
		r_RangeLow = LL;
		r_LastFirst = FirstVisible;
		r_LastLast = LastVisible;
		r_RangeValid = 1;
	}
	else if (lastBarExtends)
	{
		// O(1): the last bar can only widen the range of the visible bars.
		if (LastBarHigh > r_RangeHigh) r_RangeHigh = LastBarHigh;
		if (LastBarLow < r_RangeLow) r_RangeLow = LastBarLow;
	}

	// --- 5. BUILDING THE TEXT ---
	// One visual line "MM/DD/YYYY HH:MM R=<points> (<bars>B)" made of two text drawings that meet at the
	// horizontal position: the main text is right aligned and ends there, the bar count is left aligned and
	// starts there, so the whole bracket "(<bars>B)" can have its own color. Without a valid range only date
	// and time are shown.
	const bool RangeShown = ShowRange && r_RangeValid;
	const int BarCount = r_LastLast - r_LastFirst + 1;
	// Colored when the bar count differs from the threshold (fewer or more bars).
	const bool Highlight = RangeShown && InPaintBars.GetYesNo() != 0 && BarCount != InBarsThreshold.GetInt();
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

	// --- 5b. TEXT CACHE ---
	// Skip the drawing calls when nothing visible changed since the last draw. A full recalculation always
	// redraws, because Sierra clears the drawings then and the inputs may have changed.
	const int HighlightFlag = Highlight ? 1 : 0;
	const bool SameAsDrawn = !sc.IsFullRecalculation && r_DrawnValid
		&& r_DrawnMain.Compare(FinalText.GetChars()) == 0
		&& r_DrawnBars.Compare(BarsText.GetChars()) == 0
		&& r_DrawnHighlight == HighlightFlag;

	if (!SameAsDrawn)
	{
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

		// --- 6b. BAR COUNT "(<bars>B)" (separate drawing, alert color when it differs from the threshold) ---
		if (RangeShown)
		{
			s_UseTool BarsTool;
			BarsTool.Clear();
			BarsTool.DrawingType = DRAWING_TEXT;
			BarsTool.LineNumber = BarsLineNumber;
			BarsTool.ChartNumber = sc.ChartNumber;
			BarsTool.AddMethod = UTAM_ADD_OR_ADJUST;

			BarsTool.BeginDateTime = (double)InHoriz.GetInt();
			BarsTool.BeginValue = (float)InVert.GetInt();
			BarsTool.UseRelativeVerticalValues = true;

			BarsTool.Color = Highlight ? InBarsColor.GetColor() : InColor.GetColor();
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

		// Remember what is on the chart now.
		r_DrawnMain.Format("%s", FinalText.GetChars());
		r_DrawnBars.Format("%s", BarsText.GetChars());
		r_DrawnHighlight = HighlightFlag;
		r_DrawnValid = 1;
	}

	// --- 7. RANGE SWITCHED OFF ---
	// The header was just redrawn without the range, so forget the old range once.
	if (!ShowRange && r_RangeValid)
		r_RangeValid = 0;
}
