/*
 * @file reclaims.cpp
 * @brief This file contains functions for managing and drawing reclaim areas on a price chart using Sierra Chart's custom study interface.
 *
 * The functions include:
 * - Drawing and updating reclaim rectangles
 * - Handling memory management for reclaim data
 * - Managing reclaim state across multiple bars
 *
 * @author dream_without
 * @date 2024-09-01
 *
 * @license MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include "sierrachart.h"

SCDLLName("FatCat Reclaims");

/**
 * @struct Reclaim
 * @brief Represents a reclaim
 *
 * This structure holds information about a specific rectangle on a financial chart,
 * including pricing on fixed and active sides, the start date, and additional metadata.
 */
struct Reclaim
{
	/**
	 * @brief Price on the fixed side of the rectangle.
	 *
	 * This is the price that remains constant along one side of the rectangle.
	 */
	float FixedSidePrice;

	/**
	 * @brief Price on the active side of the rectangle.
	 *
	 * This is the price that may vary or move during the period the rectangle is active.
	 */
	float ActiveSidePrice;

	/**
	 * @brief The maximum height in ticks that the reclaims got to be during its existance
	 *
	 * when the current rectangle height is smaller than MaxHeight by a certain number of ticks, 
	 * a new reclaim should be created.
	 */
	int MaxHeight;

	/**
	 * @brief The current height in ticks of the reclaim
	 *
	 * This is calcuated as abs(ActiveSidePrice-FixedSidePrice)
	 */
	int CurrentHeight;

	/**
	 * @brief The maximum retracement (How many ticks smaller the reclaim got from the MaxHeight)
	 *
	 */
	int MaxRetracement;

	/**
	 * @brief The start date and time when the rectangle is created.
	 *
	 * Represents the left anchor of the rectangle
	 */
	SCDateTime StartDate;

	/**
	 * @brief The line number associated with the rectangle.
	 *
	 * This is the sierra chart LineNumber of the rectangle drawing that corresplonds to the reclaim
	 */
	int LineNumber;

	/**
	 * @brief Flag indicating if the rectangle has been deleted.
	 *
	 * When set to `true`, the rectangle is considered deleted and should no longer be displayed.
	 */
	bool Deleted;

	/**
	 * @brief Type of the reclaim.
	 *
	 * Defines the type of reclaim:
	 * - `0`: Bullish reclaim
	 * - `1`: Bearish reclaim
	 */
	int Type;
};

/**
 * @brief Checks for price overlap in the last specified number of bars.
 *
 * This function determines whether there is an overlap in the price range
 * of the last `numberOfBars` bars on a chart. An overlap occurs when each
 * bar's price range (high to low) overlaps with the previous ones.
 *
 * @param sc A reference to the study interface, providing access to chart data.
 * @param numberOfBars The number of bars to check for price overlap.
 * @return `true` if there is an overlap in the price ranges of the last `numberOfBars` bars, otherwise `false`.
 */
bool CheckPriceOverlap(SCStudyInterfaceRef sc, int numberOfBars)
{
	// Ensure there are enough bars to check
	if (sc.ArraySize < numberOfBars)
		return false;

	// Initialize variables for high and low price ranges
	float lastHigh = sc.High[sc.ArraySize - 1];
	float lastLow = sc.Low[sc.ArraySize - 1];

	// Iterate through the last `numberOfBars` bars
	bool isOverlap = true;
	for (int i = 1; i < numberOfBars; ++i)
	{
		// Retrieve the high and low prices of the current bar
		float high = sc.High[sc.ArraySize - 1 - i];
		float low = sc.Low[sc.ArraySize - 1 - i];

		// Update the maximum high and minimum low
		if (low >= lastHigh || high <= lastLow)
		{
			isOverlap = false;
			break;
		}
	}

	return isOverlap;
}

/**
 * @brief Draws or updates a rectangle on the chart to represent a reclaim area.
 *
 * This function either draws a new rectangle or updates an existing one on the chart
 * based on the provided reclaim data.
 *
 * @param sc A reference to the study interface, providing access to chart data and tools.
 * @param reclaim A reference to the `Reclaim` structure containing the data needed to draw the rectangle.
 * @param createNew A boolean flag indicating whether to create a new rectangle (`true`) or update an existing one (`false`).
 *                  - `true`: A new rectangle is drawn, and a new line number is assigned.
 *                  - `false`: The existing rectangle with the specified line number is updated.
 * @return The line number of the newly created rectangle, or `-1` if an existing rectangle was updated.
 */
int DrawReclaim(SCStudyInterfaceRef sc, const Reclaim &reclaim, bool createNew = false)
{
	// Draw the initial rectangle
	s_UseTool RectangleTool;
	RectangleTool.Clear(); // Initialize the Tool structure

	RectangleTool.ChartNumber = sc.ChartNumber;
	RectangleTool.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
	RectangleTool.AddAsUserDrawnDrawing = 0;
	RectangleTool.Region = 0;

	// Define the rectangle coordinates
	RectangleTool.BeginDateTime = reclaim.StartDate;
	RectangleTool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize + sc.Input[2].GetInt()];
	RectangleTool.BeginValue = reclaim.FixedSidePrice;
	RectangleTool.EndValue = reclaim.ActiveSidePrice;

	// Set the rectangle color
	if (reclaim.Type == 0)
	{
		// bullish reclaim
		RectangleTool.Color = sc.Input[3].GetColor();
		RectangleTool.SecondaryColor = sc.Input[3].GetColor();
	}
	else
	{
		// bearish reclaim
		RectangleTool.Color = sc.Input[4].GetColor();
		RectangleTool.SecondaryColor = sc.Input[4].GetColor();
	}

	RectangleTool.TransparencyLevel = 70;

	if (!createNew)
	{
		RectangleTool.LineNumber = reclaim.LineNumber;
		sc.UseTool(RectangleTool);
		// always return -1 when updating existing rectangle
		return -1;
	}
	else
	{
		// Creating new rectangle. Allow sierra to choose a new LineNumber.
		sc.UseTool(RectangleTool);
		// return the LineNumber of the new rectangle
		return RectangleTool.LineNumber;
	}
}

/**
 * @brief Deletes a reclaim rectangle from the chart.
 *
 * This function removes a previously drawn rectangle from the chart, identified by the
 * `LineNumber` in the provided `Reclaim` structure.
 *
 * @param sc A reference to the study interface, providing access to chart data and tools.
 * @param reclaim A reference to the `Reclaim` structure that contains the line number of the rectangle to be deleted.
 */
void DeleteReclaim(SCStudyInterfaceRef sc, const Reclaim &reclaim)
{
	sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, reclaim.LineNumber);
}

/**
 * @brief Updates and manages the drawing of reclaim rectangles on the chart based on the current price.
 *
 * This function updates the reclaim areas on the chart by adjusting the `ActiveSidePrice` and `FixedSidePrice`
 * of both bullish (up) and bearish (down) reclaims. If a reclaim has been fully reclaimed (price crosses the
 * fixed side), the corresponding rectangle is deleted. Otherwise, the rectangle is updated or drawn with the
 * specified colors.
 *
 * @param sc A reference to the study interface, providing access to chart data and tools.
 * @param size The number of reclaim structures to update in the `upReclaims` and `downReclaims` arrays.
 */
void UpdateReclaims(SCStudyInterfaceRef sc, int size)
{
	// get sierra chart persistent variables for up and down reclaims
	Reclaim *upReclaims = (Reclaim *)sc.GetPersistentPointer(1);
	Reclaim *downReclaims = (Reclaim *)sc.GetPersistentPointer(2);

	bool ShowCurrentReclaim = sc.Input[6].GetYesNo();

	// get current price
	float CurrentPrice = sc.LastTradePrice;
	//float CurrentHigh = max(sc.High[sc.Index], sc.High[sc.Index-1]);
	//float CurrentLow = min(sc.Low[sc.Index], sc.Low[sc.Index-1]);
	float CurrentHigh = sc.High[sc.Index-1];
	float CurrentLow = sc.Low[sc.Index-1];
	float CurrentClose = sc.Close[sc.Index-1];
	float CurrentOpen = sc.Open[sc.Index-1];

	// Loop all up reclaims and update them according to CurrentPrice
	for (int i = 0; i < size; i++)
	{
		if (i == 0)
		{
			// update active side of first rectangle
			upReclaims[i].ActiveSidePrice = CurrentHigh;

			// update reclaim max height parameter if it got bigger
			upReclaims[i].CurrentHeight = (int)((upReclaims[i].ActiveSidePrice-upReclaims[i].FixedSidePrice)/sc.TickSize);

			int newMaxHeight = int((CurrentHigh-upReclaims[i].FixedSidePrice)/sc.TickSize);
			if (newMaxHeight>upReclaims[i].MaxHeight) {
				upReclaims[i].MaxHeight = newMaxHeight;
			}

			int newMaxRetracement = int((upReclaims[i].FixedSidePrice+upReclaims[i].MaxHeight*sc.TickSize-CurrentClose)/sc.TickSize);
			if(newMaxRetracement>upReclaims[i].MaxRetracement) {
				upReclaims[i].MaxRetracement = newMaxRetracement;
			}

			if (CurrentLow <= upReclaims[i].FixedSidePrice)
			{
				// update fixed side as well
				upReclaims[i].FixedSidePrice = CurrentLow;
				upReclaims[i].ActiveSidePrice = CurrentLow;
				upReclaims[i].StartDate = sc.BaseDateTimeIn[sc.ArraySize - 1];
				upReclaims[i].CurrentHeight = 0;
				upReclaims[i].MaxHeight = 0;
				upReclaims[i].MaxRetracement = 0;
			}
		}
		else
		{
			if (CurrentLow < upReclaims[i].ActiveSidePrice)
			{
				upReclaims[i].ActiveSidePrice = max(CurrentLow, upReclaims[i].FixedSidePrice);
			}

			if (CurrentLow<=upReclaims[i].FixedSidePrice || upReclaims[i].ActiveSidePrice<=upReclaims[i].FixedSidePrice)
			{
				// delete drawing, the reclaim has been reclaimed
				upReclaims[i].Deleted = true; // update struct in array
				// delete drawing
				DeleteReclaim(sc, upReclaims[i]);
				continue;
			}
		}


		if(i==0 && !ShowCurrentReclaim) {
			// don't draw active reclaim
			continue;
		}

		DrawReclaim(sc, upReclaims[i]);
	}

	// Loop all down reclaims and update them according to CurrentPrice
	for (int i = 0; i < size; i++)
	{

		if (i == 0)
		{

			// update active side of first rectangle
			downReclaims[i].ActiveSidePrice = CurrentLow;

			downReclaims[i].CurrentHeight= (int)((downReclaims[i].FixedSidePrice-downReclaims[i].ActiveSidePrice)/sc.TickSize);

			// update reclaim max height parameter if it got bigger
			int newMaxHeight = int((downReclaims[i].FixedSidePrice-CurrentLow)/sc.TickSize);
			if (newMaxHeight>downReclaims[i].MaxHeight) {
				downReclaims[i].MaxHeight = newMaxHeight;
			}

			int newMaxRetracement = int((CurrentClose-(downReclaims[i].FixedSidePrice-downReclaims[i].MaxHeight*sc.TickSize))/sc.TickSize);
			if(newMaxRetracement>downReclaims[i].MaxRetracement) {
				downReclaims[i].MaxRetracement = newMaxRetracement;
			}

			if (CurrentHigh >= downReclaims[i].FixedSidePrice)
			{
				// update fixed side as well
				downReclaims[i].FixedSidePrice = CurrentHigh;
				downReclaims[i].StartDate = sc.BaseDateTimeIn[sc.ArraySize - 1];
				downReclaims[i].CurrentHeight = 0;
				downReclaims[i].MaxHeight = 0;
				downReclaims[i].MaxRetracement = 0;
			}
		}
		else
		{
			if (CurrentHigh > downReclaims[i].ActiveSidePrice)
			{
				downReclaims[i].ActiveSidePrice = min(CurrentHigh, downReclaims[i].FixedSidePrice);
			}

			if (CurrentHigh>=downReclaims[i].FixedSidePrice || downReclaims[i].ActiveSidePrice>=downReclaims[i].FixedSidePrice)
			{
				// delete drawing, the reclaim has been reclaimed
				downReclaims[i].Deleted = true; // update struct in array
				// delete drawing
				DeleteReclaim(sc, downReclaims[i]);
				continue;
			}
		}


		if(i==0 && !ShowCurrentReclaim) {
			// don't draw active reclaim
			continue;
		}
		DrawReclaim(sc, downReclaims[i]);
	}
}

/**
 * @brief A Sierra Chart study function that manages the drawing of reclaim rectangles on the chart.
 *
 * This function tracks price movements to identify and visualize "reclaim" areas on the chart.
 * The function handles the initialization, updating, and deletion of
 * these reclaim rectangles, as well as memory management.
 *
 * @param sc A reference to the study interface, providing access to chart data, user inputs,
 *           and drawing tools.
 */
SCSFExport scsf_Reclaims(SCStudyInterfaceRef sc)
{
	// user inputs
	SCInputRef MaxNumberOfReclaims = sc.Input[0];	   // length of the p_UpReclaims and p_DownReclaims arrays
	SCInputRef NewReclaimThreshold = sc.Input[1];	   // Minimum size in ticks that an existing reclaim must be to start creating new ones (if there is enough bar overlap)
	SCInputRef RectangleExtendBars = sc.Input[2];	   // How many bars the rectangles should extend to the right
	SCInputRef UpReclaimsColor = sc.Input[3];		   // color of bullish reclaims
	SCInputRef DownReclaimsColor = sc.Input[4];		   // color of bearish reclaims
	SCInputRef UpdateOnBarClose = sc.Input[5];		   // When true, only update reclaims on bar close
	SCInputRef ShowCurrentReclaim = sc.Input[6];		// Display the reclaim being build when set to true

	// Persistent variables to store the previous price (required to only update reclaims if price has changed)
	float &PreviousPrice = sc.GetPersistentFloat(0);

	// Persistent pointers to up and down Reclaims
	Reclaim *p_UpReclaims = (Reclaim *)sc.GetPersistentPointer(1);
	Reclaim *p_DownReclaims = (Reclaim *)sc.GetPersistentPointer(2);
	int &lastIndex = sc.GetPersistentInt(3); 

	// Set default study properties
	if (sc.SetDefaults)
	{

		sc.GraphName = "FatCat reclaims";
		sc.StudyDescription = "Draws reclaims on the chart";
		sc.GraphRegion = 0;

		// Inputs default values
		MaxNumberOfReclaims.Name = "Max active reclaims";
		MaxNumberOfReclaims.SetInt(100);			  
		MaxNumberOfReclaims.SetIntLimits(1, 1000); 

		// Inputs default values
		NewReclaimThreshold.Name = "Threshold tick size";
		NewReclaimThreshold.SetInt(2); 
		NewReclaimThreshold.SetIntLimits(1,1000);

		// Inputs default values
		RectangleExtendBars.Name = "Extend right amount";
		RectangleExtendBars.SetInt(10000); // Default to 10 bars extension
        RectangleExtendBars.SetIntLimits(0, 10000); // Allow extension to a maximum of 500 bars

		UpReclaimsColor.Name = "Up reclaims color";
		UpReclaimsColor.SetColor(RGB(0, 100, 255)); 

		DownReclaimsColor.Name = "Down reclaims color";
		DownReclaimsColor.SetColor(RGB(255, 100, 0)); 

		UpdateOnBarClose.Name = "Only update on bar close";
		UpdateOnBarClose.SetYesNo(1); 

		ShowCurrentReclaim.Name = "Show reclaim being built";
		ShowCurrentReclaim.SetYesNo(0); 

		return;
	}

	// Get the current price

	// Initialize stuff on the first run
	if (sc.Index == 0)
	{
		PreviousPrice = sc.LastTradePrice;

		if (p_UpReclaims == NULL)
		{
			// Allocate array for up reclaims.
			p_UpReclaims = (Reclaim *)sc.AllocateMemory(MaxNumberOfReclaims.GetInt() * sizeof(Reclaim));

			// inizialize default values for Deleted and Type fields
			for (int i = 0; i < MaxNumberOfReclaims.GetInt(); i++)
			{
				p_UpReclaims[i].Deleted = true;
				p_UpReclaims[i].Type = 0;
				p_UpReclaims[i].CurrentHeight= 0;
				p_UpReclaims[i].MaxHeight = 0;
				p_UpReclaims[i].MaxRetracement = 0;
			}

			// initialize values for first reclaim
			p_UpReclaims[0].FixedSidePrice = sc.LastTradePrice;
			p_UpReclaims[0].ActiveSidePrice = sc.LastTradePrice;
			p_UpReclaims[0].StartDate = sc.BaseDateTimeIn[sc.ArraySize - 1];
			p_UpReclaims[0].Deleted = false;

			// store array in the persistent variable
			if (p_UpReclaims != NULL)
			{
				sc.SetPersistentPointer(1, p_UpReclaims);
			}

			// draw first reclaim and store the sierra chart linenumber
			p_UpReclaims[0].LineNumber = DrawReclaim(sc, p_UpReclaims[0], true);
		}

		if (p_DownReclaims == NULL)
		{
			// Allocate array for down reclaims.
			p_DownReclaims = (Reclaim *)sc.AllocateMemory(MaxNumberOfReclaims.GetInt() * sizeof(Reclaim));

			// inizialize default values for Deleted and Type fields
			for (int i = 0; i < MaxNumberOfReclaims.GetInt(); i++)
			{
				p_DownReclaims[i].Deleted = true;
				p_DownReclaims[i].Type = 1;
				p_DownReclaims[i].CurrentHeight = 0;
				p_DownReclaims[i].MaxHeight = 0;
				p_DownReclaims[i].MaxRetracement = 0;
			}

			// initialize values for first reclaim
			p_DownReclaims[0].FixedSidePrice = sc.LastTradePrice;
			p_DownReclaims[0].ActiveSidePrice = sc.LastTradePrice;
			p_DownReclaims[0].StartDate = sc.BaseDateTimeIn[sc.ArraySize - 1];
			p_DownReclaims[0].Deleted = false;

			// store array in the persistent variable
			if (p_DownReclaims != NULL)
			{
				sc.SetPersistentPointer(2, p_DownReclaims);
			}

			// draw first reclaim and store the sierra chart linenumber
			p_DownReclaims[0].LineNumber = DrawReclaim(sc, p_DownReclaims[0], true);
		}

		return;
	}


	// return if no new bar has formed and UpdateOnBarClose is set to true
	if (UpdateOnBarClose.GetYesNo() && lastIndex == sc.Index) { 
        return; 
    } 

    lastIndex = sc.Index; 

	// If the price has changed, update stuff
	// store new value for PreviousPrice
	PreviousPrice = sc.LastTradePrice;

	// Check if we need to create a new bullish reclaim
	if (p_UpReclaims[0].MaxRetracement >= NewReclaimThreshold.GetInt())
	{

		// delete rectangle that corresponds to the last array element
		DeleteReclaim(sc, p_UpReclaims[MaxNumberOfReclaims.GetInt() - 1]);

		// Shift elements of the array to the right
		for (int i = MaxNumberOfReclaims.GetInt() - 1; i > 0; --i)
		{
			p_UpReclaims[i] = p_UpReclaims[i - 1];
		}

		// first member of the array is now the new reclaim, so update its values
		p_UpReclaims[0].FixedSidePrice = sc.LastTradePrice;
		p_UpReclaims[0].ActiveSidePrice = p_UpReclaims[0].FixedSidePrice;
		p_UpReclaims[0].StartDate = sc.BaseDateTimeIn[sc.ArraySize - 1];
		p_UpReclaims[0].MaxHeight = 0;
		p_UpReclaims[0].CurrentHeight = 0;
		p_UpReclaims[0].MaxRetracement = 0;
		p_UpReclaims[0].Deleted = false;

		// draw the new rectangle and store the sierra LineNumber
		p_UpReclaims[0].LineNumber = DrawReclaim(sc, p_UpReclaims[0], true);
	}

	// Check if we need to create a new bearish reclaim
	if (p_DownReclaims[0].MaxRetracement >= NewReclaimThreshold.GetInt())
	{
		// delete rectangle that corresponds to the last array element
		DeleteReclaim(sc,p_DownReclaims[MaxNumberOfReclaims.GetInt() - 1]);

		// Shift elements of the array to the right
		for (int i = MaxNumberOfReclaims.GetInt() - 1; i > 0; --i)
		{
			p_DownReclaims[i] = p_DownReclaims[i - 1];
		}

		// first member of the array is now the new reclaim, so update its values
		p_DownReclaims[0].FixedSidePrice = sc.LastTradePrice;
		p_DownReclaims[0].ActiveSidePrice = p_DownReclaims[0].FixedSidePrice;
		p_DownReclaims[0].StartDate = sc.BaseDateTimeIn[sc.ArraySize - 1];
		p_DownReclaims[0].MaxHeight = 0;
		p_DownReclaims[0].CurrentHeight = 0;
		p_DownReclaims[0].MaxRetracement = 0;
		p_DownReclaims[0].Deleted = false;

		// draw the new rectangle and store the sierra LineNumber
		p_DownReclaims[0].LineNumber = DrawReclaim(sc, p_DownReclaims[0], true);
	}

	// update existing reclaims
	UpdateReclaims(sc, MaxNumberOfReclaims.GetInt());

	// Memory management: Deallocate when the study is unloaded
	if (sc.LastCallToFunction)
	{
		// clear memory for bullish reclaims
		if (p_UpReclaims != NULL)
		{
			sc.FreeMemory(p_UpReclaims);
			sc.SetPersistentPointer(1, NULL);
		}

		// clear memory for bearish reclaims
		if (p_DownReclaims != NULL)
		{
			sc.FreeMemory(p_DownReclaims);
			sc.SetPersistentPointer(2, NULL);
		}
	}
}
