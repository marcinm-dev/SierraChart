#include "sierrachart.h"

SCDLLName("ChangeIntervaltoM20")

SCSFExport scsf_ChangeIntervalToM20(SCStudyInterfaceRef sc)
{
    const int ButtonID = 50; 

    if (sc.SetDefaults)
    {
        sc.GraphName = "Change Interval to M20";
        sc.AutoLoop = 0;      // Automatic per-bar calculation disabled 
        sc.GraphRegion = 0;   
        sc.HideStudy = 1;     

        sc.SetCustomStudyControlBarButtonText(ButtonID, "20M");

        return;
    }

    if (sc.MenuEventID == ButtonID)
    {
        // 1. UI: Refresh the Control Bar button immediately 
        sc.SetCustomStudyControlBarButtonEnable(ButtonID, 0);

        // 2. PERFORMANCE: Skip needless drawing of the old interval 
        sc.DoNotRedrawChartAfterStudyReturns = 1;

        // 3. POINTER: Get the handle of the chart window 
        HWND hWnd = static_cast<HWND>(sc.ChartWindowHandle);

        if (hWnd)
        {
            // PURE UI STREAM: Send the characters as one stream.
            // Using 0x0D (ASCII Enter) instead of VK_RETURN in WM_KEYDOWN
            // gives the Sierra Chart input buffer maximum consistency.
            ::PostMessage(hWnd, WM_CHAR, '2', 0);
            ::PostMessage(hWnd, WM_CHAR, '0', 0);
            ::PostMessage(hWnd, WM_CHAR, 'm', 0); 
            ::PostMessage(hWnd, WM_CHAR, 0x0D, 0); // 0x0D is Carriage Return (Enter)
        }

        return;
    }
}