#include "sierrachart.h"
#include <windows.h>
#include <fstream>

SCDLLName("DiNapoliExpansion")

// --- STAŁE ARCHITEKTONICZNE ---
const int GLOBAL_SYNC_SLOT = 9999;           

// 1. BEZPIECZEŃSTWO BINARNE: Upakowanie struktur (Etap 1 GDI)
#pragma pack(push, 1)
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
    
    bool isVisibleOnCurrentTF;
    int dwA, dwB, dwC;
    int dwCOP, dwOP, dwXOP;
    int reserved; // Rezerwowe miejsce pod przyszłe parametry
};

struct DiNapoliData {
    SavedExpansion expansions[20];
    int numSaved;
};
#pragma pack(pop)


// --- FUNKCJA ODŚWIEŻAJĄCA TEKSTY I GEOMETRIĘ (HOISTED) ---
void RefreshExpansionStrings(SCStudyInterfaceRef sc, SavedExpansion& exp) {
    if (exp.pA <= 0) return;
    
    float wave = exp.pB - exp.pA;
    exp.vCOP = sc.RoundToTickSize(exp.pC + (wave * 0.618f));
    exp.vOP  = sc.RoundToTickSize(exp.pC + (wave * 1.000f));
    exp.vXOP = sc.RoundToTickSize(exp.pC + (wave * 1.618f));

    SCString t, tPrice;
    tPrice = sc.FormatGraphValue(exp.pA, sc.BaseGraphValueFormat);
    t.Format("A %s", tPrice.GetChars());
    strncpy(exp.sA, t.GetChars(), 31); exp.sA[31] = '\0';
    exp.dwA = (tPrice.GetLength() / 2) + 2; // Przywrócono Standard + 2

    tPrice = sc.FormatGraphValue(exp.pB, sc.BaseGraphValueFormat);
    t.Format("B %s", tPrice.GetChars());
    strncpy(exp.sB, t.GetChars(), 31); exp.sB[31] = '\0'; 
    exp.dwB = (tPrice.GetLength() / 2) + 2; 

    tPrice = sc.FormatGraphValue(exp.pC, sc.BaseGraphValueFormat);
    t.Format("C %s", tPrice.GetChars());
    strncpy(exp.sC, t.GetChars(), 31); exp.sC[31] = '\0';
    exp.dwC = (tPrice.GetLength() / 2) + 2;

    SCString sTemp;
    sTemp = sc.FormatGraphValue(exp.vCOP, sc.BaseGraphValueFormat);
    strncpy(exp.sCOP, sTemp.GetChars(), 31);
    exp.sCOP[31] = '\0';
    exp.dwCOP = (sTemp.GetLength() / 2) + 1;

    sTemp = sc.FormatGraphValue(exp.vOP, sc.BaseGraphValueFormat);
    strncpy(exp.sOP,  sTemp.GetChars(), 31);
    exp.sOP[31] = '\0';
    exp.dwOP = (sTemp.GetLength() / 2) + 1;

    sTemp = sc.FormatGraphValue(exp.vXOP, sc.BaseGraphValueFormat);
    strncpy(exp.sXOP, sTemp.GetChars(), 31);
    exp.sXOP[31] = '\0';
    exp.dwXOP = (sTemp.GetLength() / 2) + 1;

    exp.isVisibleOnCurrentTF = (abs(exp.idxC - exp.idxA) >= sc.Input[12].GetInt());
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

// --- FUNKCJA DOPASOWANIA (BAR-BASED RANGE) ---
int FindBestMatchIndex(SCStudyInterfaceRef sc, float targetPrice, SCDateTime targetTime, bool isHigh, int searchRangeBars) {
    int baseIdx = sc.GetNearestMatchForSCDateTime(sc.ChartNumber, targetTime);
    if (baseIdx < 0) return -1;

    int startScan = max(0, baseIdx - searchRangeBars);
    int endScan = min(sc.ArraySize - 1, baseIdx + searchRangeBars);
    int bestIdx = baseIdx;
    bool foundExactPrice = false;

    for (int i = startScan; i <= endScan; i++) {
        float priceToCheck = isHigh ? sc.High[i] : sc.Low[i];
        if (fabs(priceToCheck - targetPrice) < (sc.TickSize / 2.0f)) {
            if (!foundExactPrice || abs(i - baseIdx) < abs(bestIdx - baseIdx)) {
                bestIdx = i; foundExactPrice = true;
            }
        }
    }
    return bestIdx; 
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
    if (outFile.is_open()) { outFile.write((char*)pData, sizeof(DiNapoliData)); outFile.close(); }
}

void LoadDiNapoliData(SCStudyInterfaceRef sc, DiNapoliData* pData) {
    SCString filePath;
    filePath.Format("%sDN_Exp_C%d_S%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
    std::ifstream inFile(filePath.GetChars(), std::ios::binary);
    if (inFile.is_open()) { inFile.read((char*)pData, sizeof(DiNapoliData)); inFile.close(); }
}


// --- SILNIK RENDERUJĄCY GDI (Etap 2) ---
void DrawReactionGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc) {
    DiNapoliData* pData = (DiNapoliData*)sc.GetPersistentPointer(500);
    if (!pData) return;

    // 1. SYNCHRONIZACJA WIDOCZNOŚCI (Slot 9999)
    bool IsVisible = (sc.GetPersistentIntFromChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT) == 0);
    if (!IsVisible) return;

    HPEN* phPen = (HPEN*)sc.GetPersistentPointer(502);
    HPEN hOldPen = (phPen && *phPen) ? (HPEN)SelectObject(hDC, *phPen) : NULL;

    int finalOffset = 5 + sc.Input[13].GetInt(); // Oddech od pierścienia

    // Lambda rysująca pojedynczą linię (Single Line)
    auto DrawSingleLine = [&](int idx, float price, int dw, bool isHigh) {
        if (idx < sc.IndexOfFirstVisibleBar - 50 || idx > sc.IndexOfLastVisibleBar + 50) return;

        int xS = sc.BarIndexToXPixelCoordinate(idx) + finalOffset; 
        // Finalny Sweet Spot: Odstęp (idx + 1) + Szerokość (dw).
        // To połączenie ze standardem "+ 2" gwarantuje docięcie do ostatniej cyfry ceny.
        int xE = sc.BarIndexToXPixelCoordinate(idx + 1 + dw); 
        int yBase = sc.RegionValueToYPixelCoordinate(price, sc.GraphRegion);
        
        if (yBase > 0) { 
            int yAdjusted = yBase + (isHigh ? 1 : 0);
            MoveToEx(hDC, xS, yAdjusted, NULL); 
            LineTo(hDC, xE, yAdjusted); 
        }
    };

    for (int i = 0; i < pData->numSaved; i++) {
        SavedExpansion& exp = pData->expansions[i];
        if (exp.pA <= 0 || !exp.isVisibleOnCurrentTF || exp.idxA < 0) continue;
        DrawSingleLine(exp.idxA, exp.pA, exp.dwA, (exp.aType == 1)); 
        DrawSingleLine(exp.idxB, exp.pB, exp.dwB, (exp.aType != 1)); 
        DrawSingleLine(exp.idxC, exp.pC, exp.dwC, (exp.aType == 1)); 
    }

   

// Obsługa podglądu (Preview) - Zunifikowana z RefreshExpansionStrings (Etap 3)
    int TState = sc.GetPersistentInt(1);
    if (TState >= 1) {
        int snapRange = sc.Input[6].GetInt();
        int AType = sc.GetPersistentInt(12);
        SavedExpansion tempExp; 
        memset(&tempExp, 0, sizeof(SavedExpansion));

        if (TState == 1) {
            float mY = (float)sc.ActiveToolYValue;
            bool isH = (fabs(mY - sc.High[sc.ActiveToolIndex]) < fabs(mY - sc.Low[sc.ActiveToolIndex]));
            tempExp.pA = isH ? (float)sc.High[sc.ActiveToolIndex] : (float)sc.Low[sc.ActiveToolIndex];
            tempExp.idxA = sc.ActiveToolIndex; 
            tempExp.idxC = tempExp.idxA; // Bumper dla widoczności
            RefreshExpansionStrings(sc, tempExp);
            DrawSingleLine(tempExp.idxA, tempExp.pA, tempExp.dwA, isH);
        }
        else if (TState == 2) {
            tempExp.pA = sc.GetPersistentFloat(10); 
            tempExp.idxA = sc.GetPersistentInt(20);
            int sB = GetSnappedIndex(sc, sc.ActiveToolIndex, snapRange, (AType == 2));
            tempExp.pB = (AType == 2) ? (float)sc.High[sB] : (float)sc.Low[sB];
            tempExp.idxB = sB; 
            tempExp.idxC = sB;
            RefreshExpansionStrings(sc, tempExp);
            DrawSingleLine(tempExp.idxA, tempExp.pA, tempExp.dwA, (AType == 1));
            DrawSingleLine(tempExp.idxB, tempExp.pB, tempExp.dwB, (AType == 2));
        }
        else if (TState == 3) {
            tempExp.pA = sc.GetPersistentFloat(10); 
            tempExp.idxA = sc.GetPersistentInt(20);
            tempExp.pB = sc.GetPersistentFloat(11); 
            tempExp.idxB = sc.GetPersistentInt(21);
            int sC = GetSnappedIndex(sc, sc.ActiveToolIndex, snapRange, (AType == 1));
            tempExp.pC = (AType == 1) ? (float)sc.High[sC] : (float)sc.Low[sC];
            tempExp.idxC = sC;
            RefreshExpansionStrings(sc, tempExp);
            DrawSingleLine(tempExp.idxA, tempExp.pA, tempExp.dwA, (AType == 1));
            DrawSingleLine(tempExp.idxB, tempExp.pB, tempExp.dwB, (AType != 1));
            DrawSingleLine(tempExp.idxC, tempExp.pC, tempExp.dwC, (AType == 1));
        }
    }



    if (hOldPen) SelectObject(hDC, hOldPen);
}





SCSFExport scsf_DiNapoliExpansion(SCStudyInterfaceRef sc)
{
    // --- 1. KONFIGURACJA (ZAMROŻONA) ---
    if (sc.SetDefaults) {
        sc.GraphName = "DiNapoli Expansion";
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
            sc.Subgraph[i].DisplayNameValueInDataLine = 0; 
        }

        sc.Input[0].Name = "Numer Przycisku Rysowania"; sc.Input[0].SetInt(2);
        sc.Input[1].Name = "Kolor 0.618"; sc.Input[1].SetColor(RGB(0, 0, 255));
        sc.Input[2].Name = "Kolor 1.000"; sc.Input[2].SetColor(RGB(0, 200, 0));
        sc.Input[3].Name = "Kolor 1.618"; sc.Input[3].SetColor(RGB(255, 0, 0));
        sc.Input[4].Name = "Kolor Pierscieni"; sc.Input[4].SetColor(RGB(65, 105, 225));
        sc.Input[5].Name = "Max Rysunkow"; sc.Input[5].SetInt(20);
        sc.Input[6].Name = "Snapping Range"; sc.Input[6].SetInt(1);
        sc.Input[7].Name = "Aktywuj Master Toggle"; sc.Input[7].SetYesNo(1);
        sc.Input[8].Name = "Przycisk HIDE"; sc.Input[8].SetInt(20);
        sc.Input[9].Name = "Przycisk LAST DEL"; sc.Input[9].SetInt(21);
        sc.Input[10].Name = "Przycisk ALL DEL"; sc.Input[10].SetInt(22);
       sc.Input[11].Name = "Search Range Bars"; sc.Input[11].SetInt(10);
        sc.Input[12].Name = "Min Bars Visibility Threshold"; sc.Input[12].SetInt(3);
        sc.Input[13].Name = "Korekta przesunięcia GDI (piksele)"; sc.Input[13].SetInt(0);

        sc.p_GDIFunction = DrawReactionGDI; // Rejestracja silnika GDI (Etap 1)
        sc.ScaleRangeType = SCALE_SAMEASREGION; // Zapobiega drganiom linii

        return;
    }

    // --- 2. PAMIĘĆ I ZASOBY GDI (Etap 1) ---
    DiNapoliData* pData = (DiNapoliData*)sc.GetPersistentPointer(500); 
    if (pData == NULL) {
        pData = (DiNapoliData*)sc.AllocateMemory(sizeof(DiNapoliData));
        memset(pData, 0, sizeof(DiNapoliData));
        sc.SetPersistentPointer(500, pData); LoadDiNapoliData(sc, pData);
    }

    s_UseTool* pTool = (s_UseTool*)sc.GetPersistentPointer(501); 
    if (pTool == NULL) {
        pTool = (s_UseTool*)sc.AllocateMemory(sizeof(s_UseTool));
        memset(pTool, 0, sizeof(s_UseTool)); sc.SetPersistentPointer(501, pTool);
    }

    HPEN* phPen = (HPEN*)sc.GetPersistentPointer(502);
    if (!phPen) {
        phPen = (HPEN*)sc.AllocateMemory(sizeof(HPEN));
        *phPen = NULL; sc.SetPersistentPointer(502, phPen);
    }

    // Bezpieczne usuwanie pióra GDI przy zamykaniu wskaźnika
    if (sc.LastCallToFunction) {
        if (phPen && *phPen) { DeleteObject(*phPen); *phPen = NULL; }
        return;
    }

    // Inicjalizacja czarnego pióra 1-pikselowego
    if (sc.IsFullRecalculation || *phPen == NULL) {
        if (*phPen) DeleteObject(*phPen);
        *phPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    }


    int DrawBtn = sc.Input[0].GetInt();
    int HideBtn = sc.Input[8].GetInt();
    int LastDelBtn = sc.Input[9].GetInt();
    int AllDelBtn = sc.Input[10].GetInt();
    int tfSearchRange = sc.Input[11].GetInt();

    int& TState = sc.GetPersistentInt(1); 
    int& LastSize = sc.GetPersistentInt(4);
    int& AType = sc.GetPersistentInt(12);
    int& LastSyncValue = sc.GetPersistentInt(15); 
    int& LastLeftIdx = sc.GetPersistentInt(16); 
    int snapRange = sc.Input[6].GetInt();

    static int cachedHideMenuID = -1; 

    sc.SetCustomStudyControlBarButtonText(DrawBtn, "ABC");
    sc.SetCustomStudyControlBarButtonText(HideBtn, "HIDE");
    sc.SetCustomStudyControlBarButtonText(LastDelBtn, "LAST DEL");
    sc.SetCustomStudyControlBarButtonText(AllDelBtn, "ALL DEL");

    int currentGlobalHide = sc.GetPersistentIntFromChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT);

    // --- 3. SYNCHRONIZACJA (MASTER PROTOCOL) ---
    if (sc.Input[7].GetYesNo() == 1 && sc.MenuEventID == HideBtn) {
        int newState = (currentGlobalHide == 0) ? 1 : 0;
        sc.SetPersistentIntForChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT, newState);
        HWND hRoot = ::GetAncestor((HWND)sc.ChartWindowHandle, GA_ROOT);
        if (hRoot != NULL) {
            if (cachedHideMenuID == -1) {
                cachedHideMenuID = FindSierraMenuCommandID(hRoot, "Chart", "Hide Chart Drawings");
            }
            int menuIDtoSend = (cachedHideMenuID != -1) ? cachedHideMenuID : 211;
            ::PostMessage(hRoot, WM_COMMAND, menuIDtoSend, 0);
        }
        sc.SetCustomStudyControlBarButtonEnable(HideBtn, 0); 
        
        // KONCEPCJA 7: Usunięto sc.MenuEventID = 0, aby sygnał "HIDE" popłynął do narzędzi Slave.
        currentGlobalHide = newState; LastSize = -1; 
    }

    bool IsVisible = (currentGlobalHide == 0);

    // --- 4. USUWANIE ---
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

    // --- 5. TF STABILITY (ONLY INDEX CACHING) ---
    if (sc.IsFullRecalculation || LastSize == -1) {
        for (int i = 1; i <= 60; i++) for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
        for (int i = 0; i < pData->numSaved; i++) {
            if (pData->expansions[i].pA > 0) {
                pData->expansions[i].idxA = FindBestMatchIndex(sc, pData->expansions[i].pA, pData->expansions[i].tA, (pData->expansions[i].aType == 1), tfSearchRange);
                pData->expansions[i].idxB = FindBestMatchIndex(sc, pData->expansions[i].pB, pData->expansions[i].tB, (pData->expansions[i].aType != 1), tfSearchRange);
                pData->expansions[i].idxC = FindBestMatchIndex(sc, pData->expansions[i].pC, pData->expansions[i].tC, (pData->expansions[i].aType == 1), tfSearchRange);
                RefreshExpansionStrings(sc, pData->expansions[i]);
            }
        }
    }

    // --- 6. INTERACTION ---
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
                RefreshExpansionStrings(sc, pData->expansions[s]);
                for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
                pData->numSaved++; SaveDiNapoliData(sc, pData); TState = 0; sc.SetCustomStudyControlBarButtonEnable(DrawBtn, 0); LastSize = -1;
            }
            sc.PointerEventType = 0; 
        }
    }

    // --- 7. REDRAW GATE ---
    bool needsRedraw = (sc.ArraySize != LastSize) || (sc.IndexOfFirstVisibleBar != LastLeftIdx) || (currentGlobalHide != LastSyncValue) || sc.IsFullRecalculation || (TState > 0);
    bool cacheMissing = false; 

    if (currentGlobalHide != LastSyncValue) { LastSyncValue = currentGlobalHide; LastSize = -1; }

    if (needsRedraw && IsVisible) {
        for (int i = 0; i < 20; i++) {
            int sBase = 1 + (i * 3);
            if (pData->expansions[i].idxA >= 0) sc.Subgraph[sBase][pData->expansions[i].idxA] = 0;
            if (pData->expansions[i].idxB >= 0) sc.Subgraph[sBase+1][pData->expansions[i].idxB] = 0;
            if (pData->expansions[i].idxC >= 0) sc.Subgraph[sBase+2][pData->expansions[i].idxC] = 0;
        }
        for (int i = 58; i <= 60; i++) for (int k = sc.IndexOfFirstVisibleBar; k <= sc.IndexOfLastVisibleBar; k++) sc.Subgraph[i][k] = 0;
    }

    // --- 8. RENDERER (LIVE PIXELS & HOISTED STRINGS) ---
    auto DrawExpansionPreformatted = [&](const SavedExpansion& exp, int idS, int subStart, bool drawLevels, bool isPreview) {
        if (exp.pA <= 0 || exp.idxA < 0) return;
        if (!isPreview && !exp.isVisibleOnCurrentTF) return;

        int rBound = exp.idxC + 15; if (rBound < sc.IndexOfFirstVisibleBar) return;

        auto GetYAdj = [&](float p, bool isHigh) -> float {
            if (!isHigh) return p;
            int pY = sc.RegionValueToYPixelCoordinate(p, sc.GraphRegion);
            if (pY == 0) { cacheMissing = true; return p; } 

            float yResult = (float)sc.YPixelCoordinateToGraphValue(pY + 1);

            if (yResult <= 1.0f || (p > 1.0f && fabsf(yResult - p) / p > 0.20f)) { 
                cacheMissing = true; 
                return p; 
            }
            return yResult;
        };

        auto AnchorPoint = [&](float p, bool isHigh, int idx, const char* label, int id, int subIdx, int dw) {
            float yLive = GetYAdj(p, isHigh);
            if (IsVisible && idx >= 0 && idx < sc.ArraySize) {
                sc.Subgraph[subIdx].PrimaryColor = sc.Input[4].GetColor(); sc.Subgraph[subIdx][idx] = yLive; 
            }
            // Etap 3: Linia systemowa usunięta. Teraz rysuje ją silnik GDI z precyzyjnym offsetem 5px.
            // Pozostawiamy tylko etykietę tekstową (Cena + Nazwa).
            pTool->Clear(); pTool->AddMethod = UTAM_ADD_OR_ADJUST; pTool->DrawingType = DRAWING_TEXT;
            pTool->AddAsUserDrawnDrawing = 0; pTool->BeginIndex = idx + 1; pTool->BeginValue = p; pTool->Color = RGB(0, 0, 0);
            pTool->FontFace = "Arial"; pTool->FontSize = 8; pTool->FontBold = 1; pTool->Text = label;
            pTool->TextAlignment = DT_LEFT | DT_TOP; pTool->LineNumber = id + 2; sc.UseTool(*pTool);
        };

        AnchorPoint(exp.pA, (exp.aType==1), exp.idxA, exp.sA, idS, subStart, exp.dwA);
        AnchorPoint(exp.pB, (exp.aType!=1), exp.idxB, exp.sB, idS + 3, subStart + 1, exp.dwB);
        AnchorPoint(exp.pC, (exp.aType==1), exp.idxC, exp.sC, idS + 6, subStart + 2, exp.dwC);

        if (!drawLevels) return;
        int sX = exp.idxC + 1;
        auto L = [&](float p, float v, COLORREF c, const char* vS, const char* rL, int id, int dw) {
            float yLive = GetYAdj(v, true); 
            int eX = sX + dw; int tX = (sX + eX) / 2;
            pTool->Clear(); pTool->AddMethod = UTAM_ADD_OR_ADJUST; pTool->DrawingType = DRAWING_LINE;
            pTool->AddAsUserDrawnDrawing = 0; pTool->BeginIndex = sX; pTool->EndIndex = eX; pTool->BeginValue = p; pTool->EndValue = p;
            pTool->Color = c; pTool->LineWidth = 1; pTool->LineNumber = id; sc.UseTool(*pTool);
            pTool->Clear(); pTool->AddMethod = UTAM_ADD_OR_ADJUST; pTool->DrawingType = DRAWING_TEXT;
            pTool->AddAsUserDrawnDrawing = 0; pTool->BeginIndex = tX; pTool->BeginValue = p; pTool->Color = c; pTool->FontSize = 8; pTool->FontBold = 1;
            pTool->FontFace = "Arial"; pTool->Text = vS; pTool->TextAlignment = DT_CENTER | DT_BOTTOM; pTool->LineNumber = id+1; sc.UseTool(*pTool);
            pTool->Text = rL; pTool->BeginValue = yLive; pTool->TextAlignment = DT_CENTER | DT_TOP; pTool->LineNumber = id+2; sc.UseTool(*pTool);
        };
        L(exp.vCOP, exp.vCOP, sc.Input[1].GetColor(), exp.sCOP, "0.618", idS+10, exp.dwCOP);
        L(exp.vOP,  exp.vOP,  sc.Input[2].GetColor(), exp.sOP,  "1.000", idS+13, exp.dwOP);
        L(exp.vXOP, exp.vXOP, sc.Input[3].GetColor(), exp.sXOP, "1.618", idS+16, exp.dwXOP);
    };

    if (IsVisible) {
        if (needsRedraw && sc.ArraySize > 0) {
            int maxR = sc.Input[5].GetInt(); int limit = min(pData->numSaved, maxR);
            for (int i = 0; i < limit; i++) DrawExpansionPreformatted(pData->expansions[i], 30000 + (i * 40), 1 + (i * 3), true, false);
            LastSize = sc.ArraySize; LastLeftIdx = sc.IndexOfFirstVisibleBar;
            if (cacheMissing) LastSize = -1; 
        }
     if (TState >= 1) {
            SavedExpansion preview; memset(&preview, 0, sizeof(SavedExpansion));
            
            // Logika Ducha Punktu A (TState 1)
            if (TState == 1) {
                float mY = (float)sc.ActiveToolYValue;
                bool isH = (fabs(mY - sc.High[sc.ActiveToolIndex]) < fabs(mY - sc.Low[sc.ActiveToolIndex]));
                preview.pA = isH ? (float)sc.High[sc.ActiveToolIndex] : (float)sc.Low[sc.ActiveToolIndex];
                preview.idxA = sc.ActiveToolIndex;
                preview.idxC = preview.idxA; // Bumper dla Clippingu renderera
                preview.aType = isH ? 1 : 2;
                RefreshExpansionStrings(sc, preview); 
                DrawExpansionPreformatted(preview, 10000, 58, false, true);
            }
            else if (TState == 2) {
                preview.pA = sc.GetPersistentFloat(10); preview.idxA = sc.GetPersistentInt(20); preview.aType = AType;
                int sB = GetSnappedIndex(sc, sc.ActiveToolIndex, snapRange, (AType == 2));
                preview.pB = (AType == 2) ? sc.High[sB] : sc.Low[sB]; preview.idxB = sB; preview.pC = preview.pB; preview.idxC = sB;
                RefreshExpansionStrings(sc, preview); DrawExpansionPreformatted(preview, 10000, 58, false, true); 
            } 
            else if (TState == 3) {
                preview.pA = sc.GetPersistentFloat(10); preview.idxA = sc.GetPersistentInt(20); preview.aType = AType;
                preview.pB = sc.GetPersistentFloat(11); preview.idxB = sc.GetPersistentInt(21);
                int sC = GetSnappedIndex(sc, sc.ActiveToolIndex, snapRange, (AType == 1));
                preview.pC = (AType == 1) ? sc.High[sC] : sc.Low[sC]; preview.idxC = sC;
                RefreshExpansionStrings(sc, preview); DrawExpansionPreformatted(preview, 10000, 58, true, true);
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
