#include "sierrachart.h"

SCDLLName("DiNapoli_MACDP_Navigator_Replica_v10")

SCSFExport scsf_DiNapoliMACDP(SCStudyInterfaceRef sc)
{
    SCSubgraphRef Predictor = sc.Subgraph[0];

    if (sc.SetDefaults)
    {
        sc.GraphName = "DiNapoli MACD Predictor";
        sc.AutoLoop = 1; 
        sc.UpdateAlways = 0; 
		sc.GraphRegion = 0;
        		
		sc.DisplayStudyName = 0;      
        sc.DisplayStudyInputValues = 0;   
        sc.GlobalDisplayStudySubgraphsNameAndValue = 1;		
				
        sc.Input[0].Name = "Numer Przycisku"; 
        sc.Input[0].SetInt(3);

        Predictor.Name = "DiNapoli MACD Predictor";
        Predictor.DrawStyle = DRAWSTYLE_LINE;
        Predictor.PrimaryColor = RGB(255, 0, 0); 
        Predictor.LineWidth = 1;
        Predictor.GraphicalDisplacement = 1; 
        Predictor.DisplayNameValueInDataLine = 0;
        Predictor.DisplayNameValueInWindowsFlags = 1;
		Predictor.IncludeInSpreadsheet = 0; // Spreadsheet Guard
		Predictor.IncludeInStudySummary = 0;
		Predictor.LineLabel = LL_DISPLAY_VALUE  | LL_VALUE_ALIGN_VALUES_SCALE | LL_VALUE_ALIGN_CENTER;
		


        return;
    }

    // --- OPTYMALIZACJA PAMIĘCI (BEZ ZMIAN) ---
    SCFloatArrayRef FastEMA = Predictor.Arrays[0];
    SCFloatArrayRef SlowEMA = Predictor.Arrays[1];
    SCFloatArrayRef SignalLine = Predictor.Arrays[2];

    // --- ARCHITEKTURA MASTER STATE ---
    int Btn = sc.Input[0].GetInt();
    int& MasterState = sc.GetPersistentInt(10); 
    int& LastHideStudy = sc.GetPersistentInt(12);
    int& Initialized = sc.GetPersistentInt(11);

    if (Initialized == 0) {
        MasterState = !sc.HideStudy; 
        LastHideStudy = sc.HideStudy;
        Initialized = 1;
    }

    // --- POPRAWIONE SPRZĘŻENIE ZWROTNE ---
    if (sc.Index == sc.UpdateStartIndex)
    {
        // 1. Sprawdzamy czy zmiana pochodzi z przycisku (Click)
        if (sc.MenuEventID == Btn)
        {
            MasterState = !MasterState; 
            
            // *** ZMIANA: USUNIĘTO sc.MenuEventID = 0; *** // Uzasadnienie: Pozostawienie MenuEventID pozwala innym wskaźnikom 
            // (jak Expansion) sprawdzić, czy ich własne przyciski zostały wciśnięte.
            
            if (MasterState == 1) sc.FlagFullRecalculate = 1;
        }
        // 2. Jeśli to nie przycisk, a flaga systemowa różni się od naszej pamięci -> zmiana w F6
        else if (sc.HideStudy != LastHideStudy)
        {
            MasterState = !sc.HideStudy;
            if (MasterState == 1) sc.FlagFullRecalculate = 1;
        }
    }

    // --- FINALNY ZATRZASK I AKTUALIZACJA UI ---
    if (sc.Index == sc.ArraySize - 1)
    {
        sc.HideStudy = !MasterState;
        LastHideStudy = sc.HideStudy; 
        
        sc.SetCustomStudyControlBarButtonEnable(Btn, MasterState);
        sc.SetCustomStudyControlBarButtonText(Btn, "MACD PRED");
        
        Predictor.DrawStyle = (MasterState == 1) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
    }

    // --- MATEMATYKA (BEZ ZMIAN) ---
    static const double alphaFast = 0.213;   
    static const double alphaSlow = 0.108;   
    static const double alphaSignal = 0.199;  
    static const double denominator = 0.105; 

    if (sc.Index == 0)
    {
        FastEMA[0] = sc.Close[0];
        SlowEMA[0] = sc.Close[0];
        SignalLine[0] = 0;
        return;
    }

    FastEMA[sc.Index] = (float)((sc.Close[sc.Index] * alphaFast) + (FastEMA[sc.Index - 1] * (1.0 - alphaFast)));
    SlowEMA[sc.Index] = (float)((sc.Close[sc.Index] * alphaSlow) + (SlowEMA[sc.Index - 1] * (1.0 - alphaSlow)));

    double currentMACD = (double)FastEMA[sc.Index] - (double)SlowEMA[sc.Index];
    SignalLine[sc.Index] = (float)((currentMACD * alphaSignal) + (SignalLine[sc.Index - 1] * (1.0 - alphaSignal)));

    if (MasterState == 1 || sc.Index == sc.ArraySize - 1)
    {
        if (sc.Index >= 25)
        {
            double rawPredictor = (SignalLine[sc.Index] - (FastEMA[sc.Index] * 0.787) + (SlowEMA[sc.Index] * 0.892)) / denominator;
            Predictor[sc.Index] = sc.RoundToTickSize((float)rawPredictor);
        }
    }
}
