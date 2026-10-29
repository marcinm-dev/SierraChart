#include "sierrachart.h"

SCDLLName("DiNapoli_DMA_3x3")

SCSFExport scsf_DiNapoliDMA3x3(SCStudyInterfaceRef sc)
{
    SCSubgraphRef DMA = sc.Subgraph[0];

    if (sc.SetDefaults)
    {
        sc.GraphName = "DiNapoli DMA 3x3";
        sc.AutoLoop = 1;
        sc.UpdateAlways = 0;
        sc.GraphRegion = 0;

        sc.DisplayStudyName = 0;
        sc.DisplayStudyInputValues = 0;
        sc.GlobalDisplayStudySubgraphsNameAndValue = 1;

        sc.Input[0].Name = "Numer Przycisku";
        sc.Input[0].SetInt(5);

        sc.Input[1].Name = "Okres SMA";
        sc.Input[1].SetInt(3);
        sc.Input[1].SetIntLimits(1, 1000);

        DMA.Name = "DiNapoli DMA 3x3";
        DMA.DrawStyle = DRAWSTYLE_LINE;
        DMA.PrimaryColor = RGB(0, 0, 128);
        DMA.LineWidth = 1;
        DMA.GraphicalDisplacement = 3; // przesuniecie do przodu o 3 bary (mozna zmienic w Subgraph Settings)
        DMA.DisplayNameValueInDataLine = 0;
        DMA.DisplayNameValueInWindowsFlags = 1;
        DMA.IncludeInSpreadsheet = 0; // Spreadsheet Guard
        DMA.IncludeInStudySummary = 0;
        DMA.LineLabel = LL_DISPLAY_VALUE | LL_VALUE_ALIGN_VALUES_SCALE | LL_VALUE_ALIGN_CENTER;

        return;
    }

    // --- ARCHITEKTURA MASTER STATE (BEZ ZMIAN) ---
    int& MasterState = sc.GetPersistentInt(10);
    int& Initialized = sc.GetPersistentInt(11);
    int& LastHideStudy = sc.GetPersistentInt(12);
    int& UIState = sc.GetPersistentInt(13); // 0 = przycisk jeszcze nie ustawiony, 1 = ukryty, 2 = widoczny

    if (Initialized == 0) {
        MasterState = !sc.HideStudy;
        LastHideStudy = sc.HideStudy;
        Initialized = 1;
    }

    // --- TRYB UKRYTY: poza barem zdarzen (UpdateStartIndex) i ostatnim barem nie robimy nic ---
    if (MasterState == 0 && sc.Index != sc.UpdateStartIndex && sc.Index != sc.ArraySize - 1)
        return;

    int Btn = sc.Input[0].GetInt();
    int Length = sc.Input[1].GetInt();

    if (sc.Index == sc.UpdateStartIndex)
    {
        // 1. Zmiana z przycisku (Click)
        if (sc.MenuEventID == Btn)
        {
            MasterState = !MasterState;

            // sc.MenuEventID celowo NIE jest zerowane - inne wskazniki
            // moga sprawdzac swoje przyciski.

            if (MasterState == 1) sc.FlagFullRecalculate = 1;
        }
        // 2. Zmiana flagi systemowej w F6
        else if (sc.HideStudy != LastHideStudy)
        {
            MasterState = !sc.HideStudy;
            if (MasterState == 1) sc.FlagFullRecalculate = 1;
        }
    }

    // --- FINALNY ZATRZASK I AKTUALIZACJA UI (raz na cykl) ---
    if (sc.Index == sc.ArraySize - 1)
    {
        sc.HideStudy = !MasterState;
        LastHideStudy = sc.HideStudy;

        // Wywolania przycisku tylko gdy stan sie zmienil albo po pelnym przeliczeniu
        // (ladowanie wykresu, zmiana interwalu). Zatrzask HideStudy/DrawStyle zostaje
        // przy kazdym ticku - to tanie przypisania, a chronia przed odswiezaniem z F6.
        if (UIState != MasterState + 1 || sc.IsFullRecalculation)
        {
            sc.SetCustomStudyControlBarButtonEnable(Btn, MasterState);
            sc.SetCustomStudyControlBarButtonText(Btn, "DMA 3x3");
            UIState = MasterState + 1;
        }

        DMA.DrawStyle = (MasterState == 1) ? DRAWSTYLE_LINE : DRAWSTYLE_IGNORE;
    }

    // --- MATEMATYKA: SMA(Close, Length) zaokraglona do ticka, przesuniecie robi Sierra (GraphicalDisplacement) ---
    // SMA nie jest rekurencyjna, wiec przy ukryciu pomijamy cala kalkulacje (rowniez ostatni bar);
    // po wlaczeniu FlagFullRecalculate odtwarza historie.
    if (MasterState == 1)
    {
        if (sc.Index >= Length - 1)
        {
            sc.SimpleMovAvg(sc.Close, DMA, Length);

            // Zaokraglenie wyniku (juz po obliczeniach) do ticka - sc.RoundToTickSize
            // uzywa sc.TickSize biezacego symbolu, wiec dopasowuje sie do kazdego instrumentu.
            DMA[sc.Index] = sc.RoundToTickSize(DMA[sc.Index]);
        }
    }
}
