#include "sierrachart.h"

// --- DEFINICJA NAZWY DLL ---
SCDLLName("DetrendedCombo")

/*
    ARCHITEKTURA PILOT-ROBOTNIK (V45)
    Hider (Pilot): Detrended Oscillator Hider (Region 0)
    Engine (Worker): Detrended Oscillator Engine (Region 1)
*/

// --- 1. DETRENDED OSCILLATOR ENGINE (ROBOTNIK) ---
SCSFExport scsf_DDO_Engine(SCStudyInterfaceRef sc)
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
        sc.GraphShortName = "DDO_Engine"; 
        sc.GraphRegion = 1; 
        sc.AutoLoop = 1;
        
        // --- PRZYWRÓCONE OPTYMALIZACJE (V48) ---
        sc.UpdateAlways = 0; // CPU Guard: Aktualizacja tylko przy nowym ticku
        sc.MaintainAdditionalChartDataArrays = 0; // RAM Guard: Brak zbędnych tablic danych
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

        if ((CurrentTime - LastProcessTime) < TimeThreshold)
            return;

        sc.SetPersistentDouble(125, CurrentTime);
    }
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

// --- 2. DETRENDED OSCILLATOR HIDER (PILOT) ---
SCSFExport scsf_DDO_Hider(SCStudyInterfaceRef sc)
{
    // IZOLACJA INPUTÓW: Tylko sterowanie interfejsem
    SCInputRef BtnID = sc.Input[0];

    int& MasterState     = sc.GetPersistentInt(120);
    int& Initialized     = sc.GetPersistentInt(121);
    int& WorkerInstanceID = sc.GetPersistentInt(124);


if (sc.SetDefaults)
    {
        sc.GraphName = "Detrended Oscillator Hider";
        sc.GraphShortName = "DDO_Hider"; 
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
        // Szukamy wyłącznie silnika po jego unikalnym ShortName
        int FoundID = sc.GetStudyIDByName(sc.ChartNumber, "DDO_Engine", 1);

        // BEZPIECZNIK: FoundID musi istnieć i NIE MOŻE być ID Hidera
        if (FoundID > 0 && FoundID != sc.StudyGraphInstanceID)
        {
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
            int ExistingID = sc.GetStudyIDByName(sc.ChartNumber, "DDO_Engine", 1);
            
            if (ExistingID == 0)
            {
                n_ACSIL::s_AddStudy AddStudy;
                AddStudy.ChartNumber = sc.ChartNumber;
                AddStudy.StudyID = 0; 
                AddStudy.CustomStudyFileAndFunctionName = "DetrendedCombo.scsf_DDO_Engine"; 
                AddStudy.ShortName = "DDO_Engine";

                WorkerInstanceID = sc.AddStudyToChart(AddStudy);
            }
            else
            {
                WorkerInstanceID = ExistingID;
            }
        }

else // OFF
        {
            // Pilot szuka ID silnika, upewniając się, że nie celuje w samego siebie[cite: 2]
            int TargetID = sc.GetStudyIDByName(sc.ChartNumber, "DDO_Engine", 1);
            
            // BEZPIECZNIK: Usuwamy tylko jeśli znalezione ID nie należy do Hidera[cite: 2]
            if (TargetID > 0 && TargetID != sc.StudyGraphInstanceID)
            {
                sc.RemoveStudyFromChart(sc.ChartNumber, TargetID);
                WorkerInstanceID = 0;
            }
            else if (WorkerInstanceID != 0 && WorkerInstanceID != sc.StudyGraphInstanceID)
            {
                // Jeśli GetStudyIDByName zawiodło, próbujemy użyć zapisanego WorkerInstanceID
                sc.RemoveStudyFromChart(sc.ChartNumber, WorkerInstanceID);
                WorkerInstanceID = 0;
            }
        }
    }

    // Wizualna synchronizacja przycisku
    sc.SetCustomStudyControlBarButtonEnable(BtnID.GetInt(), MasterState);
    sc.SetCustomStudyControlBarButtonText(BtnID.GetInt(), (MasterState ? "DDO ON" : "DDO OFF"));
}
