#include "sierrachart.h"

// Definicja nazwy biblioteki DLL - kluczowe dla kompilacji zdalnej
SCDLLName("New Lower High") 

/*==========================================================================*/
SCSFExport scsf_NewLowerHigh(SCStudyInterfaceRef sc)
{
	// Referencja do subgrafu - używamy wzorca z scsf_InsideBar
	SCSubgraphRef Subgraph_Highlight = sc.Subgraph[0];


// ETAP 1: Konfiguracja domyślna (Tryb Stealth V5 - Fix)
	if (sc.SetDefaults)
	{
		sc.GraphName = "New Lower High";
		sc.GraphRegion = 0; 
		sc.AutoLoop = 1;

// --- FLAGI OPTYMALIZACJI INTERFEJSU (STEALTH V7 - FINAL FIX) ---
		sc.UpdateAlways = 0; // CPU Guard: Aktualizacja tylko przy nowym ticku[cite: 1, 4]
		
		// KOREKTA: Przekazanie wymaganych parametrów ChartNumber oraz StudyID
		int32_t StudyID = sc.StudyGraphInstanceID; 
		int32_t DisableValue = 0;
		sc.SetStudyAlwaysShowNameValueLabelsWhenEnabled(sc.ChartNumber, StudyID, DisableValue); //


		
		sc.DisplayStudyName = 0; // Ukrywa nazwę wskaźnika na wykresie[cite: 3, 4]
		sc.DisplayStudyInputValues = 0; // Ukrywa parametry wejściowe[cite: 3, 4]
		sc.GlobalDisplayStudySubgraphsNameAndValue = 0; // Ukrywa wartości subgrafów w nagłówku[cite: 3, 4]

		// --- CONTROL BAR BUTTON ---
		sc.Input[0].Name = "Numer Przycisku";
		sc.Input[0].SetInt(63);
		// --- KONFIGURACJA SUBGRAFU ---
		Subgraph_Highlight.Name = "New Lower High";
		Subgraph_Highlight.DrawStyle = DRAWSTYLE_COLOR_BAR;
	Subgraph_Highlight.PrimaryColor = RGB(255, 0, 0); 
		Subgraph_Highlight.DrawZeros = false; 

		// KOREKTA ETYKIET: Wyłączenie nazw i wartości na poziomie subgrafu[cite: 1]
		Subgraph_Highlight.LineLabel = 0; // Odznacza "Name Label" i "Value Label"[cite: 1, 3]
		
		Subgraph_Highlight.IncludeInStudySummary = 0; //[cite: 1, 3]
		Subgraph_Highlight.IncludeInSpreadsheet = 0; //[cite: 1, 3]
		Subgraph_Highlight.DisplayNameValueInDataLine = 0; //[cite: 1, 3]
		Subgraph_Highlight.DisplayNameValueInWindowsFlags = 0; //[cite: 1, 3]

		return;
	}


	// --- CONTROL BAR BUTTON: MASTER STATE ---
	// Stays above the HideStudy return below: a hidden study must still see the click.
	int Btn = sc.Input[0].GetInt();
	int& MasterState = sc.GetPersistentInt(10);   // 1 visible, 0 hidden = single source of truth
	int& Initialized = sc.GetPersistentInt(11);
	int& LastHideStudy = sc.GetPersistentInt(12);

	if (Initialized == 0)
	{
		MasterState = !sc.HideStudy;
		LastHideStudy = sc.HideStudy;
		Initialized = 1;
	}

	// Change state once per pass, never per bar
	if (sc.Index == sc.UpdateStartIndex)
	{
		if (sc.MenuEventID == Btn)                 // button click (MenuEventID is NOT zeroed)
		{
			MasterState = !MasterState;
			if (MasterState == 1) sc.FlagFullRecalculate = 1;
		}
		else if (sc.HideStudy != LastHideStudy)    // Hide Study changed in the settings (F6)
		{
			MasterState = !sc.HideStudy;
			if (MasterState == 1) sc.FlagFullRecalculate = 1;
		}
	}

	// Latch and UI update, once per pass
	if (sc.Index == sc.ArraySize - 1)
	{
		sc.HideStudy = !MasterState;
		LastHideStudy = sc.HideStudy;

		sc.SetCustomStudyControlBarButtonEnable(Btn, MasterState);
		sc.SetCustomStudyControlBarButtonText(Btn, "LH");

		Subgraph_Highlight.DrawStyle = (MasterState == 1) ? DRAWSTYLE_COLOR_BAR : DRAWSTYLE_IGNORE;
	}

// Jeśli wskaźnik jest ukryty, natychmiast przerwij wykonywanie logiki.
if (sc.HideStudy)
    return;


// ETAP 2 & 3: Logika Chirurgiczna i Renderowanie (Lower High)
	
	// Zabezpieczenie przed wyjściem poza indeks 0 przy sprawdzaniu bara wstecz
	if (sc.Index > 0)
	{
		// Translacja Trade Sense: "High <= High.1" na ASCIL
		if (sc.High[sc.Index] <= sc.High[sc.Index - 1])
		{
			// Aktywacja podświetlenia dla bieżącego bara
			Subgraph_Highlight[sc.Index] = 1.0f;
			Subgraph_Highlight.DataColor[sc.Index] = Subgraph_Highlight.PrimaryColor;
		}
		else
		{
			// Resetowanie stanu dla zachowania czystości wizualnej[cite: 1]
			Subgraph_Highlight[sc.Index] = 0.0f;
		}
	}
	else
	{
		Subgraph_Highlight[sc.Index] = 0.0f;
	}
}