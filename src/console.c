/*
 * uefi-md5sum: UEFI MD5Sum validator - gConsole related functions
 * Copyright © 2024 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "boot.h"

/* Dimensions of the UEFI text console */
CONSOLE_DIMENSIONS gConsole = { COLS_MIN, ROWS_MIN };

/* Incremental vertical position at which we display alert messages */
UINTN gAlertYPos = ROWS_MIN / 2 + 1;

/* String used to erase a single line on the console */
STATIC CHAR16 EmptyLine[STRING_MAX] = { 0 };

/* Structure used for scrolling messages */
STATIC struct {
	CHAR16* Section;
	UINTN Index;
	UINTN Lines;
	UINTN YPos;
	UINTN MaxLines;
} Scroll = { 0 };

/**
  gConsole initialisation.
**/
VOID InitConsole(VOID)
{
	EFI_STATUS Status;
	UINTN i;

	// Clear the console
	if (!gIsTestMode)
		gST->ConOut->ClearScreen(gST->ConOut);

	// Find the amount of console real-estate we have at out disposal
	Status = gST->ConOut->QueryMode(gST->ConOut, gST->ConOut->Mode->Mode,
		&gConsole.Cols, &gConsole.Rows);
	if (EFI_ERROR(Status)) {
		// Couldn't get the console dimensions
		gConsole.Cols = COLS_MIN;
		gConsole.Rows = ROWS_MIN;
	}
	if (gConsole.Cols >= PATH_MAX)
		gConsole.Cols = PATH_MAX - 1;
	gAlertYPos = 2;

	// Populate a blank line we can use to erase a line
	for (i = 0; i < gConsole.Cols; i++)
		EmptyLine[i] = L' ';
	EmptyLine[i] = L'\0';

	// Print the reference URL of this application
	SetText(TEXT_DARKGRAY);
	PrintCentered(L"https://md5.akeo.ie", 0);
	DefText();
}

/**
  Print a centered message on the console.

  @param[in]  Message    The text message to print.
  @param[in]  YPos       The vertical position to print the message to.
**/
VOID PrintCentered(
	IN CONST CHAR16* Message,
	IN CONST UINTN YPos
)
{
	UINTN MessagePos;

	MessagePos = (gConsole.Cols / 2 > SafeStrLen(Message) / 2) ?
		gConsole.Cols / 2 - SafeStrLen(Message) / 2 : 0;
	if (!gIsTestMode) {
		SetTextPosition(0, YPos);
		Print(EmptyLine);
		SetTextPosition(MessagePos, YPos);
	}
	Print(L"%s\n", Message);
}

/**
  Initialize a scrolling section on the console.

  @param[in]   YPos              The vertical start position of the scrolling section.
  @param[in]   NumberOfLines     How many lines should the scrolling section have.

  @retval EFI_SUCCESS            The scroll section was successfully initialized.
  @retval EFI_INVALID_PARAMETER  The provided parameters are invalid.
  @retval EFI_OUT_OF_RESOURCES   A memory allocation error occurred.
**/
EFI_STATUS InitScrollSection(
	CONST UINTN YPos,
	CONST UINTN NumberOfLines
)
{
	EFI_STATUS Status = EFI_SUCCESS;

	V_ASSERT(gConsole.Rows > 8);
	if (NumberOfLines < 2 || NumberOfLines + YPos >= gConsole.Rows)
		return EFI_INVALID_PARAMETER;

	// Set up the console real estate for scrolling messages
	ZeroMem(&Scroll, sizeof(Scroll));
	// Default position where the scroll section starts
	Scroll.YPos = YPos;
	// Maximum number of lines we display/scroll
	Scroll.MaxLines = NumberOfLines;
	Scroll.Section = AllocateZeroPool(Scroll.MaxLines * (gConsole.Cols + 1) * sizeof(CHAR16));
	if (Scroll.Section == NULL)
		Status = EFI_OUT_OF_RESOURCES;
	return Status;
}

/**
  Scroll section teardown.
**/
VOID ExitScrollSection(VOID)
{
	SafeFree(Scroll.Section);
}

/**
  Print a hash entry that has failed processing.
  Do this over a specific section of the console we cycle over.

  @param[in]  Status     The Status code from the failed operation on the entry.
  @param[in]  Path       A pointer to the CHAR16 string with the Path of the entry.
**/
VOID PrintFailedEntry(
	IN CONST EFI_STATUS Status,
	IN CONST CHAR16* Path
)
{
	CHAR16 ErrorMsg[128], *Src, *Line;
	UINTN Index;

	if (!EFI_ERROR(Status) || Path == NULL || Scroll.Section == NULL ||
		(Scroll.YPos + Scroll.MaxLines >= gConsole.Rows))
		return;

	// Display a more explicit message (than "CRC Error") for files that fail MD5
	if (Status == EFI_CRC_ERROR)
		UnicodeSPrint(ErrorMsg, ARRAY_SIZE(ErrorMsg), L": [27] Checksum Error");
	else
		UnicodeSPrint(ErrorMsg, ARRAY_SIZE(ErrorMsg), L": [%d] %r", (Status & 0x7FFFFFFF), Status);
	if (gIsTestMode)
		SafeStrCat(ErrorMsg, ARRAY_SIZE(ErrorMsg), L"\r\n");

	// Fill a new line in our scroll section
	V_ASSERT(Scroll.Index < Scroll.MaxLines);
	Line = &Scroll.Section[Scroll.Index * (gConsole.Cols + 1)];

	// Copy as much of the Path as we have space available before the error message
	V_ASSERT(StrLen(ErrorMsg) < gConsole.Cols);
	for (Index = 0; (Path[Index] != 0) && (Index < gConsole.Cols - StrLen(ErrorMsg)); Index++)
		Line[Index] = Path[Index];

	// Add a truncation mark to the path if it was too long (but longer than 16 characters)
	if (Path[Index] != 0 && Index > 16) {
		Line[Index - 3] = L'.';
		Line[Index - 2] = L'.';
		Line[Index - 1] = L'.';
	}

	// Append the error message (since we made sure that we had enough space)
	Src = ErrorMsg;
	while (*Src != 0)
		Line[Index++] = *Src++;

	// Fill the remainder of the line with spaces and terminate it
	V_ASSERT(Index <= gConsole.Cols);
	if (!gIsTestMode) {
		while (Index < gConsole.Cols)
			Line[Index++] = L' ';
	}
	Line[Index] = 0;
	// Be paranoid about string overflow
	V_ASSERT(Line[gConsole.Cols] == 0);

	if (Scroll.Lines < Scroll.MaxLines) {
		// We haven't reached scroll capacity yet, so just output the new
		// line after the last.
		Scroll.Lines++;
		SetTextPosition(0, Scroll.YPos + Scroll.Index);
		gST->ConOut->OutputString(gST->ConOut, Line);
	} else {
		// We have reached scroll capacity, so we reprint all the lines at
		// their new position.
		SetTextPosition(0, Scroll.YPos);
		V_ASSERT(Scroll.Index < Scroll.MaxLines);
		// Start reprinting after the the line we just updated (i.e. from the
		// line at Scroll.Index + 1) and make sure to apply Scroll.MaxLines
		// as the modulo.
		for (Index = Scroll.Index + 1; Index <= Scroll.Index + Scroll.MaxLines; Index++) {
			Line = &Scroll.Section[(Index % Scroll.MaxLines) * (gConsole.Cols + 1)];
			// Be paranoid about array overflow
			V_ASSERT((UINTN)Line + (gConsole.Cols + 1) * sizeof(CHAR16) <=
				(UINTN)Scroll.Section + Scroll.MaxLines * (gConsole.Cols + 1) * sizeof(CHAR16));
			gST->ConOut->OutputString(gST->ConOut, Line);
		}
	}
	Scroll.Index = (Scroll.Index + 1) % Scroll.MaxLines;
}

/**
  Initialize a progress bar.

  @param[in]  Progress   A pointer to a PROGRESS_DATA structure.
**/
VOID InitProgress(
	IN PROGRESS_DATA* Progress
)
{
	UINTN i, MessagePos;

	Progress->Active = FALSE;

	if (gConsole.Cols < COLS_MIN || gConsole.Rows < ROWS_MIN ||
		gConsole.Cols >= STRING_MAX || Progress->Message == NULL)
		return;

	if (Progress->YPos > gConsole.Rows - 3)
		Progress->YPos = gConsole.Rows - 3;

	if ((SafeStrLen(Progress->Message) + 6) / 2 > gConsole.Cols / 2)
		return;
	MessagePos = gConsole.Cols / 2 - (SafeStrLen(Progress->Message) + 6) / 2;

	Progress->Current = 0;
	Progress->LastCol = 0;
	Progress->PPos = MessagePos + SafeStrLen(Progress->Message) + 2;

	if (!gIsTestMode) {
		SetTextPosition(MessagePos, Progress->YPos);
		Print(L"%s: 0.0%%", Progress->Message);

		SetTextPosition(0, Progress->YPos + 1);
		for (i = 0; i < gConsole.Cols; i++)
			Print(L"░");
	}

	Progress->Active = TRUE;
}

/**
  Update a progress bar.

  @param[in]  Progress   A pointer to a PROGRESS_DATA structure.
**/
VOID UpdateProgress(
	IN PROGRESS_DATA* Progress
)
{
	UINTN CurCol, PerMille;

	if (Progress == NULL || !Progress->Active || Progress->Maximum == 0 ||
		gConsole.Cols < COLS_MIN || gConsole.Cols >= STRING_MAX)
		return;

	if (Progress->Current > Progress->Maximum)
		Progress->Current = Progress->Maximum;

	// Update the percentage figure
	PerMille = (UINTN)((Progress->Current * 1000) / Progress->Maximum);
	if (!gIsTestMode) {
		SetTextPosition(Progress->PPos, Progress->YPos);
		Print(L"%d.%d%%", PerMille / 10, PerMille % 10);
	}

	// Update the progress bar
	CurCol = (UINTN)((Progress->Current * gConsole.Cols) / Progress->Maximum);
	if (!gIsTestMode) {
		for (; CurCol > Progress->LastCol && Progress->LastCol < gConsole.Cols; Progress->LastCol++) {
			SetTextPosition(Progress->LastCol, Progress->YPos + 1);
			Print(L"%c", BLOCKELEMENT_FULL_BLOCK);
		}
	}

	if (Progress->Current == Progress->Maximum)
		Progress->Active = FALSE;
}

/**
  Display a countdown on screen.

  @param[in]  Message   A message to display with the countdown.
  @param[in]  Duration  The duration of the countdown (in ms).
**/
VOID CountDown(
	IN CONST CHAR16* Message,
	IN CONST UINTN Duration
)
{
	UINTN MessagePos, CounterPos;
	INTN i;

	V_ASSERT(gConsole.Cols / 2 > SafeStrLen(Message) / 2 - 1);
	MessagePos = gConsole.Cols / 2 - SafeStrLen(Message) / 2 - 1;
	CounterPos = MessagePos + SafeStrLen(Message) + 2;

	if (gIsTestMode)
		return;

	SetTextPosition(0, gConsole.Rows - 2);
	Print(EmptyLine);
	SetTextPosition(MessagePos, gConsole.Rows - 2);
	SetText(TEXT_YELLOW);
	Print(L"[%s ", Message);

	gST->ConIn->Reset(gST->ConIn, FALSE);
	for (i = (INTN)Duration; i >= 0; i -= 200) {
		// Allow the user to press a key to interrupt the countdown
		if (gST->BootServices->CheckEvent(gST->ConIn->WaitForKey) != EFI_NOT_READY)
			break;
		if (i % 1000 == 0) {
			SetTextPosition(CounterPos, gConsole.Rows - 2);
			Print(L"%d]   ", i / 1000);
		}
		Sleep(200000);
	}
}
