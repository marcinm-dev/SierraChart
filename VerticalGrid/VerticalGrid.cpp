#include "sierrachart.h"

SCDLLName("TN_VerticalGrid_GDI_Final") 

// Pomocnicza metoda tygodnia (ZAMROŻONA)
int GetWeekManual(int dateValue) { return dateValue / 7; }

// Prototyp funkcji rysującej GDI
void DrawVerticalGridGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc);

// ZŁOTY ŚRODEK: Zasięg siatki w przyszłość (zastępuje wpisywanie 200/500/1000 w wielu miejscach)
#define MAX_FORWARD_BARS 250

/*==========================================================================*/
SCSFExport scsf_VerticalGridTN(SCStudyInterfaceRef sc)
{
	SCInputRef Input_GridColor       = sc.Input[0];
	SCInputRef Input_Spacing         = sc.Input[1];
	SCInputRef Input_GapSize         = sc.Input[2];
	SCInputRef Input_DotLength       = sc.Input[3]; 
	SCInputRef Input_JitterThreshold = sc.Input[4];

	if (sc.SetDefaults)
	{
		sc.GraphName = "Vertical Grid";
		sc.StudyDescription = "Profesjonalna siatka GDI (V27 - Apex Performance).";

		sc.AutoLoop = 0;           
		sc.GraphRegion = 0;        
		sc.FreeDLL = 0;            

		sc.p_GDIFunction = DrawVerticalGridGDI;
		sc.DrawStudyUnderneathMainPriceGraph = 1;

		// Ukryty bufor mapy (Służy tylko do historii)
		sc.Subgraph[0].Name = "Internal Grid Map";
		sc.Subgraph[0].DrawStyle = DRAWSTYLE_IGNORE; 
		sc.Subgraph[0].DisplayNameValueInWindowsFlags = 0;     // Ukrywa wartości w oknach danych
		sc.Subgraph[0].IncludeInSpreadsheet = 0;               // Wyłącza z Excela
		sc.Subgraph[0].DisplayNameValueInDataLine = 0;        // Ukrywa napis w lewym górnym rogu regionu

		Input_GridColor.Name = "Grid Line Color";
		Input_GridColor.SetColor(RGB(128, 128, 128)); 
		Input_Spacing.Name = "Vertical Spacing (Every N periods)";
		Input_Spacing.SetInt(1);
		Input_GapSize.Name = "Line Gap Size (Pixels)";
		Input_GapSize.SetInt(6); 
		Input_DotLength.Name = "Dot Length (Pixels)";
		Input_DotLength.SetInt(3);
		Input_JitterThreshold.Name = "Hide Grid if Spacing < (Pixels)";
		Input_JitterThreshold.SetInt(100); 

		return;
	}

	// 1. GLOBALNY CACHE I MAPOWANIE (Optymalizacja Etap 1: Throttling)
	int& r_CachedGridType = sc.GetPersistentInt(20);
	int Spacing = Input_Spacing.GetInt();
	bool isNewBar = (sc.UpdateStartIndex >= sc.ArraySize - 1);

	// Przeliczaj typ siatki tylko przy zmianie interwału lub pełnym odświeżeniu
	if (sc.IsFullRecalculation || r_CachedGridType == 0) {
		if (sc.ChartDataType == 2) { 
			r_CachedGridType = (sc.SecondsPerBar < 3600) ? 1 : 2; 
		} else { 
			double DaysDiff = sc.BaseDateTimeIn[sc.ArraySize-1].GetAsDouble() - sc.BaseDateTimeIn[sc.ArraySize-2].GetAsDouble();
			if (DaysDiff <= 4.0) r_CachedGridType = 3; else if (DaysDiff <= 32.0) r_CachedGridType = 4; else r_CachedGridType = 5; 
		}
	}

	// 2. MAPOWANIE HISTORII (Optymalizacja: Redundantne zerowanie)
	if (sc.IsFullRecalculation) {
		for (int i = 0; i < sc.ArraySize; i++) sc.Subgraph[0][i] = 0;
	} else if (sc.ArraySize > 0) {
		sc.Subgraph[0][sc.ArraySize - 1] = 0; 
	}

	if (sc.UpdateStartIndex < sc.ArraySize) {
		for (int i = sc.UpdateStartIndex; i < sc.ArraySize; i++) {
			if (i <= 0) continue;
			SCDateTime cur = sc.BaseDateTimeIn[i]; SCDateTime pre = sc.BaseDateTimeIn[i-1];
			bool isNew = false;
			if (r_CachedGridType == 1 && sc.GetTradingDayDate(cur) != sc.GetTradingDayDate(pre)) isNew = true;
			else if (r_CachedGridType == 2 && GetWeekManual(sc.GetTradingDayDate(cur)) != GetWeekManual(sc.GetTradingDayDate(pre))) isNew = true;
			else if (r_CachedGridType == 3 && cur.GetMonth() != pre.GetMonth()) isNew = true;
			else if (r_CachedGridType == 4 && cur.GetYear() != pre.GetYear()) isNew = true;
			else if (r_CachedGridType == 5 && (cur.GetYear() / 10) != (pre.GetYear() / 10)) isNew = true;

			if (isNew && (i % Spacing == 0)) sc.Subgraph[0][i] = 1.0f;
		}
	}

	// 3. MAPOWANIE PRZYSZŁOŚCI (Dynamiczny zasięg oparty na MAX_FORWARD_BARS)
	int* pFutureMap = (int*)sc.GetPersistentPointer(2);
	if (pFutureMap == NULL) { 
		pFutureMap = new int[MAX_FORWARD_BARS + 1]; 
		sc.SetPersistentPointer(2, pFutureMap); 
	}

	if (sc.IsFullRecalculation || isNewBar) {
		for (int j = 0; j <= MAX_FORWARD_BARS; j++) {
			int idx = sc.ArraySize + j; pFutureMap[j] = 0;
			SCDateTime fCur = sc.BaseDateTimeIn[idx]; SCDateTime fPre = sc.BaseDateTimeIn[idx-1];
			if (fCur == 0 || fPre == 0) continue;
			bool fNew = false;
			if (r_CachedGridType == 1 && sc.GetTradingDayDate(fCur) != sc.GetTradingDayDate(fPre)) fNew = true;
			else if (r_CachedGridType == 2 && GetWeekManual(sc.GetTradingDayDate(fCur)) != GetWeekManual(sc.GetTradingDayDate(fPre))) fNew = true;
			else if (r_CachedGridType == 3 && fCur.GetMonth() != fPre.GetMonth()) fNew = true;
			else if (r_CachedGridType == 4 && fCur.GetYear() != fPre.GetYear()) fNew = true;
			else if (r_CachedGridType == 5 && (fCur.GetYear() / 10) != (fPre.GetYear() / 10)) fNew = true;

			if (fNew && (idx % Spacing == 0)) pFutureMap[j] = 1;
		}
	}

	
}
void DrawVerticalGridGDI(HWND hWnd, HDC hDC, SCStudyInterfaceRef sc)
{
	if (sc.ArraySize < 2) return;

	// Odczyt Cache (Ad.1 i Ad.2)
	const int StartBar = sc.IndexOfFirstVisibleBar; 
	const int EndBar   = sc.ArraySize + MAX_FORWARD_BARS;

	const int Spacing  = sc.Input[1].GetInt();
	const int JitterThreshold = sc.Input[4].GetInt();
	const int* pFutureMap = (const int*)sc.GetPersistentPointer(2);

	// JITTER CACHE (V24 - Optimized X-Recycling)
	int& r_LStart = sc.GetPersistentInt(11); int& r_LEnd = sc.GetPersistentInt(12);
	int& r_LRes = sc.GetPersistentInt(13); int& r_LThr = sc.GetPersistentInt(14);
	if ((StartBar != r_LStart || EndBar != r_LEnd || JitterThreshold != r_LThr) && JitterThreshold > 0) {
		int fX = -1; int sX = -1;
		int x_prev_jitter = sc.BarIndexToXPixelCoordinate(StartBar - 1);
		for (int i = StartBar; i < EndBar; i++) {
			int x_curr_jitter = sc.BarIndexToXPixelCoordinate(i);
			bool line = (i < sc.ArraySize) ? (sc.Subgraph[0][i] == 1.0f) : (pFutureMap && pFutureMap[i - sc.ArraySize] == 1);
			if (line) {
				int x = (x_curr_jitter + x_prev_jitter) / 2;
				if (fX == -1) fX = x; else { sX = x; break; }
			}
			x_prev_jitter = x_curr_jitter;
		}
		r_LRes = (fX != -1 && sX != -1 && abs(sX - fX) < JitterThreshold) ? 0 : 1;
		r_LStart = StartBar; r_LEnd = EndBar; r_LThr = JitterThreshold;
	} else if (JitterThreshold <= 0) r_LRes = 1;


	if (r_LRes == 0) return;

	// CLIPPING & PEN CACHE (V25/V22)
	int l=sc.StudyRegionLeftCoordinate; int t=sc.StudyRegionTopCoordinate;
	int r=sc.StudyRegionRightCoordinate; int b=sc.StudyRegionBottomCoordinate;
	if (t >= b) return;
	HRGN& r_hRgn = (HRGN&)sc.GetPersistentPointer(1);
	int& cl=sc.GetPersistentInt(16); int& ct=sc.GetPersistentInt(17); 
	int& cr=sc.GetPersistentInt(18); int& cb=sc.GetPersistentInt(19);
	if (!r_hRgn || l!=cl || t!=ct || r!=cr || b!=cb) {
		if (r_hRgn) DeleteObject(r_hRgn); r_hRgn = CreateRectRgn(l,t,r,b);
		cl=l; ct=t; cr=r; cb=b;
	}
	SelectClipRgn(hDC, r_hRgn);

	COLORREF col = sc.Input[0].GetColor(); HPEN hP = (HPEN)sc.GetPersistentPointer(0);
	COLORREF lCol = (COLORREF)sc.GetPersistentInt(10);
	if (!hP || col != lCol) {
		if (hP) DeleteObject(hP); hP = CreatePen(PS_SOLID, 1, col);
		sc.SetPersistentPointer(0, hP); sc.SetPersistentInt(10, (int)col);
	}
	HGDIOBJ hOld = SelectObject(hDC, hP);

	
// -------------------------------------------------------------------
	// 3. APEX LOOP (Ad.3: Polyline Engine Optimized)
	// -------------------------------------------------------------------
	const int Dot = sc.Input[3].GetInt(); 
	const int Gap = sc.Input[2].GetInt();
	const int Segment = Dot + Gap; // Optymalizacja kroku Y: Obliczamy raz
	
	POINT pts[1000];
	int x_prev = sc.BarIndexToXPixelCoordinate(StartBar - 1);

	for (int i = StartBar; i < EndBar; i++)
	{
		int x_curr = sc.BarIndexToXPixelCoordinate(i);
		bool draw = (i < sc.ArraySize) ? (sc.Subgraph[0][i] == 1.0f) : (pFutureMap && pFutureMap[i - sc.ArraySize] == 1);

		if (draw && abs(x_curr - x_prev) >= 2) {
			int x_mid = (x_curr + x_prev) / 2;
			int ptCount = 0;
			
			// Pętla pionowa używa pre-kalkulowanego kroku Segment
			for (int y = t; y < b && ptCount < 998; y += Segment) {
				int s_e = y + Dot; 
				if (s_e > b) s_e = b;
				
				pts[ptCount].x = x_mid; pts[ptCount].y = y;
				pts[ptCount+1].x = x_mid; pts[ptCount+1].y = s_e;
				ptCount += 2;
			}
			
			// Rysowanie całej linii jednym wywołaniem systemowym (PolyPolyline)
			if (ptCount >= 2) {
				DWORD polyCounts[500];
				int numLines = ptCount / 2;
				for (int c = 0; c < numLines; c++) polyCounts[c] = 2;
				PolyPolyline(hDC, pts, polyCounts, numLines);
			}
		}
		x_prev = x_curr;
	}


	
	SelectClipRgn(hDC, NULL); SelectObject(hDC, hOld);
}
