#include "sierrachart.h"
#include <windows.h>

SCDLLName("DiNapoliRetracement")

// Slot offsets - zamrożone mapowanie pamięci
#define S_FP 100   
#define S_FT 130   
#define S_RP 160   
#define S_RT 190   
#define S_FI 220   
#define S_RI 250   
#define S_TYPE 280 
#define S_STEP 310 
#define S_LCI 340 

SCSFExport scsf_DiNapoliRetracement(SCStudyInterfaceRef sc)
{
    if (sc.SetDefaults) {
        sc.GraphName = "DiNapoli Retracement v_POWER_CLEAN";
        sc.GraphRegion = 0; 
        sc.AutoLoop = 0; 
        sc.UpdateAlways = 1; // Płynność klawiszy niezależnie od ticków [cite: 2026-03-04]
        sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_WHEN_ACS_BUTTON_ENABLED;

        sc.Input[0].Name = "Numer Przycisku"; sc.Input[0].SetInt(1); 
        sc.Input[1].Name = "Kolor 0.382 (Red)"; sc.Input[1].SetColor(RGB(255, 0, 0)); 
        sc.Input[2].Name = "Kolor 0.618 (Pink)"; sc.Input[2].SetColor(RGB(255, 0, 255)); 
        sc.Input[3].Name = "Kolor 1.000 (Black)"; sc.Input[3].SetColor(RGB(0, 0, 0)); 
        sc.Input[4].Name = "Kolor Pierscieni (TN Blue)"; sc.Input[4].SetColor(RGB(0, 0, 255)); 
        sc.Input[5].Name = "Max Rysunkow"; sc.Input[5].SetInt(30);
        return;
    }

    int Btn = sc.Input[0].GetInt();
    sc.SetCustomStudyControlBarButtonText(Btn, "DiNapoli");

    int& TState = sc.GetPersistentInt(1); 
    int& NumSaved = sc.GetPersistentInt(2);   
    int& SessionSteps = sc.GetPersistentInt(3); 
    int& LastSize = sc.GetPersistentInt(4); 
    int& KeyLock = sc.GetPersistentInt(8); 

    // --- 1. LOGIKA KLAWIATURY (Ctrl + X / Space) ---
    if (sc.ChartWindowIsActive) {
        bool CtrlD = (GetAsyncKeyState(VK_CONTROL) & 0x8000);
        bool XD = (GetAsyncKeyState('X') & 0x8000);
        bool SpaceD = (GetAsyncKeyState(VK_SPACE) & 0x8000);

        if (!XD && !SpaceD) KeyLock = 0;

        if (KeyLock == 0) {
            if (CtrlD && XD) { // Czyścimy wszystko [cite: 2026-03-04]
                for(int id = 10000; id <= 10025; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
                for(int i = 0; i < NumSaved; i++) {
                    for(int j = 0; j < 20; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, 20000 + (i * 20) + j);
                }
                NumSaved = 0; TState = 0; SessionSteps = 0; LastSize = -1; KeyLock = 1;
                sc.SetCustomStudyControlBarButtonEnable(Btn, 0);
            }
            else if (SpaceD && TState > 0) {
                for(int id = 10000; id <= 10025; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
                TState = 0; sc.SetCustomStudyControlBarButtonEnable(Btn, 0); KeyLock = 1;
            }
        }
    }

    // --- 2. LOGIKA MYSZY ---
    if (sc.PointerEventType == SC_ACS_BUTTON_ON && sc.MenuEventID == Btn) { TState = 1; SessionSteps = 0; sc.MenuEventID = 0; }
    
    int cIdx = sc.ActiveToolIndex; float mY = (float)sc.ActiveToolYValue;

    if (sc.PointerEventType == SC_POINTER_BUTTON_DOWN && sc.MenuEventID == Btn) {
        float sP = (TState == 1) ? ((fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx])) ? sc.High[cIdx] : sc.Low[cIdx]) : ((mY < sc.GetPersistentFloat(10)) ? sc.Low[cIdx] : sc.High[cIdx]);
        
        if (TState == 1) { 
            sc.GetPersistentFloat(10) = sP; 
            sc.GetPersistentSCDateTime(10) = sc.BaseDateTimeIn[cIdx]; 
            sc.GetPersistentInt(11) = (sP == sc.High[cIdx] ? 1 : 2); 
            TState = 2; 
        }
        else if (TState == 2) {
            int s = NumSaved % sc.Input[5].GetInt();
            sc.SetPersistentFloat(S_FP+s, sc.GetPersistentFloat(10)); 
            sc.SetPersistentSCDateTime(S_FT+s, sc.GetPersistentSCDateTime(10));
            sc.SetPersistentFloat(S_RP+s, sP); 
            sc.SetPersistentSCDateTime(S_RT+s, sc.BaseDateTimeIn[cIdx]);
            sc.SetPersistentInt(S_TYPE+s, sc.GetPersistentInt(11)); 
            sc.SetPersistentInt(S_STEP+s, SessionSteps);
            sc.SetPersistentInt(S_FI+s, sc.GetNearestMatchForSCDateTime(sc.ChartNumber, sc.GetPersistentSCDateTime(10))); 
            sc.SetPersistentInt(S_RI+s, cIdx);
            sc.SetPersistentInt(S_LCI+s, sc.ArraySize - 1); 
            
            if (NumSaved < sc.Input[5].GetInt()) NumSaved++; 
            SessionSteps++; LastSize = -1;
        }
        sc.PointerEventType = 0;
    }

    // --- 3. INCREMENTAL TRAILING ---
    for (int i = 0; i < NumSaved; i++) {
        float cFP = sc.GetPersistentFloat(S_FP+i); 
        int type = sc.GetPersistentInt(S_TYPE+i);
        int startScan = sc.GetPersistentInt(S_LCI+i); 
        if (startScan >= sc.ArraySize) startScan = sc.GetPersistentInt(S_FI+i);

        for (int j = startScan; j < sc.ArraySize; j++) {
            if ((type == 1 && sc.High[j] > cFP) || (type == 2 && sc.Low[j] < cFP)) {
                cFP = (type == 1) ? sc.High[j] : sc.Low[j]; 
                sc.SetPersistentFloat(S_FP+i, cFP); 
                sc.SetPersistentSCDateTime(S_FT+i, sc.BaseDateTimeIn[j]); 
                sc.SetPersistentInt(S_FI+i, j); 
                LastSize = -1;
            }
        }
        sc.SetPersistentInt(S_LCI+i, sc.ArraySize - 1);
    }

    // --- 4. RENDERER (PRZYWRÓCONA STABILNOŚĆ) ---
    s_UseTool T;
    auto DrawSet = [&](float fP, int fA, float rP, int rA, int idS, int step) {
        if (fP <= 0 || rP <= 0) return;
        
        float ndg = sc.TickSize * 1.0f; // Nudge UP [cite: 2026-03-03]

        // Węzły: ○, Size 15, TN Blue [cite: 2026-02-27]
        T.Clear(); T.AddMethod = UTAM_ADD_OR_ADJUST;
        T.DrawingType = DRAWING_TEXT; T.FontSize = 15; T.FontBold = 1;
        T.Color = sc.Input[4].GetColor(); T.Text = "○"; 
        T.TextAlignment = DT_CENTER | DT_VCENTER; 
        T.BeginIndex = fA; T.BeginValue = fP + ndg; T.LineNumber = idS; sc.UseTool(T);
        T.BeginIndex = rA; T.BeginValue = rP + ndg; T.LineNumber = idS + 1; sc.UseTool(T);

        // String Buffering dla precyzji [cite: 2026-03-04]
        SCString Buf100 = sc.FormatGraphValue(rP, sc.BaseGraphValueFormat);
        int DW = (Buf100.GetLength() / 2) + 1; // Formula [cite: 2026-02-27]
        
        float rng = fP - rP; 
        float v38 = sc.RoundToTickSize(fP - rng * 0.382f); 
        float v61 = sc.RoundToTickSize(fP - rng * 0.618f);
        SCString Buf38 = sc.FormatGraphValue(v38, sc.BaseGraphValueFormat);
        SCString Buf61 = sc.FormatGraphValue(v61, sc.BaseGraphValueFormat);

        int sX = sc.ArraySize + (step * DW); int eX = sX + DW; int tX = (sX + eX) / 2;

        auto L = [&](float p, COLORREF c, const char* n, int stIdx, int id, int w, const SCString& label) {
            T.Clear(); T.AddMethod = UTAM_ADD_OR_ADJUST;
            T.DrawingType = DRAWING_LINE; T.BeginIndex = stIdx; T.EndIndex = eX;
            T.BeginValue = p; T.EndValue = p; T.Color = c; T.LineWidth = w; T.LineNumber = id; sc.UseTool(T);
            
            T.Clear(); T.AddMethod = UTAM_ADD_OR_ADJUST;
            T.DrawingType = DRAWING_TEXT; T.BeginIndex = tX; T.BeginValue = p;
            T.Color = c; T.FontSize = 8; T.FontBold = 1; T.Text = label; 
            T.TextAlignment = DT_CENTER | DT_BOTTOM; T.LineNumber = id + 1; sc.UseTool(T);
            T.Text = n; T.TextAlignment = DT_CENTER | DT_TOP; T.LineNumber = id + 2; sc.UseTool(T);
        };

        L(rP, sc.Input[3].GetColor(), "1.000", rA, idS + 3, 2, Buf100); 
        L(v38, sc.Input[1].GetColor(), "0.382", sX, idS + 6, 1, Buf38); 
        L(v61, sc.Input[2].GetColor(), "0.618", sX, idS + 9, 1, Buf61);

        T.Clear(); T.AddMethod = UTAM_ADD_OR_ADJUST;
        T.DrawingType = DRAWING_LINE; T.BeginIndex = eX; T.EndIndex = eX; 
        T.BeginValue = rP; T.EndValue = v38; T.Color = RGB(160, 160, 160); T.LineStyle = LINESTYLE_DOT; 
        T.LineNumber = idS + 12; sc.UseTool(T);
    };

    if (sc.ArraySize != LastSize) {
        for (int i = 0; i < NumSaved; i++) 
            DrawSet(sc.GetPersistentFloat(S_FP+i), sc.GetPersistentInt(S_FI+i), 
                    sc.GetPersistentFloat(S_RP+i), sc.GetPersistentInt(S_RI+i), 
                    20000+(i * 20), sc.GetPersistentInt(S_STEP+i));
        LastSize = sc.ArraySize;
    }

    if (TState == 2) {
        int fIdx = sc.GetNearestMatchForSCDateTime(sc.ChartNumber, sc.GetPersistentSCDateTime(10));
        DrawSet(sc.GetPersistentFloat(10), fIdx, 
                (mY < sc.GetPersistentFloat(10)) ? sc.Low[cIdx] : sc.High[cIdx], cIdx, 10000, SessionSteps);
    }
}
