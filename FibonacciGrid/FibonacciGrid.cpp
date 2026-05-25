#include "sierrachart.h"
#include <windows.h>
#include <cmath>

SCDLLName("FibonacciGrid")

// --- BEZPIECZEŃSTWO BINARNE: Płaska struktura fali z indywidualnymi parametrami stref ---
// --- STAŁE ARCHITEKTONICZNE SYNCHRONIZACJI ---
const int GLOBAL_SYNC_SLOT = 9999;

#pragma pack(push, 1)
struct SavedGrid {
    float priceA;
    float priceB;
    SCDateTime timeA;
    SCDateTime timeB;
    int idxA;
    int idxB;
    float calculatedPrices[10];
    int lineNumber;
    float customRatios[10];    // ETAP 3: Tablica rozszerzona do 10 stref
    COLORREF customColors[10]; // ETAP 3: Tablica rozszerzona do 10 stref
    int customUse[10];         // ETAP 3: Tablica rozszerzona do 10 stref
    int customWidth[10];       // ← ZMIANA: Grubość linii per poziom (1-3)
};

struct FibonacciPreset {  // ← ZMIANA
    float ratios[10];
    COLORREF colors[10];
    int use[10];
    int width[10];            // ← ZMIANA: Grubość linii per poziom (1-3)
};

struct FibonacciData {
    SavedGrid drawings[30]; 
    int numSaved;           
    int TState;
    int LastSize;
    int DraggingIdx;        
    int DraggedPoint;       
    int SelectedIdx;        
    unsigned int LastClickTime; // ETAP 3: Rejestr czasu dla asynchronicznego emulatora dwukliku
    
    // --- ETAP 1 HIDE: Rejestry magistrali synchronizacji i osłony GPU ---
    int LastSyncValue;          // Zapamiętany poprzedni stan ukrycia (detekcja zmiany impulsu)
    int StabCounter;            // Licznik klatek amortyzacji GPU dla bezpiecznego redrawu
    int LastLeftIdx;            // ← ZMIANA: Ostatni zapamiętany sc.IndexOfFirstVisibleBar (detekcja scrolla)
    FibonacciPreset presets[4]; // ← ZMIANA
    int activeConfig;           // ← ZMIANA
    HFONT hCachedFont;          // ← ZMIANA: cache GDI font (tworzony raz, żyje z pData)
    HPEN  hCachedDashPen;       // ← ZMIANA: cache GDI pen kropkowany
};
#pragma pack(pop)

// --- DYNAMICZNE WYSZUKIWANIE ID MENU (Pointer Proxy) ---
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

// --- FUNKCJA DOPASOWANIA (BAR-BASED RANGE) --- // ← ZMIANA
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

// --- SILNIK DOCIĄGANIA DO KNOTÓW (Snapping Engine) ---
int GetSnappedIndex(SCStudyInterfaceRef sc, int mouseIdx, int range, bool findHigh) {
    int startIdx = mouseIdx - range;
    if (startIdx < 0) startIdx = 0;
    
    int endIdx = mouseIdx + range;
    if (endIdx >= sc.ArraySize) endIdx = sc.ArraySize - 1;

    int bestIdx = mouseIdx;
    float bestVal = findHigh ? sc.High[mouseIdx] : sc.Low[mouseIdx];
    
    for (int i = startIdx; i <= endIdx; i++) {
        float currentVal = findHigh ? sc.High[i] : sc.Low[i];
        if (findHigh) {
            if (currentVal > bestVal) { 
                bestVal = currentVal; 
                bestIdx = i; 
            }
        } else {
            if (currentVal < bestVal) { 
                bestVal = currentVal; 
                bestIdx = i; 
            }
        }
    }
    return bestIdx;
}

// --- HELPER: Tworzenie lub aktualizacja natywnej linii grzbietu (SpineLine) --- // ← ZMIANA
int ApplySpineLine(SCStudyInterfaceRef sc, int lineNumber, int idxA, float priceA, int idxB, float priceB) { // ← ZMIANA
    s_UseTool SpineLine;
    SpineLine.Clear();
    SpineLine.ChartNumber = sc.ChartNumber;
    SpineLine.DrawingType = DRAWING_LINE;
    SpineLine.LineNumber = lineNumber;
    SpineLine.AddMethod = UTAM_ADD_OR_ADJUST;
    SpineLine.AddAsUserDrawnDrawing = 0;
    SpineLine.BeginIndex = idxA;
    SpineLine.BeginValue = priceA;
    SpineLine.EndIndex = idxB;
    SpineLine.EndValue = priceB;
    SpineLine.Color = RGB(0, 0, 255);
    SpineLine.LineStyle = LINESTYLE_DOT;
    SpineLine.LineWidth = 1;
    sc.UseTool(SpineLine);
    return SpineLine.LineNumber; // ← ZMIANA: zwraca przydzielony numer (ważne dla nowych linii)
}

// --- HELPER: Ścieżka do pliku danych wykresu --- // ← ZMIANA
void GetDataFilePath(SCStudyInterfaceRef sc, char* buf) { // ← ZMIANA
    sprintf(buf, "%sFibGrid_%d_%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID); // ← ZMIANA
}

// --- HELPER: Ścieżka do globalnego pliku presetów --- // ← ZMIANA
void GetPresetsFilePath(SCStudyInterfaceRef sc, char* buf) { // ← ZMIANA
    sprintf(buf, "%sFibGridPresets.dat", sc.DataFilesFolder().GetChars()); // ← ZMIANA
}

// --- ETAP 3: PROGRAMOWA PROCEDURA OKNA MODALNEGO (WYGODA Z TRADE NAVIGATORA) ---

// ← ZMIANA: Kontener danych przekazywanych do okna modalnego
struct ModalUserData {
    SavedGrid*     pGrid;          // wskaźnik na rysunek (kolory modyfikowane on-the-fly)
    FibonacciData* pData;          // dostęp do globalnych presetów
    SavedGrid      original;       // snapshot przy otwarciu — dla Cancel i CURRENT
    int            selectedConfig; // 0-3 = Config 1-4,  4 = CURRENT
    char           presetsPath[512]; // ← ZMIANA: ścieżka do globalnego pliku presetów
    HFONT          hFont;           // ← ZMIANA: Light Mode — Segoe UI
    HFONT          hFontBold;       // ← ZMIANA: Antracyt header — Segoe UI Bold
    HBRUSH         hBrushParchment; // ← ZMIANA: Light Mode — tło pól EDIT + checkboxów (nieparzyste wiersze)
    HBRUSH         hBrushWhite;     // ← ZMIANA: Zebra — alabastrowe tło parzystych wierszy
    HBRUSH         hBrushWindow;    // ← ZMIANA: Light Mode — tło okna + radio/GroupBox
};

LRESULT CALLBACK ModalConfigWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ModalUserData* pUD = (ModalUserData*)GetWindowLongPtrA(hWnd, GWLP_USERDATA); // ← ZMIANA

    switch (message) {

case WM_CREATE: {
        CREATESTRUCTA* pCreate = (CREATESTRUCTA*)lParam;
        pUD = (ModalUserData*)pCreate->lpCreateParams; // ← ZMIANA
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)pUD);

        SavedGrid* pGrid = pUD->pGrid; // ← ZMIANA: lokalny alias dla czytelności

        // ← ZMIANA: BS_GROUPBOX usunięty — zastąpiony flat panelem rysowanym w WM_PAINT
        // ← ZMIANA: Current w=68, P1-P4 w=50; marginesy zewnętrzne 15px po obu stronach panelu (symetria)
        HWND hCurr = ::CreateWindowA("BUTTON", "Current", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            30, 347, 68, 20, hWnd, (HMENU)(UINT_PTR)304, NULL, NULL);
        ::CreateWindowA("BUTTON", "P1", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            115, 347, 50, 20, hWnd, (HMENU)(UINT_PTR)300, NULL, NULL); // ← ZMIANA: x=111→115
        ::CreateWindowA("BUTTON", "P2", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            182, 347, 50, 20, hWnd, (HMENU)(UINT_PTR)301, NULL, NULL); // ← ZMIANA: x=174→182
        ::CreateWindowA("BUTTON", "P3", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            249, 347, 50, 20, hWnd, (HMENU)(UINT_PTR)302, NULL, NULL); // ← ZMIANA: x=237→249
        ::CreateWindowA("BUTTON", "P4", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            316, 347, 48, 20, hWnd, (HMENU)(UINT_PTR)303, NULL, NULL); // ← ZMIANA: x=316, w=48 (koniec x=364, nie zakrywa prawej krawędzi panelu)
        ::SendMessageA(hCurr, BM_SETCHECK, BST_CHECKED, 0); // domyślnie Current

// ← ZMIANA: Tabela od gT=10; checkbox wyśrodkowany w Use(15..60), Edit wyśrodkowany w Ratio(60..215)
        for (int i = 0; i < 10; i++) {
            HWND hCheck = ::CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                31, 37 + i * 26 + 3, 20, 20, hWnd, (HMENU)(UINT_PTR)(100 + i), NULL, NULL); // ← ZMIANA: x=31 centruje 13px kwadrat w kolumnie Use
            SendMessageA(hCheck, BM_SETCHECK, pGrid->customUse[i] ? BST_CHECKED : BST_UNCHECKED, 0);

            char buf[32];
            sprintf(buf, "%g", pGrid->customRatios[i]);
            ::CreateWindowA("EDIT", buf, WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_AUTOHSCROLL, // ← ZMIANA: ES_CENTER→ES_RIGHT (wyrównanie liczb do prawej)
                100, 37 + i * 26 + 3, 58, 20, hWnd, (HMENU)(UINT_PTR)(200 + i), NULL, NULL); // ← ZMIANA: w=75→58 (mieści się w col1..col2=165)

            char wbuf[4];
            int w = pGrid->customWidth[i];
            sprintf(wbuf, "%d", (w >= 1 && w <= 3) ? w : 1);
            ::CreateWindowA("EDIT", wbuf, WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER, // ← ZMIANA: kolumna Width
                175, 37 + i * 26 + 3, 65, 20, hWnd, (HMENU)(UINT_PTR)(210 + i), NULL, NULL); // ← ZMIANA: x=175,w=65 — wyśrodkowany w col2(165)..col3(250)
        }

        // ← ZMIANA: Flat buttons — BS_OWNERDRAW; rysowanie przez WM_DRAWITEM
        // ← ZMIANA: Reset na x=15 (pierwsza akcja), Overwrite na x=106 (rzadziej używane)
        ::CreateWindowA("BUTTON", "Reset", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            15, 390, 76, 25, hWnd, (HMENU)(UINT_PTR)401, NULL, NULL);

        HWND hNadpisz = ::CreateWindowA("BUTTON", "Overwrite", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            106, 390, 76, 25, hWnd, (HMENU)(UINT_PTR)400, NULL, NULL);
        ::EnableWindow(hNadpisz, FALSE);

        ::CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            197, 390, 76, 25, hWnd, (HMENU)(UINT_PTR)IDCANCEL, NULL, NULL);
        ::CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            288, 390, 76, 25, hWnd, (HMENU)(UINT_PTR)IDOK, NULL, NULL);

        // ← ZMIANA: Light Mode — font Segoe UI + pędzle tła; persist przez cały czas życia okna
        pUD->hFont     = ::CreateFontA(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, "Segoe UI");
        pUD->hFontBold = ::CreateFontA(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, // ← ZMIANA: Stalowy Niebieski — bold dla nagłówka
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, "Segoe UI");
        pUD->hBrushParchment = ::CreateSolidBrush(RGB(255, 255, 225));
        pUD->hBrushWhite     = ::CreateSolidBrush(RGB(255, 255, 255)); // ← ZMIANA: Zebra — czysta biel (nie szarość)
        pUD->hBrushWindow    = ::CreateSolidBrush(RGB(240, 240, 240)); // ← ZMIANA: spójność z Sierra Chart
        if (pUD->hFont) {
            HWND hChild = ::GetWindow(hWnd, GW_CHILD);
            while (hChild) {
                ::SendMessageA(hChild, WM_SETFONT, (WPARAM)pUD->hFont, FALSE);
                hChild = ::GetWindow(hChild, GW_HWNDNEXT);
            }
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = ::BeginPaint(hWnd, &ps);

        // ← ZMIANA: Stałe geometrii tabeli — nowy layout 380×490
        const int gL   = 15;   // lewa krawędź (margines 15px)
        const int gR   = 365;  // prawa krawędź (gL+350)
        const int gT   = 15;   // górna krawędź (nagłówek) // ← ZMIANA: 10→15 (margines od title bar)
        const int gHdr = 37;   // dolna krawędź nagłówka / górna krawędź danych (gT+22) // ← ZMIANA
        const int gB   = gHdr + 10 * 26; // dolna krawędź (= 297) // ← ZMIANA
        const int col1 = 60;   // separator Use | Ratio (gL+45)
        const int col2 = 165;  // ← ZMIANA: separator Ratio | Width (col1+105)
        const int col3 = 250;  // ← ZMIANA: separator Width | Color (col2+85) — poszerzona kolumna Width

        // ← ZMIANA: Zebra — naprzemienne tło wierszy (pergamin / alabaster)
        if (pUD && pUD->hBrushParchment && pUD->hBrushWhite) {
            for (int i = 0; i < 10; i++) {
                int yRow = gHdr + i * 26;
                RECT rRow = { gL + 1, yRow, gR, yRow + 26 };
                ::FillRect(hdc, &rRow, (i % 2 == 0) ? pUD->hBrushParchment : pUD->hBrushWhite);
            }
        }

        // ← ZMIANA: Stalowy Niebieski header — spójność z liniami Fibo i węzłami na wykresie
        HBRUSH hHdrBrush = ::CreateSolidBrush(RGB(52, 88, 148));
        RECT rHdrFill = { gL, gT, gR, gHdr };
        ::FillRect(hdc, &rHdrFill, hHdrBrush);
        ::DeleteObject(hHdrBrush);

        // ← ZMIANA: Light Mode — jasnoszare linie siatki
        HPEN hPenGrid = ::CreatePen(PS_SOLID, 1, RGB(218, 222, 229));
        HPEN hOldPen  = (HPEN)::SelectObject(hdc, hPenGrid);

        // Obramowanie zewnętrzne
        ::MoveToEx(hdc, gL, gT, NULL); ::LineTo(hdc, gR, gT);
        ::MoveToEx(hdc, gL, gB, NULL); ::LineTo(hdc, gR, gB);
        ::MoveToEx(hdc, gL, gT, NULL); ::LineTo(hdc, gL, gB);
        ::MoveToEx(hdc, gR, gT, NULL); ::LineTo(hdc, gR, gB);

        // Linia pod nagłówkiem
        ::MoveToEx(hdc, gL, gHdr, NULL); ::LineTo(hdc, gR, gHdr);

        // Pionowe separatory kolumn
        ::MoveToEx(hdc, col1, gT, NULL); ::LineTo(hdc, col1, gB);
        ::MoveToEx(hdc, col2, gT, NULL); ::LineTo(hdc, col2, gB);
        ::MoveToEx(hdc, col3, gT, NULL); ::LineTo(hdc, col3, gB); // ← ZMIANA: separator Width | Color

        // Poziome linie między wierszami danych
        for (int i = 1; i < 10; i++) {
            int lineY = gHdr + i * 26; // ← ZMIANA
            ::MoveToEx(hdc, gL, lineY, NULL); ::LineTo(hdc, gR, lineY);
        }

        ::SelectObject(hdc, hOldPen);
        ::DeleteObject(hPenGrid);

        // ← ZMIANA: Biały bold tekst nagłówków na antracycie
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, RGB(255, 255, 255));
        HFONT hOldHdrFont = (pUD && pUD->hFontBold)
            ? (HFONT)::SelectObject(hdc, (HGDIOBJ)pUD->hFontBold) : NULL;
        RECT rHdrUse   = { gL,   gT, col1, gHdr };
        RECT rHdrRatio = { col1, gT, col2, gHdr };
        RECT rHdrWidth = { col2, gT, col3, gHdr }; // ← ZMIANA: nowy nagłówek Width
        RECT rHdrColor = { col3, gT, gR,   gHdr }; // ← ZMIANA: Color przesunięty na col3
        ::DrawTextA(hdc, "Use",   -1, &rHdrUse,   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::DrawTextA(hdc, "Ratio", -1, &rHdrRatio, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::DrawTextA(hdc, "Width", -1, &rHdrWidth, DT_CENTER | DT_VCENTER | DT_SINGLELINE); // ← ZMIANA
        ::DrawTextA(hdc, "Color", -1, &rHdrColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (hOldHdrFont) ::SelectObject(hdc, hOldHdrFont);
        ::SetTextColor(hdc, ::GetSysColor(COLOR_WINDOWTEXT)); // reset koloru tekstu

        if (pUD && pUD->pGrid) {
            // ← ZMIANA: Próbki koloru — przesunięte do kolumny col3..gR (230..365), swatch 50px wyśrodkowany
            for (int i = 0; i < 10; i++) {
                HBRUSH hBrush = ::CreateSolidBrush(pUD->pGrid->customColors[i]);
                RECT r = { 282, 37 + i * 26 + 3, 332, 37 + i * 26 + 23 }; // ← ZMIANA: centrum kolumny 250..365 (32px margines po każdej stronie)
                ::FillRect(hdc, &r, hBrush);
                ::DeleteObject(hBrush);
                // ← ZMIANA: 1px ciemna ramka wokół swatcha — widoczność jasnych kolorów (np. biały)
                HPEN hPenFrame = ::CreatePen(PS_SOLID, 1, RGB(100, 102, 112));
                HPEN hOldPenF  = (HPEN)::SelectObject(hdc, hPenFrame);
                HBRUSH hOldBrF = (HBRUSH)::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
                ::Rectangle(hdc, r.left, r.top, r.right, r.bottom);
                ::SelectObject(hdc, hOldPenF);
                ::SelectObject(hdc, hOldBrF);
                ::DeleteObject(hPenFrame);
            }
        }
        // ← ZMIANA: Flat panel profili — zastępuje BS_GROUPBOX; rysowany pod radio buttonami
        {
            HPEN hPP = ::CreatePen(PS_SOLID, 1, RGB(200, 202, 208));
            HPEN hOP = (HPEN)::SelectObject(hdc, hPP);
            ::MoveToEx(hdc, 15,  335, NULL); ::LineTo(hdc, 365, 335); // top
            ::MoveToEx(hdc, 15,  375, NULL); ::LineTo(hdc, 365, 375); // bottom
            ::MoveToEx(hdc, 15,  335, NULL); ::LineTo(hdc,  15, 375); // left
            ::MoveToEx(hdc, 365, 335, NULL); ::LineTo(hdc, 365, 375); // right
            ::SelectObject(hdc, hOP);
            ::DeleteObject(hPP);
            if (pUD && pUD->hBrushWindow) {            // wycięcie krawędzi dla tytułu
                // ← ZMIANA: cutout wyśrodkowany w panelu (15..365), centrum x=190, szer.~176px
                // Lewa linia widoczna: x=15..102, prawa: x=278..365 — obramowanie dochodzi do tekstu z obu stron
                RECT rCut = { 102, 329, 278, 342 };
                ::FillRect(hdc, &rCut, pUD->hBrushWindow);
            }
            ::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, RGB(100, 102, 112));
            RECT rTitle = { 102, 328, 278, 342 }; // ← ZMIANA: wyśrodkowany, centrum x=190
            ::DrawTextA(hdc, "Active Profile & Storage", -1, &rTitle,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            ::SetTextColor(hdc, ::GetSysColor(COLOR_WINDOWTEXT));
        }
        ::EndPaint(hWnd, &ps);
        break;
    }

    // ← ZMIANA: Light Mode — tło okna zamiast systemowego szarego
    case WM_ERASEBKGND: {
        if (pUD && pUD->hBrushWindow) {
            RECT rc;
            ::GetClientRect(hWnd, &rc);
            ::FillRect((HDC)wParam, &rc, pUD->hBrushWindow);
            return 1;
        }
        return ::DefWindowProcA(hWnd, message, wParam, lParam);
    }

    // ← ZMIANA: Zebra — naprzemienne tło pól EDIT (pergamin/alabaster wg indeksu wiersza)
    case WM_CTLCOLOREDIT: {
        if (pUD) {
            int ctrlId = ::GetDlgCtrlID((HWND)lParam);
            HDC hdcCtrl = (HDC)wParam;
            // Obsługa Ratio (200-209) i Width (210-219) — ta sama logika zebry
            int rowIdx = -1;
            if (ctrlId >= 200 && ctrlId <= 209) rowIdx = ctrlId - 200;
            else if (ctrlId >= 210 && ctrlId <= 219) rowIdx = ctrlId - 210; // ← ZMIANA: Width
            if (rowIdx >= 0) {
                bool isEven = (rowIdx % 2 == 0);
                if (isEven && pUD->hBrushParchment) {
                    ::SetBkColor(hdcCtrl, RGB(255, 255, 225));
                    ::SetTextColor(hdcCtrl, RGB(40, 40, 50));
                    return (LRESULT)pUD->hBrushParchment;
                } else if (!isEven && pUD->hBrushWhite) {
                    ::SetBkColor(hdcCtrl, RGB(255, 255, 255));
                    ::SetTextColor(hdcCtrl, RGB(40, 40, 50));
                    return (LRESULT)pUD->hBrushWhite;
                }
            }
        }
        return ::DefWindowProcA(hWnd, message, wParam, lParam);
    }

    // ← ZMIANA: Zebra — tło checkboxów (pergamin/alabaster wg wiersza) i radio (tło okna)
    case WM_CTLCOLORSTATIC: {
        if (pUD) {
            int ctrlId = ::GetDlgCtrlID((HWND)lParam);
            HDC hdcCtrl = (HDC)wParam;
            if (ctrlId >= 100 && ctrlId <= 109) {
                bool isEven = ((ctrlId - 100) % 2 == 0);
                if (isEven && pUD->hBrushParchment) {
                    ::SetBkColor(hdcCtrl, RGB(255, 255, 225));
                    ::SetTextColor(hdcCtrl, RGB(40, 40, 50));
                    return (LRESULT)pUD->hBrushParchment;
                } else if (!isEven && pUD->hBrushWhite) {
                    ::SetBkColor(hdcCtrl, RGB(255, 255, 255));
                    ::SetTextColor(hdcCtrl, RGB(40, 40, 50));
                    return (LRESULT)pUD->hBrushWhite;
                }
            }
            if (pUD->hBrushWindow) {
                ::SetBkColor(hdcCtrl, RGB(240, 240, 240)); // ← ZMIANA: spójność z tłem okna
                ::SetTextColor(hdcCtrl, RGB(40, 40, 50));
                return (LRESULT)pUD->hBrushWindow;
            }
        }
        return ::DefWindowProcA(hWnd, message, wParam, lParam);
    }

    // ← ZMIANA: Flat buttons — pełne własne renderowanie (brak 3D Win98)
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* pDIS = (DRAWITEMSTRUCT*)lParam;
        if (!pDIS || pDIS->CtlType != ODT_BUTTON) break;
        HDC  hdc = pDIS->hDC;
        RECT rc  = pDIS->rcItem;
        bool isDisabled = (pDIS->itemState & ODS_DISABLED) != 0;
        bool isPressed  = (pDIS->itemState & ODS_SELECTED) != 0;
        bool isOK       = (pDIS->CtlID == IDOK);

        COLORREF bg, fg, border;
        if (isDisabled) {
            bg = RGB(235,237,242); fg = RGB(160,160,160); border = RGB(210,213,220);
        } else if (isOK) {
            bg     = isPressed ? RGB(35,65,120)  : RGB(52,88,148);
            fg     = RGB(255,255,255);
            border = RGB(35,65,120);
        } else {
            bg     = isPressed ? RGB(210,213,220) : RGB(235,237,242);
            fg     = RGB(40,40,50);
            border = RGB(190,193,200);
        }

        HBRUSH hBr = ::CreateSolidBrush(bg);
        ::FillRect(hdc, &rc, hBr);
        ::DeleteObject(hBr);

        HPEN hPen = ::CreatePen(PS_SOLID, 1, border);
        HPEN hOld = (HPEN)::SelectObject(hdc, hPen);
        ::MoveToEx(hdc, rc.left,     rc.top,      NULL);
        ::LineTo  (hdc, rc.right-1,  rc.top);
        ::LineTo  (hdc, rc.right-1,  rc.bottom-1);
        ::LineTo  (hdc, rc.left,     rc.bottom-1);
        ::LineTo  (hdc, rc.left,     rc.top);
        ::SelectObject(hdc, hOld);
        ::DeleteObject(hPen);

        char buf[64] = {};
        ::GetWindowTextA(pDIS->hwndItem, buf, 63);
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, fg);
        HFONT hOldFont = pUD && pUD->hFont
            ? (HFONT)::SelectObject(hdc, (HGDIOBJ)pUD->hFont) : NULL;
        ::DrawTextA(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (hOldFont) ::SelectObject(hdc, hOldFont);

        if (pDIS->itemState & ODS_FOCUS) {
            RECT rf = { rc.left+3, rc.top+3, rc.right-4, rc.bottom-4 };
            ::DrawFocusRect(hdc, &rf);
        }
        return TRUE;
    }

case WM_LBUTTONDOWN: {
        if (!pUD || !pUD->pGrid) break; // ← ZMIANA
        SavedGrid* pGrid = pUD->pGrid;  // ← ZMIANA: lokalny alias
        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);

        // Hit-Testing: kolumna Color x=272..322, wiersze y=37+i*26+3..+23 // ← ZMIANA: nowa pozycja swatch
        if (mouseX >= 282 && mouseX <= 332) { // ← ZMIANA: centrum kolumny col3(250)..gR(365)
            for (int i = 0; i < 10; i++) {
                int yTop = 37 + i * 26 + 3; // ← ZMIANA
                int yBottom = yTop + 20;

                // Sprawdzamy pionowy przedział dla każdego z 10 poziomów
                if (mouseY >= yTop && mouseY <= yBottom) {
                    CHOOSECOLORA cc;
                    static COLORREF acrCustClr[16];
                    ZeroMemory(&cc, sizeof(cc));
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hWnd;
                    cc.lpCustColors = (LPDWORD)acrCustClr;
                    cc.rgbResult = pGrid->customColors[i];
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

                    // Pancerne wywołanie palety systemowej przez LoadLibrary (Fortyfikacja Linkera Sierry)
                    HMODULE hComDlg = ::LoadLibraryA("comdlg32.dll");
                    if (hComDlg) {
                        typedef BOOL (WINAPI* LPFNCHOOSECOLORA)(CHOOSECOLORA*);
                        LPFNCHOOSECOLORA pChooseColorA = (LPFNCHOOSECOLORA)::GetProcAddress(hComDlg, "ChooseColorA");
                        if (pChooseColorA && pChooseColorA(&cc)) {
                            pGrid->customColors[i] = cc.rgbResult;
                            ::InvalidateRect(hWnd, NULL, TRUE); // Wymuszenie komunikatu WM_PAINT dla odświeżenia próbki koloru
                        }
                        ::FreeLibrary(hComDlg);
                    }
                    break; // Cel osiągnięty, przerywamy skanowanie pionowe
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        if (!pUD || !pUD->pGrid) return ::DefWindowProcA(hWnd, message, wParam, lParam); // ← ZMIANA
        SavedGrid*     pGrid = pUD->pGrid; // ← ZMIANA: lokalny alias
        FibonacciData* pData = pUD->pData; // ← ZMIANA: lokalny alias
        int wmId = LOWORD(wParam);

        // ← ZMIANA: Obsługa radio buttonów — ładowanie presetu do kontrolek
        if (wmId >= 300 && wmId <= 303) {
            int cfg = wmId - 300;
            pUD->selectedConfig = cfg;
            ::EnableWindow(::GetDlgItem(hWnd, 400), TRUE);
            for (int i = 0; i < 10; i++) {
                ::SendMessageA(::GetDlgItem(hWnd, 100 + i), BM_SETCHECK,
                    pData->presets[cfg].use[i] ? BST_CHECKED : BST_UNCHECKED, 0);
                char buf[32];
                sprintf(buf, "%g", pData->presets[cfg].ratios[i]);
                ::SetWindowTextA(::GetDlgItem(hWnd, 200 + i), buf);
                pGrid->customColors[i] = pData->presets[cfg].colors[i];
                // ← ZMIANA: Załadowanie Width z presetu
                char wbuf[4];
                int pw = pData->presets[cfg].width[i];
                sprintf(wbuf, "%d", (pw >= 1 && pw <= 3) ? pw : 1);
                ::SetWindowTextA(::GetDlgItem(hWnd, 210 + i), wbuf);
            }
            ::InvalidateRect(hWnd, NULL, TRUE);
        }
        // ← ZMIANA: Powrót do CURRENT — przywrócenie snapshot
        else if (wmId == 304) {
            pUD->selectedConfig = 4;
            ::EnableWindow(::GetDlgItem(hWnd, 400), FALSE);
            for (int i = 0; i < 10; i++) {
                ::SendMessageA(::GetDlgItem(hWnd, 100 + i), BM_SETCHECK,
                    pUD->original.customUse[i] ? BST_CHECKED : BST_UNCHECKED, 0);
                char buf[32];
                sprintf(buf, "%g", pUD->original.customRatios[i]);
                ::SetWindowTextA(::GetDlgItem(hWnd, 200 + i), buf);
                pGrid->customColors[i] = pUD->original.customColors[i];
                // ← ZMIANA: Przywrócenie Width z snapshotu
                char wbuf[4];
                int ow = pUD->original.customWidth[i];
                sprintf(wbuf, "%d", (ow >= 1 && ow <= 3) ? ow : 1);
                ::SetWindowTextA(::GetDlgItem(hWnd, 210 + i), wbuf);
            }
            ::InvalidateRect(hWnd, NULL, TRUE);
        }
        // ← ZMIANA: Nadpisz — zapis stanu kontrolek do globalnego presetu
        else if (wmId == 400) {
            int cfg = pUD->selectedConfig;
            if (cfg >= 0 && cfg <= 3) {
                for (int i = 0; i < 10; i++) {
                    pData->presets[cfg].use[i]    = (::SendMessageA(::GetDlgItem(hWnd, 100 + i), BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                    char buf[64];
                    ::GetWindowTextA(::GetDlgItem(hWnd, 200 + i), buf, 64);
                    pData->presets[cfg].ratios[i] = (float)atof(buf);
                    pData->presets[cfg].colors[i] = pGrid->customColors[i];
                    // ← ZMIANA: Zapis Width do presetu
                    char wbuf[8];
                    ::GetWindowTextA(::GetDlgItem(hWnd, 210 + i), wbuf, 8);
                    int pw = atoi(wbuf);
                    if (pw < 1) pw = 1; if (pw > 3) pw = 3;
                    pData->presets[cfg].width[i] = pw;
                }
                // ← ZMIANA: Utrwalenie wszystkich 4 presetów do globalnego pliku
                FILE* fp = fopen(pUD->presetsPath, "wb");
                if (fp) {
                    fwrite(pData->presets, sizeof(FibonacciPreset), 4, fp);
                    fclose(fp);
                }
            }
            // Modal zostaje otwarty
        }
        // ← ZMIANA: RESET — przywraca hardkodowane defaults; dla C1-C4 zapisuje do pliku od razu
        else if (wmId == 401) {
            static const float defRatios[10]    = {0.000f,0.250f,0.382f,0.500f,0.618f,0.700f,0.786f,0.850f,0.886f,1.000f};
            static const COLORREF defColors[10] = {
                RGB(0,0,255), RGB(255,0,0), RGB(255,0,0), RGB(0,0,255), RGB(255,0,0),
                RGB(0,0,0),   RGB(255,0,0), RGB(255,0,0), RGB(128,128,128), RGB(0,0,255)
            };
            static const int defUse[4][10] = {
                {1,0,0,0,1,1,1,0,1,1},
                {1,0,1,1,0,0,0,0,0,1},
                {1,1,0,1,0,0,0,0,0,1},
                {1,0,1,0,0,0,0,0,0,1},
            };
            int cfg    = pUD->selectedConfig;
            int useCfg = (cfg >= 0 && cfg <= 3) ? cfg : 0;
            for (int i = 0; i < 10; i++) {
                ::SendMessageA(::GetDlgItem(hWnd, 100 + i), BM_SETCHECK,
                    defUse[useCfg][i] ? BST_CHECKED : BST_UNCHECKED, 0);
                char buf[32];
                sprintf(buf, "%g", defRatios[i]);
                ::SetWindowTextA(::GetDlgItem(hWnd, 200 + i), buf);
                pGrid->customColors[i] = defColors[i];
                char wResetBuf[4];
                sprintf(wResetBuf, "%d", (i == 0 || i == 9) ? 2 : 1);
                ::SetWindowTextA(::GetDlgItem(hWnd, 210 + i), wResetBuf); // ← ZMIANA: 0.000 i 1.000 → width=2
            }
            if (cfg >= 0 && cfg <= 3) {
                for (int i = 0; i < 10; i++) {
                    pData->presets[cfg].use[i]    = defUse[cfg][i];
                    pData->presets[cfg].ratios[i] = defRatios[i];
                    pData->presets[cfg].colors[i] = defColors[i];
                    pData->presets[cfg].width[i]  = (i == 0 || i == 9) ? 2 : 1; // ← ZMIANA: 0.000 i 1.000 → width=2
                }
                FILE* fp = fopen(pUD->presetsPath, "wb");
                if (fp) {
                    fwrite(pData->presets, sizeof(FibonacciPreset), 4, fp);
                    fclose(fp);
                }
            }
            ::InvalidateRect(hWnd, NULL, TRUE);
        }
        else if (wmId == IDOK) {
            // Zrzucenie 10 rekordów z kontrolek Win32 do bufora RAM
            for (int i = 0; i < 10; i++) {
                HWND hCheck = ::GetDlgItem(hWnd, 100 + i);
                pGrid->customUse[i] = (::SendMessageA(hCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

                HWND hEdit = ::GetDlgItem(hWnd, 200 + i);
                char buf[64];
                ::GetWindowTextA(hEdit, buf, 64);
                pGrid->customRatios[i] = (float)atof(buf);

                // ← ZMIANA: Odczyt Width i zapis do rysunku
                char wbuf[8];
                ::GetWindowTextA(::GetDlgItem(hWnd, 210 + i), wbuf, 8);
                int w = atoi(wbuf);
                if (w < 1) w = 1; if (w > 3) w = 3;
                pGrid->customWidth[i] = w;
            }
            ::DestroyWindow(hWnd);
        }
        else if (wmId == IDCANCEL) {
            // ← ZMIANA: Przywrócenie snapshot — rysunek niezmieniony
            memcpy(pGrid, &pUD->original, sizeof(SavedGrid));
            ::DestroyWindow(hWnd);
        }
        break;
    }


case WM_DESTROY:
        // NAPRAWA ETAPU 4: Usunięto PostQuitMessage(0) - to ono bezprawnie pożerało wątek główny jądra Sierra Chart!
        if (pUD) { // ← ZMIANA: Light Mode — zwolnienie zasobów GDI
            if (pUD->hFont)           { ::DeleteObject(pUD->hFont);           pUD->hFont = NULL; }
            if (pUD->hFontBold)       { ::DeleteObject(pUD->hFontBold);       pUD->hFontBold = NULL; }       // ← ZMIANA
            if (pUD->hBrushParchment) { ::DeleteObject(pUD->hBrushParchment); pUD->hBrushParchment = NULL; }
            if (pUD->hBrushWhite)     { ::DeleteObject(pUD->hBrushWhite);     pUD->hBrushWhite = NULL; }     // ← ZMIANA
            if (pUD->hBrushWindow)    { ::DeleteObject(pUD->hBrushWindow);    pUD->hBrushWindow = NULL; }
        }
        break;

    default:
        return ::DefWindowProcA(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Funkcja zarządzająca wstrzykiwaniem okna i blokowaniem wątku nadrzędnego (Trwały Tryb Modalny)
void ShowModalConfig(HWND hParent, SavedGrid& grid, FibonacciData* pData, const char* presetsPath) { // ← ZMIANA: dodano presetsPath
    static bool classRegistered = false; // ← ZMIANA: rejestracja klasy Win32 tylko raz przez cały czas życia DLL

    // ← ZMIANA: Kontener danych modalnych — żyje na stosie przez całą pętlę msgloop
    ModalUserData userData;
    userData.pGrid          = &grid;
    userData.pData          = pData;
    userData.selectedConfig = 4; // domyślnie CURRENT
    memcpy(&userData.original, &grid, sizeof(SavedGrid)); // snapshot dla Cancel/CURRENT
    strncpy(userData.presetsPath, presetsPath, 511); // ← ZMIANA
    userData.presetsPath[511] = '\0';               // ← ZMIANA
    userData.hFont           = NULL;                // ← ZMIANA: init Light Mode handles
    userData.hFontBold       = NULL;               // ← ZMIANA
    userData.hBrushParchment = NULL;
    userData.hBrushWhite     = NULL;               // ← ZMIANA
    userData.hBrushWindow    = NULL;

    if (!classRegistered) { // ← ZMIANA: guard — rejestracja tylko przy pierwszym wywołaniu
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = ModalConfigWndProc;
        wc.hInstance = ::GetModuleHandleA(NULL);
        wc.hCursor = ::LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "FibGridConfigClass";
        ::RegisterClassA(&wc);
        classRegistered = true; // ← ZMIANA
    } // ← ZMIANA

HWND hwndDlg = ::CreateWindowExA(WS_EX_DLGMODALFRAME, "FibGridConfigClass", "Properties (Config / 10 Levels)",
        WS_POPUPWINDOW | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 400, 460, hParent, NULL, ::GetModuleHandleA(NULL), &userData); // ← ZMIANA: h=455→460 (gap GroupBox↔buttons +5px)

    if (!hwndDlg) return;

    // Centrowanie geometryczne okna właściwości względem środka wykresu Sierra Chart
    RECT rectP, rectC;
    ::GetWindowRect(hParent, &rectP);
    ::GetWindowRect(hwndDlg, &rectC);
    int x = rectP.left + (rectP.right - rectP.left - (rectC.right - rectC.left)) / 2;
    int y = rectP.top + (rectP.bottom - rectP.top - (rectC.bottom - rectC.top)) / 2;
    ::SetWindowPos(hwndDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ::EnableWindow(hParent, FALSE); // ZAMROŻENIE SIERRY: Pełna blokada modalna
    ::ShowWindow(hwndDlg, SW_SHOW);
    ::UpdateWindow(hwndDlg);

    MSG msg;
    while (::IsWindow(hwndDlg)) {
        if (::PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageA(&msg);
        } else {
            ::Sleep(5);
        }
    }
    ::EnableWindow(hParent, TRUE); // ODBLOKOWANIE SIERRY po zamknięciu okna
    ::SetFocus(hParent);
}

// --- ETAP 5: SILNIK RENDERUJĄCY GDI (Czyste Win32 API) ---
void DrawFibonacciGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc) {
    FibonacciData* pData = (FibonacciData*)sc.GetPersistentPointer(500);
    if (!pData) return;

    // --- ETAP 4 HIDE: Tarcza Wydajnościowa GDI (Odcięcie potoku GPU ze Slotu 5) ---
    bool effectiveIsVisible = (sc.GetPersistentInt(5) == 1);
    if (!effectiveIsVisible) return;

// Kapsułkowanie zasobów GDI - Lazy-init piór i czcionki (cached w pData, żyją przez cały cykl życia wskaźnika)
    if (!pData->hCachedDashPen) { // ← ZMIANA: tworzone tylko raz
        pData->hCachedDashPen = ::CreatePen(PS_DOT, 1, RGB(0, 0, 255));
    }
    // hSolidPen usunięty — był dead code (nigdy nie SelectObject'd do DC) // ← ZMIANA
    HPEN hDashPen = pData->hCachedDashPen; // ← ZMIANA: alias lokalny
    HPEN hOldPen = (HPEN)::SelectObject(hDC, hDashPen);

    if (!pData->hCachedFont) { // ← ZMIANA: tworzone tylko raz
        pData->hCachedFont = ::CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
    }
    HFONT hFont = pData->hCachedFont; // ← ZMIANA: alias lokalny
    HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont); // Wstrzyknięcie czcionki do potoku renderera Windows
    COLORREF hOldTextColor = ::GetTextColor(hDC); // ← ZMIANA: zapis koloru tekstu DC (zakres funkcji)
    int      hOldBkMode    = ::GetBkMode(hDC);    // ← ZMIANA: zapis trybu tła DC (zakres funkcji)
    UINT     hOldTextAlign = ::GetTextAlign(hDC); // ← ZMIANA: zapis wyrównania tekstu DC (zakres funkcji)

    // ← ZMIANA: GDI Pen Pool — pióra tworzone raz per unikalna para (kolor, grubość), reużywane w całej pętli siatek
    struct PenCacheEntry { COLORREF color; int width; HPEN pen; }; // ← ZMIANA
    PenCacheEntry penCache[20]; // ← ZMIANA: maks. 20 unikalnych par (10 poziomów × 2 grubości)
    int penCacheCount = 0;      // ← ZMIANA

// Matematyczne wyznaczenie indeksu aktualnie rysowanego obiektu w maszynie stanów
    int activeSlot = pData->numSaved % 30;


// 1. LOGIKA LIVE PREVIEW (Inteligentne, wymuszone przyciąganie horyzontalno-pionowe w locie)
    if (pData->TState == 2) {
        int xA = sc.BarIndexToXPixelCoordinate(pData->drawings[activeSlot].idxA);
        int yA = sc.RegionValueToYPixelCoordinate(pData->drawings[activeSlot].priceA, sc.GraphRegion);
        
int aIdx = pData->drawings[activeSlot].idxA;
        bool isAHigh = (pData->drawings[activeSlot].priceA == sc.High[aIdx]);
        bool findBHigh = !isAHigh; 

        int cIdx = sc.ActiveToolIndex;
        // Pobranie dynamicznego parametru bezpośrednio z konfiguracji Sierry dla wątku renderowania GDI
        int snapRange = sc.Input[8].GetInt();
        int snappedBIdx = GetSnappedIndex(sc, cIdx, snapRange, findBHigh);
        float previewPriceB = findBHigh ? sc.High[snappedBIdx] : sc.Low[snappedBIdx];

        int previewX = sc.BarIndexToXPixelCoordinate(snappedBIdx);
        int previewY = sc.RegionValueToYPixelCoordinate(previewPriceB, sc.GraphRegion);
        
        // ETAP 2: Usunięto GDI ::MoveToEx i ::LineTo dla ukośnej linii podglądu. Rysuje ją teraz natywna linia Sierry.

        // --- JAWNE UCHWYTY KOTWICY (Wzór 1:1 z natywnego rysunku Sierry) ---
        HPEN hHandlePen = ::CreatePen(PS_SOLID, 2, RGB(0, 120, 255));   
        HBRUSH hHandleBrush = (HBRUSH)::GetStockObject(WHITE_BRUSH);    
        
        HPEN hOldPenRes = (HPEN)::SelectObject(hDC, hHandlePen);
        HBRUSH hOldBrushRes = (HBRUSH)::SelectObject(hDC, hHandleBrush);
        
        ::Ellipse(hDC, xA - 4, yA - 4, xA + 5, yA + 5);
        ::Ellipse(hDC, previewX - 4, previewY - 4, previewX + 5, previewY + 5); 
        
        ::SelectObject(hDC, hOldBrushRes);
        ::SelectObject(hDC, hOldPenRes);
        ::DeleteObject(hHandlePen);
    }


// 2. LOGIKA RENDEROWANIA ZAMROŻONYCH SIATEK - ETAP 2 (Odczyt geometrii w locie z linii natywnej)
    ::SetBkMode(hDC, TRANSPARENT);     // ← ZMIANA: raz przed pętlą, nie limit-razy
    ::SetTextAlign(hDC, TA_NOUPDATECP); // ← ZMIANA
    int minBarThreshold = sc.Input[9].GetInt(); // ← ZMIANA: hoist przed pętlą
    int limit = min(pData->numSaved, 30);
    for (int k = 0; k < limit; k++) {
        SavedGrid& grid = pData->drawings[k];

        // PIERWSZE ZADANIE: Bezpieczny warunek renderowania oparty na cenach (zapobiega pomijaniu odczytu z dysku)
        if (grid.priceA == 0.0f || grid.priceB == 0.0f) continue;
        if (abs(grid.idxB - grid.idxA) < minBarThreshold) { // ← ZMIANA: Min Bars Visibility Threshold
            if (grid.lineNumber != 0)
                sc.DeleteACSChartDrawing(sc.ChartNumber, 0, grid.lineNumber); // ← ZMIANA: usunięcie natywnego spine poniżej progu
            continue;
        }

        int drawIdxA = grid.idxA;
        float drawPriceA = grid.priceA;
        int drawIdxB = grid.idxB;
        float drawPriceB = grid.priceB;
        
float tmpCalcPrices[10];
        for (int i = 0; i < 10; i++) tmpCalcPrices[i] = grid.calculatedPrices[i];

        // Pobranie aktualnych współrzędnych z natywnej linii Sierre (Real-Time Follow podczas przesuwania)
        s_UseTool ExistingLine;
        bool lineFound = sc.GetUserDrawingByLineNumber(sc.ChartNumber, grid.lineNumber, ExistingLine);
        if (lineFound) {
            drawIdxA = ExistingLine.BeginIndex;
            drawPriceA = ExistingLine.BeginValue;
            drawIdxB = ExistingLine.EndIndex;
            drawPriceB = ExistingLine.EndValue;

// Błyskawiczna rekalkulacja GDI dla 10 zdefiniowanych poziomów (Matryca z Trade Navigatora - Poprawna Kolejność)
            float ratios[10] = {0.000f, 0.250f, 0.382f, 0.500f, 0.618f, 0.700f, 0.786f, 0.850f, 0.886f, 1.000f};
            for (int i = 0; i < 10; i++) {
                tmpCalcPrices[i] = sc.RoundToTickSize(drawPriceA + ratios[i] * (drawPriceB - drawPriceA));
            }
        }

        if (drawIdxA >= sc.ArraySize || drawIdxB >= sc.ArraySize) continue;

        // ← ZMIANA: Viewport culling — pomijanie siatek całkowicie poza widocznym obszarem (margines ±50 barów)
        {
            int minIdx = (drawIdxA < drawIdxB) ? drawIdxA : drawIdxB;
            int maxIdx = (drawIdxA > drawIdxB) ? drawIdxA : drawIdxB;
            if (maxIdx < sc.IndexOfFirstVisibleBar - 50 || minIdx > sc.IndexOfLastVisibleBar + 50) continue;
        }

        int xA = sc.BarIndexToXPixelCoordinate(drawIdxA);
        int xB = sc.BarIndexToXPixelCoordinate(drawIdxB);

        int xLeft  = (xA < xB) ? xA : xB;
        int xRight = (xA > xB) ? xA : xB;


        for (int i = 0; i < 10; i++) {
            if (!grid.customUse[i]) continue; // RESTRYKCJA ERGONOMII: Ukrywanie linii wyłączonych w Menu

            int yLevel = sc.RegionValueToYPixelCoordinate(tmpCalcPrices[i], sc.GraphRegion);
            if (yLevel <= 0) continue;

// ← ZMIANA: Grubość z customWidth (per poziom, zakres 1-3); fallback do 1 dla zerowych wartości (stare pliki)
int currentLineWidth = (grid.customWidth[i] >= 1 && grid.customWidth[i] <= 3) ? grid.customWidth[i] : 1;

            // ← ZMIANA: Lookup w puli — pióro tworzone tylko raz per unikalna para (kolor, grubość)
            HPEN hCustomPen = NULL; // ← ZMIANA
            for (int p = 0; p < penCacheCount; p++) { // ← ZMIANA
                if (penCache[p].color == grid.customColors[i] && penCache[p].width == currentLineWidth) { // ← ZMIANA
                    hCustomPen = penCache[p].pen; break; // ← ZMIANA
                } // ← ZMIANA
            } // ← ZMIANA
            if (hCustomPen == NULL) { // ← ZMIANA: nowy kolor/grubość — tworzenie i rejestracja w puli
                hCustomPen = ::CreatePen(PS_SOLID, currentLineWidth, grid.customColors[i]); // ← ZMIANA
                if (penCacheCount < 20) { // ← ZMIANA
                    penCache[penCacheCount].color = grid.customColors[i]; // ← ZMIANA
                    penCache[penCacheCount].width = currentLineWidth;     // ← ZMIANA
                    penCache[penCacheCount].pen   = hCustomPen;           // ← ZMIANA
                    penCacheCount++;                                       // ← ZMIANA
                } // ← ZMIANA
            } // ← ZMIANA
            ::SelectObject(hDC, hCustomPen); // ← ZMIANA: bez zapisu hOldCustomPen — DC przywracany przez hOldPen na końcu funkcji
            ::SetTextColor(hDC, grid.customColors[i]); // Synchronizacja koloru napisów z kolorem linii strefy

            ::MoveToEx(hDC, xLeft, yLevel, NULL);
            ::LineTo(hDC, xRight, yLevel);

            char ratioBuf[32];
            sprintf(ratioBuf, "%g", grid.customRatios[i]); // Konwersja float na tekst w locie dla niestandardowych Ratio

            RECT rectLeft = { xLeft - 150, yLevel - 20, xLeft - 5, yLevel + 20 };
            ::DrawTextA(hDC, ratioBuf, -1, &rectLeft, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

            SCString priceStr = sc.FormatGraphValue(tmpCalcPrices[i], sc.BaseGraphValueFormat);
            RECT rectRight = { xRight + 5, yLevel - 20, xRight + 150, yLevel + 20 };
            ::DrawTextA(hDC, priceStr.GetChars(), -1, &rectRight, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
            // ← ZMIANA: SelectObject przywracający i DeleteObject usunięte — pióra żyją do końca pętli siatek
        }



        // ETAP 2: Rysowanie krążków kontrolnych, gdy obiekt jest ZAZNACZONY lub PODNIESIONY do edycji
        if (pData->SelectedIdx == k || pData->DraggingIdx == k) {
            int yA = sc.RegionValueToYPixelCoordinate(drawPriceA, sc.GraphRegion);
            int yB = sc.RegionValueToYPixelCoordinate(drawPriceB, sc.GraphRegion);

            HPEN hHandlePen = ::CreatePen(PS_SOLID, 2, RGB(0, 120, 255));   // Electric Blue, grubość 2px
            HBRUSH hHandleBrush = (HBRUSH)::GetStockObject(WHITE_BRUSH);    // Opaque White Masking
            
            HPEN hOldPenRes = (HPEN)::SelectObject(hDC, hHandlePen);
            HBRUSH hOldBrushRes = (HBRUSH)::SelectObject(hDC, hHandleBrush);
            
            // Nałożenie punktów orientacyjnych na fizyczne końce modyfikowanej fali rynkowej
            ::Ellipse(hDC, xA - 4, yA - 4, xA + 5, yA + 5);
            ::Ellipse(hDC, xB - 4, yB - 4, xB + 5, yB + 5);
            
            ::SelectObject(hDC, hOldBrushRes);
            ::SelectObject(hDC, hOldPenRes);
            ::DeleteObject(hHandlePen);
        }
    }



// Bezpieczne czyszczenie uchwytów GDI (Zapobieganie GDI Leaks)
    ::SelectObject(hDC, hOldPen);  // ← Przywrócenie oryginalnego pióra DC — deselektuje ostatnie pióro z puli
    for (int p = 0; p < penCacheCount; p++) ::DeleteObject(penCache[p].pen); // ← ZMIANA: jednorazowe czyszczenie całej puli
    ::SelectObject(hDC, hOldFont); // Przywrócenie domyślnej czcionki platformy Sierra Chart
    // hDashPen, hFont — cached w pData, kasowane w LastCallToFunction // ← ZMIANA
    ::SetBkMode(hDC, hOldBkMode);        // ← ZMIANA: przywrócenie trybu tła (naprawa białej obramówki ceny)
    ::SetTextColor(hDC, hOldTextColor);  // ← ZMIANA: przywrócenie koloru tekstu
    ::SetTextAlign(hDC, hOldTextAlign);  // ← ZMIANA: przywrócenie wyrównania tekstu
}

// --- GŁÓWNA FUNKCJA WSKAŹNIKA ---
SCSFExport scsf_FibonacciGrid(SCStudyInterfaceRef sc) {
    
    // 1. SEKCJA KONFIGURACJI DOMYŚLNEJ
    if (sc.SetDefaults) {
        sc.GraphName = "Fibonacci Grid ";
        sc.GraphRegion = 0; 
        sc.AutoLoop = 0; 
        sc.UpdateAlways = 1; 

sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_ALWAYS; // ETAP 2: Poprawny enum ACSIL dla stałej interakcji z obiektami RAM
        sc.ReceiveCharacterEvents = 1;

sc.p_GDIFunction = DrawFibonacciGDI; 
        sc.ScaleRangeType = SCALE_SAMEASREGION;   

// --- STEALTH MODE: Ukrycie nagłówka wskaźnika --- // ← ZMIANA
        sc.DisplayStudyName = 0;                              // ← ZMIANA
        sc.DisplayStudyInputValues = 0;                       // ← ZMIANA
        sc.GlobalDisplayStudySubgraphsNameAndValue = 0;       // ← ZMIANA

// --- STYMULATOR ODŚWIEŻANIA (Heartbeat Trigger dla trybu Offline) ---
        sc.Subgraph[0].Name = "Heartbeat";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_HIDDEN; // Ukryty, nie generuje linii ani napisów
        sc.Subgraph[0].DisplayNameValueInDataLine = 0;    // ← ZMIANA
        sc.Subgraph[0].DisplayNameValueInWindowsFlags = 0; // ← ZMIANA
        sc.Subgraph[0].IncludeInSpreadsheet = 0;           // ← ZMIANA
        sc.Subgraph[0].IncludeInStudySummary = 0;          // ← ZMIANA
        sc.Subgraph[0].LineLabel = 0;                      // ← ZMIANA

// INTEGRACJA Z CONTROL BAR - Konfiguracja wejść dla przycisków (Elastyczność UI)
        sc.Input[0].Name = "Control Bar Button ID - FIB1"; // ← ZMIANA
        sc.Input[0].SetInt(31);

        sc.Input[1].Name = "Control Bar Button ID - FIB2"; // ← ZMIANA
        sc.Input[1].SetInt(32);

        sc.Input[2].Name = "Control Bar Button ID - FIB3"; // ← ZMIANA
        sc.Input[2].SetInt(33);

        sc.Input[3].Name = "Control Bar Button ID - FIB4"; // ← ZMIANA
        sc.Input[3].SetInt(34);

        sc.Input[4].Name = "Control Bar Button ID - Last Delete"; // ← ZMIANA
        sc.Input[4].SetInt(35);

        sc.Input[5].Name = "Control Bar Button ID - All Delete"; // ← ZMIANA
        sc.Input[5].SetInt(36);

        // --- ETAP 2 HIDE: Konfiguracja wejść integracyjnych dla trybu Slave ---
        sc.Input[6].Name = "Sync z Master HIDE (Button 20)"; // ← ZMIANA
        sc.Input[6].SetYesNo(1);

        sc.Input[7].Name = "Master Hide Button ID"; // ← ZMIANA
        sc.Input[7].SetInt(20);
		
// Dynamiczny parametr dociągania do knotów (Snapping Range) z wartością domyślną 0
        sc.Input[8].Name = "Snapping Range (Bars)";
        sc.Input[8].SetInt(0);

        sc.Input[9].Name = "Min Bars Visibility Threshold"; // ← ZMIANA
        sc.Input[9].SetInt(3);                              // ← ZMIANA

        sc.Input[10].Name = "Search Range Bars"; // ← ZMIANA
        sc.Input[10].SetInt(25);                 // ← ZMIANA

        sc.Input[11].Name = "GPU Safe-Lock Frames (Offline)"; // ← ZMIANA
        sc.Input[11].SetInt(10);                              // ← ZMIANA

        // Nadanie nazw przyciskom na pasku na podstawie dynamicznych wartości z menu ustawień
        sc.SetCustomStudyControlBarButtonText(sc.Input[0].GetInt(), "FIB 1"); // ← ZMIANA
        sc.SetCustomStudyControlBarButtonText(sc.Input[1].GetInt(), "FIB 2"); // ← ZMIANA
        sc.SetCustomStudyControlBarButtonText(sc.Input[2].GetInt(), "FIB 3"); // ← ZMIANA
        sc.SetCustomStudyControlBarButtonText(sc.Input[3].GetInt(), "FIB 4"); // ← ZMIANA
        sc.SetCustomStudyControlBarButtonText(sc.Input[4].GetInt(), "LAST DEL");
        sc.SetCustomStudyControlBarButtonText(sc.Input[5].GetInt(), "ALL DEL");

        return;
    }

// 2. ALOKACJA PAMIĘCI PERSISTENT POINTER Z AUTOMATYCZNYM ODCZYTEM Z PLIKU .DAT (ETAP 5 - FIX)
    FibonacciData* pData = (FibonacciData*)sc.GetPersistentPointer(500); 

if (pData == NULL) {
        pData = (FibonacciData*)sc.AllocateMemory(sizeof(FibonacciData));
        if (pData != NULL) {
            memset(pData, 0, sizeof(FibonacciData));
            sc.SetPersistentPointer(500, pData);
            pData->LastSize = -1;
            pData->TState = 0;
            pData->DraggingIdx = -1;
            pData->DraggedPoint = 0;
            pData->SelectedIdx = -1;
            pData->LastLeftIdx = -1; // ← ZMIANA

            // ← ZMIANA: Inicjalizacja 4 presetów wartościami fabrycznymi
            {
                float defRatios[10] = {0.000f, 0.250f, 0.382f, 0.500f, 0.618f, 0.700f, 0.786f, 0.850f, 0.886f, 1.000f};
                COLORREF defColors[10] = {
                    RGB(0,0,255), RGB(255,0,0), RGB(255,0,0), RGB(0,0,255), RGB(255,0,0),
                    RGB(0,0,0),   RGB(255,0,0), RGB(255,0,0), RGB(128,128,128), RGB(0,0,255)
                };
                int defUse0[10] = {1,0,0,0,1,1,1,0,1,1}; // ← ZMIANA: FIB1 — pełny zestaw
                int defUse1[10] = {1,0,1,1,0,0,0,0,0,1}; // ← ZMIANA: FIB2 — 0.382, 0.500
                int defUse2[10] = {1,1,0,1,0,0,0,0,0,1}; // ← ZMIANA: FIB3 — 0.250, 0.500
                int defUse3[10] = {1,0,1,0,0,0,0,0,0,1}; // ← ZMIANA: FIB4 — 0.382
                int* defUsePtrs[4] = {defUse0, defUse1, defUse2, defUse3}; // ← ZMIANA
                for (int p = 0; p < 4; p++) {
                    for (int i = 0; i < 10; i++) {
                        pData->presets[p].ratios[i] = defRatios[i];
                        pData->presets[p].colors[i] = defColors[i];
                        pData->presets[p].use[i]    = defUsePtrs[p][i]; // ← ZMIANA
                        pData->presets[p].width[i]  = (i == 0 || i == 9) ? 2 : 1; // ← ZMIANA: poziomy 0.000 i 1.000 grubsze
                    }
                }
                pData->activeConfig = 0;
            }

            char fn[512];
            GetDataFilePath(sc, fn); // ← ZMIANA
            FILE* f = fopen(fn, "rb");
            if (f) {
                // ← ZMIANA: Guard rozmiaru — odrzucenie pliku .dat o niekompatybilnym rozmiarze
                fseek(f, 0, SEEK_END);
                long fileSize = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (fileSize == (long)sizeof(FibonacciData)) {
                    fread(pData, sizeof(FibonacciData), 1, f);
                    if (pData->activeConfig < 0 || pData->activeConfig > 3) pData->activeConfig = 0; // ← ZMIANA: clamp
                }
                fclose(f);

                // Sanacja i mrożenie stanów ulotnych jądra myszy po wciągnięciu płaskiego bloku bajtów RAM
                pData->LastSize = -1;
                pData->TState = 0;
                pData->DraggingIdx = -1;
                pData->DraggedPoint = 0;
                pData->SelectedIdx = -1;
                pData->LastLeftIdx = -1;   // ← ZMIANA
                pData->hCachedFont    = NULL; // ← ZMIANA: runtime handle nieważny po restarcie — wymuszenie lazy-init
                pData->hCachedDashPen = NULL; // ← ZMIANA
            }

            // ← ZMIANA: Globalny plik presetów nadpisuje presety — zawsze wygrywa nad per-chart .dat
            {
                char presetsFn[512];
                GetPresetsFilePath(sc, presetsFn); // ← ZMIANA
                FILE* fp = fopen(presetsFn, "rb");
                if (fp) {
                    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
                    if (sz == (long)(sizeof(FibonacciPreset) * 4))
                        fread(pData->presets, sizeof(FibonacciPreset), 4, fp);
                    fclose(fp);
                }
            }
        }
    }

    // 3. CZYSZCZENIE PAMIĘCI RAM PRZY USUWANIU WSKAŹNIKA
    if (sc.LastCallToFunction) {
        if (pData != NULL) {
            if (pData->hCachedFont)    { ::DeleteObject(pData->hCachedFont);    } // ← ZMIANA: cleanup cached GDI font
            if (pData->hCachedDashPen) { ::DeleteObject(pData->hCachedDashPen); } // ← ZMIANA: cleanup cached GDI pen
            sc.FreeMemory(pData);
            sc.SetPersistentPointer(500, NULL);
        }
        return;
    }

if (pData == NULL) return;

    // DYNAMICZNE MAPOWANIE PRZYCISKÓW Z MENU USTAWIEN (sc.Input) - Likwidacja sztywnych literałów
    int DrawBtn1   = sc.Input[0].GetInt(); // ← ZMIANA
    int DrawBtn2   = sc.Input[1].GetInt(); // ← ZMIANA
    int DrawBtn3   = sc.Input[2].GetInt(); // ← ZMIANA
    int DrawBtn4   = sc.Input[3].GetInt(); // ← ZMIANA
    int LastDelBtn = sc.Input[4].GetInt(); // ← ZMIANA
    int AllDelBtn  = sc.Input[5].GetInt(); // ← ZMIANA
    int HideBtn    = sc.Input[7].GetInt(); // ← ZMIANA

// --- ETAP 3 HIDE: Most decyzyjny i osłona GPU (Slave Mode) ---
    if (sc.IsFullRecalculation) {
        pData->StabCounter = sc.Input[11].GetInt(); // ← ZMIANA
    }

    int currentGlobalHide = sc.GetPersistentIntFromChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT);
    bool isSyncImpulse = (sc.Input[6].GetYesNo() && sc.MenuEventID == HideBtn);
    
    bool effectiveIsVisible = (currentGlobalHide == 0);
    if (isSyncImpulse) {
        pData->StabCounter = sc.Input[11].GetInt();  // ← ZMIANA
        pData->LastSize = -1;     // Wymuszenie otwarcia bramki redrawu
        if (currentGlobalHide == pData->LastSyncValue) {
            effectiveIsVisible = !effectiveIsVisible;
            pData->LastSyncValue = effectiveIsVisible ? 0 : 1; 
        }
    }

    // ============================================================================================
    // === MODYFIKACJA SEKCJI (ZMIANA ARCHITEKTONICZNA - PASYWNY MOST SYNCHRONIZACJI Z DINAPOLI) ===
    // ============================================================================================
    if (currentGlobalHide != pData->LastSyncValue) { 
        pData->LastSyncValue = currentGlobalHide; 
        pData->LastSize = -1; 
        pData->StabCounter = sc.Input[11].GetInt(); // ← ZMIANA
        
        // WYMUSZENIE NATYCHMIASTOWEGO REPAINTU GDI OFFLINE PRZEZ AKTYWACJĘ GENERATORA BIĆ SERCA
        sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue + 0.001f;
    }

    // Przeliczenie widoczności na podstawie zsynchronizowanego i wyrównanego rejestru cache RAM
    effectiveIsVisible = (pData->LastSyncValue == 0);
    // ============================================================================================
    // === KONIEC MODYFIKACJI SEKCJI ==============================================================
    // ============================================================================================

    // Most decyzyjny (Single Source of Truth) dla silnika GDI - Rejestracja w Slocie 5
    sc.SetPersistentInt(5, effectiveIsVisible ? 1 : 0);

    // ← ZMIANA: Detekcja przewijania — wymuszenie odświeżenia GDI w trybie offline
    if (effectiveIsVisible && sc.IndexOfFirstVisibleBar != pData->LastLeftIdx) {
        pData->LastLeftIdx = sc.IndexOfFirstVisibleBar;
        sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue + 0.001f;
    }

// 5. POPRAWIONY HANDSHAKE PASKA I POINTER PROXY (Wzorzec z DiNapoli)

    if (sc.PointerEventType == SC_ACS_BUTTON_ON) { // ← ZMIANA
        int hitConfig = -1;
        if      (sc.MenuEventID == DrawBtn1) hitConfig = 0; // ← ZMIANA
        else if (sc.MenuEventID == DrawBtn2) hitConfig = 1; // ← ZMIANA
        else if (sc.MenuEventID == DrawBtn3) hitConfig = 2; // ← ZMIANA
        else if (sc.MenuEventID == DrawBtn4) hitConfig = 3; // ← ZMIANA

        if (hitConfig >= 0) { // ← ZMIANA
            pData->activeConfig = hitConfig; // ← ZMIANA
            pData->TState = 1;

            HWND hRoot = ::GetAncestor((HWND)sc.ChartWindowHandle, GA_ROOT);
            if (hRoot != NULL) {
                int& CachedPointerID = sc.GetPersistentInt(45); // ← ZMIANA
                if (CachedPointerID <= 0) CachedPointerID = FindSierraMenuCommandID(hRoot, "Tools", "Pointer"); // ← ZMIANA
                if (CachedPointerID > 0) {
                    ::PostMessageA(hRoot, WM_COMMAND, CachedPointerID, 0);
                }
            }
        } // ← ZMIANA
    }

// 6. ASYNCHRONICZNY HAMULEC BEZPIECZEŃSTWA Z PEŁNĄ SANACJĄ OBIEKTÓW OSIEROCONYCH
    if (sc.CharacterEventCode == 32 && pData->TState > 0) {
        // Namierzamy slot, który właśnie próbowaliśmy rysować
        int activeSlot = pData->numSaved % 30;

        // Jeśli zdążył wygenerować linię w Sierrze, bezwzględnie ją wycinamy
        if (pData->drawings[activeSlot].lineNumber != 0) {
            sc.DeleteACSChartDrawing(sc.ChartNumber, 0, pData->drawings[activeSlot].lineNumber);
        }

        // Czyszczenie niedokończonych rekordów geometrycznych w strukturze RAM
        memset(&pData->drawings[activeSlot], 0, sizeof(SavedGrid));

        // Reset parametrów jądra sterowania
        pData->TState = 0;
        sc.SetCustomStudyControlBarButtonEnable(DrawBtn1, 0); // ← ZMIANA
        sc.SetCustomStudyControlBarButtonEnable(DrawBtn2, 0); // ← ZMIANA
        sc.SetCustomStudyControlBarButtonEnable(DrawBtn3, 0); // ← ZMIANA
        sc.SetCustomStudyControlBarButtonEnable(DrawBtn4, 0); // ← ZMIANA
        sc.CharacterEventCode = 0;

        // WYMUSZENIE NATYCHMIASTOWEGO REPAINTU OFFLINE: Linia zniknie w ułamku milisekundy
        sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue + 0.001f;
    }
	
	
    // OBSŁUGA PRZYCISKU LAST DEL (ETAP 2)
    if (sc.MenuEventID == LastDelBtn) {
        if (pData->numSaved > 0) {
            // Kalkulacja indeksu wejściowego w płaskim buforze kołowym
            int lastIdx = (pData->numSaved - 1) % 30;
            
            // Utylizacja przerywanej linii grzbietu z rejestrów platformy Sierra Chart
            if (pData->drawings[lastIdx].lineNumber != 0) {
                sc.DeleteACSChartDrawing(sc.ChartNumber, 0, pData->drawings[lastIdx].lineNumber);
            }
            
            // Sanacja komórki RAM - czyszczenie bajtowe struktury do stanu zerowego
            memset(&pData->drawings[lastIdx], 0, sizeof(SavedGrid));
            
// Dekrementacja licznika i bezwzględny reset menedżera selekcji
            pData->numSaved--;
            pData->SelectedIdx = -1;
            
            // ZAPIS ZMIAN NA DYSK - STRUMIEŃ BINARNY (ETAP 4)
            char fn[512];
            GetDataFilePath(sc, fn); // ← ZMIANA
            FILE* f = fopen(fn, "wb");
            if (f) {
                fwrite(pData, sizeof(FibonacciData), 1, f);
                fclose(f);
            }

            // WYMUSZENIE NATYCHMIASTOWEGO REPAINTU OFFLINE (ETAP 5)
            sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue + 0.001f;
        }
        // Wyłączenie podświetlenia przycisku i natychmiastowe zwolnienie magistrali zdarzeń (Gasić sygnał)
        sc.SetCustomStudyControlBarButtonEnable(LastDelBtn, 0);
            sc.MenuEventID = 0;
        }

    // OBSŁUGA PRZYCISKU ALL DEL (ETAP 3)
    if (sc.MenuEventID == AllDelBtn) {
        // Pętla masowej utylizacji linii grzbietu z rejestrów platformy Sierra Chart
        int limit = min(pData->numSaved, 30);
        for (int k = 0; k < limit; k++) {
            if (pData->drawings[k].lineNumber != 0) {
                sc.DeleteACSChartDrawing(sc.ChartNumber, 0, pData->drawings[k].lineNumber);
            }
        }

        // Atomowy reset całego bufora tablicowego rysunków w pamięci RAM
        memset(pData->drawings, 0, sizeof(pData->drawings));

        // Przywrócenie stanów spoczynkowych i fabrycznych parametrów jądra wskaźnika
        pData->numSaved = 0;
        pData->TState = 0;
        pData->DraggingIdx = -1;
        pData->DraggedPoint = 0;
pData->SelectedIdx = -1;

        // ZAPIS ZMIAN NA DYSK - STRUMIEŃ BINARNY (ETAP 4)
        char fn[512];
        GetDataFilePath(sc, fn); // ← ZMIANA
        FILE* f = fopen(fn, "wb");
        if (f) {
            fwrite(pData, sizeof(FibonacciData), 1, f);
            fclose(f);
        }

        // WYMUSZENIE NATYCHMIASTOWEGO REPAINTU OFFLINE (ETAP 5)
        sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue + 0.001f;

        // Wyłączenie podświetlenia przycisku i natychmiastowe zwolnienie magistrali (Gasić sygnał)
        sc.SetCustomStudyControlBarButtonEnable(AllDelBtn, 0);
        sc.MenuEventID = 0;
    }

// 7. SEKCJA INTERAKCJI MYSZY - DWUSTOPNIOWY MODEL SELEKCJI WEKTOROWEJ (SPINE SELECTION STANDARD)
    if (!sc.DownloadingHistoricalData) {
        int activeSlot = pData->numSaved % 30;
        int cIdx = sc.ActiveToolIndex;
        float mY = (float)sc.ActiveToolYValue;
        // Podpięcie dynamicznego parametru użytkownika (wartość 0 w silniku GetSnappedIndex wyłączy snapping)
        int snapRange = sc.Input[8].GetInt();

        // --- OBSŁUGA RUCHU MYSZY (Płynne śledzenie i rekalkulacja w RAM) ---
        if (sc.PointerEventType == SC_POINTER_MOVE) {
            if (pData->TState == 0 && pData->DraggingIdx != -1) {
                int k = pData->DraggingIdx;
                
                if (pData->DraggedPoint == 1) {
                    bool isAHigh = (fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx]));
                    int snappedAIdx = GetSnappedIndex(sc, cIdx, snapRange, isAHigh);
                    pData->drawings[k].idxA = snappedAIdx;
                    pData->drawings[k].priceA = isAHigh ? sc.High[snappedAIdx] : sc.Low[snappedAIdx];
                    pData->drawings[k].timeA = sc.BaseDateTimeIn[snappedAIdx];
                }
                else if (pData->DraggedPoint == 2) {
                    int aIdx = pData->drawings[k].idxA;
                    bool isAHigh = (pData->drawings[k].priceA == sc.High[aIdx]);
                    bool findBHigh = !isAHigh;
                    int snappedBIdx = GetSnappedIndex(sc, cIdx, snapRange, findBHigh);
                    pData->drawings[k].idxB = snappedBIdx;
                    pData->drawings[k].priceB = findBHigh ? sc.High[snappedBIdx] : sc.Low[snappedBIdx];
                    pData->drawings[k].timeB = sc.BaseDateTimeIn[snappedBIdx];
                }

// Pancerne przeliczenie cen stref bezpośrednio w buforze RAM dla 10 poziomów
                float pA = pData->drawings[k].priceA;
                float pB = pData->drawings[k].priceB;
                for (int i = 0; i < 10; i++) {
                    pData->drawings[k].calculatedPrices[i] = sc.RoundToTickSize(pA + pData->drawings[k].customRatios[i] * (pB - pA));
                }


                ApplySpineLine(sc, pData->drawings[k].lineNumber, // ← ZMIANA
                    pData->drawings[k].idxA, pData->drawings[k].priceA,
                    pData->drawings[k].idxB, pData->drawings[k].priceB);
            }
else if (pData->TState == 2) {
                int aIdx = pData->drawings[activeSlot].idxA;
                bool isAHigh = (pData->drawings[activeSlot].priceA == sc.High[aIdx]);
                bool findBHigh = !isAHigh;
                // Pełna unifikacja - zastąpienie sztywnego literału dynamiczną zmienną snapRange
                int snappedBIdx = GetSnappedIndex(sc, cIdx, snapRange, findBHigh);
                float previewPriceB = findBHigh ? sc.High[snappedBIdx] : sc.Low[snappedBIdx];

                ApplySpineLine(sc, pData->drawings[activeSlot].lineNumber, // ← ZMIANA
                    pData->drawings[activeSlot].idxA, pData->drawings[activeSlot].priceA,
                    snappedBIdx, previewPriceB);
            }
        }

        // --- MANIPULACJA STRUKTURĄ (Pancerny Interfejs Zdarzenia UP) ---
        else if (sc.PointerEventType == SC_POINTER_BUTTON_UP) {
            if (pData->TState == 0) {
                // KROK 1: Jeśli punkt jest w powietrzu -> Kliknięcie go "upuszcza" (Drop / Freeze)
                if (pData->DraggingIdx != -1) {
                    pData->DraggingIdx = -1;
                    pData->DraggedPoint = 0;
                }
                // KROK 2: Analiza kliknięć interakcyjnych w spoczynku
                else {
                    int mouseX = sc.PointerHorzWindowCoord;
                    int mouseY = sc.PointerVertWindowCoord;
                    bool hitHandle = false;

                    // PODKROK A: Jeśli jakiś obiekt jest już ZAZNACZONY, sprawdzamy kliknięcie w jego węzły
                    if (pData->SelectedIdx != -1) {
                        int k = pData->SelectedIdx;
                        int xA = sc.BarIndexToXPixelCoordinate(pData->drawings[k].idxA);
                        int yA = sc.RegionValueToYPixelCoordinate(pData->drawings[k].priceA, sc.GraphRegion);
                        int xB = sc.BarIndexToXPixelCoordinate(pData->drawings[k].idxB);
                        int yB = sc.RegionValueToYPixelCoordinate(pData->drawings[k].priceB, sc.GraphRegion);

                        if (abs(mouseX - xA) <= 14 && abs(mouseY - yA) <= 14) {
                            pData->DraggingIdx = k;
                            pData->DraggedPoint = 1;
                            hitHandle = true;
                        }
                        else if (abs(mouseX - xB) <= 14 && abs(mouseY - yB) <= 14) {
                            pData->DraggingIdx = k;
                            pData->DraggedPoint = 2;
                            hitHandle = true;
                        }
                    }

                    // PODKROK B: Jeśli nie trafiono w węzeł zaznaczonego rysunku -> Matematyczny Hit-Test linii grzbietu (Spine)
                    if (!hitHandle) {
                        int limit = min(pData->numSaved, 30);
                        int newSelectedIdx = -1;

                        for (int k = 0; k < limit; k++) {
                            if (pData->drawings[k].lineNumber == 0) continue;

                            int xA = sc.BarIndexToXPixelCoordinate(pData->drawings[k].idxA);
                            int yA = sc.RegionValueToYPixelCoordinate(pData->drawings[k].priceA, sc.GraphRegion);
                            int xB = sc.BarIndexToXPixelCoordinate(pData->drawings[k].idxB);
                            int yB = sc.RegionValueToYPixelCoordinate(pData->drawings[k].priceB, sc.GraphRegion);

                            // Zaawansowana geometria wektorowa: rzut prostopadły kursora na segment linii kotwicy
                            double abX = xB - xA;
                            double abY = yB - yA;
                            double amX = mouseX - xA;
                            double amY = mouseY - yA;
                            double abLenSq = abX * abX + abY * abY;
                            double t = 0.0;
                            
                            if (abLenSq > 0.0) {
                                t = (amX * abX + amY * abY) / abLenSq;
                                if (t < 0.0) t = 0.0;
                                if (t > 1.0) t = 1.0;
                            }
                            
                            double cx = xA + t * abX;
                            double cy = yA + t * abY;
                            double dist = sqrt((mouseX - cx) * (mouseX - cx) + (mouseY - cy) * (mouseY - cy));

                            if (dist <= 10.0) { // Margines tolerancji trafienia w linię siatki = 10 pikseli
                                newSelectedIdx = k;
                                break;
                            }
                        }


// ASYNCHRONICZNY EMULATOR DWUKLIKU NA GRZBIECIE (SPINE QUICK EDIT HOOK)
                        unsigned int currentClickTime = ::GetTickCount();
                        if (newSelectedIdx != -1 && pData->SelectedIdx == newSelectedIdx && (currentClickTime - pData->LastClickTime <= 300)) {
                            // ← ZMIANA: Przeładowanie najnowszych presetów z globalnego pliku przed otwarciem modalu
                            char presetsFn[512];
                            GetPresetsFilePath(sc, presetsFn); // ← ZMIANA
                            {
                                FILE* fp = fopen(presetsFn, "rb");
                                if (fp) {
                                    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
                                    if (sz == (long)(sizeof(FibonacciPreset) * 4))
                                        fread(pData->presets, sizeof(FibonacciPreset), 4, fp);
                                    fclose(fp);
                                }
                            }
                            // Wywołanie autorskiego okna modalnego Win32 skompilowanego bezpośrednio wewnątrz DLL
                            ShowModalConfig((HWND)sc.ChartWindowHandle, pData->drawings[newSelectedIdx], pData, presetsFn); // ← ZMIANA

// Wymuszenie natychmiastowej rekalkulacji ramy cenowej Fibo po zamknięciu okna dialogowego dla 10 stref
                            float pA = pData->drawings[newSelectedIdx].priceA;
                            float pB = pData->drawings[newSelectedIdx].priceB;
                            for (int i = 0; i < 10; i++) {
                                pData->drawings[newSelectedIdx].calculatedPrices[i] = sc.RoundToTickSize(pA + pData->drawings[newSelectedIdx].customRatios[i] * (pB - pA));
                            }

                            char fn[512];
                            GetDataFilePath(sc, fn); // ← ZMIANA
                            FILE* f = fopen(fn, "wb");
                            if (f) {
                                fwrite(pData, sizeof(FibonacciData), 1, f);
                                fclose(f);
                            }

                            // ETAP 5: Wymuszenie natychmiastowego bicia serca (Repaint Trigger) w ułamku sekundy zamknięcia menu
                            sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue + 0.001f;

                            // ETAP 4: Zerowanie rejestru czasu chroni przed błędem powtórnej iniekcji okna przy wielokrotnych kliknięciach
                            currentClickTime = 0;
                        }
                        pData->LastClickTime = currentClickTime;

                        pData->SelectedIdx = newSelectedIdx;
                    }
                }
            }
            else if (pData->TState > 0) {
                if (pData->TState == 1) {
                    bool isAHigh = (fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx]));
                    int snappedAIdx = GetSnappedIndex(sc, cIdx, snapRange, isAHigh);
                    
                    pData->drawings[activeSlot].priceA = isAHigh ? sc.High[snappedAIdx] : sc.Low[snappedAIdx];
                    pData->drawings[activeSlot].timeA = sc.BaseDateTimeIn[snappedAIdx];
                    pData->drawings[activeSlot].idxA = snappedAIdx;

// ← ZMIANA: Inicjalizacja poziomów z aktywnego presetu zamiast hardkodowanych wartości
                    int cfg = pData->activeConfig;
                    for (int i = 0; i < 10; i++) {
                        pData->drawings[activeSlot].customRatios[i] = pData->presets[cfg].ratios[i];
                        pData->drawings[activeSlot].customColors[i] = pData->presets[cfg].colors[i];
                        pData->drawings[activeSlot].customUse[i]    = pData->presets[cfg].use[i];
                        pData->drawings[activeSlot].customWidth[i]  = pData->presets[cfg].width[i]; // ← ZMIANA
                    }

                    pData->drawings[activeSlot].lineNumber = ApplySpineLine(sc, 0, // ← ZMIANA: 0 = nowa linia, zwraca przydzielony lineNumber
                        snappedAIdx, pData->drawings[activeSlot].priceA,
                        snappedAIdx, pData->drawings[activeSlot].priceA);
                    pData->TState = 2;
                }
                else if (pData->TState == 2) {
                    int aIdx = pData->drawings[activeSlot].idxA;
                    bool isAHigh = (pData->drawings[activeSlot].priceA == sc.High[aIdx]);
                    bool findBHigh = !isAHigh;
                    
                    int snappedBIdx = GetSnappedIndex(sc, cIdx, snapRange, findBHigh);
                    
                    pData->drawings[activeSlot].priceB = findBHigh ? sc.High[snappedBIdx] : sc.Low[snappedBIdx];
                    pData->drawings[activeSlot].timeB = sc.BaseDateTimeIn[snappedBIdx];
                    pData->drawings[activeSlot].idxB = snappedBIdx;
                    
                    ApplySpineLine(sc, pData->drawings[activeSlot].lineNumber, // ← ZMIANA
                        pData->drawings[activeSlot].idxA, pData->drawings[activeSlot].priceA,
                        snappedBIdx, pData->drawings[activeSlot].priceB);

                    float pA = pData->drawings[activeSlot].priceA;
                    float pB = pData->drawings[activeSlot].priceB;

                    for (int i = 0; i < 10; i++) {
                        float rawPrice = pA + pData->drawings[activeSlot].customRatios[i] * (pB - pA);
                        pData->drawings[activeSlot].calculatedPrices[i] = sc.RoundToTickSize(rawPrice);
                    }

                    pData->numSaved++;

                    char fn[512];
                    GetDataFilePath(sc, fn); // ← ZMIANA
                    FILE* f = fopen(fn, "wb");
                    if (f) {
                        fwrite(pData, sizeof(FibonacciData), 1, f);
                        fclose(f);
                    }

                    sc.SetCustomStudyControlBarButtonEnable(DrawBtn1, 0); // ← ZMIANA
                    sc.SetCustomStudyControlBarButtonEnable(DrawBtn2, 0); // ← ZMIANA
                    sc.SetCustomStudyControlBarButtonEnable(DrawBtn3, 0); // ← ZMIANA
                    sc.SetCustomStudyControlBarButtonEnable(DrawBtn4, 0); // ← ZMIANA
                    pData->TState = 0;
                }
            }
        }


    // --- AUTOMATYCZNY AMORTYZATOR I BIENIE SERCA REFRESHERA (Wymuszenie klatek GDI Offline) ---
    if ((pData->TState > 0 || pData->DraggingIdx != -1) && sc.PointerEventType == SC_POINTER_MOVE) {
        sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue;
    }


// 8. OBSŁUGA ZMIANY INTERWAŁÓW / REKALKULACJI (Wielobiektowy Timeframe Search Engine)
    effectiveIsVisible = (sc.GetPersistentInt(5) == 1); // ← ZMIANA: shadow variable eliminated

    if (effectiveIsVisible) {
        // Jeśli podsystem wybudza się ze stanu ukrycia (-2), wymuszamy twarde otwarcie bramki obliczeniowej
        if (pData->LastSize == -2) {
            pData->LastSize = -1;
        }

        if (pData->LastSize == -1 || sc.IsFullRecalculation || pData->StabCounter > 0) { 
            pData->LastSize = sc.ArraySize;

            int searchRangeBars = sc.Input[10].GetInt(); // ← ZMIANA
            int limit = min(pData->numSaved, 30);
            for (int k = 0; k < limit; k++) {
                if (pData->drawings[k].priceA > 0.0f && pData->drawings[k].priceB > 0.0f) {

                    // 1. Dopasowanie indeksu Punktu A z precyzyjnym wyszukaniem ceny (Search Range Bars) // ← ZMIANA
                    int approxIdxA = sc.GetNearestMatchForSCDateTime(sc.ChartNumber, pData->drawings[k].timeA); // ← ZMIANA
                    if (approxIdxA >= 0 && approxIdxA < sc.ArraySize) { // ← ZMIANA
                        bool isAHigh = (fabs(sc.High[approxIdxA] - pData->drawings[k].priceA) < fabs(sc.Low[approxIdxA] - pData->drawings[k].priceA)); // ← ZMIANA
                        pData->drawings[k].idxA = FindBestMatchIndex(sc, pData->drawings[k].priceA, pData->drawings[k].timeA, isAHigh, searchRangeBars); // ← ZMIANA
                    } // ← ZMIANA

                    // 2. Dopasowanie indeksu Punktu B z precyzyjnym wyszukaniem ceny (Search Range Bars) // ← ZMIANA
                    int approxIdxB = sc.GetNearestMatchForSCDateTime(sc.ChartNumber, pData->drawings[k].timeB); // ← ZMIANA
                    if (approxIdxB >= 0 && approxIdxB < sc.ArraySize) { // ← ZMIANA
                        bool isBHigh = (fabs(sc.High[approxIdxB] - pData->drawings[k].priceB) < fabs(sc.Low[approxIdxB] - pData->drawings[k].priceB)); // ← ZMIANA
                        pData->drawings[k].idxB = FindBestMatchIndex(sc, pData->drawings[k].priceB, pData->drawings[k].timeB, isBHigh, searchRangeBars); // ← ZMIANA
                    } // ← ZMIANA

                    // Automatyczne przeliczenie cen stref dla nowych indeksów czasowych dla wszystkich 10 poziomów
                    float pA = pData->drawings[k].priceA;
                    float pB = pData->drawings[k].priceB;
                    
                    for (int i = 0; i < 10; i++) {
                        float rawPrice = pA + pData->drawings[k].customRatios[i] * (pB - pA);
                        pData->drawings[k].calculatedPrices[i] = sc.RoundToTickSize(rawPrice);
                    }

                    // Rekonstrukcja obiektów natywnych po przywróceniu widoczności lub zmianie interwału
                    if (abs(pData->drawings[k].idxB - pData->drawings[k].idxA) >= sc.Input[9].GetInt()) { // ← ZMIANA: nie odtwarzaj spine poniżej progu
                        ApplySpineLine(sc, pData->drawings[k].lineNumber, // ← ZMIANA
                            pData->drawings[k].idxA, pData->drawings[k].priceA,
                            pData->drawings[k].idxB, pData->drawings[k].priceB);
                    } else if (pData->drawings[k].lineNumber != 0) { // ← ZMIANA: poniżej progu — usuń jeśli jeszcze istnieje
                        sc.DeleteACSChartDrawing(sc.ChartNumber, 0, pData->drawings[k].lineNumber); // ← ZMIANA
                    } // ← ZMIANA
                }
            }
        }
        if (pData->StabCounter > 0) pData->StabCounter--;
    } 
else if (!effectiveIsVisible && pData->LastSize != -2) {
        // Masowa utylizacja linii pomocniczych hosta w ułamku sekundy wygaszenia widoczności
        int limit = min(pData->numSaved, 30);
        for (int k = 0; k < limit; k++) {
            if (pData->drawings[k].lineNumber != 0) {
                sc.DeleteACSChartDrawing(sc.ChartNumber, 0, pData->drawings[k].lineNumber);
            }
        }
        pData->LastSize = -2; // Aktywacja State -2 Guard: pętla wykona się dokładnie raz (ochrona CPU)
        pData->LastSyncValue = currentGlobalHide; // UNIFIKACJA SLAVE: Wyrównanie cache pamięci podręcznej do szyny globalnej
    }
}
}
