#include "sierrachart.h"
#include <windows.h>
#include <cmath>

SCDLLName("FibonacciGrid")

// --- BEZPIECZEŃSTWO BINARNE: Płaska struktura modelu A-B ---
#pragma pack(push, 1)
struct SavedGrid {
    float priceA;
    float priceB;
    SCDateTime timeA;
    SCDateTime timeB;
    int idxA;
    int idxB;
    float calculatedPrices[10];
};

struct FibonacciData {
    SavedGrid drawings[30]; // ROZSZERZENIE: Płaski bufor binarnego zapisu do 30 siatek jednocześnie
    int numSaved;           // Globalny licznik monitorujący całkowitą liczbę stworzonych rysunków
    int TState;
    int LastSize;
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

// --- ETAP 5: SILNIK RENDERUJĄCY GDI (Czyste Win32 API) ---
void DrawFibonacciGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc) {
    FibonacciData* pData = (FibonacciData*)sc.GetPersistentPointer(500);
    if (!pData) return;

// Kapsułkowanie zasobów GDI - Tworzenie systemowych piór graficznych Windows
HPEN hDashPen = ::CreatePen(PS_DASH, 1, RGB(128, 128, 128));  // Przerywane dla kotwicy i podglądu
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
        
        // Dynamiczne określenie genu Punktu A: czy zamrożona cena była szczytem (High) danej świecy?
        int aIdx = pData->drawings[activeSlot].idxA;
        bool isAHigh = (pData->drawings[activeSlot].priceA == sc.High[aIdx]);
        bool findBHigh = !isAHigh; // MATRYCA SYMETRII: Jeśli A to High -> B musi być Low. Jeśli A to Low -> B musi być High.

        // Przeliczenie pozycji podglądu B przez Snapping Engine (zbieżny z zakresem 2 barów)
        int cIdx = sc.ActiveToolIndex;
        int snappedBIdx = GetSnappedIndex(sc, cIdx, 2, findBHigh);
        float previewPriceB = findBHigh ? sc.High[snappedBIdx] : sc.Low[snappedBIdx];

        // Konwersja zablokowanych współrzędnych knotów na fizyczne piksele monitora
        int previewX = sc.BarIndexToXPixelCoordinate(snappedBIdx);
        int previewY = sc.RegionValueToYPixelCoordinate(previewPriceB, sc.GraphRegion);
        
        ::MoveToEx(hDC, xA, yA, NULL);
        ::LineTo(hDC, previewX, previewY);

        // --- JAWNE UCHWYTY KOTWICY (Wzór 1:1 z natywnego rysunku Sierry) ---
        HPEN hHandlePen = ::CreatePen(PS_SOLID, 2, RGB(0, 120, 255));   // Jasnoniebieski Electric Blue
        HBRUSH hHandleBrush = (HBRUSH)::GetStockObject(WHITE_BRUSH);    // Opaque White Masking
        
        HPEN hOldPenRes = (HPEN)::SelectObject(hDC, hHandlePen);
        HBRUSH hOldBrushRes = (HBRUSH)::SelectObject(hDC, hHandleBrush);
        
        ::Ellipse(hDC, xA - 4, yA - 4, xA + 5, yA + 5);
        ::Ellipse(hDC, previewX - 4, previewY - 4, previewX + 5, previewY + 5); // Krążek podglądu idealnie zablokowany na knocie
        
        ::SelectObject(hDC, hOldBrushRes);
        ::SelectObject(hDC, hOldPenRes);
        ::DeleteObject(hHandlePen);
    }

// 2. LOGIKA RENDEROWANIA ZAMROŻONYCH SIATEK - ITERACJA PO CAŁYM BUFORZE RAM
    int limit = min(pData->numSaved, 30);
    for (int k = 0; k < limit; k++) {
        SavedGrid& grid = pData->drawings[k];
        if (grid.priceA == 0.0f || grid.priceB == 0.0f) continue;
        if (grid.idxA >= sc.ArraySize || grid.idxB >= sc.ArraySize) continue;

        int xA = sc.BarIndexToXPixelCoordinate(grid.idxA);
        int yA = sc.RegionValueToYPixelCoordinate(grid.priceA, sc.GraphRegion);
        int xB = sc.BarIndexToXPixelCoordinate(grid.idxB);
        int yB = sc.RegionValueToYPixelCoordinate(grid.priceB, sc.GraphRegion);

        int xLeft  = (xA < xB) ? xA : xB;
        int xRight = (xA > xB) ? xA : xB;

        ::SelectObject(hDC, hDashPen);
        ::MoveToEx(hDC, xA, yA, NULL);
        ::LineTo(hDC, xB, yB);

        ::SelectObject(hDC, hSolidPen);
        ::SetBkMode(hDC, TRANSPARENT);
        ::SetTextColor(hDC, RGB(255, 140, 0));
        ::SetTextAlign(hDC, TA_NOUPDATECP); // Ochrona przed błędem TA_UPDATECP

        const char* ratioStrings[5] = { "0", "0.382", "0.5", "0.618", "1" };

        for (int i = 0; i < 5; i++) {
            int yLevel = sc.RegionValueToYPixelCoordinate(grid.calculatedPrices[i], sc.GraphRegion);
            if (yLevel <= 0) continue;

            ::MoveToEx(hDC, xLeft, yLevel, NULL);
            ::LineTo(hDC, xRight, yLevel);

            // Inline Text & Split Formatting (Współrzędne pobierane z referencji siatki w pętli)
            RECT rectLeft = { xLeft - 150, yLevel - 20, xLeft - 5, yLevel + 20 };
            ::DrawTextA(hDC, ratioStrings[i], -1, &rectLeft, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

            SCString priceStr = sc.FormatGraphValue(grid.calculatedPrices[i], sc.BaseGraphValueFormat);
            RECT rectRight = { xRight + 5, yLevel - 20, xRight + 150, yLevel + 20 };
            ::DrawTextA(hDC, priceStr.GetChars(), -1, &rectRight, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }
    }



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

        sc.ReceivePointerEvents = ACS_RECEIVE_POINTER_EVENTS_WHEN_ACS_BUTTON_ENABLED; 
        sc.ReceiveCharacterEvents = 1; 

sc.p_GDIFunction = DrawFibonacciGDI; 
        sc.ScaleRangeType = SCALE_SAMEASREGION;   

// --- STYMULATOR ODŚWIEŻANIA (Heartbeat Trigger dla trybu Offline) ---
        sc.Subgraph[0].Name = "Heartbeat";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_HIDDEN; // Ukryty, nie generuje linii ani napisów

        // INTEGRACJA Z CONTROL BAR (PRZYCISK 31) - Konfiguracja jednorazowa
        sc.SetCustomStudyControlBarButtonText(31, "FIB RETR"); 

        return;
    }

    // 2. ALOKACJA PAMIĘCI PERSISTENT POINTER
    FibonacciData* pData = (FibonacciData*)sc.GetPersistentPointer(500); 

    if (pData == NULL) {
        pData = (FibonacciData*)sc.AllocateMemory(sizeof(FibonacciData));
        if (pData != NULL) {
            memset(pData, 0, sizeof(FibonacciData));
            sc.SetPersistentPointer(500, pData);
            pData->LastSize = -1;
            pData->TState = 0;
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



// 5. POPRAWIONY HANDSHAKE PASKA I POINTER PROXY (Wzorzec z DiNapoli)
    if (sc.PointerEventType == SC_ACS_BUTTON_ON && sc.MenuEventID == 31) {
        pData->TState = 1; 
        
        HWND hRoot = ::GetAncestor((HWND)sc.ChartWindowHandle, GA_ROOT);
        if (hRoot != NULL) {
            int CachedPointerID = FindSierraMenuCommandID(hRoot, "Tools", "Pointer");
            if (CachedPointerID > 0) {
                ::PostMessageA(hRoot, WM_COMMAND, CachedPointerID, 0);
            }
        }
    }

    // 6. ASYNCHRONICZNY HAMULEC BEZPIECZEŃSTWA
    if (sc.CharacterEventCode == 32 && pData->TState > 0) {
        pData->TState = 0;
        sc.SetCustomStudyControlBarButtonEnable(31, 0);
        sc.CharacterEventCode = 0;
    }

// 7. MASZYNA STANÓW MYSZY (UP-Based) Z MULTI-BUFOREM KOŁOWYM
    if (!sc.DownloadingHistoricalData) {
        if (sc.PointerEventType == SC_POINTER_BUTTON_UP && pData->TState > 0) {
            int cIdx = sc.ActiveToolIndex;
            float mY = (float)sc.ActiveToolYValue;
            int snapRange = 2; 
            int activeSlot = pData->numSaved % 30; // Zabezpieczenie: dynamiczny przydział slotu bufora

            if (pData->TState == 1) {
                bool isAHigh = (fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx]));
                int snappedAIdx = GetSnappedIndex(sc, cIdx, snapRange, isAHigh);
                
                pData->drawings[activeSlot].priceA = isAHigh ? sc.High[snappedAIdx] : sc.Low[snappedAIdx];
                pData->drawings[activeSlot].timeA = sc.BaseDateTimeIn[snappedAIdx];
                pData->drawings[activeSlot].idxA = snappedAIdx;
                
                pData->TState = 2; 
            }
else if (pData->TState == 2) {
                // Odczytanie genu startowego z zamrożonej struktury Punktu A
                int aIdx = pData->drawings[activeSlot].idxA;
                bool isAHigh = (pData->drawings[activeSlot].priceA == sc.High[aIdx]);
                bool findBHigh = !isAHigh; // Wymuszenie rygoru: Punkt B bezwarunkowo przyjmuje przeciwległy knot fali
                
                int snappedBIdx = GetSnappedIndex(sc, cIdx, snapRange, findBHigh);
                
                pData->drawings[activeSlot].priceB = findBHigh ? sc.High[snappedBIdx] : sc.Low[snappedBIdx];
                pData->drawings[activeSlot].timeB = sc.BaseDateTimeIn[snappedBIdx];
                pData->drawings[activeSlot].idxB = snappedBIdx;
                
                // SILNIK MATEMATYCZNY - KIERUNEK WEKTORA (A = 0%, B = 100%)
                float ratios[5] = {0.000f, 0.382f, 0.500f, 0.618f, 1.000f};
                float pA = pData->drawings[activeSlot].priceA;
                float pB = pData->drawings[activeSlot].priceB;
                
                for (int i = 0; i < 5; i++) {
                    float rawPrice = pA + ratios[i] * (pB - pA);
                    pData->drawings[activeSlot].calculatedPrices[i] = sc.RoundToTickSize(rawPrice);
                }
                
                for (int i = 5; i < 10; i++) {
                    pData->drawings[activeSlot].calculatedPrices[i] = 0.0f;
                }
                
                pData->numSaved++; // Sukces: przesunięcie licznika globalnego (zamyka i zabezpiecza strukturę w RAM)
                sc.SetCustomStudyControlBarButtonEnable(31, 0); 
                pData->TState = 0;                             
            }
        }
    }

    // --- PULS BIENIA SERCA (Wymuszenie odświeżenia klatek GDI przy ruchu myszy) ---
    if (pData->TState > 0 && sc.PointerEventType == SC_POINTER_MOVE) {
        // Przypisanie dynamicznej wartości ceny do subgrafu alarmuje jądro Sierry.
        // Platforma natychmiast wywołuje zdarzenie przerysowania okna graficznego (GDI).
        sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue;
    }

// 8. OBSŁUGA ZMIANY INTERWAŁÓW / REKALKULACJI (Wielobiektowy Timeframe Search Engine)
    if (pData->LastSize == -1 || sc.IsFullRecalculation) { 
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

                // Automatyczne przeliczenie cen stref dla nowych indeksów czasowych
                float ratios[5] = {0.000f, 0.382f, 0.500f, 0.618f, 1.000f};
                float pA = pData->drawings[k].priceA;
                float pB = pData->drawings[k].priceB;
                
                for (int i = 0; i < 5; i++) {
                    float rawPrice = pA + ratios[i] * (pB - pA);
                    pData->drawings[k].calculatedPrices[i] = sc.RoundToTickSize(rawPrice);
                }
            }
        }
    }
}
