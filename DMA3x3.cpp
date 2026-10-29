#include "sierrachart.h"

SCDLLName("DiNapoli_DMA_3x3")

SCSFExport scsf_DiNapoliDMA3x3(SCStudyInterfaceRef sc)
{
    SCSubgraphRef DMA = sc.Subgraph[0];

    if (sc.SetDefaults)
    {
        sc.GraphName = "DiNapoli DMA 3x3";
        sc.AutoLoop = 1;
        sc.UpdateAlways = 0;
        sc.GraphRegion = 0;

        sc.DisplayStudyName = 0;
        sc.DisplayStudyInputValues = 0;
        sc.GlobalDisplayStudySubgraphsNameAndValue = 1;

        sc.Input[0].Name = "Numer Przycisku";
        sc.Input[0].SetInt(5);

        sc.Input[1].Name = "Okres SMA";
        sc.Input[1].SetInt(3);
        sc.Input[1].SetIntLimits(1, 1000);

        DMA.Name = "DiNapoli DMA 3x3";
        DMA.DrawStyle = DRAWSTYLE_LINE;
        DMA.PrimaryColor = RGB(0, 0, 128);
        DMA.LineWidth = 1;
        DMA.GraphicalDisplacement = 3; // shift forward by 3 bars (can be changed in Subgraph Settings)
        DMA.DisplayNameValueInDataLine = 0;
        DMA.DisplayNameValueInWindowsFlags = 1;
        DMA.IncludeInSpreadsheet = 0; // Spreadsheet Guard
        DMA.IncludeInStudySummary = 0;
        DMA.LineLabel = LL_DISPLAY_VALUE | LL_VALUE_ALIGN_VALUES_SCALE | LL_VALUE_ALIGN_CENTER;

        return;
    }

    // --- MASTER STATE ARCHITECTURE ---
    int& MasterState = sc.GetPersistentInt(10);
    int& Initialized = sc.GetPersistentInt(11);
    int& LastHideStudy = sc.GetPersistentInt(12);
    int& UIState = sc.GetPersistentInt(13); // 0 = button not set yet, 1 = hidden, 2 = visible

    if (Initialized == 0) {
        MasterState = !sc.HideStudy;
        LastHideStudy = sc.HideStudy;
        Initialized = 1;
    }

    // --- HIDDEN MODE: do nothing except on the event bar (UpdateStartIndex) and the last bar ---
    if (MasterState == 0 && sc.Index != sc.UpdateStartIndex && sc.Index != sc.ArraySize - 1)
        return;

    int Btn = sc.Input[0].GetInt();
    int Length = sc.Input[1].GetInt();

    if (sc.Index == sc.UpdateStartIndex)
    {
        // 1. Change from the button (click)
        if (sc.MenuEventID == Btn)
        {
            MasterState = !MasterState;

            // sc.MenuEventID is intentionally NOT reset - other studies
            // may check their own buttons.

            if (MasterState == 1) sc.FlagFullRecalculate = 1;
        }
        // 2. Change of the system flag (Hide Study in the study settings)
        else if (sc.HideStudy != LastHideStudy)
        {
            MasterState = !sc.HideStudy;
            if (MasterState == 1) sc.FlagFullRecalculate = 1;
        }
    }

    // --- FINAL LATCH AND UI UPDATE (once per cycle) ---
    if (sc.Index == sc.ArraySize - 1)
    {
        sc.HideStudy = !MasterState;
        LastHideStudy = sc.HideStudy;

        // Button calls only when the state changed or after a full recalculation
        // (chart load, timeframe change). The HideStudy/DrawStyle latch stays on
        // every tick - these are cheap assignments that protect against the
        // background refresh from the study settings.
        if (UIState != MasterState + 1 || sc.IsFullRecalculation)
        {
            sc.SetCustomStudyControlBarButtonEnable(Btn, MasterState);
            sc.SetCustomStudyControlBarButtonText(Btn, "DMA 3x3");
            UIState = MasterState + 1;
        }

        DMA.DrawStyle = (MasterState == 1) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
    }

    // --- MATH: SMA(Close, Length) rounded to tick size, the shift is done by Sierra (GraphicalDisplacement) ---
    // SMA is not recursive, so while hidden the whole calculation is skipped (including the last bar);
    // FlagFullRecalculate rebuilds the history when the study is shown again.
    if (MasterState == 1)
    {
        if (sc.Index >= Length - 1)
        {
            sc.SimpleMovAvg(sc.Close, DMA, Length);

            // Round the finished result to the tick - sc.RoundToTickSize uses
            // the current symbol's sc.TickSize, so it adapts to every instrument.
            DMA[sc.Index] = sc.RoundToTickSize(DMA[sc.Index]);
        }
    }
}
