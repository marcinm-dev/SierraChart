#include "sierrachart.h"

SCDLLName("ChangeIntervaltoM20")

SCSFExport scsf_ChangeIntervalToM20(SCStudyInterfaceRef sc)
{
    const int ButtonID = 50; 

    if (sc.SetDefaults)
    {
        sc.GraphName = "Change Interval to M20";
        sc.AutoLoop = 0;      // Wyłączone automatyczne przeliczanie słupków 
        sc.GraphRegion = 0;   
        sc.HideStudy = 1;     

        sc.SetCustomStudyControlBarButtonText(ButtonID, "20M");

        return;
    }

    if (sc.MenuEventID == ButtonID)
    {
        // 1. UI: Natychmiastowe odświeżenie przycisku na pasku 
        sc.SetCustomStudyControlBarButtonEnable(ButtonID, 0);

        // 2. PERFORMANCE: Blokujemy niepotrzebne rysowanie starego interwału 
        sc.DoNotRedrawChartAfterStudyReturns = 1;

        // 3. POINTER: Pobieramy uchwyt okna wykresu 
        HWND hWnd = static_cast<HWND>(sc.ChartWindowHandle);

        if (hWnd)
        {
            // PURE UI STREAM: Wysyłamy znaki w jednym strumieniu.
            // Użycie 0x0D (ASCII Enter) zamiast VK_RETURN w WM_KEYDOWN
            // zapewnia maksymalną spójność dla bufora wejściowego Sierry.
            ::PostMessage(hWnd, WM_CHAR, '2', 0);
            ::PostMessage(hWnd, WM_CHAR, '0', 0);
            ::PostMessage(hWnd, WM_CHAR, 'm', 0); 
            ::PostMessage(hWnd, WM_CHAR, 0x0D, 0); // 0x0D to Carriage Return (Enter)
        }

        return;
    }
}