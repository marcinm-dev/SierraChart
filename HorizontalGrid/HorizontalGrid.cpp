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
		sc.StudyDescription = "Profesjonalna siatka pozioma - Optymalizacja Kernel GDI 1x10.";

		sc.AutoLoop = 0; 
		sc.UpdateAlways = 1; 
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

	

float VisibleHigh, VisibleLow;
	sc.GetMainGraphVisibleHighAndLow(VisibleHigh, VisibleLow);

	// --- ETAP 5: BRAMKA DETEKCJI ZMIAN (State Gate) ---
	// Monitorujemy zmiany zakresu widocznego wykresu
	float& r_LastHigh = sc.GetPersistentFloat(70020);
	float& r_LastLow  = sc.GetPersistentFloat(70021);

	if (VisibleHigh != r_LastHigh || VisibleLow != r_LastLow)
	{
		float Range = VisibleHigh - VisibleLow;
		if (Range > 0)
		{
			float IdealStep = GetIdealStep(Range, sc);
			sc.GetPersistentFloat(70011) = IdealStep;

			if (Input_OverrideScale.GetYesNo())
			{
				sc.BaseGraphScaleIncrement = IdealStep;
			}
		}

		// Aktualizujemy stan bramki
		r_LastHigh = VisibleHigh;
		r_LastLow  = VisibleLow;
	}



	// --- OPTYMALIZACJA 2: CLEANUP ZASOBÓW (Zarządzanie Cyklem Życia) ---
	// Ta sekcja wykonuje się TYLKO przy usuwaniu wskaźnika z wykresu
	if (sc.LastCallToFunction)
	
	{
		HGDIOBJ hPen = sc.GetPersistentPointer(70000);
		if (hPen) 
		{
			DeleteObject(hPen);
			sc.SetPersistentPointer(70000, NULL);
		}

		HGDIOBJ hRgn = sc.GetPersistentPointer(70001);
		if (hRgn) 
		{
			DeleteObject(hRgn);
			sc.SetPersistentPointer(70001, NULL);
		}
	}
}

/*==========================================================================*/
// SILNIK WYBORU KROKU - ZAMROŻONY (NIEZMIENIONY)
float GetIdealStep(float Range, SCStudyInterfaceRef sc)
{
	static const float Seeds[] = {
		0.25f, 0.50f, 0.75f, 1.00f, 2.00f, 5.00f,     
		10.00f, 25.00f, 50.00f, 100.00f, 250.00f, 500.00f, 1000.00f 
	};
	const int NumSeeds = sizeof(Seeds) / sizeof(Seeds[0]);

	int selectedIdx = 0;

	for (int i = 0; i < NumSeeds - 1; i++)
	{
		float currentS = Seeds[i];
		float nextS    = Seeds[i+1];
		
		float L_curr = Range / currentS;
		float L_next = Range / nextS;

		if (currentS < 10.0f)
		{
			if (L_next >= 15.5f || L_curr >= 40.5f)
			{
				selectedIdx = i + 1;
				continue;
			}
		}
		else
		{
			if (L_curr >= 28.5f)
			{
				selectedIdx = i + 1;
				continue;
			}
		}
		break;
	}

	return Seeds[selectedIdx];
}

/*==========================================================================*/
// RENDEROWANIE GDI - ZMODYFIKOWANO (OPTIMIZATION 1: EXTCREATEPEN)
void DrawHorizontalGridGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc)
{
	if (sc.Input[2].GetYesNo() == 0) return;

	float FinalStep = sc.GetPersistentFloat(70011);
	if (FinalStep <= 0) return;

	int L_coord = sc.StudyRegionLeftCoordinate;
	int T_coord = sc.StudyRegionTopCoordinate;
	int R_coord = sc.StudyRegionRightCoordinate;
	int B_coord = sc.StudyRegionBottomCoordinate;

	HPEN& r_hPen = (HPEN&)sc.GetPersistentPointer(70000);
	HRGN& r_hRgn = (HRGN&)sc.GetPersistentPointer(70001);
	
	COLORREF currentColor = sc.Input[1].GetColor();
	// Zmienne do monitorowania zmian wzoru kropki
	const int Dot = sc.Input[4].GetInt(); 
	const int Gap = sc.Input[5].GetInt();

	// --- START ZMIAN (OZNACOWANIE OPTYMALIZACJI) ---
	if (!r_hPen || 
		currentColor != (COLORREF)sc.GetPersistentInt(70002) || 
		Dot != sc.GetPersistentInt(70012) || 
		Gap != sc.GetPersistentInt(70013))
	{
		if (r_hPen) DeleteObject(r_hPen);

		LOGBRUSH lb;
		lb.lbStyle = BS_SOLID;
		lb.lbColor = currentColor;
		lb.lbHatch = 0;

		// Definicja wzoru użytkownika (1 kropka, 10 przerw)
		DWORD styleArray[2] = { (DWORD)Dot, (DWORD)Gap };

		// Używamy PS_GEOMETRIC dla precyzji w pikselach oraz PS_ENDCAP_FLAT, 
// aby uciąć naddatek rysowany na końcach każdego segmentu.
r_hPen = ExtCreatePen(PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT, 1, &lb, 2, styleArray);


		sc.SetPersistentInt(70002, (int)currentColor);
		sc.SetPersistentInt(70012, Dot);
		sc.SetPersistentInt(70013, Gap);
	}
	// --- KONIEC ZMIAN ---

	
// --- ETAP 4: OPTYMALIZACJA CACHE REGIONU (Standard Fidelity) ---
	int& cL = sc.GetPersistentInt(70004); int& cT = sc.GetPersistentInt(70005);
	int& cR = sc.GetPersistentInt(70006); int& cB = sc.GetPersistentInt(70007);

	if (!r_hRgn || L_coord != cL || T_coord != cT || R_coord != cR || B_coord != cB)
	{
		if (r_hRgn) DeleteObject(r_hRgn);
		r_hRgn = CreateRectRgn(L_coord, T_coord, R_coord, B_coord);
		
		// Zapisujemy nowe wymiary do cache
		cL = L_coord; cT = T_coord; cR = R_coord; cB = B_coord;
	}



	SelectClipRgn(hDC, r_hRgn);
	HGDIOBJ hOldPen = SelectObject(hDC, r_hPen);

	float VisibleHigh, VisibleLow;
	sc.GetMainGraphVisibleHighAndLow(VisibleHigh, VisibleLow);
	


// Kotwiczymy punkt startowy idealnie do ticka instrumentu
	float StartPrice = sc.RoundToTickSize(floorf(VisibleLow / FinalStep) * FinalStep);

	// --- START ZMIAN (PĘTLA ZOPTYMALIZOWANA) ---
	for (float Price = StartPrice; Price <= VisibleHigh + FinalStep; Price += FinalStep)
	{
		// Wymuszenie zaokrąglenia ceny każdej linii przed konwersją na piksel (Fidelity ES)
		int y = sc.RegionValueToYPixelCoordinate(sc.RoundToTickSize(Price), sc.GraphRegion);
		
		if (y >= T_coord && y <= B_coord) 
		{
			// USUNIĘTO: Pętlę segmentową (for x < R_coord)
			// TERAZ: Jedno wywołanie LineTo. System operacyjny sam nanosi wzór 1x10.
			MoveToEx(hDC, L_coord, y, NULL);
			LineTo(hDC, R_coord, y);
		}
	}
	// --- KONIEC ZMIAN ---

	SelectObject(hDC, hOldPen);
	SelectClipRgn(hDC, NULL);
}
