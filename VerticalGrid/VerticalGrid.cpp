#include "sierrachart.h"

SCDLLName("TN_VerticalGrid_GDI_Final") 

// Pomocnicza metoda tygodnia (ZAMROŻONA)
int GetWeekManual(int dateValue) { return dateValue / 7; }

// Prototyp funkcji rysującej GDI (Silnik Stipple)
void DrawVerticalGridGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc);

/*==========================================================================*/
SCSFExport scsf_VerticalGridTN(SCStudyInterfaceRef sc)
{
	SCInputRef Input_GridColor = sc.Input[0];
	SCInputRef Input_Spacing = sc.Input[1];
	SCInputRef Input_GapSize = sc.Input[2];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Vertical Grid TN (GDI FINAL)";
		sc.StudyDescription = "Profesjonalna siatka GDI pod słupkami ceny (TradeNavigator Style).";

		sc.AutoLoop = 0;           
		sc.GraphRegion = 0;        
		sc.FreeDLL = 0;            

		// Rejestracja niskopoziomowego silnika GDI
		sc.p_GDIFunction = DrawVerticalGridGDI;
		
		// Warstwa tła (pod słupkami ceny)
		sc.DrawStudyUnderneathMainPriceGraph = 1;

		Input_GridColor.Name = "Grid Line Color";
		Input_GridColor.SetColor(RGB(128, 128, 128)); 

		Input_Spacing.Name = "Vertical Spacing (Every N periods)";
		Input_Spacing.SetInt(1);
		Input_Spacing.SetIntLimits(1, 100);

		Input_GapSize.Name = "Line Gap Size (Pixels)";
		Input_GapSize.SetInt(6); 
		Input_GapSize.SetIntLimits(1, 50);

		return;
	}

	// Mechanizm czyszczenia starych rysunków s_UseTool (ZAMROŻONY)
	int& r_LastMaxIndex = sc.GetPersistentInt(1);
	if (sc.IsFullRecalculation)
	{
		for (int i = 0; i <= r_LastMaxIndex; i++)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_VERTICALLINE, 60000 + i);
		}
		r_LastMaxIndex = 0;
	}
}

/*==========================================================================*/
// SILNIK GRAFICZNY GDI (Stage 3 - Stipple Engine)
/*==========================================================================*/
void DrawVerticalGridGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc)
{
	if (sc.ArraySize < 2) return;

	// 1. Logika mapowania interwałów (ZAMROŻONA)
	int TargetGridType = 0; 
	if (sc.ChartDataType == 2) { // Intraday
		if (sc.SecondsPerBar < 3600) TargetGridType = 1; 
		else TargetGridType = 2; 
	} else { // Historical
		double DaysDiff = sc.BaseDateTimeIn[sc.ArraySize-1].GetAsDouble() - sc.BaseDateTimeIn[sc.ArraySize-2].GetAsDouble();
		if (DaysDiff <= 4.0) TargetGridType = 3; 
		else if (DaysDiff <= 32.0) TargetGridType = 4; 
		else TargetGridType = 5; 
	}

	// Poprawka 3: Sanity Check - zabezpieczenie przed błędną wysokością regionu
	int top    = sc.StudyRegionTopCoordinate;
	int bottom = sc.StudyRegionBottomCoordinate;
	if (top >= bottom) return;

	// Poprawka 1: Ochrona osi (Clipping Region) - blokujemy malowanie poza wykres
	HRGN hRgn = CreateRectRgn(sc.StudyRegionLeftCoordinate, top, sc.StudyRegionRightCoordinate, bottom);
	SelectClipRgn(hDC, hRgn);

	// 2. Konfiguracja narzędzi malarskich GDI
	COLORREF GridColor = sc.Input[0].GetColor();
	HPEN hPen = CreatePen(PS_SOLID, 1, GridColor);
	HGDIOBJ hOldPen = SelectObject(hDC, hPen);


	// 3. Cache parametrów (Optymalizacja "Micro-Lag")
	const int StartBar = sc.IndexOfFirstVisibleBar; 
	const int EndBar   = sc.ArraySize + 200; // Stabilna ekstrapolacja V8
	const int Spacing  = sc.Input[1].GetInt();
	const int GapSize  = sc.Input[2].GetInt();

	for (int i = StartBar; i < EndBar; i++)
	{
		// Zabezpieczenie zakresu tablicy (indeks -1)
		if (i <= 0 || i >= sc.ArraySize + 200) continue;

		// Korzystamy z faktu, że Sierra pre-wypełnia tablicę dla Forward Projection
		SCDateTime CurrentDT = sc.BaseDateTimeIn[i];
		SCDateTime PrevDT    = sc.BaseDateTimeIn[i-1];
		
		if (CurrentDT == 0 || PrevDT == 0) continue;

		bool IsNewPeriod = false;

		// Logika detekcji sesji (ZAMROŻONA - Futures Friendly)
		if (TargetGridType == 1) { 
			if (sc.GetTradingDayDate(CurrentDT) != sc.GetTradingDayDate(PrevDT)) IsNewPeriod = true;
		}
		else if (TargetGridType == 2) { 
			if (GetWeekManual(sc.GetTradingDayDate(CurrentDT)) != GetWeekManual(sc.GetTradingDayDate(PrevDT))) IsNewPeriod = true;
		}
		else if (TargetGridType == 3) { 
			if (CurrentDT.GetMonth() != PrevDT.GetMonth()) IsNewPeriod = true;
		}
		else if (TargetGridType == 4) { 
			if (CurrentDT.GetYear() != PrevDT.GetYear()) IsNewPeriod = true;
		}
		else if (TargetGridType == 5) { 
			if ((CurrentDT.GetYear() / 10) != (PrevDT.GetYear() / 10)) IsNewPeriod = true;
		}

		// Rysowanie z użyciem zoptymalizowanych zmiennych lokalnych
		if (IsNewPeriod && (i % Spacing == 0))
		{
			int x_current = sc.BarIndexToXPixelCoordinate(i);
			int x_previous = sc.BarIndexToXPixelCoordinate(i - 1);

			if (abs(x_current - x_previous) < 2) continue; 

			int x = (x_current + x_previous) / 2;
			
			for (int y_pos = top; y_pos < bottom; y_pos += (3 + GapSize))
			{


				int segment_end = y_pos + 3;
				if (segment_end > bottom) segment_end = bottom;
				
				MoveToEx(hDC, x, y_pos, NULL);
				LineTo(hDC, x, segment_end);
			}
		}
	}
// 4. Zwolnienie zasobów GDI i usunięcie bariery clippingu
	SelectClipRgn(hDC, NULL);
	DeleteObject(hRgn);
	SelectObject(hDC, hOldPen);
	DeleteObject(hPen);
}
