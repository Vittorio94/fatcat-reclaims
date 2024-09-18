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
	 * @brief The expected value of the reclaim
	 *
	 * Every time price touches the active side of the rectangle after a pullback, ev increases by 1
	 */
	int EV;

	/**
	 * @brief Wether to increse EV the next time active side is touched
	 *
	 * When price pulls away enough ticks from the active side price, this is set to true.
	 * The next time price touches the active side of the reclaim, if IncreaseExpectedValueOnNextTouch
	 * is true then ExpectedValue is increased by 1
	 */
	bool IncreaseEVOnNextTouch;

	/**
	 * @brief The swing counter of the reclaim
	 *
	 * Every time price touches the active side of the rectangle after a pullback, Swing increases by 1
	 */
	int Swing;

	/**
	 * @brief Wether to incresse the swing counter the next time active side is touched
	 *
	 * When price pulls away enough ticks from the active side price, this is set to true.
	 * The next time price touches the active side of the reclaim, if IncreaseSwingOnNextTouch
	 * is true then Swing is increased by 1
	 */
	bool IncreaseSwingOnNextTouch;

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
	int RectLineNumber;

	/**
	 * @brief The line number associated with the EV text.
	 *
	 * This is the sierra chart LineNumber of the EV text drawing that corresplonds to the reclaim
	 */
	int EVTextLineNumber;

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

	/**
	 * @brief The time when the reclaim became smaller than the minimum reclaim size
	 *
	 * This is used to modulate the colors of the reclaims that become hollow because they are too small
	 */
	SCDateTime DecayStartTime;
};

/**
 * Sign function for floats
 */
int Sign(float value)
{
    if (value > 0)
        return 1;
    else if (value < 0)
        return -1;
    else
        return 0;
}

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

bool StartNewReclaimCheck(SCStudyInterfaceRef sc, const Reclaim &reclaim) {
	if(reclaim.MaxRetracement < sc.Input[1].GetInt()){
		return false;
	}

	if(sc.Input[12].GetYesNo()) {
		if(sc.Close[sc.Index-1]==sc.Open[sc.Index-1]) {
			return false;
		}

		// find first non-doji candle index
		int nonDojiCandleIndex=0;
		for(int i=0; i<3; i++) {
			if(sc.Close[sc.Index-2-i]-sc.Open[sc.Index-2-i]!=0) {
				nonDojiCandleIndex = sc.Index-2-i;
				break;
			}
		}

		if (nonDojiCandleIndex==0) {
			// too many overlapping dojis, start new reclaim
			return true;
		}

		if(Sign(sc.Close[sc.Index-1]-sc.Open[sc.Index-1]) == Sign(sc.Close[nonDojiCandleIndex]-sc.Open[nonDojiCandleIndex])){
			return false;
		}
	}

	return true;
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
 * @param reclaimIndex The index of the reclaim in the reclaims array. The reclaims with reclaimIndex==0 are drawn differently
 * @return The line number of the newly created rectangle, or `-1` if an existing rectangle was updated.
 */
int DrawReclaim(SCStudyInterfaceRef sc, const Reclaim &reclaim, bool createNew = false, int reclaimIndex=0)
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
		if(reclaimIndex==0) {
			// current reclaim
			//RectangleTool.Color = sc.Input[6].GetColor();
			RectangleTool.Color = RGB(255,255,255);
			RectangleTool.SecondaryColor = sc.Input[6].GetColor();
			RectangleTool.TransparencyLevel = sc.Input[9].GetInt();
		} else {
			// old reclaim
			RectangleTool.Color = sc.Input[3].GetColor();
			RectangleTool.SecondaryColor = sc.Input[3].GetColor();
			
			//RectangleTool.TransparencyLevel = sc.Input[8].GetInt();
			RectangleTool.TransparencyLevel = max(0, sc.Input[8].GetInt()-sc.Input[8].GetInt() * reclaim.EV/10);
		}
	}
	else
	{
		if(reclaimIndex==0) {
			// current reclaim
			//RectangleTool.Color = sc.Input[7].GetColor();
			RectangleTool.Color = RGB(255,255,255);
			RectangleTool.SecondaryColor = sc.Input[7].GetColor();
			RectangleTool.TransparencyLevel = sc.Input[9].GetInt();
		} else {
			// old reclaim
			RectangleTool.Color = sc.Input[4].GetColor();
			RectangleTool.SecondaryColor = sc.Input[4].GetColor();
			//RectangleTool.TransparencyLevel = sc.Input[8].GetInt();
			RectangleTool.TransparencyLevel = max(0, sc.Input[8].GetInt()-sc.Input[8].GetInt() * reclaim.EV/10);
		}
	}

	if(reclaimIndex!=0 && reclaim.CurrentHeight<=sc.Input[10].GetInt() && reclaim.EV<sc.Input[14].GetInt()) {
		SCDateTime timeDelta = sc.CurrentSystemDateTime-reclaim.DecayStartTime;
		RectangleTool.Color = sc.Input[11].GetColor();
		double seconds = timeDelta.GetTimeInSeconds();
		if(seconds<5) {
			RectangleTool.TransparencyLevel = RectangleTool.TransparencyLevel+(100-RectangleTool.TransparencyLevel)*seconds/5;
		} else {
			RectangleTool.SecondaryColor = sc.Input[11].GetColor();
			RectangleTool.TransparencyLevel = 100;
		}
	}


	if (!createNew)
	{
		RectangleTool.LineNumber = reclaim.RectLineNumber;
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
 * @brief Draws or updates the EV texto of a reclaim.
 *
 * This function either draws a new text string or updates an existing one on the chart
 * based on the provided reclaim data.
 *
 * @param sc A reference to the study interface, providing access to chart data and tools.
 * @param reclaim A reference to the `Reclaim` structure containing the data needed to draw the EV text.
 * @param createNew A boolean flag indicating whether to create a new text (`true`) or update an existing one (`false`).
 *                  - `true`: A new text is drawn, and a new line number is assigned.
 *                  - `false`: The existing text with the specified line number is updated.
 * @param reclaimIndex The index of the reclaim in the reclaims array. The reclaims with reclaimIndex==0 are drawn differently
 * @return The line number of the newly created text, or `-1` if an existing text was updated.
 */
int DrawReclaimEVText(SCStudyInterfaceRef sc, const Reclaim &reclaim, bool createNew = false, int reclaimIndex=0){
	if(reclaimIndex==0 || (reclaim.EV<sc.Input[18].GetInt() && reclaim.Swing==0)) {
		return -1;
	}

	// Draw the initial rectangle
	s_UseTool TextTool;
	TextTool.Clear(); // Initialize the Tool structure

	TextTool.ChartNumber = sc.ChartNumber;
	TextTool.DrawingType = DRAWING_TEXT;
	TextTool.AddAsUserDrawnDrawing = 0;
	TextTool.Region = 0;

	// Define the rectangle coordinates
	TextTool.BeginIndex = sc.ArraySize+sc.Input[19].GetInt();

	SCString textString;
	//textString.Format("EV: %d, increase flag: %d", reclaim.EV, reclaim.IncreaseEVOnNextTouch);
	if(reclaim.Swing <1) {
		textString.Format("%d", reclaim.EV);
	} else {
		textString.Format("%d-%d", reclaim.EV, reclaim.Swing);
	}
	TextTool.Text = textString;
	TextTool.FontSize = sc.Input[20].GetInt();

	// Set the text color
	if (reclaim.Type == 0)
	{
		TextTool.BeginValue = reclaim.ActiveSidePrice;
		TextTool.Color = sc.Input[15].GetColor();
	}
	else
	{
		TextTool.BeginValue = reclaim.ActiveSidePrice+sc.TickSize;
		TextTool.Color = sc.Input[16].GetColor();
	}

	if(reclaimIndex!=0 && reclaim.CurrentHeight<=sc.Input[10].GetInt() && reclaim.EV<sc.Input[14].GetInt()) {
		TextTool.Color = sc.Input[17].GetColor();
	}

	if (!createNew)
	{
		TextTool.LineNumber = reclaim.EVTextLineNumber;
		sc.UseTool(TextTool);
		// always return -1 when updating existing rectangle
		return -1;
	}
	else
	{
		// Creating new rectangle. Allow sierra to choose a new LineNumber.
		sc.UseTool(TextTool);
		// return the LineNumber of the new rectangle
		return TextTool.LineNumber;
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
	sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, reclaim.RectLineNumber);
	sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, reclaim.EVTextLineNumber);
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
 * @param checkPreviousBar When true, uses the high and low of the previous bar instead of the CurrentPrice to update reclaims
 */
void UpdateReclaims(SCStudyInterfaceRef sc, int size, bool checkPreviousBar=false)
{
	// get sierra chart persistent variables for up and down reclaims
	Reclaim *upReclaims = (Reclaim *)sc.GetPersistentPointer(1);
	Reclaim *downReclaims = (Reclaim *)sc.GetPersistentPointer(2);
	int EVPullbackSize = sc.Input[13].GetInt();
	int SwingPullbackSize = sc.Input[21].GetInt();




	// get current price
	float CurrentPrice = sc.LastTradePrice;
	//float CurrentHigh = max(sc.High[sc.Index], sc.High[sc.Index-1]);
	//float CurrentLow = min(sc.Low[sc.Index], sc.Low[sc.Index-1]);
	float CurrentHigh = CurrentPrice;
	float CurrentLow = CurrentPrice;
	//float CurrentClose = sc.Close[sc.Index-1];
	float CurrentClose = sc.Close[sc.Index];

	if(checkPreviousBar) {
		CurrentHigh = sc.High[sc.Index-1];
		CurrentLow = sc.Low[sc.Index-1];
	}



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
				upReclaims[i].StartDate = sc.BaseDateTimeIn[sc.Index];
				upReclaims[i].CurrentHeight = 0;
				upReclaims[i].MaxHeight = 0;
				upReclaims[i].MaxRetracement = 0;
			}
		}
		else
		{
			// update EV
			if(checkPreviousBar && !upReclaims[i].IncreaseEVOnNextTouch) {
				int pullbackSizeInTicks = int((CurrentHigh-upReclaims[i].ActiveSidePrice)/sc.TickSize);
				if(pullbackSizeInTicks>=EVPullbackSize) {
					upReclaims[i].IncreaseEVOnNextTouch=true;
				}
			}

			if(checkPreviousBar && upReclaims[i].IncreaseEVOnNextTouch && CurrentLow<=upReclaims[i].ActiveSidePrice) {
				upReclaims[i].EV=upReclaims[i].EV+1;
				upReclaims[i].IncreaseEVOnNextTouch = false;
			}

			// update Swing
			if(checkPreviousBar && !upReclaims[i].IncreaseSwingOnNextTouch) {
				int pullbackSizeInTicks = int((CurrentHigh-upReclaims[i].ActiveSidePrice)/sc.TickSize);
				if(pullbackSizeInTicks>=SwingPullbackSize) {
					upReclaims[i].IncreaseSwingOnNextTouch=true;
				}
			}

			if(checkPreviousBar && upReclaims[i].IncreaseSwingOnNextTouch && CurrentLow<=upReclaims[i].ActiveSidePrice) {
				upReclaims[i].Swing=upReclaims[i].Swing+1;
				upReclaims[i].IncreaseSwingOnNextTouch = false;
			}



			// update ActiveSidePrice
			if (CurrentLow < upReclaims[i].ActiveSidePrice)
			{
				upReclaims[i].ActiveSidePrice = max(CurrentLow, upReclaims[i].FixedSidePrice);
				upReclaims[i].CurrentHeight = (int)((upReclaims[i].ActiveSidePrice-upReclaims[i].FixedSidePrice)/sc.TickSize);
				if(upReclaims[i].CurrentHeight<=sc.Input[10].GetInt()) {
					// reclaim must become hollow, set DecayStartDatetime
					upReclaims[i].DecayStartTime=sc.CurrentSystemDateTime;
				}
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


		DrawReclaim(sc, upReclaims[i], false, i);
		if(upReclaims[i].EVTextLineNumber==-1 && (upReclaims[i].EV>=sc.Input[18].GetInt() || upReclaims[i].Swing>0)) {
			upReclaims[i].EVTextLineNumber = DrawReclaimEVText(sc, upReclaims[i], true, i);
		} else {
			DrawReclaimEVText(sc, upReclaims[i], false, i);
		}
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
				downReclaims[i].ActiveSidePrice = CurrentHigh;
				downReclaims[i].StartDate = sc.BaseDateTimeIn[sc.Index];
				downReclaims[i].CurrentHeight = 0;
				downReclaims[i].MaxHeight = 0;
				downReclaims[i].MaxRetracement = 0;
			}
		}
		else
		{
			// update EV
			if(checkPreviousBar && !downReclaims[i].IncreaseEVOnNextTouch) {
				int pullbackSizeInTicks = int((downReclaims[i].ActiveSidePrice-CurrentLow)/sc.TickSize);
				if(pullbackSizeInTicks>=EVPullbackSize) {
					downReclaims[i].IncreaseEVOnNextTouch=true;
				}
			}

			if(checkPreviousBar && downReclaims[i].IncreaseEVOnNextTouch && CurrentHigh>=downReclaims[i].ActiveSidePrice) {
				downReclaims[i].EV=downReclaims[i].EV+1;
				downReclaims[i].IncreaseEVOnNextTouch = false;
			}

			// update Swing counter
			if(checkPreviousBar && !downReclaims[i].IncreaseSwingOnNextTouch) {
				int pullbackSizeInTicks = int((downReclaims[i].ActiveSidePrice-CurrentLow)/sc.TickSize);
				if(pullbackSizeInTicks>=SwingPullbackSize) {
					downReclaims[i].IncreaseSwingOnNextTouch=true;
				}
			}

			if(checkPreviousBar && downReclaims[i].IncreaseSwingOnNextTouch && CurrentHigh>=downReclaims[i].ActiveSidePrice) {
				downReclaims[i].Swing=downReclaims[i].Swing+1;
				downReclaims[i].IncreaseSwingOnNextTouch = false;
			}

			if (CurrentHigh > downReclaims[i].ActiveSidePrice)
			{
				downReclaims[i].ActiveSidePrice = min(CurrentHigh, downReclaims[i].FixedSidePrice);
				downReclaims[i].CurrentHeight= (int)((downReclaims[i].FixedSidePrice-downReclaims[i].ActiveSidePrice)/sc.TickSize);
				if(downReclaims[i].CurrentHeight<=sc.Input[10].GetInt()) {
					// reclaim must become hollow, set DecayStartDatetime
					downReclaims[i].DecayStartTime=sc.CurrentSystemDateTime;
				}
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

		DrawReclaim(sc, downReclaims[i], false, i);
		if(downReclaims[i].EVTextLineNumber==-1 && (downReclaims[i].EV>=sc.Input[18].GetInt() || downReclaims[i].Swing>0)) {
			downReclaims[i].EVTextLineNumber = DrawReclaimEVText(sc, downReclaims[i], true, i);
		} else {
			DrawReclaimEVText(sc, downReclaims[i], false, i);
		}
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
	SCInputRef UpCurrentReclaimColor = sc.Input[6];		// Color of the most recent bullish reclaim
	SCInputRef DownCurrentReclaimColor = sc.Input[7];		// Color of the most recent bearish reclaim
	SCInputRef OldReclaimsTransparency = sc.Input[8];		// Transparency of old reclaims from 0 (opaque) to 100 (transparent)
	SCInputRef CurrentReclaimsTransparency = sc.Input[9];		// Transparency of current reclaims from 0 (opaque) to 100 (transparent)
	SCInputRef MinReclaimSize = sc.Input[10];		// Reclaims smaller than this value will be hollow
	SCInputRef HollowReclaimsColor = sc.Input[11];		// Color of the hollow reclaims
	SCInputRef LookForOppositeBarColor = sc.Input[12];		// When true, only start a new reclaim if the candle that pulled back is the opposite color of the one before
	SCInputRef EVPullbackSize = sc.Input[13];		// Minimum pullback size in tick required to increse EV by 1 the next time active side is touched
	SCInputRef EVThreshold = sc.Input[14];		// If EV of reclaim is larger than this, don't make it hollow
	SCInputRef UpReclaimsTextColor = sc.Input[15];		// Color of the EV text for the bullish reclaims
	SCInputRef DownReclaimsTextColor = sc.Input[16];		// Color of the EV text for the bearish reclaims
	SCInputRef HollowReclaimsTextColor = sc.Input[17];		// Color of the EV text for the hollow 
	SCInputRef EVTextThreshold = sc.Input[18];		// If EV is less than this value, don't display the EV text
	SCInputRef EVTextShift = sc.Input[19];		// If EV is less than this value, don't display the EV text
	SCInputRef EVTextFontSize = sc.Input[20];		// Font size of EV text
	SCInputRef SwingPullbackSize = sc.Input[21];		// Minimum pullback size in tick required to increse the swing counter by 1 the next time active side is touched
	SCInputRef BarLookback = sc.Input[22];		// Number of bars to look back for creating existing reclaim when the study is loaded



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
		MaxNumberOfReclaims.Name = "Max active reclaims (DO NOT CHANGE IF STUDY IS ALREADY ON CHART)";
		MaxNumberOfReclaims.SetInt(500);			  
		MaxNumberOfReclaims.SetIntLimits(1, 1000); 

		// Inputs default values
		NewReclaimThreshold.Name = "Threshold tick size";
		NewReclaimThreshold.SetInt(1); 
		NewReclaimThreshold.SetIntLimits(1,1000);

		// Inputs default values
		RectangleExtendBars.Name = "Extend right amount";
		RectangleExtendBars.SetInt(10000); // Default to 10 bars extension
        RectangleExtendBars.SetIntLimits(0, 10000); // Allow extension to a maximum of 500 bars

		UpReclaimsColor.Name = "Existing bullish reclaims color";
		UpReclaimsColor.SetColor(RGB(0, 100, 255)); 

		DownReclaimsColor.Name = "Existing bearish reclaims color";
		DownReclaimsColor.SetColor(RGB(255, 0, 100)); 

		UpdateOnBarClose.Name = "Only update on bar close";
		UpdateOnBarClose.SetYesNo(0); 

		UpCurrentReclaimColor.Name="Current bullish reclaim color";		
		UpCurrentReclaimColor.SetColor(RGB(0, 100, 255)); 

		DownCurrentReclaimColor.Name="Current bearish reclaim color";		
		DownCurrentReclaimColor.SetColor(RGB(255, 0, 100)); 

		OldReclaimsTransparency.Name="Transparency of existing reclaims"; 
		OldReclaimsTransparency.SetInt(90); 
        OldReclaimsTransparency.SetIntLimits(0, 100); 

		CurrentReclaimsTransparency.Name="Transparency of current reclaims"; 
		CurrentReclaimsTransparency.SetInt(50); 
        CurrentReclaimsTransparency.SetIntLimits(0, 100); 

		MinReclaimSize.Name = "Reclaims smaller than this are hollow";
		MinReclaimSize.SetInt(2); 
        MinReclaimSize.SetIntLimits(0, 10000); 

		HollowReclaimsColor.Name = "Hollow reclaims color";
		HollowReclaimsColor.SetColor(RGB(50, 50, 50)); 

		LookForOppositeBarColor.Name = "Look for opposite bar color when starting a new pullback";
		LookForOppositeBarColor.SetYesNo(1); 

		EVPullbackSize.Name = "Minimum pullback required in ticks to add 1 EV to reclaim";
		EVPullbackSize.SetInt(3); 
        EVPullbackSize.SetIntLimits(0, 10000); 

		EVThreshold.Name = "Don't make reclaim hollow if EV is bigger than this";
		EVThreshold.SetInt(4); 
        EVThreshold.SetIntLimits(0, 10000); 

		UpReclaimsTextColor.Name = "Text color of bullish reclaims";
		UpReclaimsTextColor.SetColor(RGB(255, 255, 255)); 
		
		DownReclaimsTextColor.Name = "Text color of bearish reclaims";
		DownReclaimsTextColor.SetColor(RGB(255, 255, 255)); 

		HollowReclaimsTextColor.Name = "Text color of hollow reclaims";
		HollowReclaimsTextColor.SetColor(RGB(255, 255, 255)); 

		EVTextThreshold.Name = "Hide text if EV is smaller than (RELOAD REQUIRED)";
		EVTextThreshold.SetInt(4); 

		EVTextShift.Name = "EV text shift";
		EVTextShift.SetInt(3); 

		EVTextFontSize.Name = "EV text font size";
		EVTextFontSize.SetInt(10); 

		SwingPullbackSize.Name = "Minimum pullback required in ticks to add 1 to the swing counter of the reclaim";
		SwingPullbackSize.SetInt(12); 
        SwingPullbackSize.SetIntLimits(0, 10000); 

		BarLookback.Name = "Number of bars to look back when loading study";
		BarLookback.SetInt(1000); 
        BarLookback.SetIntLimits(0, 100000); 


		sc.AutoLoop = 1;
		return;
	}


	int startBarIndex = sc.ArraySize - sc.Input[22].GetInt();
	 if (startBarIndex < 0){
        startBarIndex = 0;
	 }

	 if(sc.Index<startBarIndex) {
		return;
	 }

	// Initialize stuff on the first run
	if (sc.Index == startBarIndex)
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
				p_UpReclaims[i].EV = 0;
				p_UpReclaims[i].IncreaseEVOnNextTouch = false;
				p_UpReclaims[i].Swing = 0;
				p_UpReclaims[i].IncreaseSwingOnNextTouch = false;
				p_UpReclaims[i].EVTextLineNumber = -1;
			}

			// initialize values for first reclaim
			p_UpReclaims[0].FixedSidePrice = sc.LastTradePrice;
			p_UpReclaims[0].ActiveSidePrice = sc.LastTradePrice;
			p_UpReclaims[0].StartDate = sc.BaseDateTimeIn[sc.Index];
			p_UpReclaims[0].Deleted = false;

			// store array in the persistent variable
			if (p_UpReclaims != NULL)
			{
				sc.SetPersistentPointer(1, p_UpReclaims);
			}

			// draw first reclaim and store the sierra chart linenumber
			p_UpReclaims[0].RectLineNumber = DrawReclaim(sc, p_UpReclaims[0], true, 0);
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
				p_DownReclaims[i].EV = 0;
				p_DownReclaims[i].IncreaseEVOnNextTouch = false;
				p_DownReclaims[i].Swing = 0;
				p_DownReclaims[i].IncreaseSwingOnNextTouch = false;
				p_DownReclaims[i].EVTextLineNumber = -1;
			}

			// initialize values for first reclaim
			p_DownReclaims[0].FixedSidePrice = sc.LastTradePrice;
			p_DownReclaims[0].ActiveSidePrice = sc.LastTradePrice;
			p_DownReclaims[0].StartDate = sc.BaseDateTimeIn[sc.Index];
			p_DownReclaims[0].Deleted = false;

			// store array in the persistent variable
			if (p_DownReclaims != NULL)
			{
				sc.SetPersistentPointer(2, p_DownReclaims);
			}

			// draw first reclaim and store the sierra chart linenumber
			p_DownReclaims[0].RectLineNumber = DrawReclaim(sc, p_DownReclaims[0], true, 0);
		}

		return;
	}

	if(!UpdateOnBarClose.GetYesNo()) {
		// update existing reclaims using currentPrice
		UpdateReclaims(sc, MaxNumberOfReclaims.GetInt(), false);
	}

	// return if no new bar has formed 
	if (lastIndex == sc.Index) { 
        return; 
    } 

	// from this point on code is only executed once per bar

    lastIndex = sc.Index; 

	// If the price has changed, update stuff
	// store new value for PreviousPrice
	PreviousPrice = sc.LastTradePrice;

	// Check if we need to create a new bullish reclaim
	if(StartNewReclaimCheck(sc, p_UpReclaims[0]))
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
		p_UpReclaims[0].StartDate = sc.BaseDateTimeIn[sc.Index];
		p_UpReclaims[0].MaxHeight = 0;
		p_UpReclaims[0].CurrentHeight = 0;
		p_UpReclaims[0].MaxRetracement = 0;
		p_UpReclaims[0].Deleted = false;
		p_UpReclaims[0].EV = 0;
		p_UpReclaims[0].IncreaseEVOnNextTouch = false;
		p_UpReclaims[0].Swing = 0;
		p_UpReclaims[0].IncreaseSwingOnNextTouch = false;
		p_UpReclaims[0].EVTextLineNumber = -1;


		// draw the new rectangle and store the sierra LineNumber
		p_UpReclaims[0].RectLineNumber = DrawReclaim(sc, p_UpReclaims[0], true, 0);

		// draw EV text for reclaim that was just shifted right
		//p_UpReclaims[1].EVTextLineNumber = DrawReclaimEVText(sc, p_UpReclaims[1], true, 1);
	}

	// Check if we need to create a new bearish reclaim
	if(StartNewReclaimCheck(sc, p_DownReclaims[0]))
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
		p_DownReclaims[0].StartDate = sc.BaseDateTimeIn[sc.Index];
		p_DownReclaims[0].MaxHeight = 0;
		p_DownReclaims[0].CurrentHeight = 0;
		p_DownReclaims[0].MaxRetracement = 0;
		p_DownReclaims[0].Deleted = false;
		p_DownReclaims[0].EV = 0;
		p_DownReclaims[0].IncreaseEVOnNextTouch = false;
		p_DownReclaims[0].Swing = 0;
		p_DownReclaims[0].IncreaseSwingOnNextTouch = false;
		p_DownReclaims[0].EVTextLineNumber = -1;

		// draw the new rectangle and store the sierra LineNumber
		p_DownReclaims[0].RectLineNumber = DrawReclaim(sc, p_DownReclaims[0], true, 0);

		// draw EV text for recaim that was just shifted right
		//p_DownReclaims[1].EVTextLineNumber = DrawReclaimEVText(sc, p_DownReclaims[1], true, 1);
	}

	// update existing reclaims
	UpdateReclaims(sc, MaxNumberOfReclaims.GetInt(), true);

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
