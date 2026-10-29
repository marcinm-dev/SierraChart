#include "sierrachart.h"

SCDLLName("ChartInfo") 

SCSFExport scsf_ChartInfo(SCStudyInterfaceRef sc)
{
	SCInputRef InHoriz = sc.Input[0];
	SCInputRef InVert = sc.Input[1];
	SCInputRef InColor = sc.Input[2];
	SCInputRef InFontSize = sc.Input[3];
	SCInputRef InBold = sc.Input[4];

if (sc.SetDefaults)
	{
		sc.GraphName = "ChartInfo"; 
		sc.AutoLoop = 0; 
		sc.GraphRegion = 0;

		InHoriz.Name = "Horizontal Position (1-100)"; InHoriz.SetInt(149); 
		InVert.Name = "Vertical Position (1-100)"; InVert.SetInt(100); 
		InColor.Name = "Text Color"; InColor.SetColor(RGB(0, 0, 0));
		InFontSize.Name = "Font Size"; InFontSize.SetInt(10);
		InBold.Name = "Use Bold Font"; InBold.SetYesNo(1); 

		return;
	}
	
	// --- ZMIENNE PAMIĘCIOWE (PERSISTENT) ---
	float& r_LastPrice = sc.GetPersistentFloat(1);
	int& r_LastMin = sc.GetPersistentInt(1);
	int& r_LineNumber = sc.GetPersistentInt(2);
	double& r_LastUpdate = sc.GetPersistentDouble(1); 
	SCString& r_CachedTimePart = sc.GetPersistentSCString(1);

	if (r_LineNumber == 0) r_LineNumber = 2026;

	// --- 1. OPTYMALIZACJA: THROTTLING (200ms) ---
	double CurrentTime = sc.GetCurrentDateTime().GetAsDouble();
	
	// *** ZMIANA: Dodano !sc.IsFullRecalculation, aby wymusić rysowanie przy starcie/zmianie interwału ***
	if ((CurrentTime - r_LastUpdate) < (0.2 / 86400.0) && r_LastUpdate != 0 && !sc.IsFullRecalculation)
		return;

	int LastIdx = sc.ArraySize - 1;
	if (LastIdx < 0) return;

	// --- DANE WEJŚCIOWE ---
	SCDateTime dt = sc.LatestDateTimeForLastBar;
	float currentPrice = sc.Close[LastIdx];
	int currentMin = dt.GetMinute();

	// Sprawdzamy czy cokolwiek się zmieniło od ostatniego rysowania
	bool priceChanged = (currentPrice != r_LastPrice);
	bool minuteChanged = (currentMin != r_LastMin);

	// *** ZMIANA: Dodano !sc.IsFullRecalculation do bramki logicznej ***
	if (!priceChanged && !minuteChanged && !r_CachedTimePart.IsEmpty() && !sc.IsFullRecalculation)
		return;

// --- 2. OPTYMALIZACJA: BUFOROWANIE CZASU ---
	if (minuteChanged || r_CachedTimePart.IsEmpty() || sc.IsFullRecalculation)
	{
		r_CachedTimePart.Format("%02d/%02d/%04d %02d:%02d", 
			dt.GetMonth(), dt.GetDay(), dt.GetYear(),
			dt.GetHour(), currentMin);
		r_LastMin = currentMin;
	}

	r_LastPrice = currentPrice;
	r_LastUpdate = CurrentTime;

	// --- 3. SKŁADANIE FINALNEGO TEKSTU ---
	SCString FinalText;
	FinalText.Format("%s = %.2f", r_CachedTimePart.GetChars(), currentPrice);

	// --- 4. RYSOWANIE (POZYCJONOWANIE RELATYWNE 1-100) ---
	s_UseTool Tool;
	Tool.Clear();
	Tool.DrawingType = DRAWING_TEXT;
	Tool.LineNumber = r_LineNumber;
	Tool.ChartNumber = sc.ChartNumber;
	Tool.AddMethod = UTAM_ADD_OR_ADJUST;
	
	Tool.BeginDateTime = (double)InHoriz.GetInt(); 
	Tool.BeginValue = (float)InVert.GetInt();
	Tool.UseRelativeVerticalValues = true; 
	
	Tool.Color = InColor.GetColor();
	Tool.FontSize = InFontSize.GetInt();
	Tool.FontBold = InBold.GetYesNo(); 
	Tool.Text = FinalText;
	Tool.TextAlignment = DT_RIGHT; 

	sc.UseTool(Tool);
}