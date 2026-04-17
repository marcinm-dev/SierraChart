#include "sierrachart.h"

SCDLLName("HorizontalGridGDI")

// Prototypy funkcji
void DrawHorizontalGridGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc);
float GetIdealStep(float Range, SCStudyInterfaceRef sc);

/*==========================================================================*/
SCSFExport scsf_HorizontalGridGDI(SCStudyInterfaceRef sc)
{
	// Definicja wejść - ZAMROŻONE
	SCInputRef Input_GridColor    = sc.Input[1];
	SCInputRef Input_Enabled      = sc.Input[2];
	SCInputRef Input_OverrideScale = sc.Input[3];
	SCInputRef Input_DotLength    = sc.Input[4]; 
	SCInputRef Input_GapSize      = sc.Input[5]; 

	if (sc.SetDefaults)
	{
		sc.GraphName = "Horizontal Grid GDI";
		sc.StudyDescription = "Profesjonalna siatka pozioma - Logika Micro/Macro (Fidelity SC).";

		sc.AutoLoop = 0; 
		sc.UpdateAlways = 1; // NAPRAWA: Wymusza przeliczanie przy przesuwaniu wykresu (offline/live)
		sc.GraphRegion = 0;
		sc.p_GDIFunction = DrawHorizontalGridGDI;
		sc.DrawStudyUnderneathMainPriceGraph = 1;

		Input_GridColor.Name = "Grid Color";
		Input_GridColor.SetColor(RGB(128, 128, 128));

		Input_Enabled.Name = "Enable Grid";
		Input_Enabled.SetYesNo(1);

		Input_OverrideScale.Name = "Override Y-Axis Scale Labels";
		Input_OverrideScale.SetYesNo(1);

		Input_DotLength.Name = "Dot Length (Pixels)";
		Input_DotLength.SetInt(1);

		Input_GapSize.Name = "Gap Size (Pixels)";
		Input_GapSize.SetInt(10);

		return;
	}

	// LOGIKA ETAPU 1 & 2 (Zaktualizowana o dynamiczne przeliczanie)
	float VisibleHigh, VisibleLow;
	sc.GetMainGraphVisibleHighAndLow(VisibleHigh, VisibleLow);
	float Range = VisibleHigh - VisibleLow;

	if (Range <= 0) return;

	// Obliczamy idealny krok na podstawie Twoich sztywnych reguł
	float IdealStep = GetIdealStep(Range, sc);
	sc.GetPersistentFloat(70011) = IdealStep;

	// Wstrzyknięcie kroku do osi Y
	if (Input_OverrideScale.GetYesNo())
	{
		sc.BaseGraphScaleIncrement = IdealStep;
	}
}

/*==========================================================================*/
// SILNIK WYBORU KROKU - IMPLEMENTACJA REGUŁ MICRO/MACRO
float GetIdealStep(float Range, SCStudyInterfaceRef sc)
{
	// Sztywne ziarna zgodnie ze specyfikacją (bez 20, 75, 750)
	static const float Seeds[] = {
		0.25f, 0.50f, 0.75f, 1.00f, 2.00f, 5.00f,     // Micro
		10.00f, 25.00f, 50.00f, 100.00f, 250.00f, 500.00f, 1000.00f // Macro
	};
	const int NumSeeds = sizeof(Seeds) / sizeof(Seeds[0]);

	int selectedIdx = 0;

	// Pętla symulacji "Look-Ahead"
	for (int i = 0; i < NumSeeds - 1; i++)
	{
		float currentS = Seeds[i];
		float nextS    = Seeds[i+1];
		
		float L_curr = Range / currentS;
		float L_next = Range / nextS;

		// GRUPA MICRO (< 10) - Mechanika Złotego Lądowania
		if (currentS < 10.0f)
		{
			// Przeskocz wyżej tylko jeśli lądowanie po skoku da >= 15.5 linii
			// LUB jeśli obecna gęstość przekroczy sufit 40.5
			if (L_next >= 15.5f || L_curr >= 40.5f)
			{
				selectedIdx = i + 1;
				continue;
			}
		}
		// GRUPA MACRO (>= 10) - Mechanika Czystej Struktury
		else
		{
			// Przeskocz wyżej bezwarunkowo jeśli przekroczysz sufit 28.5 linii
			if (L_curr >= 28.5f)
			{
				selectedIdx = i + 1;
				continue;
			}
		}

		// Jeśli warunki skoku nie są spełnione, przerywamy pętlę i zostajemy przy bieżącym ziarnie
		break;
	}

	return Seeds[selectedIdx];
}

/*==========================================================================*/
// RENDEROWANIE GDI - PRZEPISANE ZNAK W ZNAK (NIETYKALNE)
void DrawHorizontalGridGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc)
{
	if (sc.Input[2].GetYesNo() == 0) return;

	float FinalStep = sc.GetPersistentFloat(70011);
	if (FinalStep <= 0) return;

	int L_coord = sc.StudyRegionLeftCoordinate;
	int T_coord = sc.StudyRegionTopCoordinate;
	int R_coord = sc.StudyRegionRightCoordinate;
	int B_coord = sc.StudyRegionBottomCoordinate;

	// CACHE GDI (70000+)
	HPEN& r_hPen = (HPEN&)sc.GetPersistentPointer(70000);
	HRGN& r_hRgn = (HRGN&)sc.GetPersistentPointer(70001);
	
	COLORREF currentColor = sc.Input[1].GetColor();

	if (!r_hPen || currentColor != (COLORREF)sc.GetPersistentInt(70002))
	{
		if (r_hPen) DeleteObject(r_hPen);
		r_hPen = CreatePen(PS_SOLID, 1, currentColor);
		sc.SetPersistentInt(70002, (int)currentColor);
	}

	if (!r_hRgn || L_coord != sc.GetPersistentInt(70004) || T_coord != sc.GetPersistentInt(70005) || R_coord != sc.GetPersistentInt(70006) || B_coord != sc.GetPersistentInt(70007))
	{
		if (r_hRgn) DeleteObject(r_hRgn);
		r_hRgn = CreateRectRgn(L_coord, T_coord, R_coord, B_coord);
		sc.SetPersistentInt(70004, L_coord); sc.SetPersistentInt(70005, T_coord);
		sc.SetPersistentInt(70006, R_coord); sc.SetPersistentInt(70007, B_coord);
	}

	SelectClipRgn(hDC, r_hRgn);
	HGDIOBJ hOldPen = SelectObject(hDC, r_hPen);

	// PARAMETRY WZORU 1x10
	const int Dot = sc.Input[4].GetInt(); 
	const int Gap = sc.Input[5].GetInt();
	const int Segment = Dot + Gap;

	float VisibleHigh, VisibleLow;
	sc.GetMainGraphVisibleHighAndLow(VisibleHigh, VisibleLow);
	float StartPrice = floorf(VisibleLow / FinalStep) * FinalStep;

	// PĘTLA CENOWA (Pionowa)
	for (float Price = StartPrice; Price <= VisibleHigh + FinalStep; Price += FinalStep)
	{
		int y = sc.RegionValueToYPixelCoordinate(Price, sc.GraphRegion);
		
		if (y >= T_coord && y <= B_coord) 
		{
			// PĘTLA SEGMENTOWA (Pozioma - Apex Style 1x10)
			for (int x = L_coord; x < R_coord; x += Segment) 
			{
				int x_end = x + Dot;
				if (x_end > R_coord) x_end = R_coord;

				MoveToEx(hDC, x, y, NULL);
				LineTo(hDC, x_end, y);
			}
		}
	}

	SelectObject(hDC, hOldPen);
	SelectClipRgn(hDC, NULL);
}
