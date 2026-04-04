#include "sierrachart.h"
#include <windows.h>
#include <fstream>

SCDLLName("DiNapoliRetracement")

const int GLOBAL_SYNC_SLOT = 9999;

#pragma pack(push, 1)
struct SavedRetracement {
    float pF, pR;            
    SCDateTime tF, tR;       
    int type;                
    int step;                
    int idxF, idxR;          
    int lastCheckedIdx;      
    int oldIdxF, oldIdxR;    
    
    char s38[32], s61[32], s100[32];
    int dw100;               
};

struct DiNapoliData {
    SavedRetracement retracements[30]; 
    int numSaved;
};
#pragma pack(pop)

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

void SaveDiNapoliData(SCStudyInterfaceRef sc, DiNapoliData* pData) {
    SCString filePath;
    filePath.Format("%sDN_Ret_C%d_S%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
    std::ofstream outFile(filePath.GetChars(), std::ios::binary | std::ios::trunc);
    if (outFile.is_open()) { outFile.write((char*)pData, sizeof(DiNapoliData)); outFile.close(); }
}

void LoadDiNapoliData(SCStudyInterfaceRef sc, DiNapoliData* pData) {
    SCString filePath;
    filePath.Format("%sDN_Ret_C%d_S%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
    std::ifstream inFile(filePath.GetChars(), std::ios::binary);
    if (inFile.is_open()) { inFile.read((char*)pData, sizeof(DiNapoliData)); inFile.close(); }
}

void RefreshRetracementStrings(SCStudyInterfaceRef sc, SavedRetracement& ret) {
    SCString t;
    t = sc.FormatGraphValue(ret.pR, sc.BaseGraphValueFormat);
    strncpy(ret.s100, t.GetChars(), 31); ret.s100[31] = '\0'; 
    ret.dw100 = (t.GetLength() / 2) + 1; 
    float rng = ret.pF - ret.pR;
    float v38 = sc.RoundToTickSize(ret.pF - rng * 0.382f);
    float v61 = sc.RoundToTickSize(ret.pF - rng * 0.618f);
    t = sc.FormatGraphValue(v38, sc.BaseGraphValueFormat); strncpy(ret.s38, t.GetChars(), 31); ret.s38[31] = '\0';
    t = sc.FormatGraphValue(v61, sc.BaseGraphValueFormat); strncpy(ret.s61, t.GetChars(), 31); ret.s61[31] = '\0';
}

SCSFExport scsf_DiNapoliRetracement(SCStudyInterfaceRef sc)
{
    if (sc.SetDefaults) {
        sc.GraphName = "DiNapoli Retracement v_ULTRA_STABLE";
        sc.GraphRegion = 0; sc.AutoLoop = 0; sc.UpdateAlways = 1; 
        sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_WHEN_ACS_BUTTON_ENABLED;
        
        // --- Etap 1: Konfiguracja zdarzeniowa ---
        sc.ReceiveCharacterEvents = 1; 

        sc.Subgraph[0].Name = "Heartbeat Sync"; sc.Subgraph[0].DrawStyle = DRAWSTYLE_HIDDEN;

        sc.Input[0].Name = "Numer Przycisku Retracement"; sc.Input[0].SetInt(1); 
        sc.Input[1].Name = "Kolor 0.382 (Red)"; sc.Input[1].SetColor(RGB(255, 0, 0)); 
        sc.Input[2].Name = "Kolor 0.618 (Pink)"; sc.Input[2].SetColor(RGB(255, 0, 255)); 
        sc.Input[3].Name = "Kolor 1.000 (Black)"; sc.Input[3].SetColor(RGB(0, 0, 0)); 
        sc.Input[4].Name = "Kolor Pierscieni (Royal Blue)"; sc.Input[4].SetColor(RGB(65, 105, 225)); 
        sc.Input[5].Name = "Max Rysunkow"; sc.Input[5].SetInt(30);
        sc.Input[6].Name = "Search Range Bars (TF Stability)"; sc.Input[6].SetInt(10);
        sc.Input[7].Name = "Sync z Master HIDE (Button 20)"; sc.Input[7].SetYesNo(1);
        sc.Input[8].Name = "Master Hide Button ID"; sc.Input[8].SetInt(20);

        for (int i = 1; i <= 60; i++) {
            sc.Subgraph[i].Name.Format("Ring %d", i); sc.Subgraph[i].DrawStyle = DRAWSTYLE_CIRCLE_HOLLOW; 
            sc.Subgraph[i].LineWidth = 5; sc.Subgraph[i].PrimaryColor = RGB(65, 105, 225); 
            sc.Subgraph[i].DisplayNameValueInDataLine = 0; 
        }
        return;
    }

    DiNapoliData* pData = (DiNapoliData*)sc.GetPersistentPointer(500);
    if (!pData) {
        pData = (DiNapoliData*)sc.AllocateMemory(sizeof(DiNapoliData));
        if (pData) { memset(pData, 0, sizeof(DiNapoliData)); sc.SetPersistentPointer(500, pData); LoadDiNapoliData(sc, pData); }
        else return;
    }

    s_UseTool* p_T = (s_UseTool*)sc.GetPersistentPointer(501);
    if (!p_T) {
        p_T = (s_UseTool*)sc.AllocateMemory(sizeof(s_UseTool));
        if (p_T) { memset(p_T, 0, sizeof(s_UseTool)); sc.SetPersistentPointer(501, p_T); }
        else return;
    }

    int Btn = sc.Input[0].GetInt();
    sc.SetCustomStudyControlBarButtonText(Btn, "DiNapoli");

    int& TState = sc.GetPersistentInt(1); 
    int& LastSize = sc.GetPersistentInt(4); 
    int& SessionSteps = sc.GetPersistentInt(3); 
    int& KeyLock = sc.GetPersistentInt(8); 
    int& LastSyncValue = sc.GetPersistentInt(15); 

    bool syncEnabled = sc.Input[7].GetYesNo();
    int masterHideBtn = sc.Input[8].GetInt();
    bool IsVisible = true;
    int currentGlobalHide = 0;

    if (syncEnabled) {
        currentGlobalHide = sc.GetPersistentIntFromChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT);
        if (sc.MenuEventID == masterHideBtn) {
            if (currentGlobalHide == LastSyncValue) { IsVisible = (currentGlobalHide != 0); } 
            else { IsVisible = (currentGlobalHide == 0); }
            LastSize = -1; 
        } else { IsVisible = (currentGlobalHide == 0); }
        if (currentGlobalHide != LastSyncValue) { LastSyncValue = currentGlobalHide; LastSize = -1; }
    }

    // --- ETAPY 2, 3, 4 & 5: Natychmiastowa Spacja (Filar Interakcji i Stabilizacji) ---
    if (sc.CharacterEventCode == 32 && TState > 0) 
    {
        // ETAP 3: Atomic Cleanup (Rysunki i Subgrafy)
        for(int id = 10000; id <= 10050; id++) {
            sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        }

        for (int k = sc.IndexOfFirstVisibleBar; k <= sc.IndexOfLastVisibleBar; k++) {
            sc.Subgraph[59][k] = 0;
            sc.Subgraph[60][k] = 0;
        }

        // ETAP 4: Reset Maszyny Stanów i UI
        TState = 0; 
        LastSize = -1; 
        sc.SetCustomStudyControlBarButtonEnable(Btn, 0); 

        // ETAP 5: Konsumpcja zdarzenia i finalizacja cyklu
        sc.CharacterEventCode = 0; 
    }

    bool cacheMissing = false;
    bool needsRedraw = (sc.ArraySize != LastSize || sc.IsFullRecalculation);

    if (sc.IsFullRecalculation || LastSize == -1) {
        for (int i = 1; i <= 60; i++) for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
        int searchRange = sc.Input[6].GetInt();
        for (int i = 0; i < pData->numSaved; i++) {
            SavedRetracement& r = pData->retracements[i];
            if (r.pF > 0) {
                r.idxF = FindBestMatchIndex(sc, r.pF, r.tF, (r.type == 1), searchRange);
                r.idxR = FindBestMatchIndex(sc, r.pR, r.tR, (r.type == 2), searchRange);
                r.oldIdxF = r.idxF; r.oldIdxR = r.idxR; r.lastCheckedIdx = sc.ArraySize - 1;
                RefreshRetracementStrings(sc, r);
            }
        }
    }

    if (sc.ChartWindowIsActive) {
        bool CtrlD = (GetAsyncKeyState(VK_CONTROL) & 0x8000), XD = (GetAsyncKeyState('X') & 0x8000);
        if (!XD) KeyLock = 0;
        if (KeyLock == 0) {
            if (CtrlD && XD) { 
                for(int id = 10000; id <= 10025; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
                for(int i = 0; i < pData->numSaved; i++) {
                    for(int j = 0; j < 20; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, 20000 + (i * 20) + j);
                    sc.Subgraph[i*2+1][pData->retracements[i].oldIdxF] = 0; sc.Subgraph[i*2+2][pData->retracements[i].oldIdxR] = 0;
                }
                pData->numSaved = 0; SaveDiNapoliData(sc, pData); TState = 0; SessionSteps = 0; LastSize = -1; KeyLock = 1;
                sc.SetCustomStudyControlBarButtonEnable(Btn, 0); needsRedraw = true;
            }
        }
    }

    if (sc.PointerEventType == SC_ACS_BUTTON_ON && sc.MenuEventID == Btn) { TState = 1; SessionSteps = 0; }
    int cIdx = sc.ActiveToolIndex; float mY = (float)sc.ActiveToolYValue;
    if (sc.PointerEventType == SC_POINTER_BUTTON_DOWN && TState > 0) {
        float sP = (TState == 1) ? ((fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx])) ? sc.High[cIdx] : sc.Low[cIdx]) : ((mY < sc.GetPersistentFloat(10)) ? sc.Low[cIdx] : sc.High[cIdx]);
        if (TState == 1) { sc.GetPersistentFloat(10) = sP; sc.GetPersistentSCDateTime(10) = sc.BaseDateTimeIn[cIdx]; sc.GetPersistentInt(11) = (sP == sc.High[cIdx] ? 1 : 2); TState = 2; }
        else if (TState == 2) {
            int s = pData->numSaved % 30;
            pData->retracements[s].pF = sc.GetPersistentFloat(10); pData->retracements[s].tF = sc.GetPersistentSCDateTime(10);
            pData->retracements[s].pR = sP; pData->retracements[s].tR = sc.BaseDateTimeIn[cIdx];
            pData->retracements[s].type = sc.GetPersistentInt(11); pData->retracements[s].step = SessionSteps;
            pData->retracements[s].idxF = sc.GetNearestMatchForSCDateTime(sc.ChartNumber, pData->retracements[s].tF); pData->retracements[s].idxR = cIdx;
            pData->retracements[s].oldIdxF = pData->retracements[s].idxF; pData->retracements[s].oldIdxR = pData->retracements[s].idxR;
            pData->retracements[s].lastCheckedIdx = sc.ArraySize - 1; RefreshRetracementStrings(sc, pData->retracements[s]);
            if (pData->numSaved < 30) pData->numSaved++; SaveDiNapoliData(sc, pData); SessionSteps++; LastSize = -1; needsRedraw = true;
        }
        sc.PointerEventType = 0;
    }

    for (int i = 0; i < pData->numSaved; i++) {
        SavedRetracement& r = pData->retracements[i]; int startScan = max(r.idxF, r.lastCheckedIdx);
        for (int j = startScan; j < sc.ArraySize; j++) {
            if ((r.type == 1 && sc.High[j] > r.pF) || (r.type == 2 && sc.Low[j] < r.pF)) {
                r.pF = (r.type == 1) ? sc.High[j] : sc.Low[j]; r.tF = sc.BaseDateTimeIn[j]; r.idxF = j;
                RefreshRetracementStrings(sc, r); needsRedraw = true; 
            }
        }
        r.lastCheckedIdx = sc.ArraySize - 1;
    }

    auto GetYAdj = [&](float p, bool isHigh) -> float {
        if (!isHigh || !IsVisible) return p; 
        int pY = sc.RegionValueToYPixelCoordinate(p, sc.GraphRegion);
        if (pY == 0) { cacheMissing = true; return p; } 
        float yResult = (float)sc.YPixelCoordinateToGraphValue(pY + 1);
        return (yResult <= 1.0f || (p > 1.0f && fabsf(yResult - p) / p > 0.20f)) ? (cacheMissing = true, p) : yResult;
    };

    auto DrawSet = [&](const SavedRetracement& r, int idS, int drawingIdx) {
        if (r.pF <= 0 || r.pR <= 0) return;
        float yFP = GetYAdj(r.pF, (r.type == 1)), yRP = GetYAdj(r.pR, (r.type == 2));
        int subF = drawingIdx * 2 + 1, subR = drawingIdx * 2 + 2;
        if (IsVisible && subR <= 60) {
            sc.Subgraph[subF].PrimaryColor = sc.Input[4].GetColor(); sc.Subgraph[subR].PrimaryColor = sc.Input[4].GetColor();
            sc.Subgraph[subF][r.idxF] = yFP; sc.Subgraph[subR][r.idxR] = yRP;
            if (drawingIdx < 30) { pData->retracements[drawingIdx].oldIdxF = r.idxF; pData->retracements[drawingIdx].oldIdxR = r.idxR; }
        }
        int sX = sc.ArraySize + (r.step * r.dw100), eX = sX + r.dw100, tX = (sX + eX) / 2;
        auto L = [&](float p, COLORREF c, const char* n, int stIdx, int id, int w, const char* label) {
            p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_LINE; p_T->LineStyle = LINESTYLE_SOLID;
            p_T->BeginIndex = stIdx; p_T->EndIndex = eX; p_T->BeginValue = p; p_T->EndValue = p; p_T->Color = c; p_T->LineWidth = w; p_T->LineNumber = id; sc.UseTool(*p_T);
            p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_TEXT; p_T->BeginIndex = tX; p_T->BeginValue = p;
            p_T->Color = c; p_T->FontSize = 8; p_T->FontBold = 1; p_T->Text = label; p_T->TextAlignment = DT_CENTER | DT_BOTTOM; p_T->LineNumber = id + 1; sc.UseTool(*p_T);
            p_T->Text = n; p_T->TextAlignment = DT_CENTER | DT_TOP; p_T->LineNumber = id + 2; sc.UseTool(*p_T);
        };
        L(r.pR, sc.Input[3].GetColor(), "1.000", r.idxR, idS + 3, 2, r.s100);
        float rng = r.pF - r.pR; 
        float v38 = sc.RoundToTickSize(r.pF - rng * 0.382f);
        L(v38, sc.Input[1].GetColor(), "0.382", sX, idS + 6, 1, r.s38);
        L(sc.RoundToTickSize(r.pF - rng * 0.618f), sc.Input[2].GetColor(), "0.618", sX, idS + 9, 1, r.s61);
        p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_LINE; p_T->LineStyle = LINESTYLE_DOT; 
        p_T->BeginIndex = eX; p_T->EndIndex = eX; p_T->BeginValue = r.pR; p_T->EndValue = v38; p_T->Color = RGB(160, 160, 160); p_T->LineNumber = idS + 12; sc.UseTool(*p_T);
    };

    if (IsVisible) {
        if (needsRedraw || cacheMissing || TState == 2) {
            for (int i = 0; i < 30; i++) { sc.Subgraph[i*2+1][pData->retracements[i].oldIdxF] = 0; sc.Subgraph[i*2+2][pData->retracements[i].oldIdxR] = 0; }
            for (int k = sc.IndexOfFirstVisibleBar; k <= sc.IndexOfLastVisibleBar; k++) { sc.Subgraph[59][k] = 0; sc.Subgraph[60][k] = 0; }
            for (int i = 0; i < pData->numSaved; i++) DrawSet(pData->retracements[i], 20000+(i * 20), i);
            LastSize = sc.ArraySize;
        }
    } else {
        if (LastSize != -2) {
            for(int id = 10000; id <= 10025; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
            for(int i = 0; i < pData->numSaved; i++) {
                for(int j = 0; j < 20; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, 20000 + (i * 20) + j);
                sc.Subgraph[i*2+1][pData->retracements[i].oldIdxF] = 0; sc.Subgraph[i*2+2][pData->retracements[i].oldIdxR] = 0;
            }
            for (int k = sc.IndexOfFirstVisibleBar; k <= sc.IndexOfLastVisibleBar; k++) { sc.Subgraph[59][k] = 0; sc.Subgraph[60][k] = 0; }
            LastSize = -2; 
        }
    }

    if (TState == 2 && IsVisible) {
        SavedRetracement preview; memset(&preview, 0, sizeof(SavedRetracement));
        preview.pF = sc.GetPersistentFloat(10); preview.idxF = sc.GetNearestMatchForSCDateTime(sc.ChartNumber, sc.GetPersistentSCDateTime(10));
        preview.pR = (mY < preview.pF) ? sc.Low[cIdx] : sc.High[cIdx]; preview.idxR = cIdx; 
        preview.type = sc.GetPersistentInt(11); preview.step = SessionSteps; RefreshRetracementStrings(sc, preview); DrawSet(preview, 10000, 29); 
    }
}
