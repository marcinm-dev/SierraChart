#include "sierrachart.h"

SCDLLName("DiNapoli Detrended Oscillator")

// --- DINAPOLI DETRENDED OSCILLATOR ---
SCSFExport scsf_DiNapoliDetrendedOscillator(SCStudyInterfaceRef sc)
{
    // Definicja linii wyświetlanych na wykresie
    SCSubgraphRef Detrend   = sc.Subgraph[0];
    SCSubgraphRef ZeroLine  = sc.Subgraph[1];
    SCSubgraphRef UpperBand = sc.Subgraph[2];
    SCSubgraphRef LowerBand = sc.Subgraph[3];
    
    // Ukryta tablica do przeliczania średniej
    SCSubgraphRef SMA_Array = sc.Subgraph[4]; 

    // Parametry wejściowe (Inputs)
    SCInputRef SMALength  = sc.Input[0];
    SCInputRef UpperLevel = sc.Input[1];
    SCInputRef LowerLevel = sc.Input[2];
    SCInputRef ZeroLevel  = sc.Input[3];

    if (sc.SetDefaults)
    {
        sc.GraphName = "DiNapoli Detrended Oscillator";
        sc.GraphRegion = 1; 
        sc.AutoLoop = 1;
        sc.ValueFormat = VALUEFORMAT_INHERITED; 

        // 1. Główna linia oscylatora
        Detrend.Name = "Detrend Osc";
        Detrend.DrawStyle = DRAWSTYLE_LINE;
        Detrend.PrimaryColor = RGB(0, 0, 255); // Niebieski
        Detrend.LineWidth = 2;

        // 2. Linia zerowa (przerywana, szara)
        ZeroLine.Name = "Zero Line";
        ZeroLine.DrawStyle = DRAWSTYLE_LINE;
        ZeroLine.LineStyle = LINESTYLE_DASH;
        ZeroLine.PrimaryColor = RGB(128, 128, 128); 
        ZeroLine.LineWidth = 1;
        
        // MAGIA SIERRA CHART - Wymuszenie rysowania równych zer!
        ZeroLine.DrawZeros = true; 

        // 3. Górna linia pomarańczowa (Overbought)
        UpperBand.Name = "Upper Band";
        UpperBand.DrawStyle = DRAWSTYLE_LINE;
        UpperBand.PrimaryColor = RGB(255, 128, 0); // Pomarańczowy
        UpperBand.LineWidth = 2;
        UpperBand.DrawZeros = true;

        // 4. Dolna linia pomarańczowa (Oversold)
        LowerBand.Name = "Lower Band";
        LowerBand.DrawStyle = DRAWSTYLE_LINE;
        LowerBand.PrimaryColor = RGB(255, 128, 0); // Pomarańczowy
        LowerBand.LineWidth = 2;
        LowerBand.DrawZeros = true;

        // Ustawienie Read-Only dla SMA
        SMALength.Name = "SMA Length (LOCKED TO 7)";
        SMALength.SetInt(7);

        UpperLevel.Name = "Upper Band (+)";
        UpperLevel.SetFloat(100.0f);

        LowerLevel.Name = "Lower Band (-)";
        LowerLevel.SetFloat(-100.0f);

        ZeroLevel.Name = "Zero Line Level"; 
        ZeroLevel.SetFloat(0.0f); 

        return;
    }

    // TWARDA BLOKADA: Jeśli użytkownik zmieni SMA na cokolwiek innego, kod natychmiast wraca do 7
    if (SMALength.GetInt() != 7)
    {
        SMALength.SetInt(7);
    }

    // Obliczanie prostej średniej kroczącej (SMA)
    sc.SimpleMovAvg(sc.BaseDataIn[SC_LAST], SMA_Array, SMALength.GetInt());

    // Obliczanie wartości Detrend
    Detrend[sc.Index] = sc.BaseDataIn[SC_LAST][sc.Index] - SMA_Array[sc.Index];
    
    // Rysowanie poziomych linii na stałych wartościach
    ZeroLine[sc.Index]  = ZeroLevel.GetFloat(); 
    UpperBand[sc.Index] = UpperLevel.GetFloat();
    LowerBand[sc.Index] = LowerLevel.GetFloat();
}  
