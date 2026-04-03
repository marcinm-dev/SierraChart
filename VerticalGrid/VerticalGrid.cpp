#include "sierrachart.h"

SCDLLName("HorizontalVerticalGrid")

// Pancerna metoda tygodnia (zamrożona)
int GetWeekManual(int dateValue) { return dateValue / 7; }

SCSFExport scsf_HorizontalVerticalGrid(SCStudyInterfaceRef sc)
{
    // --- WEJŚCIA (ZAMROŻONE) ---
    SCInputRef InVColor = sc.Input[0];
    SCInputRef InVDensity = sc.Input[1];

    if (sc.SetDefaults)
    {
        sc.GraphName = "HorizontalVerticalGrid (V17 Fix M5/M10)";
        sc.GraphRegion = 0;
        sc.AutoLoop = 0; 
        sc.UpdateAlways = 0; 

        InVColor.Name = "Vertical Grid Color";
        InVColor.SetColor(RGB(128, 128, 128));
        InVDensity.Name = "Vertical Spacing (Show every N-th)";
        InVDensity.SetInt(1);

        return;
    }

    // --- OPTYMALIZACJA I REAKCJA NA RUCH ---
    bool IsFullRecalc = (sc.Index == 0);
    bool IsNewBar = (sc.ArraySize != sc.GetPersistentInt(10));
    bool IsScroll = (sc.IndexOfFirstVisibleBar != sc.GetPersistentInt(11));
    
    if (!IsFullRecalc && !IsNewBar && !IsScroll) return;
    
    sc.SetPersistentInt(10, sc.ArraySize);
    sc.SetPersistentInt(11, sc.IndexOfFirstVisibleBar);

    // --- PROFESJONALNA DETEKCJA INTERWAŁU ---
    int targetPeriod = 0; 
    if (sc.IsIntradayChart()) 
    {
        // Jeśli H1 lub więcej (H1 = 3600s, H4 = 14400s)
        if (sc.SecondsPerBar >= 3600) targetPeriod = 1; // Grid Weekly
        else targetPeriod = 0; // Grid Daily (dla M1, M5, M10, M15 itd.)
    }
    else // Wykresy Daily, Weekly, Monthly, Yearly
    {
        if (sc.TimePeriodType == TIME_PERIOD_LENGTH_UNIT_DAYS) targetPeriod = 2; // Grid Monthly
        else if (sc.TimePeriodType == TIME_PERIOD_LENGTH_UNIT_YEARS) targetPeriod = 4; // Grid 10Y
        else targetPeriod = 3; // Grid Yearly (dla Weekly/Monthly)
    }

    // --- RENDERER (DYNAMIKA I CZYSZCZENIE) ---
    
    // CZYSZCZENIE POPRZEDNIEGO RENDERU (Zapobiega "duchom" przy scrollowaniu)
    for (int id = 60000; id < 60500; id++) {
        sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
    }

    int drawingID = 60000; 
    s_UseTool T;
    T.Clear();
    T.DrawingType = DRAWING_VERTICALLINE; 
    T.Color = InVColor.GetColor();
    T.LineStyle = LINESTYLE_DOT; 
    T.LineWidth = 1;
    T.AddMethod = UTAM_ADD_ALWAYS; // Używamy ADD_ALWAYS bo czyścimy ID ręcznie

    // Zakres: tylko widoczne bary
    int startIdx = sc.IndexOfFirstVisibleBar;
    if (startIdx < 1) startIdx = 1;
    int endIdx = sc.IndexOfLastVisibleBar;
    if (endIdx >= sc.ArraySize) endIdx = sc.ArraySize - 1;

    for (int i = startIdx; i <= endIdx; i++)
    {
        bool isNew = false;
        
        // Stabilne pobieranie dat dla przejść
        int tDateC = (int)sc.GetTradingDayDate(sc.BaseDateTimeIn[i]);
        int tDateP = (int)sc.GetTradingDayDate(sc.BaseDateTimeIn[i-1]);

        if (targetPeriod == 0) { // < H1 -> Daily
            isNew = (tDateC != tDateP);
        }
        else if (targetPeriod == 1) { // H1-H4 -> Weekly
            isNew = (GetWeekManual(tDateC) != GetWeekManual(tDateP));
        }
        else if (targetPeriod == 2) { // Daily -> Monthly
            int yC, mC, dC, yP, mP, dP;
            sc.BaseDateTimeIn[i].GetDateYMD(yC, mC, dC);
            sc.BaseDateTimeIn[i-1].GetDateYMD(yP, mP, dP);
            isNew = (mC != mP || yC != yP);
        }
        else if (targetPeriod == 3) { // Weekly/Monthly -> Yearly
            int yC, mC, dC, yP, mP, dP;
            sc.BaseDateTimeIn[i].GetDateYMD(yC, mC, dC);
            sc.BaseDateTimeIn[i-1].GetDateYMD(yP, mP, dP);
            isNew = (yC != yP);
        }
        else if (targetPeriod == 4) { // Yearly -> 10Y
            int yC, mC, dC, yP, mP, dP;
            sc.BaseDateTimeIn[i].GetDateYMD(yC, mC, dC);
            sc.BaseDateTimeIn[i-1].GetDateYMD(yP, mP, dP);
            isNew = (yC/10 != yP/10);
        }

        if (isNew && drawingID < 60500) {
            T.LineNumber = drawingID++;
            T.BeginIndex = i; 
            sc.UseTool(T);
        }
    }
}
