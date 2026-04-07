#include "sierrachart.h"
#include <windows.h>
#include <fstream>

SCDLLName("DiNapoliRetracement")

// --- STAŁE ARCHITEKTONICZNE ---
const int GLOBAL_SYNC_SLOT = 9999;
#define RETR_BASE_ID 40000
#define SLOT_SIZE 40

#pragma pack(push, 1)
struct SavedRetracement {
    float pF, pR;            
    SCDateTime tF, tR;       
    int type;                
    int step;                
    int idxF, idxR;          
    int lastCheckedIdx;      
    int reserved[2];    
    char s38[32], s61[32], s100[32];
    int dw38, dw61, dw100;               
    bool isVisibleOnCurrentTF; 
};

struct DiNapoliData {
    SavedRetracement retracements[20]; 
    int numSaved;
};
#pragma pack(pop)

// --- SILNIK DOPASOWANIA (TIME-PRICE ALIGNMENT) ---
int FindBestMatchIndex(SCStudyInterfaceRef sc, float targetPrice, SCDateTime targetTime, bool isHigh, int searchRangeBars) {
    int baseIdx = sc.GetContainingIndexForSCDateTime(sc.ChartNumber, targetTime);
    if (baseIdx < 0) return -1;
    int startScan = max(0, baseIdx - searchRangeBars), endScan = min(sc.ArraySize - 1, baseIdx + searchRangeBars), bestIdx = baseIdx;
    bool foundExactPrice = false;
    for (int i = startScan; i <= endScan; i++) {
        float priceToCheck = isHigh ? sc.High[i] : sc.Low[i];
        if (fabs(priceToCheck - targetPrice) < (sc.TickSize / 2.0f)) {
            if (!foundExactPrice || abs(i - baseIdx) < abs(bestIdx - baseIdx)) { bestIdx = i; foundExactPrice = true; }
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
    SCString t = sc.FormatGraphValue(ret.pR, sc.BaseGraphValueFormat);
    strncpy(ret.s100, t.GetChars(), 31); ret.s100[31] = '\0'; 
    ret.dw100 = (t.GetLength() / 2) + 1; 
    float rng = ret.pF - ret.pR, v38 = sc.RoundToTickSize(ret.pF - rng * 0.382f), v61 = sc.RoundToTickSize(ret.pF - rng * 0.618f);
    t = sc.FormatGraphValue(v38, sc.BaseGraphValueFormat); strncpy(ret.s38, t.GetChars(), 31); ret.s38[31] = '\0'; ret.dw38 = (t.GetLength() / 2) + 1;
    t = sc.FormatGraphValue(v61, sc.BaseGraphValueFormat); strncpy(ret.s61, t.GetChars(), 31); ret.s61[31] = '\0'; ret.dw61 = (t.GetLength() / 2) + 1;
}

// --- NATYWNE RENDEROWANIE GDI (CZARNA LINIA 1.000) ---
void DrawReactionGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc) {
    DiNapoliData* pData = (DiNapoliData*)sc.GetPersistentPointer(500);
    if (!pData) return;

    int syncBtn = sc.Input[8].GetInt();
    bool IsVisible = (sc.GetPersistentIntFromChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT) == 0);
    if (sc.Input[7].GetYesNo() && sc.MenuEventID == syncBtn) IsVisible = !IsVisible;
    if (!IsVisible) return;

    HPEN hPen = CreatePen(PS_SOLID, 2, sc.Input[3].GetColor()); 
    HPEN hOldPen = (HPEN)SelectObject(hDC, hPen);
    int finalOffset = (sc.Subgraph[1].LineWidth / 2) + 2 + sc.Input[13].GetInt();
    int TState = sc.GetPersistentInt(1);
    SCDateTime sessionTime = sc.GetPersistentSCDateTime(10);

    for (int i = 0; i < pData->numSaved; i++) {
        SavedRetracement& r = pData->retracements[i];
        // Surgical Fix: Porównanie czasu odporne na błędy precyzji
        bool isCurrentSession = (TState > 0 && fabs(r.tF.GetAsDouble() - sessionTime.GetAsDouble()) < 0.0000001);
        if (r.pF <= 0 || (!r.isVisibleOnCurrentTF && !isCurrentSession) || r.idxR < 0) continue;
        int xS = sc.BarIndexToXPixelCoordinate(r.idxR) + finalOffset; 
        int xE = sc.BarIndexToXPixelCoordinate(sc.ArraySize + (r.step * r.dw100) + r.dw100);
        int yP = sc.RegionValueToYPixelCoordinate(r.pR, sc.GraphRegion);
        if (yP > 0) { MoveToEx(hDC, xS, yP, NULL); LineTo(hDC, xE, yP); }
    }
    if (TState == 2) {
        float pF = sc.GetPersistentFloat(10), pR = (((float)sc.ActiveToolYValue) < pF) ? sc.Low[sc.ActiveToolIndex] : sc.High[sc.ActiveToolIndex];
        SCString t = sc.FormatGraphValue(pR, sc.BaseGraphValueFormat);
        int dw100 = (t.GetLength() / 2) + 1, xS = sc.BarIndexToXPixelCoordinate(sc.ActiveToolIndex) + finalOffset;
        int xE = sc.BarIndexToXPixelCoordinate(sc.ArraySize + (sc.GetPersistentInt(3) * dw100) + dw100), yP = sc.RegionValueToYPixelCoordinate(pR, sc.GraphRegion);
        if (yP > 0) { MoveToEx(hDC, xS, yP, NULL); LineTo(hDC, xE, yP); }
    }
    SelectObject(hDC, hOldPen); DeleteObject(hPen);
}

SCSFExport scsf_DiNapoliRetracement(SCStudyInterfaceRef sc)
{
    if (sc.SetDefaults) {
        sc.GraphName = "DiNapoli Retracement v_ULTRA_STABLE_GDI";
        sc.GraphRegion = 0; sc.AutoLoop = 0; sc.UpdateAlways = 1; 
        sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_WHEN_ACS_BUTTON_ENABLED;
        sc.ReceiveCharacterEvents = 1; sc.ScaleRangeType = SCALE_SAMEASREGION; 
        sc.p_GDIFunction = DrawReactionGDI;

        sc.Input[0].Name = "Numer Przycisku Retracement"; sc.Input[0].SetInt(1); 
        sc.Input[1].Name = "Kolor 0.382 (Red)"; sc.Input[1].SetColor(RGB(255, 0, 0)); 
        sc.Input[2].Name = "Kolor 0.618 (Pink)"; sc.Input[2].SetColor(RGB(255, 0, 255)); 
        sc.Input[3].Name = "Kolor 1.000 (Black)"; sc.Input[3].SetColor(RGB(0, 0, 0)); 
        sc.Input[4].Name = "Kolor Pierscieni (Royal Blue)"; sc.Input[4].SetColor(RGB(65, 105, 225)); 
        sc.Input[5].Name = "Max Rysunkow"; sc.Input[5].SetInt(20); 
        sc.Input[7].Name = "Sync z Master HIDE (Button 20)"; sc.Input[7].SetYesNo(1);
        sc.Input[8].Name = "Master Hide Button ID"; sc.Input[8].SetInt(20);
        sc.Input[9].Name = "Przycisk RETR DEL"; sc.Input[9].SetInt(23); 
        sc.Input[11].Name = "Search Range Bars (TF Stability)"; sc.Input[11].SetInt(10);
        sc.Input[12].Name = "Min Bars Cluster Visibility"; sc.Input[12].SetInt(3);
        sc.Input[13].Name = "Korekta przesunięcia GDI (piksele)"; sc.Input[13].SetInt(0);

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

    int Btn = sc.Input[0].GetInt(), RetrDelBtn = sc.Input[9].GetInt(); 
    sc.SetCustomStudyControlBarButtonText(Btn, "DiNapoli");
    sc.SetCustomStudyControlBarButtonText(RetrDelBtn, "RETR DEL"); 

    int& TState = sc.GetPersistentInt(1), &LastSize = sc.GetPersistentInt(4), &SessionSteps = sc.GetPersistentInt(3); 
    int& LastSyncValue = sc.GetPersistentInt(15), &LastLeftIdx = sc.GetPersistentInt(16);

    int syncBtn = sc.Input[8].GetInt();
    int currentGlobalHide = sc.GetPersistentIntFromChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT);
    bool syncClicked = (sc.Input[7].GetYesNo() && sc.MenuEventID == syncBtn);
    bool IsVisible = (currentGlobalHide == 0);
    if (syncClicked) IsVisible = !IsVisible;

    // Surgical Fix: Nuclear Reset czyści również sloty sesji
    if (sc.MenuEventID == RetrDelBtn) {
        for (int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        for (int i = 0; i < 20; i++) for (int j = 0; j < SLOT_SIZE; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, RETR_BASE_ID + (i * SLOT_SIZE) + j);
        for (int i = 1; i <= 60; i++) for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
        memset(pData, 0, sizeof(DiNapoliData)); SaveDiNapoliData(sc, pData);
        TState = 0; SessionSteps = 0; LastSize = -1; 
        sc.GetPersistentFloat(10) = 0; sc.GetPersistentSCDateTime(10) = 0; sc.GetPersistentInt(11) = 0;
        sc.SetCustomStudyControlBarButtonEnable(Btn, 0); 
        sc.SetCustomStudyControlBarButtonEnable(RetrDelBtn, 0); 
        sc.MenuEventID = 0; 
    }

    if (sc.CharacterEventCode == 32 && TState > 0) {
        for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        for (int k = sc.IndexOfFirstVisibleBar; k <= sc.IndexOfLastVisibleBar; k++) { sc.Subgraph[59][k] = 0; sc.Subgraph[60][k] = 0; }
        TState = 0; LastSize = -1; sc.SetCustomStudyControlBarButtonEnable(Btn, 0); sc.CharacterEventCode = 0; 
    }

    if (sc.IsFullRecalculation || LastSize == -1) {
        for (int i = 1; i <= 60; i++) for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
        for (int i = 0; i < pData->numSaved; i++) {
            SavedRetracement& r = pData->retracements[i];
            if (r.pF > 0) { r.idxF = FindBestMatchIndex(sc, r.pF, r.tF, (r.type == 1), sc.Input[11].GetInt()); r.idxR = FindBestMatchIndex(sc, r.pR, r.tR, (r.type == 2), sc.Input[11].GetInt()); RefreshRetracementStrings(sc, r); }
        }
        int threshold = sc.Input[12].GetInt(); 
        for (int i = 0; i < pData->numSaved; i++) {
            SCDateTime cTF = pData->retracements[i].tF; int maxSpan = 0; bool valid = true;
            for (int j = 0; j < pData->numSaved; j++) { if (pData->retracements[j].tF == cTF) { if (pData->retracements[j].idxF < 0 || pData->retracements[j].idxR < 0) valid = false; int d = abs(pData->retracements[j].idxR - pData->retracements[j].idxF); if (d > maxSpan) maxSpan = d; } }
            pData->retracements[i].isVisibleOnCurrentTF = valid && ((maxSpan + 1) >= threshold);
        }
    }

    if (sc.PointerEventType == SC_ACS_BUTTON_ON && sc.MenuEventID == Btn) { TState = 1; SessionSteps = 0; }
    if (sc.PointerEventType == SC_POINTER_BUTTON_DOWN && TState > 0) {
        float mY = (float)sc.ActiveToolYValue; int cIdx = sc.ActiveToolIndex;
        float sP = (TState == 1) ? ((fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx])) ? sc.High[cIdx] : sc.Low[cIdx]) : ((mY < sc.GetPersistentFloat(10)) ? sc.Low[cIdx] : sc.High[cIdx]);
        if (TState == 1) { sc.GetPersistentFloat(10) = sP; sc.GetPersistentSCDateTime(10) = sc.BaseDateTimeIn[cIdx]; sc.GetPersistentInt(11) = (sP == sc.High[cIdx] ? 1 : 2); TState = 2; }
        else if (TState == 2) {
            int s = pData->numSaved % 20; SavedRetracement& r = pData->retracements[s];
            r.pF = sc.GetPersistentFloat(10); r.tF = sc.GetPersistentSCDateTime(10); r.pR = sP; r.tR = sc.BaseDateTimeIn[cIdx]; r.type = sc.GetPersistentInt(11); r.step = SessionSteps;
            r.idxF = sc.GetContainingIndexForSCDateTime(sc.ChartNumber, r.tF); r.idxR = cIdx; RefreshRetracementStrings(sc, r);
            if (pData->numSaved < 20) pData->numSaved++; SaveDiNapoliData(sc, pData); SessionSteps++; LastSize = -1; 
        }
        sc.PointerEventType = 0;
    }

    bool trailingUpdated = false;
    SCDateTime sessionAnchor = sc.GetPersistentSCDateTime(10);
    for (int i = 0; i < pData->numSaved; i++) {
        SavedRetracement& r = pData->retracements[i]; int startScan = max(r.idxF, r.lastCheckedIdx);
        for (int j = startScan; j < sc.ArraySize; j++) { 
            if ((r.type == 1 && sc.High[j] > r.pF) || (r.type == 2 && sc.Low[j] < r.pF)) { 
                r.pF = (r.type == 1) ? sc.High[j] : sc.Low[j]; r.tF = sc.BaseDateTimeIn[j]; r.idxF = j; 
                RefreshRetracementStrings(sc, r); trailingUpdated = true; 
                // Surgical Fix: Trailing aktualizuje Anchor sesji
                if (TState > 0 && fabs(r.tF.GetAsDouble() - sessionAnchor.GetAsDouble()) < 0.0000001) {
                    sc.GetPersistentFloat(10) = r.pF; sc.GetPersistentSCDateTime(10) = r.tF;
                }
            } 
        }
        r.lastCheckedIdx = sc.ArraySize - 1;
    }
    if (trailingUpdated) SaveDiNapoliData(sc, pData);

    bool needsRedraw = (sc.ArraySize != LastSize) || (sc.IndexOfFirstVisibleBar != LastLeftIdx) || (currentGlobalHide != LastSyncValue) || syncClicked || sc.IsFullRecalculation || (TState > 0 && sc.PointerEventType == SC_POINTER_MOVE) || trailingUpdated;

    bool cacheMissing = false;
    auto GetYAdj = [&](float p, bool isHigh) -> float {
        int pY = sc.RegionValueToYPixelCoordinate(p, sc.GraphRegion);
        if (pY == 0) { cacheMissing = true; return p; } 
        return (float)sc.YPixelCoordinateToGraphValue(pY + (isHigh ? 0 : 1)); 
    };

    SCDateTime sTime = sc.GetPersistentSCDateTime(10); 
    auto DrawSet = [&](const SavedRetracement& r, int idS, int drawingIdx, bool isPreview) {
        if (r.pF <= 0 || r.pR <= 0) return;
        bool isCurrentSession = (TState > 0 && fabs(r.tF.GetAsDouble() - sTime.GetAsDouble()) < 0.0000001);
        if (!isPreview && !r.isVisibleOnCurrentTF && !isCurrentSession) return;
        float yFP = GetYAdj(r.pF, (r.type == 1)), yRP = GetYAdj(r.pR, (r.type == 2));
        if (IsVisible && drawingIdx * 2 + 2 <= 60) { sc.Subgraph[drawingIdx * 2 + 1][r.idxF] = yFP; sc.Subgraph[drawingIdx * 2 + 2][r.idxR] = yRP; }
        int sX = sc.ArraySize + (r.step * r.dw100), connX = sX + r.dw100, tX = (sX + connX) / 2;
        auto L = [&](float p, float v, COLORREF c, const char* n, int lineStartIdx, int id, int w, const char* label, int textIdx, int lineEndIdx, bool drawLine) {
            float yLive = GetYAdj(v, true); 
            if (drawLine) { p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_LINE; p_T->BeginIndex = lineStartIdx; p_T->EndIndex = lineEndIdx; p_T->BeginValue = p; p_T->EndValue = p; p_T->Color = c; p_T->LineWidth = w; p_T->LineNumber = id; sc.UseTool(*p_T); }
            p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_TEXT; p_T->BeginIndex = textIdx; p_T->BeginValue = p; p_T->Color = c; p_T->FontFace = "Arial"; p_T->FontSize = 8; p_T->FontBold = 1; p_T->Text = label; p_T->TextAlignment = DT_CENTER | DT_BOTTOM; p_T->LineNumber = id + 1; sc.UseTool(*p_T);
            p_T->Text = n; p_T->BeginValue = yLive; p_T->TextAlignment = DT_CENTER | DT_TOP; p_T->LineNumber = id + 2; sc.UseTool(*p_T);
        };
        L(r.pR, r.pR, sc.Input[3].GetColor(), "1.000", r.idxR, idS + 3, 2, r.s100, tX, connX, false);
        float rng = r.pF - r.pR; L(sc.RoundToTickSize(r.pF - rng * 0.382f), sc.RoundToTickSize(r.pF - rng * 0.382f), sc.Input[1].GetColor(), "0.382", sX, idS + 6, 1, r.s38, tX, connX, true);
        L(sc.RoundToTickSize(r.pF - rng * 0.618f), sc.RoundToTickSize(r.pF - rng * 0.618f), sc.Input[2].GetColor(), "0.618", sX, idS + 9, 1, r.s61, tX, connX, true);
        p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_LINE; p_T->LineStyle = LINESTYLE_DOT; p_T->BeginIndex = connX; p_T->EndIndex = connX; p_T->BeginValue = r.pR; p_T->EndValue = sc.RoundToTickSize(r.pF - rng * 0.382f); p_T->Color = RGB(160, 160, 160); p_T->LineNumber = idS + 12; sc.UseTool(*p_T);
    };

    if (IsVisible && (needsRedraw || TState > 0)) {
        for (int i = 1; i <= 60; i++) for (int k = sc.IndexOfFirstVisibleBar; k <= sc.IndexOfLastVisibleBar; k++) sc.Subgraph[i][k] = 0;
        for (int i = 0; i < pData->numSaved; i++) DrawSet(pData->retracements[i], RETR_BASE_ID + (i * SLOT_SIZE), i, false); 
        if (TState == 2) {
            SavedRetracement pr; memset(&pr, 0, sizeof(SavedRetracement)); pr.pF = sc.GetPersistentFloat(10); pr.tF = sTime; pr.idxF = sc.GetContainingIndexForSCDateTime(sc.ChartNumber, sTime);
            pr.pR = (((float)sc.ActiveToolYValue) < pr.pF) ? sc.Low[sc.ActiveToolIndex] : sc.High[sc.ActiveToolIndex]; pr.idxR = sc.ActiveToolIndex; pr.type = sc.GetPersistentInt(11); pr.step = SessionSteps; RefreshRetracementStrings(sc, pr); pr.isVisibleOnCurrentTF = true; DrawSet(pr, 10000, 29, true); 
        }
        LastSize = sc.ArraySize; LastLeftIdx = sc.IndexOfFirstVisibleBar; LastSyncValue = currentGlobalHide; if (cacheMissing) LastSize = -1; 
    } else if (!IsVisible && LastSize != -2) {
        for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        for(int i = 0; i < 20; i++) for(int j = 0; j < SLOT_SIZE; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, RETR_BASE_ID + (i * SLOT_SIZE) + j);
        for (int i = 1; i <= 60; i++) for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
        LastSize = -2; LastSyncValue = currentGlobalHide;
    }
}
