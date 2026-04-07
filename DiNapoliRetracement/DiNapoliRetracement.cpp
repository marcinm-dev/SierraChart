#include "sierrachart.h"
#include <windows.h>
#include <fstream>

SCDLLName("DiNapoliRetracement")

// --- STAŁE ARCHITEKTONICZNE (ZAMROŻONE) ---
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

// --- GDI RENDERER ---
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
        bool isCurrentSession = (TState > 0 && fabs(r.tF.GetAsDouble() - sessionTime.GetAsDouble()) < 0.0000001);
        if (r.pF <= 0 || (!r.isVisibleOnCurrentTF && !isCurrentSession) || r.idxR < 0) continue;
        int xS = sc.BarIndexToXPixelCoordinate(r.idxR) + finalOffset; 
        int xE = sc.BarIndexToXPixelCoordinate(sc.ArraySize + (r.step * r.dw100) + r.dw100);
        int yP = sc.RegionValueToYPixelCoordinate(r.pR, sc.GraphRegion);
        if (yP > 0) { MoveToEx(hDC, xS, yP, NULL); LineTo(hDC, xE, yP); }
    }

    if (TState == 2) {
        float pR = sc.GetPersistentFloat(12);
        int sR = sc.GetPersistentInt(13);
        int dw100 = (sc.FormatGraphValue(pR, sc.BaseGraphValueFormat).GetLength() / 2) + 1;
        int xS = sc.BarIndexToXPixelCoordinate(sR) + finalOffset;
        int xE = sc.BarIndexToXPixelCoordinate(sc.ArraySize + (sc.GetPersistentInt(3) * dw100) + dw100);
        int yP = sc.RegionValueToYPixelCoordinate(pR, sc.GraphRegion);
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
        sc.Input[6].Name = "Snapping Range"; sc.Input[6].SetInt(1); 
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
        memset(pData, 0, sizeof(DiNapoliData)); sc.SetPersistentPointer(500, pData); LoadDiNapoliData(sc, pData);
    }
    s_UseTool* p_T = (s_UseTool*)sc.GetPersistentPointer(501);
    if (!p_T) {
        p_T = (s_UseTool*)sc.AllocateMemory(sizeof(s_UseTool));
        memset(p_T, 0, sizeof(s_UseTool)); sc.SetPersistentPointer(501, p_T);
    }

    int Btn = sc.Input[0].GetInt(), RetrDelBtn = sc.Input[9].GetInt(), snapRange = sc.Input[6].GetInt();
    sc.SetCustomStudyControlBarButtonText(Btn, "DiNapoli"); sc.SetCustomStudyControlBarButtonText(RetrDelBtn, "RETR DEL"); 
    int& TState = sc.GetPersistentInt(1), &LastSize = sc.GetPersistentInt(4), &SessionSteps = sc.GetPersistentInt(3); 
    int& LastSyncValue = sc.GetPersistentInt(15), &LastLeftIdx = sc.GetPersistentInt(16);
    int currentGlobalHide = sc.GetPersistentIntFromChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT);
    bool IsVisible = (currentGlobalHide == 0);
    if (sc.Input[7].GetYesNo() && sc.MenuEventID == sc.Input[8].GetInt()) IsVisible = !IsVisible;

    // [NUCLEAR ATOMIC RESET]
    if (sc.MenuEventID == RetrDelBtn) {
        for (int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        for (int i = 0; i < 20; i++) for (int j = 0; j < SLOT_SIZE; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, RETR_BASE_ID + (i * SLOT_SIZE) + j);
        for (int i = 1; i <= 60; i++) for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
        memset(pData, 0, sizeof(DiNapoliData)); SaveDiNapoliData(sc, pData);
        TState = 0; SessionSteps = 0; LastSize = -1; 
        sc.GetPersistentFloat(10) = 0; sc.GetPersistentSCDateTime(10) = 0; sc.GetPersistentInt(11) = 0;
        sc.GetPersistentFloat(12) = 0; sc.GetPersistentInt(13) = 0;
        sc.SetCustomStudyControlBarButtonEnable(Btn, 0); sc.SetCustomStudyControlBarButtonEnable(RetrDelBtn, 0); sc.MenuEventID = 0; 
    }

    if (sc.CharacterEventCode == 32 && TState > 0) {
        for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        for (int k = sc.IndexOfFirstVisibleBar; k <= sc.IndexOfLastVisibleBar; k++) { sc.Subgraph[59][k] = 0; sc.Subgraph[60][k] = 0; }
        TState = 0; LastSize = -1; sc.SetCustomStudyControlBarButtonEnable(Btn, 0); sc.CharacterEventCode = 0; 
    }

    if (sc.IsFullRecalculation || LastSize == -1 || sc.ArraySize != LastSize) {
        for (int i = 1; i <= 60; i++) for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
        for (int i = 0; i < pData->numSaved; i++) {
            SavedRetracement& r = pData->retracements[i];
            if (r.pF > 0) { 
                r.idxF = FindBestMatchIndex(sc, r.pF, r.tF, (r.type == 1), sc.Input[11].GetInt()); 
                r.idxR = FindBestMatchIndex(sc, r.pR, r.tR, (r.type == 2), sc.Input[11].GetInt()); 
                RefreshRetracementStrings(sc, r); 
            }
        }
        int threshold = sc.Input[12].GetInt(); 
        for (int i = 0; i < pData->numSaved; i++) {
            SCDateTime cTF = pData->retracements[i].tF; int maxSpan = 0; bool valid = true;
            for (int j = 0; j < pData->numSaved; j++) { if (fabs(pData->retracements[j].tF.GetAsDouble() - cTF.GetAsDouble()) < 0.0000001) { if (pData->retracements[j].idxF < 0 || pData->retracements[j].idxR < 0) valid = false; int d = abs(pData->retracements[j].idxR - pData->retracements[j].idxF); if (d > maxSpan) maxSpan = d; } }
            pData->retracements[i].isVisibleOnCurrentTF = valid && ((maxSpan + 1) >= threshold);
        }
    }

    if (sc.PointerEventType == SC_ACS_BUTTON_ON && sc.MenuEventID == Btn) { TState = 1; SessionSteps = 0; }
    
    if (TState > 0) {
        SCDateTime sTime = sc.GetPersistentSCDateTime(10);
        int fType = sc.GetPersistentInt(11);
        int cIdx = sc.ActiveToolIndex;

        if (TState == 1 && sc.PointerEventType == SC_POINTER_BUTTON_DOWN) {
            float mY = (float)sc.ActiveToolYValue; bool isH = (fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx]));
            sc.GetPersistentFloat(10) = isH ? sc.High[cIdx] : sc.Low[cIdx]; sc.GetPersistentSCDateTime(10) = sc.BaseDateTimeIn[cIdx]; sc.GetPersistentInt(11) = isH ? 1 : 2; TState = 2; 
            if (SessionSteps > 0 && pData->numSaved > 0) {
                SavedRetracement& last = pData->retracements[pData->numSaved - 1];
                sc.GetPersistentFloat(12) = (fType == 2) ? last.pR + sc.TickSize : last.pR - sc.TickSize;
                sc.GetPersistentInt(13) = last.idxR;
            } else { sc.GetPersistentFloat(12) = sc.GetPersistentFloat(10); sc.GetPersistentInt(13) = cIdx; }
            sc.PointerEventType = 0;
        }
        else if (TState == 2) {
            int sR = GetSnappedIndex(sc, cIdx, snapRange, (fType == 2));
            float pR = (fType == 2 ? sc.High[sR] : sc.Low[sR]);
            if (SessionSteps > 0 && pData->numSaved > 0) {
                SavedRetracement& last = pData->retracements[pData->numSaved - 1];
                bool isValid = (fType == 2) ? (pR >= last.pR + sc.TickSize) : (pR <= last.pR - sc.TickSize);
                if (isValid) { sc.GetPersistentFloat(12) = pR; sc.GetPersistentInt(13) = sR; }
            } else { sc.GetPersistentFloat(12) = pR; sc.GetPersistentInt(13) = sR; }

            if (sc.PointerEventType == SC_POINTER_BUTTON_DOWN) {
                float finalPR = sc.GetPersistentFloat(12); int finalIdxR = sc.GetPersistentInt(13);
                int s = pData->numSaved % 20; SavedRetracement& r = pData->retracements[s];
                r.pF = sc.GetPersistentFloat(10); r.tF = sc.GetPersistentSCDateTime(10); r.pR = finalPR; r.tR = sc.BaseDateTimeIn[finalIdxR]; r.type = fType; r.step = SessionSteps;
                r.idxF = sc.GetContainingIndexForSCDateTime(sc.ChartNumber, r.tF); r.idxR = finalIdxR; RefreshRetracementStrings(sc, r);
                if (pData->numSaved < 20) pData->numSaved++; SaveDiNapoliData(sc, pData); SessionSteps++; LastSize = -1; 
                sc.GetPersistentFloat(12) = (fType == 2) ? finalPR + sc.TickSize : finalPR - sc.TickSize; sc.GetPersistentInt(13) = finalIdxR;
                sc.PointerEventType = 0;
            }
        }
    }

    bool trailingUpdated = false; SCDateTime sA = sc.GetPersistentSCDateTime(10);
    for (int i = 0; i < pData->numSaved; i++) {
        SavedRetracement& r = pData->retracements[i]; int startScan = max(r.idxF, r.lastCheckedIdx);
        for (int j = startScan; j < sc.ArraySize; j++) { 
            if ((r.type == 1 && sc.High[j] > r.pF) || (r.type == 2 && sc.Low[j] < r.pF)) { 
                r.pF = (r.type == 1) ? sc.High[j] : sc.Low[j]; r.tF = sc.BaseDateTimeIn[j]; r.idxF = j; RefreshRetracementStrings(sc, r); trailingUpdated = true; 
                if (TState > 0 && fabs(r.tF.GetAsDouble() - sA.GetAsDouble()) < 0.0000001) { sc.GetPersistentFloat(10) = r.pF; sc.GetPersistentSCDateTime(10) = r.tF; }
            } 
        }
        r.lastCheckedIdx = sc.ArraySize - 1;
    }
    if (trailingUpdated) SaveDiNapoliData(sc, pData);

    // [REDRAW GATE & GPU GUARD]
    bool needsRedraw = (sc.ArraySize != LastSize) || (sc.IndexOfFirstVisibleBar != LastLeftIdx) || (currentGlobalHide != LastSyncValue) || sc.IsFullRecalculation || trailingUpdated || (TState > 0 && sc.PointerEventType == SC_POINTER_MOVE);
    bool cacheMissing = false;
    auto GetYAdjPr = [&](float p, bool isHigh) -> float {
        int pY = sc.RegionValueToYPixelCoordinate(p, sc.GraphRegion);
        if (pY == 0) { cacheMissing = true; return p; }
        return (float)sc.YPixelCoordinateToGraphValue(pY + (isHigh ? 0 : 1));
    };

    if (IsVisible && (needsRedraw || TState > 0)) {
        for (int k = sc.IndexOfFirstVisibleBar; k <= sc.IndexOfLastVisibleBar; k++) { sc.Subgraph[59][k] = 0; sc.Subgraph[60][k] = 0; }
        for (int i = 0; i < pData->numSaved; i++) {
            SavedRetracement& r = pData->retracements[i];
            bool isCurSes = (TState > 0 && fabs(r.tF.GetAsDouble() - sA.GetAsDouble()) < 0.0000001);
            if (!r.isVisibleOnCurrentTF && !isCurSes) continue;
            sc.Subgraph[i * 2 + 1][r.idxF] = GetYAdjPr(r.pF, (r.type == 1)); 
            sc.Subgraph[i * 2 + 2][r.idxR] = GetYAdjPr(r.pR, (r.type == 2));
            int idS = RETR_BASE_ID + (i * SLOT_SIZE), sX = sc.ArraySize + (r.step * r.dw100), connX = sX + r.dw100, tX = (sX + connX) / 2;
            float rng = r.pF - r.pR;
            auto L = [&](float p, float v, COLORREF c, const char* n, int id, const char* label, int textIdx, int lineStartIdx, int lineEndIdx, int width) {
                p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_LINE; p_T->BeginIndex = lineStartIdx; p_T->EndIndex = lineEndIdx; p_T->BeginValue = p; p_T->EndValue = p; p_T->Color = c; p_T->LineWidth = width; p_T->LineNumber = id; sc.UseTool(*p_T);
                p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_TEXT; p_T->BeginIndex = textIdx; p_T->BeginValue = p; p_T->Color = c; p_T->FontFace = "Arial"; p_T->FontSize = 8; p_T->FontBold = 1; p_T->Text = label; p_T->TextAlignment = DT_CENTER | DT_BOTTOM; p_T->LineNumber = id + 1; sc.UseTool(*p_T);
                p_T->Text = n; p_T->BeginValue = GetYAdjPr(v, true); p_T->TextAlignment = DT_CENTER | DT_TOP; p_T->LineNumber = id + 2; sc.UseTool(*p_T);
            };
            L(r.pR, r.pR, sc.Input[3].GetColor(), "1.000", idS + 3, r.s100, tX, r.idxR, connX, 0); 
            L(sc.RoundToTickSize(r.pF - rng * 0.382f), sc.RoundToTickSize(r.pF - rng * 0.382f), sc.Input[1].GetColor(), "0.382", idS + 6, r.s38, tX, sX, connX, 1);
            L(sc.RoundToTickSize(r.pF - rng * 0.618f), sc.RoundToTickSize(r.pF - rng * 0.618f), sc.Input[2].GetColor(), "0.618", idS + 9, r.s61, tX, sX, connX, 1);
            p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_LINE; p_T->LineStyle = LINESTYLE_DOT; p_T->BeginIndex = connX; p_T->EndIndex = connX; p_T->BeginValue = r.pR; p_T->EndValue = sc.RoundToTickSize(r.pF - rng * 0.382f); p_T->Color = RGB(160, 160, 160); p_T->LineNumber = idS + 12; sc.UseTool(*p_T);
        }
        if (TState == 2) {
            float lvsP = sc.GetPersistentFloat(12); int lvsI = sc.GetPersistentInt(13);
            SavedRetracement pr; memset(&pr, 0, sizeof(SavedRetracement)); 
            pr.pF = sc.GetPersistentFloat(10); pr.tF = sc.GetPersistentSCDateTime(10); pr.idxF = sc.GetContainingIndexForSCDateTime(sc.ChartNumber, pr.tF);
            pr.pR = lvsP; pr.idxR = lvsI; pr.type = sc.GetPersistentInt(11); pr.step = SessionSteps; RefreshRetracementStrings(sc, pr);
            sc.Subgraph[59][pr.idxF] = GetYAdjPr(pr.pF, (pr.type == 1)); sc.Subgraph[60][pr.idxR] = GetYAdjPr(pr.pR, (pr.type == 2));
            int sX = sc.ArraySize + (pr.step * pr.dw100), connX = sX + pr.dw100, tX = (sX + connX) / 2; float rng = pr.pF - pr.pR;
            auto L = [&](float p, float v, COLORREF c, const char* n, int id, const char* label, int textIdx, int lineStartIdx, int lineEndIdx) {
                p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_LINE; p_T->BeginIndex = lineStartIdx; p_T->EndIndex = lineEndIdx; p_T->BeginValue = p; p_T->EndValue = p; p_T->Color = c; p_T->LineWidth = 1; p_T->LineNumber = id; sc.UseTool(*p_T);
                p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_TEXT; p_T->BeginIndex = textIdx; p_T->BeginValue = p; p_T->Color = c; p_T->FontFace = "Arial"; p_T->FontSize = 8; p_T->FontBold = 1; p_T->Text = label; p_T->TextAlignment = DT_CENTER | DT_BOTTOM; p_T->LineNumber = id + 1; sc.UseTool(*p_T);
                p_T->Text = n; p_T->BeginValue = GetYAdjPr(v, true); p_T->TextAlignment = DT_CENTER | DT_TOP; p_T->LineNumber = id + 2; sc.UseTool(*p_T);
            };
            L(sc.RoundToTickSize(pr.pF - rng * 0.382f), sc.RoundToTickSize(pr.pF - rng * 0.382f), sc.Input[1].GetColor(), "0.382", 10000 + 6, pr.s38, tX, sX, connX);
            L(sc.RoundToTickSize(pr.pF - rng * 0.618f), sc.RoundToTickSize(pr.pF - rng * 0.618f), sc.Input[2].GetColor(), "0.618", 10000 + 9, pr.s61, tX, sX, connX);
            p_T->Clear(); p_T->AddMethod = UTAM_ADD_OR_ADJUST; p_T->DrawingType = DRAWING_LINE; p_T->LineStyle = LINESTYLE_DOT; p_T->BeginIndex = connX; p_T->EndIndex = connX; p_T->BeginValue = pr.pR; p_T->EndValue = sc.RoundToTickSize(pr.pF - rng * 0.382f); p_T->Color = RGB(160, 160, 160); p_T->LineNumber = 10000 + 12; sc.UseTool(*p_T);
        }
        LastSize = sc.ArraySize; LastLeftIdx = sc.IndexOfFirstVisibleBar; LastSyncValue = currentGlobalHide; if (cacheMissing) LastSize = -1;
    } else if (!IsVisible && LastSize != -2) {
        for(int id = 10000; id <= 10050; id++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, id);
        for(int i = 0; i < 20; i++) for(int j = 0; j < SLOT_SIZE; j++) sc.DeleteACSChartDrawing(sc.ChartNumber, 0, RETR_BASE_ID + (i * SLOT_SIZE) + j);
        for (int i = 1; i <= 60; i++) for (int k = 0; k < sc.ArraySize; k++) sc.Subgraph[i][k] = 0;
        LastSize = -2; LastSyncValue = currentGlobalHide;
    }
}
