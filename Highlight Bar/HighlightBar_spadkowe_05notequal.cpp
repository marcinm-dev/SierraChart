#include "sierrachart.h"

// Definicja nazwy biblioteki DLL - kluczowe dla kompilacji zdalnej
SCDLLName("Spadkowe 0.5") 

/*==========================================================================*/
SCSFExport scsf_Spadkowe05(SCStudyInterfaceRef sc)
{
	// Referencja do subgrafu - używamy wzorca z scsf_InsideBar
	SCSubgraphRef Subgraph_Highlight = sc.Subgraph[0];


// ETAP 1: Konfiguracja domyślna (Tryb Stealth V5 - Fix)
	if (sc.SetDefaults)
	{
		sc.GraphName = "Spadkowe 0.5";
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

		// --- KONFIGURACJA SUBGRAFU ---
		Subgraph_Highlight.Name = "Spadkowe Highlight";
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


// Jeśli wskaźnik jest ukryty, natychmiast przerwij wykonywanie logiki.
if (sc.HideStudy)
    return;


	// ETAP 2 & 3: Logika Chirurgiczna i Renderowanie
	
	// Pobranie danych cenowych dla bieżącego indeksu[cite: 1]
	float High = sc.High[sc.Index];
	float Low = sc.Low[sc.Index];
	float Close = sc.Close[sc.Index];
	float Range = High - Low;

	// Zabezpieczenie przed barami o zerowej zmienności (Doji/Błędy danych)[cite: 1]
	if (Range > 0.0f)
	{
		// Twoja formuła z Trade Navigatora przeniesiona do ASCIL[cite: 1]
		// Parametr 0.25f jest wbity na sztywno zgodnie z Twoim wymogiem.
		
if ((Close - Low) < (0.5f * Range))		
		
		{
			// Aktywacja podświetlenia dla bieżącego bara[cite: 1]
			Subgraph_Highlight[sc.Index] = 1.0f;
			Subgraph_Highlight.DataColor[sc.Index] = Subgraph_Highlight.PrimaryColor;
		}
		else
		{
			// Resetowanie wartości, aby DrawZeros = false mogło zadziałać[cite: 1]
			Subgraph_Highlight[sc.Index] = 0.0f;
		}
	}
	else
	{
		Subgraph_Highlight[sc.Index] = 0.0f;
	}
}