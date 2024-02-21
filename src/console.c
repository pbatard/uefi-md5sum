/*
 * uefi-md5sum: UEFI MD5Sum validator - Console related functions
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
CONSOLE_DIMENSIONS Console = { COLS_MIN, ROWS_MIN };

/* Incremental vertical position at which we display alert messages */
UINTN AlertYPos = ROWS_MIN / 2 + 1;

/* String used to erase a single line on the console */
STATIC CHAR16 EmptyLine[STRING_MAX] = { 0 };

/**
  Console initialisation.
**/
VOID InitConsole(VOID)
{
	EFI_STATUS Status;
	UINTN i;

	// Clear the console
	if (!IsTestMode)
		gST->ConOut->ClearScreen(gST->ConOut);

	// Find the amount of console real-estate we have at out disposal
	Status = gST->ConOut->QueryMode(gST->ConOut, gST->ConOut->Mode->Mode,
		&Console.Cols, &Console.Rows);
	if (EFI_ERROR(Status)) {
		// Couldn't get the console dimensions
		Console.Cols = COLS_MIN;
		Console.Rows = ROWS_MIN;
	}
	if (Console.Cols >= STRING_MAX)
		Console.Cols = STRING_MAX - 1;
	AlertYPos = Console.Rows / 2 + 1;

	// Populate a blank line we can use to erase a line
	for (i = 0; i < Console.Cols; i++)
		EmptyLine[i] = L' ';
	EmptyLine[i] = L'\0';

	// Print the reference URL of this application
	SetText(EFI_TEXT_ATTR(EFI_DARKGRAY, EFI_BLACK));
	PrintCentered(L"https://md5.akeo.ie", 0);
	DefText();
}

/**
  Print a centered message on the console.

  @param[in]  Message    The text message to print.
  @param[in]  YPos       The vertical position to print the message to.
**/
VOID PrintCentered(
	IN CHAR16* Message,
	IN UINTN YPos
)
{
	UINTN MessagePos;

	if (!IsTestMode) {
		MessagePos = Console.Cols / 2 - SafeStrLen(Message) / 2;
		V_ASSERT(MessagePos > MARGIN_H);
		SetTextPosition(0, YPos);
		Print(EmptyLine);
		SetTextPosition(MessagePos, YPos);
	}
	Print(L"%s\n", Message);
}

/**
  Print a hash entry that has failed processing.
  Do this over a specific section of the console we cycle over.

  @param[in]  Status     The Status code from the failed operation on the entry.
  @param[in]  Path       A pointer to the CHAR16 string with the Path of the entry.
  @param[in]  NumFailed  The current number of failed entries.
**/
VOID PrintFailedEntry(
	IN CONST EFI_STATUS Status,
	IN CHAR16* Path,
	IN CONST UINTN NumFailed
)
{
	if (!EFI_ERROR(Status) || Path == NULL)
		return;

	// Truncate the path in case it's very long.
	// TODO: Ideally we'd want long path reduction similar to what Windows does.
	if (SafeStrLen(Path) > 80)
		Path[80] = L'\0';
	if (!IsTestMode) {
		// Clear any existing text on this line
		SetTextPosition(0, 1 + (NumFailed % (Console.Rows / 2 - 4)));
		gST->ConOut->OutputString(gST->ConOut, EmptyLine);
	}
	SetTextPosition(MARGIN_H, 1 + (NumFailed % (Console.Rows / 2 - 4)));
	SetText(TEXT_RED);
	Print(L"[FAIL]");
	DefText();
	// Display a more explicit message (than "CRC Error") for files that fail MD5
	if ((Status & 0x7FFFFFFF) == 27)
		Print(L" File '%s': [27] MD5 Checksum Error\n", Path);
	else
		Print(L" File '%s': [%d] %r\n", Path, (Status & 0x7FFFFFFF), Status);
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

	if (Console.Cols < COLS_MIN || Console.Rows < ROWS_MIN || Console.Cols >= STRING_MAX ||
		Progress->Message == NULL || IsTestMode)
		return;

	if (SafeStrLen(Progress->Message) > Console.Cols - MARGIN_H * 2 - 8)
		return;

	if (Progress->YPos > Console.Rows - 3)
		Progress->YPos = Console.Rows - 3;

	MessagePos = Console.Cols / 2 - (SafeStrLen(Progress->Message) + 6) / 2;
	V_ASSERT(MessagePos > MARGIN_H);

	Progress->Current = 0;
	Progress->LastCol = 0;
	Progress->PPos = MessagePos + SafeStrLen(Progress->Message) + 2;

	SetTextPosition(MessagePos, Progress->YPos);
	Print(L"%s: 0.0%%", Progress->Message);

	SetTextPosition(MARGIN_H, Progress->YPos + 1);
	for (i = 0; i < Console.Cols - MARGIN_H * 2; i++)
		Print(L"░");

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
		Console.Cols < COLS_MIN || Console.Cols >= STRING_MAX || IsTestMode)
		return;

	if (Progress->Current > Progress->Maximum)
		Progress->Current = Progress->Maximum;

	// Update the percentage figure
	PerMille = (UINTN)((Progress->Current * 1000) / Progress->Maximum);
	SetTextPosition(Progress->PPos, Progress->YPos);
	Print(L"%d.%d%%", PerMille / 10, PerMille % 10);

	// Update the progress bar
	CurCol = (UINTN)((Progress->Current * (Console.Cols - MARGIN_H * 2)) / Progress->Maximum);
	for (; CurCol > Progress->LastCol && Progress->LastCol < Console.Cols; Progress->LastCol++) {
		SetTextPosition(MARGIN_H + Progress->LastCol, Progress->YPos + 1);
		Print(L"%c", BLOCKELEMENT_FULL_BLOCK);
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
	IN CHAR16* Message,
	IN UINTN Duration
)
{
	UINTN MessagePos, CounterPos;
	INTN i;

	if (IsTestMode)
		return;

	MessagePos = Console.Cols / 2 - SafeStrLen(Message) / 2 - 1;
	CounterPos = MessagePos + SafeStrLen(Message) + 2;
	V_ASSERT(MessagePos > MARGIN_H);
	SetTextPosition(0, Console.Rows - 2);
	Print(EmptyLine);
	SetTextPosition(MessagePos, Console.Rows - 2);
	SetText(TEXT_YELLOW);
	Print(L"[%s ", Message);

	gST->ConIn->Reset(gST->ConIn, FALSE);
	for (i = (INTN)Duration; i >= 0; i -= 200) {
		// Allow the user to press a key to interrupt the countdown
		if (gST->BootServices->CheckEvent(gST->ConIn->WaitForKey) != EFI_NOT_READY)
			break;
		if (i % 1000 == 0) {
			SetTextPosition(CounterPos, Console.Rows - 2);
			Print(L"%d]   ", i / 1000);
		}
		Sleep(200000);
	}
}
