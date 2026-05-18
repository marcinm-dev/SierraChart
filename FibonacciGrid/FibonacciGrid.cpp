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

// --- ETAP 3: PROGRAMOWA PROCEDURA OKNA MODALNEGO (WYGODA Z TRADE NAVIGATORA) ---
LRESULT CALLBACK ModalConfigWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SavedGrid* pGrid = (SavedGrid*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);

    switch (message) {

case WM_CREATE: {
        CREATESTRUCTA* pCreate = (CREATESTRUCTA*)lParam;
        pGrid = (SavedGrid*)pCreate->lpCreateParams;
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)pGrid);

// Kompaktowa matryca rozmieszczenia kontrolek dla 10 poziomów (Bez przycisku tekstowego)
        for (int i = 0; i < 10; i++) {
            HWND hCheck = ::CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 15 + i * 25, 20, 20, hWnd, (HMENU)(UINT_PTR)(100 + i), NULL, NULL);
            SendMessageA(hCheck, BM_SETCHECK, pGrid->customUse[i] ? BST_CHECKED : BST_UNCHECKED, 0);

            char buf[32];
            sprintf(buf, "%g", pGrid->customRatios[i]);
            ::CreateWindowA("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 50, 15 + i * 25, 60, 20, hWnd, (HMENU)(UINT_PTR)(200 + i), NULL, NULL);
        }

        // Przyciski przeniesione na dół zaktualizowanego formularza (y = 280)
        ::CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 40, 280, 80, 25, hWnd, (HMENU)(UINT_PTR)IDOK, NULL, NULL);
        ::CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE, 140, 280, 80, 25, hWnd, (HMENU)(UINT_PTR)IDCANCEL, NULL, NULL);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = ::BeginPaint(hWnd, &ps);
        if (pGrid) {
            // Próbki kwadratów koloru dla wszystkich 10 stref
            for (int i = 0; i < 10; i++) {
                HBRUSH hBrush = ::CreateSolidBrush(pGrid->customColors[i]);
                RECT r = { 205, 15 + i * 25, 235, 15 + i * 25 + 20 };
                ::FillRect(hdc, &r, hBrush);
                ::DeleteObject(hBrush);
            }
        }
        ::EndPaint(hWnd, &ps);
        break;
    }

case WM_LBUTTONDOWN: {
        if (!pGrid) break;
        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);

        // Hit-Testing: Sprawdzamy czy kursor znajduje się w horyzontalnych granicach kolumny kwadratów (X: 205 do 235)
        if (mouseX >= 205 && mouseX <= 235) {
            for (int i = 0; i < 10; i++) {
                int yTop = 15 + i * 25;
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
        if (!pGrid) return ::DefWindowProcA(hWnd, message, wParam, lParam);
        int wmId = LOWORD(wParam);

        if (wmId == IDOK) {
            // Zrzucenie 10 rekordów z kontrolek Win32 do bufora RAM
            for (int i = 0; i < 10; i++) {
                HWND hCheck = ::GetDlgItem(hWnd, 100 + i);
                pGrid->customUse[i] = (::SendMessageA(hCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

                HWND hEdit = ::GetDlgItem(hWnd, 200 + i);
                char buf[64];
                ::GetWindowTextA(hEdit, buf, 64);
                pGrid->customRatios[i] = (float)atof(buf);
            }
            ::DestroyWindow(hWnd);
        }
        else if (wmId == IDCANCEL) {
            ::DestroyWindow(hWnd);
        }
        break;
    }


case WM_DESTROY:
        // NAPRAWA ETAPU 4: Usunięto PostQuitMessage(0) - to ono bezprawnie pożerało wątek główny jądra Sierra Chart!
        break;
    default:
        return ::DefWindowProcA(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Funkcja zarządzająca wstrzykiwaniem okna i blokowaniem wątku nadrzędnego (Trwały Tryb Modalny)
void ShowModalConfig(HWND hParent, SavedGrid& grid) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = ModalConfigWndProc;
    wc.hInstance = ::GetModuleHandleA(NULL);
    wc.hCursor = ::LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "FibGridConfigClass";
    ::RegisterClassA(&wc);

HWND hwndDlg = ::CreateWindowExA(WS_EX_DLGMODALFRAME, "FibGridConfigClass", "Properties (1 Profile / 10 Levels)", 
        WS_POPUPWINDOW | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 265, 355, hParent, NULL, wc.hInstance, &grid);

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

// Kapsułkowanie zasobów GDI - Tworzenie systemowych piór graficznych Windows
HPEN hDashPen = ::CreatePen(PS_DOT, 1, RGB(0, 0, 255));  // Kropkowane (Blue) dla kotwicy i podglądu
    HPEN hSolidPen = ::CreatePen(PS_SOLID, 1, RGB(255, 140, 0)); // Ciągłe pomarańczowe dla stref siatki
    HPEN hOldPen = (HPEN)::SelectObject(hDC, hDashPen);

    // Wdrożona prawidłowo, pojedyncza systemowa czcionka Arial Bold 8
    HFONT hFont = ::CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
    HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont); // Wstrzyknięcie czcionki do potoku renderera Windows


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
        int snappedBIdx = GetSnappedIndex(sc, cIdx, 2, findBHigh);
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
    int limit = min(pData->numSaved, 30);
    for (int k = 0; k < limit; k++) {
        SavedGrid& grid = pData->drawings[k];
        
        // PIERWSZE ZADANIE: Bezpieczny warunek renderowania oparty na cenach (zapobiega pomijaniu odczytu z dysku)
        if (grid.priceA == 0.0f || grid.priceB == 0.0f) continue; 

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

        int xA = sc.BarIndexToXPixelCoordinate(drawIdxA);
        int xB = sc.BarIndexToXPixelCoordinate(drawIdxB);

        int xLeft  = (xA < xB) ? xA : xB;
        int xRight = (xA > xB) ? xA : xB;

        // DRUGIE ZADANIE: Jeśli linia natywna wygasła po restarcie platformy, silnik GDI awaryjnie sam rysuje przerywany grzbiet (Spine)
        if (!lineFound) {
            int yA = sc.RegionValueToYPixelCoordinate(drawPriceA, sc.GraphRegion);
            int yB = sc.RegionValueToYPixelCoordinate(drawPriceB, sc.GraphRegion);
            ::MoveToEx(hDC, xA, yA, NULL);
            ::LineTo(hDC, xB, yB);
        }

        ::SetBkMode(hDC, TRANSPARENT);
        ::SetTextAlign(hDC, TA_NOUPDATECP); 

        for (int i = 0; i < 10; i++) {
            if (!grid.customUse[i]) continue; // RESTRYKCJA ERGONOMII: Ukrywanie linii wyłączonych w Menu

            int yLevel = sc.RegionValueToYPixelCoordinate(tmpCalcPrices[i], sc.GraphRegion);
            if (yLevel <= 0) continue;

// Dynamiczne wyznaczenie grubości (poziomy bazowe 0.000 i 1.000 otrzymują grubość 2, reszta 1)
int currentLineWidth = (i == 0 || i == 9) ? 2 : 1;

// Dynamiczna kreacja i natychmiastowa utylizacja wielokolorowych piór GDI jądra Windows
HPEN hCustomPen = ::CreatePen(PS_SOLID, currentLineWidth, grid.customColors[i]);
HPEN hOldCustomPen = (HPEN)::SelectObject(hDC, hCustomPen);
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

            ::SelectObject(hDC, hOldCustomPen);
            ::DeleteObject(hCustomPen);
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



// Bezpieczne czyszczenie i uwalnianie uchwytów jądra Windows GDI (Zapobieganie GDI Leaks)




// Bezpieczne czyszczenie i uwalnianie uchwytów jądra Windows GDI (Zapobieganie GDI Leaks)
    ::SelectObject(hDC, hOldPen);
    ::SelectObject(hDC, hOldFont); // Przywrócenie domyślnej czcionki platformy Sierra Chart
    ::DeleteObject(hDashPen);
    ::DeleteObject(hSolidPen);
    ::DeleteObject(hFont);         // BEZPIECZEŃSTWO: Fizyczne usunięcie czcionki z jądra OS Windows
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

// --- STYMULATOR ODŚWIEŻANIA (Heartbeat Trigger dla trybu Offline) ---
        sc.Subgraph[0].Name = "Heartbeat";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_HIDDEN; // Ukryty, nie generuje linii ani napisów

// INTEGRACJA Z CONTROL BAR - Konfiguracja wejść dla przycisków (Elastyczność UI)
        sc.Input[0].Name = "Control Bar Button ID - Draw Tool";
        sc.Input[0].SetInt(31);

        sc.Input[1].Name = "Control Bar Button ID - Last Delete";
        sc.Input[1].SetInt(35);

        sc.Input[2].Name = "Control Bar Button ID - All Delete";
        sc.Input[2].SetInt(36);

        // --- ETAP 2 HIDE: Konfiguracja wejść integracyjnych dla trybu Slave ---
        sc.Input[3].Name = "Sync z Master HIDE (Button 20)";
        sc.Input[3].SetYesNo(1);

        sc.Input[4].Name = "Master Hide Button ID";
        sc.Input[4].SetInt(20);

        // Nadanie nazw przyciskom na pasku na podstawie dynamicznych wartości z menu ustawień
        sc.SetCustomStudyControlBarButtonText(sc.Input[0].GetInt(), "FIB RETR"); 
        sc.SetCustomStudyControlBarButtonText(sc.Input[1].GetInt(), "LAST DEL"); 
        sc.SetCustomStudyControlBarButtonText(sc.Input[2].GetInt(), "ALL DEL"); 

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

            // NAPRAWA: Dodano nawiasy () do sc.DataFilesFolder() przed wywołaniem GetChars()
            char fn[512];
            sprintf(fn, "%sFibGrid_%d_%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
            FILE* f = fopen(fn, "rb");
            if (f) {
                fread(pData, sizeof(FibonacciData), 1, f);
                fclose(f);
                
                // Sanacja i mrożenie stanów ulotnych jądra myszy po wciągnięciu płaskiego bloku bajtów RAM
                pData->LastSize = -1;
                pData->TState = 0;
                pData->DraggingIdx = -1;
                pData->DraggedPoint = 0;
                pData->SelectedIdx = -1;
            }
        }
    }

    // 3. CZYSZCZENIE PAMIĘCI RAM PRZY USUWANIU WSKAŹNIKA
    if (sc.LastCallToFunction) { 
        if (pData != NULL) {
            sc.FreeMemory(pData);
            sc.SetPersistentPointer(500, NULL);
        }
        return;
    }

if (pData == NULL) return;

    // DYNAMICZNE MAPOWANIE PRZYCISKÓW Z MENU USTAWIEN (sc.Input) - Likwidacja sztywnych literałów
    int DrawBtn    = sc.Input[0].GetInt();
    int LastDelBtn = sc.Input[1].GetInt();
    int AllDelBtn  = sc.Input[2].GetInt();
    int HideBtn    = sc.Input[4].GetInt(); // ETAP 3 HIDE: Mapowanie przycisku ukrywania

    // --- ETAP 3 HIDE: Most decyzyjny i osłona GPU (Slave Mode) ---
    if (sc.IsFullRecalculation) {
        pData->StabCounter = 10; // Amortyzacja na zimnym starcie
    }

    int currentGlobalHide = sc.GetPersistentIntFromChartStudy(sc.ChartNumber, 0, GLOBAL_SYNC_SLOT);
    bool isSyncImpulse = (sc.Input[3].GetYesNo() && sc.MenuEventID == HideBtn);
    
    bool effectiveIsVisible = (currentGlobalHide == 0);
    if (isSyncImpulse) {
        pData->StabCounter = 10;  // Stabilizacja klatek przy przełączaniu
        pData->LastSize = -1;     // Wymuszenie otwarcia bramki redrawu
        if (currentGlobalHide == pData->LastSyncValue) {
            effectiveIsVisible = !effectiveIsVisible;
            pData->LastSyncValue = effectiveIsVisible ? 0 : 1; 
        }
    }

    // Most decyzyjny (Single Source of Truth) dla silnika GDI - Rejestracja w Slocie 5
    sc.SetPersistentInt(5, effectiveIsVisible ? 1 : 0);

// 5. POPRAWIONY HANDSHAKE PASKA I POINTER PROXY (Wzorzec z DiNapoli)

    if (sc.PointerEventType == SC_ACS_BUTTON_ON && sc.MenuEventID == DrawBtn) {
        pData->TState = 1; 
        
        HWND hRoot = ::GetAncestor((HWND)sc.ChartWindowHandle, GA_ROOT);
        if (hRoot != NULL) {
            int CachedPointerID = FindSierraMenuCommandID(hRoot, "Tools", "Pointer");
            if (CachedPointerID > 0) {
                ::PostMessageA(hRoot, WM_COMMAND, CachedPointerID, 0);
            }
        }
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
        sc.SetCustomStudyControlBarButtonEnable(DrawBtn, 0);
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
            sprintf(fn, "%sFibGrid_%d_%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
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
        sprintf(fn, "%sFibGrid_%d_%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
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
        int snapRange = 2;

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


                s_UseTool SpineLine;
                SpineLine.Clear();
                SpineLine.ChartNumber = sc.ChartNumber;
                SpineLine.DrawingType = DRAWING_LINE;
                SpineLine.LineNumber = pData->drawings[k].lineNumber;
                SpineLine.AddMethod = UTAM_ADD_OR_ADJUST;
                SpineLine.AddAsUserDrawnDrawing = 0; 
                SpineLine.BeginIndex = pData->drawings[k].idxA;
                SpineLine.BeginValue = pData->drawings[k].priceA;
                SpineLine.EndIndex = pData->drawings[k].idxB;
                SpineLine.EndValue = pData->drawings[k].priceB;
SpineLine.Color = RGB(0, 0, 255);
SpineLine.LineStyle = LINESTYLE_DOT;
                SpineLine.LineWidth = 1;
                sc.UseTool(SpineLine);
            }
            else if (pData->TState == 2) {
                int aIdx = pData->drawings[activeSlot].idxA;
                bool isAHigh = (pData->drawings[activeSlot].priceA == sc.High[aIdx]);
                bool findBHigh = !isAHigh;
                int snappedBIdx = GetSnappedIndex(sc, cIdx, 2, findBHigh);
                float previewPriceB = findBHigh ? sc.High[snappedBIdx] : sc.Low[snappedBIdx];

                s_UseTool SpineLine;
                SpineLine.Clear();
                SpineLine.ChartNumber = sc.ChartNumber;
                SpineLine.DrawingType = DRAWING_LINE;
                SpineLine.LineNumber = pData->drawings[activeSlot].lineNumber;
                SpineLine.AddMethod = UTAM_ADD_OR_ADJUST;
                SpineLine.AddAsUserDrawnDrawing = 0; 
                SpineLine.BeginIndex = pData->drawings[activeSlot].idxA;
                SpineLine.BeginValue = pData->drawings[activeSlot].priceA;
                SpineLine.EndIndex = snappedBIdx;
                SpineLine.EndValue = previewPriceB;
SpineLine.Color = RGB(0, 0, 255);
SpineLine.LineStyle = LINESTYLE_DOT;
                SpineLine.LineWidth = 1;
                sc.UseTool(SpineLine);
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
                            // Wywołanie autorskiego okna modalnego Win32 skompilowanego bezpośrednio wewnątrz DLL
                            ShowModalConfig((HWND)sc.ChartWindowHandle, pData->drawings[newSelectedIdx]);
                            
// Wymuszenie natychmiastowej rekalkulacji ramy cenowej Fibo po zamknięciu okna dialogowego dla 10 stref
                            float pA = pData->drawings[newSelectedIdx].priceA;
                            float pB = pData->drawings[newSelectedIdx].priceB;
                            for (int i = 0; i < 10; i++) {
                                pData->drawings[newSelectedIdx].calculatedPrices[i] = sc.RoundToTickSize(pA + pData->drawings[newSelectedIdx].customRatios[i] * (pB - pA));
                            }
                            
                            // NAPRAWA: Korekta wywołania sc.DataFilesFolder() z nawiasami
                            char fn[512];
                            sprintf(fn, "%sFibGrid_%d_%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
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

// ETAP 3: Kompletna inicjalizacja 10 poziomów (Pełna chronologia rynkowa)
                    float defaultRatios[10] = {0.000f, 0.250f, 0.382f, 0.500f, 0.618f, 0.700f, 0.786f, 0.850f, 0.886f, 1.000f};
                    COLORREF defaultColors[10] = {
                        RGB(0, 0, 255),   // 0: Blue
                        RGB(255, 0, 0),   // 0.25: Red
                        RGB(255, 0, 0),   // 0.382: Red
                        RGB(0, 0, 255),   // 0.5: Blue
                        RGB(255, 0, 0),   // 0.618: Red
                        RGB(0, 0, 0),     // 0.7: Black
                        RGB(255, 0, 0),   // 0.786: Red
                        RGB(255, 0, 0),   // 0.85: Red (Teraz poprawnie przed 0.886)
                        RGB(128, 128, 128),// 0.886: Grey
                        RGB(0, 0, 255)    // 1: Blue
                    };
// Matryca widoczności: indeksy 1 (0.25), 2 (0.382) oraz 3 (0.5) otrzymują stan 0 (wyłączone)
int defaultUse[10] = { 1, 0, 0, 0, 1, 1, 1, 0, 1, 1 };

for (int i = 0; i < 10; i++) {
    pData->drawings[activeSlot].customRatios[i] = defaultRatios[i];
    pData->drawings[activeSlot].customColors[i] = defaultColors[i];
    pData->drawings[activeSlot].customUse[i] = defaultUse[i];
}

                    s_UseTool SpineLine;
                    SpineLine.Clear();
                    SpineLine.ChartNumber = sc.ChartNumber;
                    SpineLine.DrawingType = DRAWING_LINE;
                    SpineLine.AddMethod = UTAM_ADD_OR_ADJUST;
                    SpineLine.AddAsUserDrawnDrawing = 0; 
                    SpineLine.BeginIndex = snappedAIdx;
                    SpineLine.BeginValue = pData->drawings[activeSlot].priceA;
                    SpineLine.EndIndex = snappedAIdx;
                    SpineLine.EndValue = pData->drawings[activeSlot].priceA;
SpineLine.Color = RGB(0, 0, 255);
SpineLine.LineStyle = LINESTYLE_DOT;
                    SpineLine.LineWidth = 1;
                    sc.UseTool(SpineLine);

                    pData->drawings[activeSlot].lineNumber = SpineLine.LineNumber; 
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
                    
                    s_UseTool SpineLine;
                    SpineLine.Clear();
                    SpineLine.ChartNumber = sc.ChartNumber;
                    SpineLine.DrawingType = DRAWING_LINE;
                    SpineLine.LineNumber = pData->drawings[activeSlot].lineNumber;
                    SpineLine.AddMethod = UTAM_ADD_OR_ADJUST;
                    SpineLine.AddAsUserDrawnDrawing = 0; 
                    SpineLine.BeginIndex = pData->drawings[activeSlot].idxA;
                    SpineLine.BeginValue = pData->drawings[activeSlot].priceA;
                    SpineLine.EndIndex = snappedBIdx;
                    SpineLine.EndValue = pData->drawings[activeSlot].priceB;
SpineLine.Color = RGB(0, 0, 255);
SpineLine.LineStyle = LINESTYLE_DOT;
                    SpineLine.LineWidth = 1;
                    sc.UseTool(SpineLine);
                    
float pA = pData->drawings[activeSlot].priceA;
                    float pB = pData->drawings[activeSlot].priceB;
                    
                    for (int i = 0; i < 10; i++) {
                        float rawPrice = pA + pData->drawings[activeSlot].customRatios[i] * (pB - pA);
                        pData->drawings[activeSlot].calculatedPrices[i] = sc.RoundToTickSize(rawPrice);
                    }
                    
                    pData->numSaved++; 

// NAPRAWA: Korekta wywołania sc.DataFilesFolder() z nawiasami
                    char fn[512];
                    sprintf(fn, "%sFibGrid_%d_%d.dat", sc.DataFilesFolder().GetChars(), sc.ChartNumber, sc.StudyGraphInstanceID);
                    FILE* f = fopen(fn, "wb");
                    if (f) {
                        fwrite(pData, sizeof(FibonacciData), 1, f);
                        fclose(f);
                    }

                    sc.SetCustomStudyControlBarButtonEnable(DrawBtn, 0); 
                    pData->TState = 0;                             
                }
            }
        }


    // --- AUTOMATYCZNY AMORTYZATOR I BIENIE SERCA REFRESHERA (Wymuszenie klatek GDI Offline) ---
    if ((pData->TState > 0 || pData->DraggingIdx != -1) && sc.PointerEventType == SC_POINTER_MOVE) {
        sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue;
    }


// 8. OBSŁUGA ZMIANY INTERWAŁÓW / REKALKULACJI (Wielobiektowy Timeframe Search Engine)
    bool effectiveIsVisible = (sc.GetPersistentInt(5) == 1);

    if (effectiveIsVisible) {
        // Jeśli podsystem wybudza się ze stanu ukrycia (-2), wymuszamy twarde otwarcie bramki obliczeniowej
        if (pData->LastSize == -2) {
            pData->LastSize = -1;
        }

        if (pData->LastSize == -1 || sc.IsFullRecalculation || pData->StabCounter > 0) { 
            pData->LastSize = sc.ArraySize;

            int limit = min(pData->numSaved, 30);
            for (int k = 0; k < limit; k++) {
                if (pData->drawings[k].priceA > 0.0f && pData->drawings[k].priceB > 0.0f) {
                    
                    // 1. Dopasowanie indeksu Punktu A po unikalnej kotwicy czasu (DateTime)
                    int newIdxA = sc.GetNearestMatchForSCDateTime(sc.ChartNumber, pData->drawings[k].timeA);
                    if (newIdxA >= 0 && newIdxA < sc.ArraySize) {
                        pData->drawings[k].idxA = newIdxA;
                    }

                    // 2. Dopasowanie indeksu Punktu B po unikalnej kotwicy czasu (DateTime)
                    int newIdxB = sc.GetNearestMatchForSCDateTime(sc.ChartNumber, pData->drawings[k].timeB);
                    if (newIdxB >= 0 && newIdxB < sc.ArraySize) {
                        pData->drawings[k].idxB = newIdxB;
                    }

                    // Automatyczne przeliczenie cen stref dla nowych indeksów czasowych dla wszystkich 10 poziomów
                    float pA = pData->drawings[k].priceA;
                    float pB = pData->drawings[k].priceB;
                    
                    for (int i = 0; i < 10; i++) {
                        float rawPrice = pA + pData->drawings[k].customRatios[i] * (pB - pA);
                        pData->drawings[k].calculatedPrices[i] = sc.RoundToTickSize(rawPrice);
                    }

                    // Rekonstrukcja obiektów natywnych po przywróceniu widoczności lub zmianie interwału
                    s_UseTool SpineLine;
                    SpineLine.Clear();
                    SpineLine.ChartNumber = sc.ChartNumber;
                    SpineLine.DrawingType = DRAWING_LINE;
                    SpineLine.LineNumber = pData->drawings[k].lineNumber;
                    SpineLine.AddMethod = UTAM_ADD_OR_ADJUST;
                    SpineLine.AddAsUserDrawnDrawing = 0; 
                    SpineLine.BeginIndex = pData->drawings[k].idxA;
                    SpineLine.BeginValue = pData->drawings[k].priceA;
                    SpineLine.EndIndex = pData->drawings[k].idxB;
                    SpineLine.EndValue = pData->drawings[k].priceB;
SpineLine.Color = RGB(0, 0, 255);
SpineLine.LineStyle = LINESTYLE_DOT;
                    SpineLine.LineWidth = 1;
                    sc.UseTool(SpineLine);
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
