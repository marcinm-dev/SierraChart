#include "sierrachart.h"

SCDLLName("DiNapoli_MACDP_Navigator_Replica_v10")

// Sierra arrays hold only float, so a double is kept as two floats:
// Hi = the value rounded to float, Lo = the small leftover that float cannot hold.
static void StoreSplit(SCFloatArrayRef Hi, SCFloatArrayRef Lo, int Index, double Value)
{
    float HiPart = (float)Value;
    Hi[Index] = HiPart;
    Lo[Index] = (float)(Value - (double)HiPart);
}

// Rebuilds the double from the two float arrays.
static double LoadSplit(SCFloatArrayRef Hi, SCFloatArrayRef Lo, int Index)
{
    return (double)Hi[Index] + (double)Lo[Index];
}

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

    // --- MEMORY OPTIMISATION (UNCHANGED) ---
    SCFloatArrayRef FastEMA = Predictor.Arrays[0];
    SCFloatArrayRef SlowEMA = Predictor.Arrays[1];
    SCFloatArrayRef SignalLine = Predictor.Arrays[2];
    SCFloatArrayRef FastEMALo = Predictor.Arrays[3];
    SCFloatArrayRef SlowEMALo = Predictor.Arrays[4];
    SCFloatArrayRef SignalLineLo = Predictor.Arrays[5];

    // --- MASTER STATE ARCHITECTURE ---
    int Btn = sc.Input[0].GetInt();
    int& MasterState = sc.GetPersistentInt(10); 
    int& LastHideStudy = sc.GetPersistentInt(12);
    int& Initialized = sc.GetPersistentInt(11);

    if (Initialized == 0) {
        MasterState = !sc.HideStudy; 
        LastHideStudy = sc.HideStudy;
        Initialized = 1;
    }

    // --- CORRECTED FEEDBACK COUPLING ---
    if (sc.Index == sc.UpdateStartIndex)
    {
        // 1. Check whether the change comes from the button (click)
        if (sc.MenuEventID == Btn)
        {
            MasterState = !MasterState; 
            
            // *** CHANGE: REMOVED sc.MenuEventID = 0; *** // Reason: Leaving MenuEventID in place lets other studies 
            // (such as Expansion) check whether their own buttons were pressed.
            
            if (MasterState == 1) sc.FlagFullRecalculate = 1;
        }
        // 2. If it is not the button and the system flag differs from our memory -> a change made in F6
        else if (sc.HideStudy != LastHideStudy)
        {
            MasterState = !sc.HideStudy;
            if (MasterState == 1) sc.FlagFullRecalculate = 1;
        }
    }

    // --- FINAL LATCH AND UI UPDATE ---
    if (sc.Index == sc.ArraySize - 1)
    {
        sc.HideStudy = !MasterState;
        LastHideStudy = sc.HideStudy; 
        
        sc.SetCustomStudyControlBarButtonEnable(Btn, MasterState);
        sc.SetCustomStudyControlBarButtonText(Btn, "MACD PRED");
        
        Predictor.DrawStyle = (MasterState == 1) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
    }

    // --- MATH (UNCHANGED) ---
    static const double alphaFast = 0.213;   
    static const double alphaSlow = 0.108;   
    static const double alphaSignal = 0.199;  
    static const double denominator = 0.105; 

    if (sc.Index == 0)
    {
        StoreSplit(FastEMA, FastEMALo, 0, (double)sc.Close[0]);
        StoreSplit(SlowEMA, SlowEMALo, 0, (double)sc.Close[0]);
        StoreSplit(SignalLine, SignalLineLo, 0, 0.0);
        return;
    }

    double prevFast = LoadSplit(FastEMA, FastEMALo, sc.Index - 1);
    double prevSlow = LoadSplit(SlowEMA, SlowEMALo, sc.Index - 1);
    double prevSignal = LoadSplit(SignalLine, SignalLineLo, sc.Index - 1);

    double fastValue = ((double)sc.Close[sc.Index] * alphaFast) + (prevFast * (1.0 - alphaFast));
    double slowValue = ((double)sc.Close[sc.Index] * alphaSlow) + (prevSlow * (1.0 - alphaSlow));

    double currentMACD = fastValue - slowValue;
    double signalValue = (currentMACD * alphaSignal) + (prevSignal * (1.0 - alphaSignal));

    StoreSplit(FastEMA, FastEMALo, sc.Index, fastValue);
    StoreSplit(SlowEMA, SlowEMALo, sc.Index, slowValue);
    StoreSplit(SignalLine, SignalLineLo, sc.Index, signalValue);

    if (MasterState == 1 || sc.Index == sc.ArraySize - 1)
    {
        if (sc.Index >= 25)
        {
            double rawPredictor = (signalValue - (fastValue * 0.787) + (slowValue * 0.892)) / denominator;

            // Nearest tick, half up, done in double (no float cast before the rounding)
            double tickSize = (double)sc.TickSize;
            if (tickSize > 0.0)
                rawPredictor = floor(rawPredictor / tickSize + 0.5) * tickSize;

            Predictor[sc.Index] = (float)rawPredictor;
        }
    }
}
