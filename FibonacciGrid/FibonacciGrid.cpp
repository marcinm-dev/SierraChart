#include "sierrachart.h"
#include <windows.h>
#include <cmath>

SCDLLName("FibonacciRetracement")

// --- BEZPIECZEŃSTWO BINARNE: Płaska struktura modelu A-B ---
#pragma pack(push, 1)
struct SavedRetracement {
    float priceA;
    float priceB;
    SCDateTime timeA;
    SCDateTime timeB;
    int idxA;
    int idxB;
    float calculatedPrices[10];
};

struct FibonacciData {
    SavedRetracement drawing;
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

    // Zabezpieczenie: Jeśli brak aktywnego rysowania i brak zapisanego rysunku - wychodzimy
    if (pData->TState == 0 && (pData->drawing.priceA == 0.0f || pData->drawing.priceB == 0.0f)) return;

    // Kapsułkowanie zasobów GDI - Tworzenie systemowych piór graficznych Windows
    HPEN hDashPen = ::CreatePen(PS_DASH, 1, RGB(128, 128, 128));  // Przerywane dla kotwicy i podglądu
    HPEN hSolidPen = ::CreatePen(PS_SOLID, 1, RGB(255, 140, 0)); // Ciągłe pomarańczowe dla stref siatki
    HPEN hOldPen = (HPEN)::SelectObject(hDC, hDashPen);

    // Dynamiczne pobranie współrzędnych myszy w pikselach
    int mouseX = sc.BarIndexToXPixelCoordinate(sc.ActiveToolIndex);
    int mouseY = sc.RegionValueToYPixelCoordinate((float)sc.ActiveToolYValue, sc.GraphRegion);

// 1. LOGIKA LIVE PREVIEW (Podgląd w locie)
    if (pData->TState == 2) {
        // Stan 2: Punkt A zatrzaśnięty, szukamy punktu B - ciągniemy przerywaną linię ukośną od A do myszy
        int xA = sc.BarIndexToXPixelCoordinate(pData->drawing.idxA);
        int yA = sc.RegionValueToYPixelCoordinate(pData->drawing.priceA, sc.GraphRegion);
        
        ::MoveToEx(hDC, xA, yA, NULL);
        ::LineTo(hDC, mouseX, mouseY);

        // --- JAWNE UCHWYTY KOTWICY (Wzór 1:1 z natywnego rysunku Sierry) ---
        HPEN hHandlePen = ::CreatePen(PS_SOLID, 2, RGB(0, 120, 255));   // Jasnoniebieski (Electric Blue), grubość 2px
        HBRUSH hHandleBrush = (HBRUSH)::GetStockObject(WHITE_BRUSH);    // Wymuszenie białego wypełnienia środka okręgu
        
        HPEN hOldPenRes = (HPEN)::SelectObject(hDC, hHandlePen);
        HBRUSH hOldBrushRes = (HBRUSH)::SelectObject(hDC, hHandleBrush);
        
        // [UCHWYT A] Rysowanie krążka o średnicy 9 pikseli wokół zamrożonego Punktu A
        ::Ellipse(hDC, xA - 4, yA - 4, xA + 5, yA + 5);
        
        // [UCHWYT B - PREVIEW] Rysowanie identycznego krążka podążającego płynnie za pozycją myszy
        ::Ellipse(hDC, mouseX - 4, mouseY - 4, mouseX + 5, mouseY + 5);
        
        // Przywrócenie pierwotnego stanu kontekstu graficznego i czyszczenie pamięci Win32
        ::SelectObject(hDC, hOldBrushRes);
        ::SelectObject(hDC, hOldPenRes);
        ::DeleteObject(hHandlePen);
    }

// 2. LOGIKA RENDEROWANIA ZAMROŻONEJ SIATKI - PANCERNY RESET FLAG RENDERERA GDI
    if (pData->drawing.priceA > 0.0f && pData->drawing.priceB > 0.0f) {
        int xA = sc.BarIndexToXPixelCoordinate(pData->drawing.idxA);
        int yA = sc.RegionValueToYPixelCoordinate(pData->drawing.priceA, sc.GraphRegion);
        int xB = sc.BarIndexToXPixelCoordinate(pData->drawing.idxB);
        int yB = sc.RegionValueToYPixelCoordinate(pData->drawing.priceB, sc.GraphRegion);

        // Bezwzględna normalizacja krawędzi horyzontalnych ramy siatki
        int xLeft  = (xA < xB) ? xA : xB;
        int xRight = (xA > xB) ? xA : xB;

        // Rysowanie dyskretnej ukośnej linii kotwicy łączącej realne punkty ekstremów A i B
        ::SelectObject(hDC, hDashPen);
        ::MoveToEx(hDC, xA, yA, NULL);
        ::LineTo(hDC, xB, yB);

        // Przełączenie parametrów na rysowanie stref i tekstu poziomego
        ::SelectObject(hDC, hSolidPen);
        ::SetBkMode(hDC, TRANSPARENT);
        ::SetTextColor(hDC, RGB(255, 140, 0));
        
        // FORTYFIKACJA GDI: Wymuszenie wyłączenia aktualizacji pozycji kursora (Zabójca błędu TA_UPDATECP)
        ::SetTextAlign(hDC, TA_NOUPDATECP); 

        const char* ratioStrings[5] = { "0", "0.382", "0.5", "0.618", "1" };

        for (int i = 0; i < 5; i++) {
            int yLevel = sc.RegionValueToYPixelCoordinate(pData->drawing.calculatedPrices[i], sc.GraphRegion);
            if (yLevel <= 0) continue;

            // Linia pozioma rozciąga się rygorystycznie od bezwzględnej lewej do bezwzględnej prawej osi X
            ::MoveToEx(hDC, xLeft, yLevel, NULL);
            ::LineTo(hDC, xRight, yLevel);

            // Inline Text & Split Formatting: Budujemy wirtualne prostokąty RECT oparte o znormalizowane xLeft i xRight
            // Lewa strona (Współczynnik przyklejony od zewnątrz do absolutnego początku linii z lewej, wyrównany do prawej)
            RECT rectLeft = { xLeft - 150, yLevel - 20, xLeft - 5, yLevel + 20 };
            ::DrawTextA(hDC, ratioStrings[i], -1, &rectLeft, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

            // Prawa strona (Precyzyjna cena rynkowa startująca zaraz po absolutnym zakończeniu linii z prawej, wyrównana do lewej)
            SCString priceStr = sc.FormatGraphValue(pData->drawing.calculatedPrices[i], sc.BaseGraphValueFormat);
            RECT rectRight = { xRight + 5, yLevel - 20, xRight + 150, yLevel + 20 };
            ::DrawTextA(hDC, priceStr.GetChars(), -1, &rectRight, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }
    }


    // Bezpieczne czyszczenie i uwalnianie uchwytów jądra Windows GDI (Zapobieganie GDI Leaks)
    ::SelectObject(hDC, hOldPen);
    ::DeleteObject(hDashPen);
    ::DeleteObject(hSolidPen);
}

// --- GŁÓWNA FUNKCJA WSKAŹNIKA ---
SCSFExport scsf_FibonacciRetracement(SCStudyInterfaceRef sc) {
    
    // 1. SEKCJA KONFIGURACJI DOMYŚLNEJ
    if (sc.SetDefaults) {
        sc.GraphName = "Fibonacci Retracement ";
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

    // 7. MASZYNA STANÓW MYSZY (UP-Based) Z SILNIKIEM MATEMATYCZNYM
    if (!sc.DownloadingHistoricalData) {
        if (sc.PointerEventType == SC_POINTER_BUTTON_UP && pData->TState > 0) {
            int cIdx = sc.ActiveToolIndex;
            float mY = (float)sc.ActiveToolYValue;
            int snapRange = 2; 

            if (pData->TState == 1) {
                bool isAHigh = (fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx]));
                int snappedAIdx = GetSnappedIndex(sc, cIdx, snapRange, isAHigh);
                
                pData->drawing.priceA = isAHigh ? sc.High[snappedAIdx] : sc.Low[snappedAIdx];
                pData->drawing.timeA = sc.BaseDateTimeIn[snappedAIdx];
                pData->drawing.idxA = snappedAIdx;
                
                pData->TState = 2; 
            }
            else if (pData->TState == 2) {
                bool isBHigh = (fabs(mY - sc.High[cIdx]) < fabs(mY - sc.Low[cIdx]));
                int snappedBIdx = GetSnappedIndex(sc, cIdx, snapRange, isBHigh);
                
                pData->drawing.priceB = isBHigh ? sc.High[snappedBIdx] : sc.Low[snappedBIdx];
                pData->drawing.timeB = sc.BaseDateTimeIn[snappedBIdx];
                pData->drawing.idxB = snappedBIdx;
                
// SILNIK MATEMATYCZNY (Wywołanie z Etapu 4) - ODWROTNY KIERUNEK (A = 0%, B = 100%)
                float ratios[5] = {0.000f, 0.382f, 0.500f, 0.618f, 1.000f};
                float pA = pData->drawing.priceA;
                float pB = pData->drawing.priceB;
                
                for (int i = 0; i < 5; i++) {
                    float rawPrice = pA + ratios[i] * (pB - pA);
                    pData->drawing.calculatedPrices[i] = sc.RoundToTickSize(rawPrice);
                }
                
                for (int i = 5; i < 10; i++) {
                    pData->drawing.calculatedPrices[i] = 0.0f;
                }
                
                sc.SetCustomStudyControlBarButtonEnable(31, 0); 
                pData->TState = 0;                             
            }
            
sc.PointerEventType = 0;
        }
    }

    // --- PULS BIENIA SERCA (Wymuszenie odświeżenia klatek GDI przy ruchu myszy) ---
    if (pData->TState > 0 && sc.PointerEventType == SC_POINTER_MOVE) {
        // Przypisanie dynamicznej wartości ceny do subgrafu alarmuje jądro Sierry.
        // Platforma natychmiast wywołuje zdarzenie przerysowania okna graficznego (GDI).
        sc.Subgraph[0][sc.ArraySize - 1] = (float)sc.ActiveToolYValue;
    }

    // 8. OBSŁUGA ZMIANY INTERWAŁÓW / REKULKULACJI
    if (pData->LastSize == -1 || sc.IsFullRecalculation) { 
        pData->LastSize = sc.ArraySize;
    }
}
