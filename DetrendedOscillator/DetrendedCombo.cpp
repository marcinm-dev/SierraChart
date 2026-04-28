#include "sierrachart.h"

SCDLLName("DiNapoli Detrended Oscillator")

// --- DINAPOLI DETRENDED OSCILLATOR ---
SCSFExport scsf_DetrendedOscillator(SCStudyInterfaceRef sc)
{
    // Definicja linii wyświetlanych na wykresie
    SCSubgraphRef Detrend   = sc.Subgraph[0];
    SCSubgraphRef ZeroLine  = sc.Subgraph[1];
    SCSubgraphRef UpperBand = sc.Subgraph[2];
    SCSubgraphRef LowerBand = sc.Subgraph[3];
    
// --- ZMIANA: SMA_Array jako tablica wewnętrzna (nie zajmuje slotu Subgraph) ---
    SCFloatArrayRef SMA_Array = Detrend.Arrays[0];

// Parametry wejściowe (Inputs)
    SCInputRef SMALength  = sc.Input[0];
    SCInputRef UpperLevel = sc.Input[1];
    SCInputRef LowerLevel = sc.Input[2];
SCInputRef ZeroLevel  = sc.Input[3];
    SCInputRef BtnID      = sc.Input[4]; 
	SCInputRef FixedHeight = sc.Input[5]; // Nowy parametr wysokości

// --- FAZA 2: ARCHITEKTURA PAMIĘCI (IZOLACJA SLOTÓW 120-122) ---
    int& MasterState   = sc.GetPersistentInt(120); // Relokacja z 20 na 120
    int& Initialized    = sc.GetPersistentInt(121); // Relokacja z 21 na 121
    int& LastHideStudy = sc.GetPersistentInt(122); // Relokacja z 22 na 122

if (sc.SetDefaults)
    {
sc.GraphName = "Detrended Oscillator";
sc.GraphRegion = 1;        
        sc.AutoLoop = 1;
        sc.UpdateAlways = 0;       
        sc.MaintainAdditionalChartDataArrays = 0; 
        sc.ValueFormat = VALUEFORMAT_INHERITED;

        // --- KONFIGURACJA WIDOCZNOŚCI (CLEAN HEADER) ---
        sc.DisplayStudyName = 0;           // Ukrywa nazwę w nagłówku
        sc.DisplayStudyInputValues = 0;    // Ukrywa parametry w nagłówku
        sc.GlobalDisplayStudySubgraphsNameAndValue = 1; // Globalny switch dla celownika

// 1. Główna linia oscylatora (Detrended Oscillator)
        Detrend.Name = "Detrend Osc";
        Detrend.DrawStyle = DRAWSTYLE_LINE;
        Detrend.PrimaryColor = RGB(0, 0, 255); 
        Detrend.LineWidth = 1;

        // Ustawienia widoczności zgodne z Twoją prośbą:
        Detrend.DisplayNameValueInDataLine = 0;       
        Detrend.DisplayNameValueInWindowsFlags = 1;   
        Detrend.IncludeInSpreadsheet = 0;

        // 2. Linia zerowa
        ZeroLine.Name = "Zero Line";
        ZeroLine.DrawStyle = DRAWSTYLE_LINE_FROM_END_OF_CHART_LEFT_TO_RIGHT;
        ZeroLine.LineStyle = LINESTYLE_DOT;
        ZeroLine.PrimaryColor = RGB(128, 128, 128); 
        ZeroLine.LineWidth = 1;
        ZeroLine.DrawZeros = true; 
        // Ustawienia widoczności:
        ZeroLine.DisplayNameValueInDataLine = 0;      // Ukryte w nagłówku
        ZeroLine.DisplayNameValueInWindowsFlags = 0;  // Ukryte w celowniku
        ZeroLine.IncludeInSpreadsheet = 0;            // Ukryte w Spreadsheet

        // 3. Górna linia (Overbought)
        UpperBand.Name = "Upper Band";
        UpperBand.DrawStyle = DRAWSTYLE_LINE_FROM_END_OF_CHART_LEFT_TO_RIGHT;
        UpperBand.PrimaryColor = RGB(255, 128, 0); 
        UpperBand.LineWidth = 2;
        UpperBand.DrawZeros = true; 
        // Ustawienia widoczności:
        UpperBand.DisplayNameValueInDataLine = 0;     // Ukryte w nagłówku
        UpperBand.DisplayNameValueInWindowsFlags = 0; // Ukryte w celowniku
        UpperBand.IncludeInSpreadsheet = 0;           // Ukryte w Spreadsheet

        // 4. Dolna linia (Oversold)
        LowerBand.Name = "Lower Band";
        LowerBand.DrawStyle = DRAWSTYLE_LINE_FROM_END_OF_CHART_LEFT_TO_RIGHT;
        LowerBand.PrimaryColor = RGB(255, 128, 0); 
        LowerBand.LineWidth = 2;
        LowerBand.DrawZeros = true;
        // Ustawienia widoczności:
        LowerBand.DisplayNameValueInDataLine = 0;     // Ukryte w nagłówku
        LowerBand.DisplayNameValueInWindowsFlags = 0; // Ukryte w celowniku
        LowerBand.IncludeInSpreadsheet = 0;           // Ukryte w Spreadsheet

        // Parametry wejściowe
        SMALength.Name = "SMA Length (LOCKED TO 7)";
        SMALength.SetInt(7);

        UpperLevel.Name = "Upper Band (+)";
        UpperLevel.SetFloat(100.0f);

        LowerLevel.Name = "Lower Band (-)";
        LowerLevel.SetFloat(-100.0f);

        ZeroLevel.Name = "Zero Line Level"; 
        ZeroLevel.SetFloat(0.0f); 

        // Konfiguracja domyślnego przycisku (Etap 1)
        BtnID.Name = "Control Bar Button ID (4)";
        BtnID.SetInt(4); 
		
		FixedHeight.Name = "Fixed Region Height %";
        FixedHeight.SetInt(20);

return;
    }

// --- FAZA 2: INICJALIZACJA I PIERWSZA SYNCHRONIZACJA ---
    if (Initialized == 0)
    {
        MasterState = !sc.HideStudy; 
        LastHideStudy = sc.HideStudy;

        // NOWOŚĆ: Wymuszenie wysokości regionu przy pierwszym załadowaniu
        // Sygnatura z listy: ChartNumber, RegionIndex, Percentage
        sc.SetGraphRegionHeightPercentage(sc.ChartNumber, sc.GraphRegion, 20);

        Initialized = 1;
    }

// --- FAZA 3: LOGIKA ZDARZEŃ I GPU SYNC ---
    if (sc.Index == sc.UpdateStartIndex)
    {
        // 1. Sprawdzamy, czy kliknięto nasz przycisk (zdefiniowany BtnID)
        if (sc.MenuEventID == BtnID.GetInt())
        {
            MasterState = !MasterState; 
            sc.FlagFullRecalculate = 1; // ETAP IV: Natychmiastowe odświeżenie grafiki (ON i OFF)
        }
        // 2. Sprzężenie zwrotne: Synchronizacja z systemowym menu F6
        else if (sc.HideStudy != LastHideStudy)
        {
            MasterState = !sc.HideStudy;
            sc.FlagFullRecalculate = 1; // Synchronizacja wizualna po użyciu F6
        }
    }

    // TWARDA BLOKADA: Jeśli użytkownik zmieni SMA na cokolwiek innego, kod natychmiast wraca do 7
    if (SMALength.GetInt() != 7)
    {
        SMALength.SetInt(7);
    }

// --- FAZA 4: SILNIK OBLICZENIOWY STEROWANY PRZEZ MASTERSTATE ---
    if (MasterState == 1)
    {
        // WYMUSZENIE WYSOKOŚCI: Działa w każdym ticku, blokując zmiany myszką
        sc.SetGraphRegionHeightPercentage(sc.ChartNumber, sc.GraphRegion, FixedHeight.GetInt());

        // Obliczanie prostej średniej kroczącej (SMA)
        sc.SimpleMovAvg(sc.BaseDataIn[SC_LAST], SMA_Array, SMALength.GetInt());

        // Seeding (od 7. bara)
        if (sc.Index >= SMALength.GetInt() - 1)
        {
            Detrend[sc.Index] = sc.BaseDataIn[SC_LAST][sc.Index] - SMA_Array[sc.Index];
        }
        
        // Rysowanie linii pomocniczych
        ZeroLine[sc.Index]  = ZeroLevel.GetFloat(); 
        UpperBand[sc.Index] = UpperLevel.GetFloat();
        LowerBand[sc.Index] = LowerLevel.GetFloat();
    }

    // --- FAZA 4: GHOST MODE I AKTUALIZACJA UI (WIZUALNA REAKCJA PRZYCISKU) ---
    if (sc.Index == sc.ArraySize - 1)
    {
        // 1. Synchronizacja z systemowym menu F6
        sc.HideStudy = !MasterState;
        LastHideStudy = sc.HideStudy; 
        
        // 2. Włączenie/Wyłączenie podświetlenia przycisku (ID 4)
        sc.SetCustomStudyControlBarButtonEnable(BtnID.GetInt(), MasterState);
        sc.SetCustomStudyControlBarButtonText(BtnID.GetInt(), "DETR OSC");
        
        // 3. Dynamiczna zmiana stylu rysowania (Ghost Mode)
        if (MasterState == 1) 
        {
            Detrend.DrawStyle = DRAWSTYLE_LINE;
            ZeroLine.DrawStyle = DRAWSTYLE_LINE_FROM_END_OF_CHART_LEFT_TO_RIGHT;
            UpperBand.DrawStyle = DRAWSTYLE_LINE_FROM_END_OF_CHART_LEFT_TO_RIGHT;
            LowerBand.DrawStyle = DRAWSTYLE_LINE_FROM_END_OF_CHART_LEFT_TO_RIGHT;
        } 
        else 
        {
            Detrend.DrawStyle = DRAWSTYLE_IGNORE;
            ZeroLine.DrawStyle = DRAWSTYLE_IGNORE;
            UpperBand.DrawStyle = DRAWSTYLE_IGNORE;
            LowerBand.DrawStyle = DRAWSTYLE_IGNORE;
        }
    }
} // Koniec funkcji scsf_DiNapoliDetrendedOscillator
