#include "sierrachart.h"
#include <windows.h>
#include <fstream>

SCDLLName("DiNapoliExpansion")

// --- STAŁE ARCHITEKTONICZNE ---
const int GLOBAL_SYNC_SLOT = 9999;           

struct SavedExpansion {
    float pA, pB, pC;
    SCDateTime tA, tB, tC;
    int aType;
    int idxA, idxB, idxC;
    float vCOP, vOP, vXOP;
    float yA, yB, yC; 
    float yCOP, yOP, yXOP;
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
        sc.GraphName = "DiNapoli Expansion (v_ULTRA_FINAL_STABLE_V3)";
        sc.GraphRegion = 0; sc.AutoLoop = 0; sc.UpdateAlways = 1;
        sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_WHEN_ACS_BUTTON_ENABLED;
        sc.ReceiveCharacterEvents = 1; 

        sc.Subgraph[0].Name = "Heartbeat";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_HIDDEN;
        sc.Subgraph[0].DisplayNameValueInDataLine = 0; 

        for (int i = 1; i <= 60; i++) {
            sc.Subgraph[i].Name.Format("Ring %d", i);
            sc.Subgraph[i].DrawStyle = DRAWSTYLE_CIRCLE_HOLLOW; 
            sc.Subgraph[i].LineWidth = 5;               
            sc.Subgraph[i].PrimaryColor = RGB(65, 105, 225); 
            sc.Subgraph[i].DisplayNameValueInWindowsFlags = 0; 
            sc.Subgraph[i].DisplayNameValueInDataLine = 0; 
        }

        sc.Input[0].Name = "Numer Przycisku Rysowania (Expansion)"; sc.Input[0].SetInt(2);
        sc.Input[1].Name = "Kolor 0.618 (Blue)"; sc.Input[1].SetColor(RGB(0, 0, 255));
        sc.Input[2].Name = "Kolor 1.000 (Green)"; sc.Input[2].SetColor(RGB(0, 200, 0));
        sc.Input[3].Name = "Kolor 1.618 (Red)"; sc.Input[3].SetColor(RGB(255, 0, 0));
        sc.Input[4].Name = "Kolor Pierscieni (Royal Blue)"; sc.Input[4].SetColor(RGB(65, 105, 225));
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

    s_UseTool* pTool = (s_UseTool*)sc.GetPersistentPointer(501); 
    if (pTool == NULL) {
        pTool = (s_UseTool*)sc.AllocateMemory(sizeof(s_UseTool));
        memset(pTool, 0, sizeof(s_UseTool));
        sc.SetPersistentPointer(501, pTool);
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
        sc.SetCustomStudyControlBarButtonEnable(HideBtn, 0); 
        sc.MenuEventID = 0; currentGlobalHide = newState; LastSize = -1; 
    }

    bool IsVisible = (currentGlobalHide == 0);

    if (sc.MenuEventID == LastDelBtn) {
        if (pData->numSaved > 0) {
            int lastIdx = pData->numSaved - 1;
            for(int j = 0; j < 40; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, 30000 + (lastIdx * 40) + j);
            for(int i=1; i<=60; i++) for(int k=0; k<sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
            pData->expansions[lastIdx].pA = 0; pData->numSaved--;
            SaveDiNapoliData(sc, pData); LastSize = -1; 
        }
        sc.SetCustomStudyControlBarButtonEnable(LastDelBtn, 0); sc.MenuEventID = 0;
    }

    if (sc.MenuEventID == AllDelBtn) {
        for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        for(int i = 0; i < 20; i++) for(int j = 0; j < 40; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, 30000 + (i * 40) + j);
        for(int i=1; i<=60; i++) for(int k=0; k<sc.ArraySize; k++) sc.Subgraph[i][k] = 0; 
        memset(pData, 0, sizeof(DiNapoliData));
        SaveDiNapoliData(sc, pData); TState = 0; LastSize = -1;
        sc.SetCustomStudyControlBarButtonEnable(DrawBtn, 0);
        sc.SetCustomStudyControlBarButtonEnable(AllDelBtn, 0);
        sc.MenuEventID = 0;
    }

    // --- 4. REFRESH INDEXES ---
    if (sc.IsFullRecalculation || LastSize == -1) {
        for (int i = 1; i <= 60; i++) {
            for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
        }

        for (int i = 0; i < pData->numSaved; i++) {
            if (pData->expansions[i].pA > 0) {
                pData->expansions[i].idxA = FindBestMatchIndex(sc, pData->expansions[i].pA, pData->expansions[i].tA, (pData->expansions[i].aType == 1));
                pData->expansions[i].idxB = FindBestMatchIndex(sc, pData->expansions[i].pB, pData->expansions[i].tB, (pData->expansions[i].aType != 1));
                pData->expansions[i].idxC = FindBestMatchIndex(sc, pData->expansions[i].pC, pData->expansions[i].tC, (pData->expansions[i].aType == 1));
                pData->expansions[i].yA = 0; // Wymuszamy reinicjalizację Cache'u Y po restarcie
                RefreshExpansionStrings(sc, pData->expansions[i]);
            }
        }
    }

    // --- 5. INTERACTION & DRAWING ---
    if (sc.CharacterEventCode == 32 && TState > 0) {
        for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        TState = 0; sc.SetCustomStudyControlBarButtonEnable(DrawBtn, 0); LastSize = -1;
    }

    if (!sc.DownloadingHistoricalData) {
        if (sc.PointerEventType == SC_ACS_BUTTON_ON && sc.MenuEventID == DrawBtn) { TState = 1; }
        int cIdx = sc.ActiveToolIndex; float mY = (float)sc.ActiveToolYValue;
        if (sc.PointerEventType == SC_POINTER_BUTTON_DOWN && TState > 0) {
            if (TState == 1) {
                bool isAHigh = (fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx]));
                sc.GetPersistentFloat(10) = isAHigh ? sc.High[cIdx] : sc.Low[cIdx];
                sc.GetPersistentSCDateTime(10) = sc.BaseDateTimeIn[cIdx];
                sc.GetPersistentInt(20) = cIdx; AType = isAHigh ? 1 : 2; TState = 2;
            }
            else if (TState == 2) {
                bool findHigh = (AType == 2); int sIdx = GetSnappedIndex(sc, cIdx, snapRange, findHigh);
                sc.GetPersistentFloat(11) = findHigh ? sc.High[sIdx] : sc.Low[sIdx];
                sc.GetPersistentSCDateTime(11) = sc.BaseDateTimeIn[sIdx];
                sc.GetPersistentInt(21) = sIdx; TState = 3;
            }
            else if (TState == 3) {
                bool findHigh = (AType == 1); int sIdx = GetSnappedIndex(sc, cIdx, snapRange, findHigh);
                int s = pData->numSaved % sc.Input[5].GetInt();
                pData->expansions[s].pA = sc.GetPersistentFloat(10); pData->expansions[s].tA = sc.GetPersistentSCDateTime(10);
                pData->expansions[s].idxA = sc.GetPersistentInt(20); pData->expansions[s].pB = sc.GetPersistentFloat(11);
                pData->expansions[s].tB = sc.GetPersistentSCDateTime(11); pData->expansions[s].idxB = sc.GetPersistentInt(21);
                pData->expansions[s].pC = findHigh ? sc.High[sIdx] : sc.Low[sIdx];
                pData->expansions[s].tC = sc.BaseDateTimeIn[sIdx]; pData->expansions[s].idxC = sIdx;
                pData->expansions[s].aType = AType; 
                pData->expansions[s].yA = 0; // Trigger redraw
                RefreshExpansionStrings(sc, pData->expansions[s]);
                for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
                pData->numSaved++; SaveDiNapoliData(sc, pData); TState = 0; sc.SetCustomStudyControlBarButtonEnable(DrawBtn, 0); 
                LastSize = -1;
            }
            sc.PointerEventType = 0; 
        }
    }

    // --- 6. CACHE VALIDATION (FIX DLA RESTARTU) ---
    bool cacheMissing = false;
    if (IsVisible) {
        for (int i = 0; i < pData->numSaved; i++) {
            if (pData->expansions[i].pA > 0 && pData->expansions[i].yA <= 1.0f) {
                cacheMissing = true; 
                break;
            }
        }
    }

    bool needsRedraw = (sc.ArraySize != LastSize) || (sc.IndexOfFirstVisibleBar != LastLeftIdx) || (currentGlobalHide != LastSyncValue) || sc.IsFullRecalculation || (TState > 0) || cacheMissing;
    if (currentGlobalHide != LastSyncValue) { LastSyncValue = currentGlobalHide; LastSize = -1; }

    if (needsRedraw) {
        // PRECYZYJNE CZYSZCZENIE (Sparse)
        for (int i = 0; i < 20; i++) {
            SavedExpansion& exp = pData->expansions[i];
            int sBase = 1 + (i * 3);
            if (exp.idxA >= 0 && exp.idxA < sc.ArraySize) sc.Subgraph[sBase][exp.idxA] = 0;
            if (exp.idxB >= 0 && exp.idxB < sc.ArraySize) sc.Subgraph[sBase+1][exp.idxB] = 0;
            if (exp.idxC >= 0 && exp.idxC < sc.ArraySize) sc.Subgraph[sBase+2][exp.idxC] = 0;
        }
        for (int i = 58; i <= 60; i++) {
            for (int k = sc.IndexOfFirstVisibleBar; k <= sc.IndexOfLastVisibleBar; k++) sc.Subgraph[i][k] = 0;
        }

        auto GetYAdj = [&](float p, bool isHigh) -> float {
            if (!isHigh) return p;
            int pY = sc.RegionValueToYPixelCoordinate(p, sc.GraphRegion);
            if (pY == 0) return 0; // Chart not ready
            return (float)sc.YPixelCoordinateToGraphValue(pY + 1);
        };

        for (int i = 0; i < pData->numSaved; i++) {
            SavedExpansion& exp = pData->expansions[i];
            if (exp.pA <= 0) continue;
            float newYA = GetYAdj(exp.pA, (exp.aType == 1));
            if (newYA > 1.0f || (exp.aType != 1)) {
                exp.yA = newYA;
                exp.yB = GetYAdj(exp.pB, (exp.aType != 1));
                exp.yC = GetYAdj(exp.pC, (exp.aType == 1));
                int pyCOP = sc.RegionValueToYPixelCoordinate(exp.vCOP, sc.GraphRegion); if (pyCOP > 0) exp.yCOP = (float)sc.YPixelCoordinateToGraphValue(pyCOP + 1);
                int pyOP = sc.RegionValueToYPixelCoordinate(exp.vOP, sc.GraphRegion); if (pyOP > 0) exp.yOP = (float)sc.YPixelCoordinateToGraphValue(pyOP + 1);
                int pyXOP = sc.RegionValueToYPixelCoordinate(exp.vXOP, sc.GraphRegion); if (pyXOP > 0) exp.yXOP = (float)sc.YPixelCoordinateToGraphValue(pyXOP + 1);
            }
        }
    }

    // --- 7. RENDERER ---
    auto DrawExpansionPreformatted = [&](const SavedExpansion& exp, int idS, int subStart, bool drawLevels) {
        if (exp.pA <= 0 || exp.idxA < 0 || exp.yA <= 1.0f) return;
        int rightBoundary = exp.idxC + 15; if (rightBoundary < sc.IndexOfFirstVisibleBar) return;

        auto AnchorPoint = [&](float p, float yCached, int idx, const char* labelStr, int id, int subIdx) {
            if (yCached <= 1.0f) return;
            if (IsVisible && idx >= 0 && idx < sc.ArraySize) {
                sc.Subgraph[subIdx].PrimaryColor = sc.Input[4].GetColor();
                sc.Subgraph[subIdx][idx] = yCached; 
            }
            int DW = (strlen(labelStr) / 2) + 2; 
            pTool->Clear(); pTool->AddMethod = UTAM_ADD_OR_ADJUST; pTool->DrawingType = DRAWING_LINE;
            pTool->AddAsUserDrawnDrawing = 0; pTool->BeginIndex = idx; pTool->EndIndex = idx + DW; pTool->BeginValue = p; pTool->EndValue = p;
            pTool->Color = RGB(0, 0, 0); pTool->LineWidth = 1; pTool->LineNumber = id + 1; sc.UseTool(*pTool);
            pTool->Clear(); pTool->AddMethod = UTAM_ADD_OR_ADJUST; pTool->DrawingType = DRAWING_TEXT;
            pTool->AddAsUserDrawnDrawing = 0; pTool->BeginIndex = idx + 1; pTool->BeginValue = p; pTool->Color = RGB(0, 0, 0);
            pTool->FontFace = "Arial"; pTool->FontSize = 8; pTool->FontBold = 1; pTool->Text = labelStr;
            pTool->TextAlignment = DT_LEFT | DT_TOP; pTool->LineNumber = id + 2; sc.UseTool(*pTool);
        };

        AnchorPoint(exp.pA, exp.yA, exp.idxA, exp.sA, idS, subStart);
        AnchorPoint(exp.pB, exp.yB, exp.idxB, exp.sB, idS + 3, subStart + 1);
        AnchorPoint(exp.pC, exp.yC, exp.idxC, exp.sC, idS + 6, subStart + 2);

        if (!drawLevels) return;
        int sX = exp.idxC + 1;
        auto L = [&](float p, float yCached, COLORREF c, const char* valStr, const char* ratioLabel, int id) {
            if (yCached <= 1.0f) return;
            int DW = (strlen(valStr) / 2) + 1; int eX = sX + DW; int tX = (sX + eX) / 2;
            pTool->Clear(); pTool->AddMethod = UTAM_ADD_OR_ADJUST; pTool->DrawingType = DRAWING_LINE;
            pTool->AddAsUserDrawnDrawing = 0; pTool->BeginIndex = sX; pTool->EndIndex = eX; pTool->BeginValue = p; pTool->EndValue = p;
            pTool->Color = c; pTool->LineWidth = 1; pTool->LineNumber = id; sc.UseTool(*pTool);
            pTool->Clear(); pTool->AddMethod = UTAM_ADD_OR_ADJUST; pTool->DrawingType = DRAWING_TEXT;
            pTool->AddAsUserDrawnDrawing = 0; pTool->BeginIndex = tX; pTool->BeginValue = p; pTool->Color = c; pTool->FontSize = 8; pTool->FontBold = 1;
            pTool->FontFace = "Arial"; pTool->Text = valStr; pTool->TextAlignment = DT_CENTER | DT_BOTTOM; pTool->LineNumber = id+1; sc.UseTool(*pTool);
            pTool->Text = ratioLabel; pTool->BeginValue = yCached; pTool->TextAlignment = DT_CENTER | DT_TOP; pTool->LineNumber = id+2; sc.UseTool(*pTool);
        };
        L(exp.vCOP, exp.yCOP, sc.Input[1].GetColor(), exp.sCOP, "0.618", idS+10);
        L(exp.vOP,  exp.yOP,  sc.Input[2].GetColor(), exp.sOP,  "1.000", idS+13);
        L(exp.vXOP, exp.yXOP, sc.Input[3].GetColor(), exp.sXOP, "1.618", idS+16);
    };

    if (IsVisible) {
        if (needsRedraw && sc.ArraySize > 0) {
            int maxR = sc.Input[5].GetInt(); int limit = min(pData->numSaved, maxR);
            for (int i = 0; i < limit; i++) DrawExpansionPreformatted(pData->expansions[i], 30000 + (i * 40), 1 + (i * 3), true);
            LastSize = sc.ArraySize; LastLeftIdx = sc.IndexOfFirstVisibleBar;
        }
        if (TState >= 2) {
            SavedExpansion preview; memset(&preview, 0, sizeof(SavedExpansion));
            preview.pA = sc.GetPersistentFloat(10); preview.idxA = sc.GetPersistentInt(20); preview.aType = AType;
            int sIdx = sc.ActiveToolIndex;
            auto GetYAdj = [&](float p, bool isHigh) -> float {
                if (!isHigh) return p;
                int pY = sc.RegionValueToYPixelCoordinate(p, sc.GraphRegion);
                if (pY == 0) return 0;
                return (float)sc.YPixelCoordinateToGraphValue(pY + 1);
            };
            if (TState == 2) {
                int snapB = GetSnappedIndex(sc, sIdx, snapRange, (AType == 2));
                preview.pB = (AType == 2) ? sc.High[snapB] : sc.Low[snapB]; preview.idxB = snapB; preview.pC = preview.pB; preview.idxC = snapB;
                RefreshExpansionStrings(sc, preview); 
                preview.yA = GetYAdj(preview.pA, (preview.aType==1)); preview.yB = GetYAdj(preview.pB, (preview.aType!=1));
                DrawExpansionPreformatted(preview, 10000, 58, false); 
            } else if (TState == 3) {
                preview.pB = sc.GetPersistentFloat(11); preview.idxB = sc.GetPersistentInt(21);
                int snapC = GetSnappedIndex(sc, sIdx, snapRange, (AType == 1));
                preview.pC = (AType == 1) ? sc.High[snapC] : sc.Low[snapC]; preview.idxC = snapC;
                RefreshExpansionStrings(sc, preview); 
                preview.yA = GetYAdj(preview.pA, (preview.aType==1)); preview.yB = GetYAdj(preview.pB, (preview.aType!=1)); preview.yC = GetYAdj(preview.pC, (preview.aType==1));
                int pyCOP = sc.RegionValueToYPixelCoordinate(preview.vCOP, sc.GraphRegion); if (pyCOP > 0) preview.yCOP = (float)sc.YPixelCoordinateToGraphValue(pyCOP + 1);
                int pyOP = sc.RegionValueToYPixelCoordinate(preview.vOP, sc.GraphRegion); if (pyOP > 0) preview.yOP = (float)sc.YPixelCoordinateToGraphValue(pyOP + 1);
                int pyXOP = sc.RegionValueToYPixelCoordinate(preview.vXOP, sc.GraphRegion); if (pyXOP > 0) preview.yXOP = (float)sc.YPixelCoordinateToGraphValue(pyXOP + 1);
                DrawExpansionPreformatted(preview, 10000, 58, true);
            }
        }
    } else {
        if (LastSize != -2) {
            for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
            for(int i = 0; i < 20; i++) for(int j = 0; j < 40; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, 30000 + (i * 40) + j);
            for (int i = 1; i <= 60; i++) for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
            LastSize = -2; 
        }
    }
}
