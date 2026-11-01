#include "sierrachart.h"

// DLL name definition - key for remote compilation
SCDLLName("Wzrostowe 0.25")

/*==========================================================================*/
SCSFExport scsf_Wzrostowe025(SCStudyInterfaceRef sc)
{
	// Reference to the subgraph - pattern taken from scsf_InsideBar
	SCSubgraphRef Subgraph_Highlight = sc.Subgraph[0];

	// STAGE 1: Default configuration (Stealth mode V5 - Fix)
	if (sc.SetDefaults)
	{
		sc.GraphName = "Wzrostowe 0.25";
		sc.GraphRegion = 0;
		sc.AutoLoop = 1;

		// --- UI OPTIMISATION FLAGS (STEALTH V7 - FINAL FIX) ---
		sc.UpdateAlways = 0; // CPU Guard: update only on a new tick

		// FIX: pass the required ChartNumber and StudyID parameters
		int32_t StudyID = sc.StudyGraphInstanceID;
		int32_t DisableValue = 0;
		sc.SetStudyAlwaysShowNameValueLabelsWhenEnabled(sc.ChartNumber, StudyID, DisableValue);

		sc.DisplayStudyName = 0; // Hides the study name on the chart
		sc.DisplayStudyInputValues = 0; // Hides the input values
		sc.GlobalDisplayStudySubgraphsNameAndValue = 0; // Hides subgraph values in the header

		// --- CONTROL BAR BUTTON ---
		sc.Input[0].Name = "Numer Przycisku";
		sc.Input[0].SetInt(64);

		// --- SUBGRAPH CONFIGURATION ---
		Subgraph_Highlight.Name = "Wzrostowe Highlight";
		Subgraph_Highlight.DrawStyle = DRAWSTYLE_COLOR_BAR;
		Subgraph_Highlight.PrimaryColor = RGB(0, 192, 0);
		Subgraph_Highlight.DrawZeros = false;

		// LABEL FIX: turn off names and values at the subgraph level
		Subgraph_Highlight.LineLabel = 0; // Clears "Name Label" and "Value Label"

		Subgraph_Highlight.IncludeInStudySummary = 0;
		Subgraph_Highlight.IncludeInSpreadsheet = 0;
		Subgraph_Highlight.DisplayNameValueInDataLine = 0;
		Subgraph_Highlight.DisplayNameValueInWindowsFlags = 0;

		return;
	}

	// Hidden study: only the event bar (click / F6) and the last bar (latch) must run
	if (sc.HideStudy && sc.Index != sc.UpdateStartIndex && sc.Index != sc.ArraySize - 1)
		return;

	// --- CONTROL BAR BUTTON: MASTER STATE ---
	// Stays above the HideStudy return below: a hidden study must still see the click.
	int Btn = sc.Input[0].GetInt();
	int& MasterState = sc.GetPersistentInt(10);   // 1 visible, 0 hidden = single source of truth
	int& Initialized = sc.GetPersistentInt(11);
	int& LastHideStudy = sc.GetPersistentInt(12);

	if (Initialized == 0)
	{
		MasterState = !sc.HideStudy;
		LastHideStudy = sc.HideStudy;
		Initialized = 1;
	}

	// Change state once per pass, never per bar
	if (sc.Index == sc.UpdateStartIndex)
	{
		if (sc.MenuEventID == Btn)                 // button click (MenuEventID is NOT zeroed)
		{
			MasterState = !MasterState;
			if (MasterState == 1) sc.FlagFullRecalculate = 1;
		}
		else if (sc.HideStudy != LastHideStudy)    // Hide Study changed in the settings (F6)
		{
			MasterState = !sc.HideStudy;
			if (MasterState == 1) sc.FlagFullRecalculate = 1;
		}
	}

	// Latch and UI update, once per pass
	if (sc.Index == sc.ArraySize - 1)
	{
		sc.HideStudy = !MasterState;
		LastHideStudy = sc.HideStudy;

		sc.SetCustomStudyControlBarButtonEnable(Btn, MasterState);
		sc.SetCustomStudyControlBarButtonText(Btn, "UP25");
	}

	// If the study is hidden, stop executing the logic immediately
	if (sc.HideStudy)
		return;

	// STAGE 2 & 3: Surgical logic and rendering

	// Read the price data for the current index
	float High = sc.High[sc.Index];
	float Low = sc.Low[sc.Index];
	float Close = sc.Close[sc.Index];
	float Range = High - Low;

	// Guard against zero-range bars (Doji / data errors)
	if (Range > 0.0f)
	{
		// Trade Navigator formula ported to ACSIL. The 0.25f factor is hard-coded.
		// Like Trade Navigator, round the threshold to the tick size, then compare.
		if ((High - Close) < sc.RoundToTickSize(0.25f * Range))
		{
			// Turn the highlight on for the current bar
			Subgraph_Highlight[sc.Index] = 1.0f;
			Subgraph_Highlight.DataColor[sc.Index] = Subgraph_Highlight.PrimaryColor;
		}
		else
		{
			// Reset the value so DrawZeros = false can take effect
			Subgraph_Highlight[sc.Index] = 0.0f;
		}
	}
	else
	{
		Subgraph_Highlight[sc.Index] = 0.0f;
	}
}
