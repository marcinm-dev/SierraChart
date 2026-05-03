#include "sierrachart.h"

// --- DEFINICJA NAZWY DLL ---
SCDLLName("DetrendedCombo")

/*
    ARCHITEKTURA PILOT-ROBOTNIK (V45)
    Switch (Pilot): Detrended Oscillator Switch (Region 0)
    Engine (Worker): Detrended Oscillator Engine (Region 1)
*/

// --- 1. DETRENDED OSCILLATOR ENGINE (ROBOTNIK) ---
SCSFExport scsf_DO_Engine(SCStudyInterfaceRef sc)
{
    SCSubgraphRef Detrend   = sc.Subgraph[0];
    SCSubgraphRef ZeroLine  = sc.Subgraph[1];
    SCSubgraphRef UpperBand = sc.Subgraph[2];
    SCSubgraphRef LowerBand = sc.Subgraph[3];
    
    SCFloatArrayRef SMA_Array = Detrend.Arrays[0];

    // IZOLACJA INPUTÓW: Tylko parametry matematyczne
    SCInputRef SMALength  = sc.Input[0];
    SCInputRef UpperLevel = sc.Input[1];
    SCInputRef LowerLevel = sc.Input[2];
    SCInputRef ZeroLevel  = sc.Input[3];


// STAŁA THROTTLINGU: 100ms wyrażone w dniach (100 / 86,400,000)
    const double TimeThreshold = 0.0000011574;



if (sc.SetDefaults)
    {
sc.GraphName = "Detrended Oscillator Engine";
        // ZMIANA V6.4: Usunięto ShortName dla czystości Study Summary
        sc.GraphRegion = 1;
        sc.AutoLoop = 1;
        
        // --- PRZYWRÓCONE OPTYMALIZACJE (V48) ---
sc.UpdateAlways = 0; // CPU Guard: Aktualizacja tylko przy nowym ticku
        // ZMIANA V6.1: Włączone dla stabilności SMA przy lukach danych (Gap Handling)
        sc.MaintainAdditionalChartDataArrays = 1; 
        sc.DisplayStudyName = 0;      
        sc.DisplayStudyInputValues = 0;    
        sc.ValueFormat = VALUEFORMAT_INHERITED; 
        sc.GlobalDisplayStudySubgraphsNameAndValue = 1;

        // 1. Główna linia
        Detrend.Name = "Detrend Osc";
        Detrend.DrawStyle = DRAWSTYLE_LINE;
        Detrend.PrimaryColor = RGB(0, 0, 255);
        Detrend.LineWidth = 1;
        Detrend.DisplayNameValueInDataLine = 0;
        Detrend.DisplayNameValueInWindowsFlags = 1;
        Detrend.IncludeInSpreadsheet = 0; // Spreadsheet Guard

        // 2. Linia zerowa
        ZeroLine.Name = "Zero Line";
        ZeroLine.DrawStyle = DRAWSTYLE_LINE_FROM_END_OF_CHART_LEFT_TO_RIGHT;
        ZeroLine.LineStyle = LINESTYLE_DOT;
        ZeroLine.PrimaryColor = RGB(128, 128, 128);
        ZeroLine.DrawZeros = true;
        ZeroLine.DisplayNameValueInDataLine = 0;      // Ukrycie w nagłówku
        ZeroLine.DisplayNameValueInWindowsFlags = 0;  // Ukrycie w celowniku[cite: 1]
        ZeroLine.IncludeInSpreadsheet = 0;

        // 3. Górna linia
        UpperBand.Name = "Upper Band";
        UpperBand.DrawStyle = DRAWSTYLE_LINE_FROM_END_OF_CHART_LEFT_TO_RIGHT;
        UpperBand.PrimaryColor = RGB(255, 128, 0);
        UpperBand.LineWidth = 2;
        UpperBand.DisplayNameValueInDataLine = 0;     // Ukrycie w nagłówku[cite: 1]
        UpperBand.DisplayNameValueInWindowsFlags = 0; // Ukrycie w celowniku[cite: 1]
        UpperBand.IncludeInSpreadsheet = 0;

        // 4. Dolna linia
        LowerBand.Name = "Lower Band";
        LowerBand.DrawStyle = DRAWSTYLE_LINE_FROM_END_OF_CHART_LEFT_TO_RIGHT;
        LowerBand.PrimaryColor = RGB(255, 128, 0);
        LowerBand.LineWidth = 2;
        LowerBand.DisplayNameValueInDataLine = 0;     // Ukrycie w nagłówku[cite: 1]
        LowerBand.DisplayNameValueInWindowsFlags = 0; // Ukrycie w celowniku[cite: 1]
        LowerBand.IncludeInSpreadsheet = 0;

        // Parametry matematyczne
        SMALength.Name = "SMA Length (LOCKED TO 7)";
        SMALength.SetInt(7);
        UpperLevel.Name = "Upper Level"; UpperLevel.SetFloat(100.0f);
        LowerLevel.Name = "Lower Level"; LowerLevel.SetFloat(-100.0f);
        ZeroLevel.Name = "Zero Level"; ZeroLevel.SetFloat(0.0f);

        return;
    }

// ELASTIC DEFAULT: Ustawienie 20% tylko przy starcie
    if (sc.Index == 0)
    {
        sc.SetGraphRegionHeightPercentage(sc.ChartNumber, sc.GraphRegion, 20); 
    }

    // --- OPTYMALIZACJA V49.4: HISTORICAL LOCK & DOUBLE THROTTLING ---
    if (!sc.IsFullRecalculation) // Tylko w trybie Real-Time
    {
        // 1. Historical Lock: Blokada przetwarzania barów historycznych w trybie Live
        if (sc.Index < sc.ArraySize - 1)
            return;

// 2. Tick Throttling (100ms): Użycie oficjalnej metody GetAsDouble()
        double CurrentTime = sc.CurrentSystemDateTimeMS.GetAsDouble(); 
        double LastProcessTime = sc.GetPersistentDouble(125);

// ZMIANA V6.1: Bypass dla zamknięcia świecy (Precyzja Bar Close)
        bool BarClosed = sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_CLOSED;

        if ((CurrentTime - LastProcessTime) < TimeThreshold && !BarClosed)
            return;

sc.SetPersistentDouble(125, CurrentTime);
    } // <--- DODANA BRAKUJĄCA KLAMRA DOMYKAJĄCA BLOK !sc.IsFullRecalculation
    // ----------------------------------------------------------------

    // Blokada silnika na SMA 7
    if (SMALength.GetInt() != 7) SMALength.SetInt(7);

    sc.SimpleMovAvg(sc.BaseDataIn[SC_LAST], SMA_Array, SMALength.GetInt());



    if (sc.Index >= SMALength.GetInt() - 1)
    {
        Detrend[sc.Index] = sc.BaseDataIn[SC_LAST][sc.Index] - SMA_Array[sc.Index];
    }

    ZeroLine[sc.Index] = ZeroLevel.GetFloat();
    UpperBand[sc.Index] = UpperLevel.GetFloat();
    LowerBand[sc.Index] = LowerLevel.GetFloat();
}

// --- 2. DETRENDED OSCILLATOR Switch (PILOT) ---
SCSFExport scsf_DO_Switch(SCStudyInterfaceRef sc)
{
    // IZOLACJA INPUTÓW: Tylko sterowanie interfejsem
    SCInputRef BtnID = sc.Input[0];

    int& MasterState     = sc.GetPersistentInt(120);
    int& Initialized     = sc.GetPersistentInt(121);
    int& WorkerInstanceID = sc.GetPersistentInt(124);


if (sc.SetDefaults)
    {
sc.GraphName = "Detrended Oscillator Switch";
        // ZMIANA V6.4: Usunięto ShortName dla czystości Study Summary
        sc.GraphRegion = 0;
        sc.AutoLoop = 0;    
        
        // --- DODANE FLAGI DLA PILOTA (V48) ---
        sc.UpdateAlways = 0; // CPU Guard dla Pilota
        sc.DisplayStudyName = 0;           
        sc.DisplayStudyInputValues = 0;
        
        BtnID.Name = "Control Bar Button ID (4)";
        BtnID.SetInt(4); 

        return;
    }

// AUDYT STARTOWY I SYNCHRONIZACJA SESJI (V46 - Zabezpieczony)
if (Initialized == 0)
    {
        // ZMIANA V6.4: Szukamy po Pełnej Nazwie (MatchShortName = 0)
        int FoundID = sc.GetStudyIDByName(sc.ChartNumber, "Detrended Oscillator Engine", 0);

// BEZPIECZNIK: FoundID musi istnieć i NIE MOŻE być ID Switcha
        if (FoundID > 0 && FoundID != sc.StudyGraphInstanceID)
        {
            // Dodatkowa weryfikacja nazwy, aby mieć 100% pewności przed synchronizacją
            MasterState = 1;
            WorkerInstanceID = FoundID;
        }
        else
        {
            MasterState = 0;
            WorkerInstanceID = 0;
        }
        Initialized = 1;
    }


    // LOGIKA TOGGLE
    if (sc.MenuEventID == BtnID.GetInt())
    {
        MasterState = !MasterState;

if (MasterState == 1) // ON
        {
            // ZMIANA V6.4: Wyszukiwanie po Pełnej Nazwie
            int ExistingID = sc.GetStudyIDByName(sc.ChartNumber, "Detrended Oscillator Engine", 0);
            
 // ZMIANA V6.6: Usunięcie zdublowanego warunku if (Precision Fix)
            if (ExistingID == 0) 
            {
                n_ACSIL::s_AddStudy AddStudy;
 
                AddStudy.ChartNumber = sc.ChartNumber;
                AddStudy.StudyID = 0; 
                AddStudy.CustomStudyFileAndFunctionName = "DetrendedCombo.scsf_DO_Engine"; 
                // ZMIANA V6.5: Usunięto AddStudy.ShortName, aby wskaźnik używał wyłącznie pełnej nazwy GraphName
                
                WorkerInstanceID = sc.AddStudyToChart(AddStudy);
            }

            else
            {
                WorkerInstanceID = ExistingID;
            }
        }

else // OFF
        {
            // ZMIANA V6.4: Wyszukiwanie po Pełnej Nazwie
            int TargetID = sc.GetStudyIDByName(sc.ChartNumber, "Detrended Oscillator Engine", 0);
            
            // BEZPIECZNIK: Usuwamy tylko jeśli ID jest poprawne i nie celuje w Pilota.
            if (TargetID > 0 && TargetID != sc.StudyGraphInstanceID)
            {
                sc.RemoveStudyFromChart(sc.ChartNumber, TargetID);
                WorkerInstanceID = 0; // Czyszczenie cache po usunięciu
            }
            else 
            {
                // Jeśli nie znaleziono po nazwie, spróbuj użyć zapisanego ID jako fallback
                if (WorkerInstanceID > 0 && WorkerInstanceID != sc.StudyGraphInstanceID)
                {
                    sc.RemoveStudyFromChart(sc.ChartNumber, WorkerInstanceID);
                    WorkerInstanceID = 0;
                }
            }
        }
    }

// Wizualna synchronizacja przycisku - ZMIANA V6.3: Stała nazwa przycisku "DETR OSC"
    sc.SetCustomStudyControlBarButtonEnable(BtnID.GetInt(), MasterState);
    sc.SetCustomStudyControlBarButtonText(BtnID.GetInt(), "DETR OSC");
}
