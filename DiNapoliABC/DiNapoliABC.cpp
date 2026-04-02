#include "sierrachart.h"
#include <windows.h>
#include <fstream>

SCDLLName("DiNapoliExpansion")

// --- STAŁE ARCHITEKTONICZNE ---
const int GLOBAL_SYNC_SLOT = 9999;           

// --- STRUKTURY DANYCH (ZAMROŻONE) ---
struct SavedExpansion {
    float pA, pB, pC;
    SCDateTime tA, tB, tC;
    int aType;
    int idxA, idxB, idxC;
    
    float vCOP, vOP, vXOP;
    
    char sA[32], sB[32], sC[32];
    char sCOP[32], sOP[32], sXOP[32];
};

struct DiNapoliData {
    SavedExpansion expansions[20];
    int numSaved;
};

// --- FUNKCJA ODŚWIEŻAJĄCA TEKSTY (ZAMROŻONA) ---
void RefreshExpansionStrings(SCStudyInterfaceRef sc, SavedExpansion& exp) {
    if (exp.pA <= 0) return;
    float wave = exp.pB - exp.pA;
    exp.vCOP = sc.RoundToTickSize(exp.pC + (wave * 0.618f));
    exp.vOP  = sc.RoundToTickSize(exp.pC + (wave * 1.000f));
    exp.vXOP = sc.RoundToTickSize(exp.pC + (wave * 1.618f));

    SCString t;
    t.Format("A %s", sc.FormatGraphValue(exp.pA, sc.BaseGraphValueFormat).GetChars());
    strncpy(exp.sA, t.GetChars(), 31);
    t.Format("B %s", sc.FormatGraphValue(exp.pB, sc.BaseGraphValueFormat).GetChars());
    strncpy(exp.sB, t.GetChars(), 31);
    t.Format("C %s", sc.FormatGraphValue(exp.pC, sc.BaseGraphValueFormat).GetChars());
    strncpy(exp.sC, t.GetChars(), 31);

    strncpy(exp.sCOP, sc.FormatGraphValue(exp.vCOP, sc.BaseGraphValueFormat).GetChars(), 31);
    strncpy(exp.sOP,  sc.FormatGraphValue(exp.vOP,  sc.BaseGraphValueFormat).GetChars(), 31);
    strncpy(exp.sXOP, sc.FormatGraphValue(exp.vXOP, sc.BaseGraphValueFormat).GetChars(), 31);
}

// --- DYNAMICZNE WYSZUKIWANIE ID MENU ---
int FindSierraMenuCommandID(HWND hRoot, const char* menuName, const char* itemName) {
    HMENU hMenu = GetMenu(hRoot);
    if (!hMenu) return -1;
    int menuCount = GetMenuItemCount(hMenu);
    for (int i = 0; i < menuCount; i++) {
        char buf[128];
        GetMenuStringA(hMenu, i, buf, 128, MF_BYPOSITION);
        if (strstr(buf, menuName)) {
            HMENU hSub = GetSubMenu(hMenu, i);
            if (!hSub) continue;
            int subCount = GetMenuItemCount(hSub);
            for (int j = 0; j < subCount; j++) {
                GetMenuStringA(hSub, j, buf, 128, MF_BYPOSITION);
                if (strstr(buf, itemName)) return GetMenuItemID(hSub, j);
            }
        }
    }
    return -1;
}

// --- FUNKCJE POMOCNICZE (NIENARUSZONE) ---
int FindBestMatchIndex(SCStudyInterfaceRef sc, float targetPrice, SCDateTime targetTime, bool isHigh) {
    int baseIdx = sc.GetNearestMatchForSCDateTime(sc.ChartNumber, targetTime);
    if (baseIdx < 0) return -1;
    int searchRange = 10;
    int startIdx = max(0, baseIdx - searchRange);
    int endIdx = min(sc.ArraySize - 1, baseIdx + searchRange);
    for (int i = startIdx; i <= endIdx; i++) {
        float priceToCheck = isHigh ? sc.High[i] : sc.Low[i];
        if (fabs(priceToCheck - targetPrice) < (sc.TickSize / 2.0f)) return i; 
    }
    return baseIdx; 
}

int GetSnappedIndex(SCStudyInterfaceRef sc, int mouseIdx, int range, bool findHigh) {
    if (range <= 0) return mouseIdx;
    int startIdx = max(0, mouseIdx - range);
    int endIdx = min(sc.ArraySize - 1, mouseIdx + range);
    int bestIdx = mouseIdx;
    float bestVal = findHigh ? sc.High[mouseIdx] : sc.Low[mouseIdx];
    for (int i = startIdx; i <= endIdx; i++) {
        float currentVal = findHigh ? sc.High[i] : sc.Low[i];
        if (findHigh) { if (currentVal > bestVal) { bestVal = currentVal; bestIdx = i; } }
        else { if (currentVal < bestVal) { bestVal = currentVal; bestIdx = i; } }
    }
    return bestIdx;
}

void SaveDiNapoliData(SCStudyInterfaceRef sc, DiNapoliData* pData) {
    SCString filePath;
    filePath.Format("%sDN_Exp_C%d_S%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
    std::ofstream outFile(filePath.GetChars(), std::ios::binary | std::ios::trunc);
    if (outFile.is_open()) {
        outFile.write((char*)pData, sizeof(DiNapoliData));
        outFile.close();
    }
}

void LoadDiNapoliData(SCStudyInterfaceRef sc, DiNapoliData* pData) {
    SCString filePath;
    filePath.Format("%sDN_Exp_C%d_S%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
    std::ifstream inFile(filePath.GetChars(), std::ios::binary);
    if (inFile.is_open()) {
        inFile.read((char*)pData, sizeof(DiNapoliData));
        inFile.close();
    }
}

SCSFExport scsf_DiNapoliExpansion(SCStudyInterfaceRef sc)
{
    // --- 1. KONFIGURACJA ---
    if (sc.SetDefaults) {
        sc.GraphName = "DiNapoli Expansion (v_ULTRA_FIXED)";
        sc.GraphRegion = 0; sc.AutoLoop = 0; sc.UpdateAlways = 1;
        sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_WHEN_ACS_BUTTON_ENABLED;
        sc.ReceiveCharacterEvents = 1; 

        sc.Subgraph[0].Name = "Heartbeat";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_HIDDEN;

        sc.Input[0].Name = "Numer Przycisku Rysowania (Expansion)"; sc.Input[0].SetInt(2);
        sc.Input[1].Name = "Kolor 0.618 (Blue)"; sc.Input[1].SetColor(RGB(0, 0, 255));
        sc.Input[2].Name = "Kolor 1.000 (Green)"; sc.Input[2].SetColor(RGB(0, 200, 0));
        sc.Input[3].Name = "Kolor 1.618 (Red)"; sc.Input[3].SetColor(RGB(255, 0, 0));
        sc.Input[4].Name = "Kolor Pierscieni (TN Blue)"; sc.Input[4].SetColor(RGB(0, 0, 255));
        sc.Input[5].Name = "Max Rysunkow"; sc.Input[5].SetInt(20);
        sc.Input[6].Name = "Snapping Range (Bars)"; sc.Input[6].SetInt(1);
        sc.Input[7].Name = "Aktywuj Master Toggle na Przycisku"; sc.Input[7].SetYesNo(1);
        sc.Input[8].Name = "Numer Przycisku HIDE (H)"; sc.Input[8].SetInt(20);
        sc.Input[9].Name = "Numer Przycisku LAST DEL"; sc.Input[9].SetInt(21);
        sc.Input[10].Name = "Numer Przycisku ALL DEL (Alt+X)"; sc.Input[10].SetInt(22);

        return;
    }

    // --- 2. PAMIĘĆ ---
    DiNapoliData* pData = (DiNapoliData*)sc.GetPersistentPointer(500); 
    if (pData == NULL) {
        pData = (DiNapoliData*)sc.AllocateMemory(sizeof(DiNapoliData));
        memset(pData, 0, sizeof(DiNapoliData));
        sc.SetPersistentPointer(500, pData);
        LoadDiNapoliData(sc, pData);
    }

    int DrawBtn = sc.Input[0].GetInt();
    int HideBtn = sc.Input[8].GetInt();
    int LastDelBtn = sc.Input[9].GetInt();
    int AllDelBtn = sc.Input[10].GetInt();
    
    int& TState = sc.GetPersistentInt(1); 
    int& LastSize = sc.GetPersistentInt(4);
    int& AType = sc.GetPersistentInt(12);
    int& LastSyncValue = sc.GetPersistentInt(15); 
    int& LastLeftIdx = sc.GetPersistentInt(16); 
    int snapRange = sc.Input[6].GetInt();

    sc.SetCustomStudyControlBarButtonText(DrawBtn, "Expansion");
    sc.SetCustomStudyControlBarButtonText(HideBtn, "HIDE");
    sc.SetCustomStudyControlBarButtonText(LastDelBtn, "LAST DEL");
    sc.SetCustomStudyControlBarButtonText(AllDelBtn, "ALL DEL");

    // --- 3. LOGIKA SYSTEMOWA (FIXED TOGGLE ORDER) ---
    int currentGlobalHide = sc.GetPersistentIntFromChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT);

    if (sc.Input[7].GetYesNo() == 1 && sc.MenuEventID == HideBtn) {
        int newState = (currentGlobalHide == 0) ? 1 : 0;
        sc.SetPersistentIntForChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT, newState);
        
        HWND hRoot = ::GetAncestor((HWND)sc.ChartWindowHandle, GA_ROOT);
        if (hRoot != NULL) {
            int dynamicID = FindSierraMenuCommandID(hRoot, "Chart", "Hide Chart Drawings");
            if (dynamicID != -1) ::PostMessage(hRoot, WM_COMMAND, dynamicID, 0);
            else ::PostMessage(hRoot, WM_COMMAND, 211, 0); 
        }
        
        sc.MenuEventID = 0; 
        currentGlobalHide = newState; 
        LastSize = -1; 
    }

    bool IsVisible = (currentGlobalHide == 0);

    if (sc.MenuEventID == LastDelBtn) {
        if (pData->numSaved > 0) {
            int lastIdx = pData->numSaved - 1;
            for(int j = 0; j < 40; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, 30000 + (lastIdx * 40) + j);
            pData->expansions[lastIdx].pA = 0; 
            pData->numSaved--;
            SaveDiNapoliData(sc, pData);
            LastSize = -1; 
        }
        sc.SetCustomStudyControlBarButtonEnable(LastDelBtn, 0);
        sc.MenuEventID = 0;
    }

    if (sc.MenuEventID == AllDelBtn) {
        for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        for(int i = 0; i < 20; i++) {
            for(int j = 0; j < 40; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, 30000 + (i * 40) + j);
        }
        memset(pData, 0, sizeof(DiNapoliData));
        SaveDiNapoliData(sc, pData); 
        TState = 0; LastSize = -1;
        sc.SetCustomStudyControlBarButtonEnable(DrawBtn, 0);
        sc.SetCustomStudyControlBarButtonEnable(AllDelBtn, 0);
        sc.MenuEventID = 0;
    }

    // --- 4. REFRESH CACHE & STRINGS ---
    if (sc.IsFullRecalculation || LastSize == -1) {
        for (int i = 0; i < pData->numSaved; i++) {
            if (pData->expansions[i].pA > 0) {
                pData->expansions[i].idxA = FindBestMatchIndex(sc, pData->expansions[i].pA, pData->expansions[i].tA, (pData->expansions[i].aType == 1));
                pData->expansions[i].idxB = FindBestMatchIndex(sc, pData->expansions[i].pB, pData->expansions[i].tB, (pData->expansions[i].aType != 1));
                pData->expansions[i].idxC = FindBestMatchIndex(sc, pData->expansions[i].pC, pData->expansions[i].tC, (pData->expansions[i].aType == 1));
                RefreshExpansionStrings(sc, pData->expansions[i]);
            }
        }
    }

    bool needsRedraw = (sc.ArraySize != LastSize) || (sc.IndexOfFirstVisibleBar != LastLeftIdx) || (currentGlobalHide != LastSyncValue) || sc.IsFullRecalculation || (TState > 0);
    if (currentGlobalHide != LastSyncValue) { LastSyncValue = currentGlobalHide; LastSize = -1; }
    if (sc.IsFullRecalculation) LastSize = -1;

    if (sc.CharacterEventCode == 32 && TState > 0) {
        for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        TState = 0; sc.SetCustomStudyControlBarButtonEnable(DrawBtn, 0);
        LastSize = -1;
    }

    // --- 5. MYSZ I RYSOWANIE ---
    if (!sc.DownloadingHistoricalData) {
        if (sc.PointerEventType == SC_ACS_BUTTON_ON && sc.MenuEventID == DrawBtn) { TState = 1; }
        int cIdx = sc.ActiveToolIndex; 
        float mY = (float)sc.ActiveToolYValue;
        if (sc.PointerEventType == SC_POINTER_BUTTON_DOWN && TState > 0) {
            if (TState == 1) {
                bool isAHigh = (fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx]));
                sc.GetPersistentFloat(10) = isAHigh ? sc.High[cIdx] : sc.Low[cIdx];
                sc.GetPersistentSCDateTime(10) = sc.BaseDateTimeIn[cIdx];
                sc.GetPersistentInt(20) = cIdx; 
                AType = isAHigh ? 1 : 2; TState = 2;
            }
            else if (TState == 2) {
                bool findHigh = (AType == 2); 
                int sIdx = GetSnappedIndex(sc, cIdx, snapRange, findHigh);
                sc.GetPersistentFloat(11) = findHigh ? sc.High[sIdx] : sc.Low[sIdx];
                sc.GetPersistentSCDateTime(11) = sc.BaseDateTimeIn[sIdx];
                sc.GetPersistentInt(21) = sIdx;
                TState = 3;
            }
            else if (TState == 3) {
                bool findHigh = (AType == 1);
                int sIdx = GetSnappedIndex(sc, cIdx, snapRange, findHigh);
                int s = pData->numSaved % sc.Input[5].GetInt();
                pData->expansions[s].pA = sc.GetPersistentFloat(10);
                pData->expansions[s].tA = sc.GetPersistentSCDateTime(10);
                pData->expansions[s].idxA = sc.GetPersistentInt(20);
                pData->expansions[s].pB = sc.GetPersistentFloat(11);
                pData->expansions[s].tB = sc.GetPersistentSCDateTime(11);
                pData->expansions[s].idxB = sc.GetPersistentInt(21);
                pData->expansions[s].pC = findHigh ? sc.High[sIdx] : sc.Low[sIdx];
                pData->expansions[s].tC = sc.BaseDateTimeIn[sIdx];
                pData->expansions[s].idxC = sIdx;
                pData->expansions[s].aType = AType;
                RefreshExpansionStrings(sc, pData->expansions[s]);
                for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
                pData->numSaved++; 
                SaveDiNapoliData(sc, pData); 
                TState = 0; sc.SetCustomStudyControlBarButtonEnable(DrawBtn, 0); LastSize = -1;
            }
            sc.PointerEventType = 0; 
        }
    }

    // --- 6. RENDERER ---
    s_UseTool Tool;
    auto DrawExpansionPreformatted = [&](const SavedExpansion& exp, int idS, bool drawLevels) {
        if (exp.pA <= 0 || exp.idxA < 0) return;
        
        int rightBoundary = exp.idxC + 15; 
        if (rightBoundary < sc.IndexOfFirstVisibleBar) return;

        bool isAHigh = (exp.aType == 1); bool isBHigh = (exp.aType != 1); bool isCHigh = (exp.aType == 1);
        float vH = -1e10f, vL = 1e10f;
        for (int i = sc.IndexOfFirstVisibleBar; i <= sc.IndexOfLastVisibleBar; i++) {
            if (sc.High[i] > vH) vH = sc.High[i];
            if (sc.Low[i] < vL) vL = sc.Low[i];
        }
        float chartRange = vH - vL; if (chartRange <= 0) chartRange = sc.TickSize * 100;
        float nudgeVal = chartRange * -0.001f; 

        auto AnchorPoint = [&](float p, int idx, const char* labelStr, int id, bool isPointHigh) {
            float pDrawn = isPointHigh ? (p + nudgeVal) : p;
            int DW = (strlen(labelStr) / 2) + 2; 
            Tool.Clear(); Tool.AddMethod = UTAM_ADD_OR_ADJUST; Tool.DrawingType = DRAWING_TEXT;
            Tool.AddAsUserDrawnDrawing = 0; Tool.FontFace = "SimSun"; Tool.FontSize = 9; Tool.FontBold = 0; 
            Tool.Color = sc.Input[4].GetColor(); Tool.Text = "○"; Tool.TextAlignment = DT_CENTER | DT_VCENTER;
            Tool.BeginIndex = idx; Tool.BeginValue = pDrawn; Tool.LineNumber = id; sc.UseTool(Tool);
            
            Tool.Clear(); Tool.AddMethod = UTAM_ADD_OR_ADJUST; Tool.DrawingType = DRAWING_LINE;
            Tool.AddAsUserDrawnDrawing = 0; Tool.BeginIndex = idx; Tool.EndIndex = idx + DW; Tool.BeginValue = pDrawn; Tool.EndValue = pDrawn;
            Tool.Color = RGB(0, 0, 0); Tool.LineWidth = 1; Tool.LineNumber = id + 1; sc.UseTool(Tool);
            
            Tool.Clear(); Tool.AddMethod = UTAM_ADD_OR_ADJUST; Tool.DrawingType = DRAWING_TEXT;
            Tool.AddAsUserDrawnDrawing = 0; Tool.BeginIndex = idx + 1; Tool.BeginValue = pDrawn; Tool.Color = RGB(0, 0, 0);
            Tool.FontFace = "Arial"; Tool.FontSize = 8; Tool.FontBold = 1; Tool.Text = labelStr;
            Tool.TextAlignment = DT_LEFT | DT_TOP; Tool.LineNumber = id + 2; sc.UseTool(Tool);
        };

        AnchorPoint(exp.pA, exp.idxA, exp.sA, idS, isAHigh);
        AnchorPoint(exp.pB, exp.idxB, exp.sB, idS + 3, isBHigh);
        AnchorPoint(exp.pC, exp.idxC, exp.sC, idS + 6, isCHigh);

        if (!drawLevels) return;
        int sX = exp.idxC + 1;
        auto L = [&](float p, COLORREF c, const char* valStr, const char* ratioLabel, int id) {
            int DW = (strlen(valStr) / 2) + 1; int eX = sX + DW; int tX = (sX + eX) / 2;
            
            Tool.Clear(); Tool.AddMethod = UTAM_ADD_OR_ADJUST; Tool.DrawingType = DRAWING_LINE;
            Tool.AddAsUserDrawnDrawing = 0; Tool.BeginIndex = sX; Tool.EndIndex = eX; Tool.BeginValue = p; Tool.EndValue = p;
            Tool.Color = c; Tool.LineWidth = 1; Tool.LineNumber = id; sc.UseTool(Tool);
            
            Tool.Clear(); Tool.AddMethod = UTAM_ADD_OR_ADJUST; Tool.DrawingType = DRAWING_TEXT;
            Tool.AddAsUserDrawnDrawing = 0; Tool.BeginIndex = tX; Tool.BeginValue = p; Tool.Color = c; Tool.FontSize = 8; Tool.FontBold = 1;
            Tool.FontFace = "Arial"; Tool.Text = valStr; Tool.TextAlignment = DT_CENTER | DT_BOTTOM; Tool.LineNumber = id+1; sc.UseTool(Tool);
            
            // --- PIXEL-PERFECT OFFSET DLA DOLNEJ ETYKIETY (RATIO) ---
            float ratioPrice = p;
            int pY = sc.RegionValueToYPixelCoordinate(p, sc.GraphRegion);
            if (pY > 0) {
                // W koordynatach ekranowych Y rośnie w dół, więc +1 piksel daje przesunięcie w dół
                ratioPrice = sc.YPixelCoordinateToGraphValue(pY + 1); 
            }
            // --------------------------------------------------------

            Tool.Text = ratioLabel; Tool.BeginValue = ratioPrice; Tool.TextAlignment = DT_CENTER | DT_TOP; Tool.LineNumber = id+2; sc.UseTool(Tool);
        };
        
        L(exp.vCOP, sc.Input[1].GetColor(), exp.sCOP, "0.618", idS+10);
        L(exp.vOP,  sc.Input[2].GetColor(), exp.sOP,  "1.000", idS+13);
        L(exp.vXOP, sc.Input[3].GetColor(), exp.sXOP, "1.618", idS+16);
    };

    // --- 7. LOGIKA WYŚWIETLANIA ---
    if (IsVisible) {
        if (needsRedraw && sc.ArraySize > 0) {
            int maxR = sc.Input[5].GetInt();
            int limit = min(pData->numSaved, maxR);
            for (int i = 0; i < limit; i++) {
                DrawExpansionPreformatted(pData->expansions[i], 30000 + (i * 40), true);
            }
            LastSize = sc.ArraySize;
            LastLeftIdx = sc.IndexOfFirstVisibleBar;
        }
        if (TState >= 2) {
            SavedExpansion preview; memset(&preview, 0, sizeof(SavedExpansion));
            preview.pA = sc.GetPersistentFloat(10); preview.idxA = sc.GetPersistentInt(20);
            preview.aType = AType;
            int sIdx = sc.ActiveToolIndex;
            if (TState == 2) {
                int snapB = GetSnappedIndex(sc, sIdx, snapRange, (AType == 2));
                preview.pB = (AType == 2) ? sc.High[snapB] : sc.Low[snapB]; preview.idxB = snapB;
                preview.pC = preview.pB; preview.idxC = snapB;
                RefreshExpansionStrings(sc, preview);
                DrawExpansionPreformatted(preview, 10000, false);
            } else if (TState == 3) {
                preview.pB = sc.GetPersistentFloat(11); preview.idxB = sc.GetPersistentInt(21);
                int snapC = GetSnappedIndex(sc, sIdx, snapRange, (AType == 1));
                preview.pC = (AType == 1) ? sc.High[snapC] : sc.Low[snapC]; preview.idxC = snapC;
                RefreshExpansionStrings(sc, preview);
                DrawExpansionPreformatted(preview, 10000, true);
            }
        }
    } else {
        if (LastSize != -2) {
            for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
            for(int i = 0; i < 20; i++) {
                for(int j = 0; j < 40; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, 30000 + (i * 40) + j);
            }
            LastSize = -2; 
        }
    }
}
