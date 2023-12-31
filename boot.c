/*
 * uefi-md5sum: UEFI MD5Sum validator
 * Copyright © 2023 Pete Batard <pete@akeo.ie>
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

/* The following header is generated by the build process on prod */
#include "version.h"

/*
 * When performing tests with GitHub Actions, we want to remove all
 * colour formatting as well force shutdown on exit (to exit qemu)
 * so we need a variable to tell us if we are running in test mode.
 */
BOOLEAN IsTestMode = FALSE;

/* We'll use this string to erase a line on the console */
STATIC CHAR16 EmptyLine[STRING_MAX];

/* Keep a copy of the main Image Handle */
STATIC EFI_HANDLE MainImageHandle = NULL;

/* Strings used to identify the plaform */
#if defined(_M_X64) || defined(__x86_64__)
  STATIC CHAR16* Arch = L"x64";
#elif defined(_M_IX86) || defined(__i386__)
  STATIC CHAR16* Arch = L"ia32";
#elif defined (_M_ARM64) || defined(__aarch64__)
  STATIC CHAR16* Arch = L"aa64";
#elif defined (_M_ARM) || defined(__arm__)
  STATIC CHAR16* Arch = L"arm";
#elif defined(_M_RISCV64) || (defined (__riscv) && (__riscv_xlen == 64))
  STATIC CHAR16* Arch = L"riscv64";
#else
#  error Unsupported architecture
#endif

/**
  Display a centered application banner
 **/
STATIC VOID DisplayBanner(
	IN UINTN Cols
)
{
	UINTN i, Len;
	CHAR16* Line;

	// The platform logo may still be displayed → remove it
	gST->ConOut->ClearScreen(gST->ConOut);

	// Make sure the console is large enough to accomodate the banner
	// but not too insanely large...
	if (Cols < 50 || Cols >= STRING_MAX)
		return;

	Line = AllocatePool((Cols + 1) * sizeof(CHAR16));
	if (Line == NULL)
		return;

	Cols -= 1;
	SetTextPosition(0, 0);
	SetText(TEXT_REVERSED);
	Print(L"%c", BOXDRAW_DOWN_RIGHT);
	for (i = 0; i < Cols - 2; i++)
		Print(L"%c", BOXDRAW_HORIZONTAL);
	Print(L"%c", BOXDRAW_DOWN_LEFT);

	SetTextPosition(0, 1);
	UnicodeSPrint(Line, Cols, L"UEFI md5sum %s (%s)", VERSION_STRING, Arch);
	Len = SafeStrLen(Line);
	V_ASSERT(Len < Cols);
	Print(L"%c", BOXDRAW_VERTICAL);
	for (i = 1; i < (Cols - Len) / 2; i++)
		Print(L" ");
	Print(Line);
	for (i += Len; i < Cols - 1; i++)
		Print(L" ");
	Print(L"%c", BOXDRAW_VERTICAL);

	SetTextPosition(0, 2);
	UnicodeSPrint(Line, Cols, L"<https://md5.akeo.ie>");
	Len = SafeStrLen(Line);
	V_ASSERT(Len < Cols);
	Print(L"%c", BOXDRAW_VERTICAL);
	for (i = 1; i < (Cols - Len) / 2; i++)
		Print(L" ");
	Print(Line);
	for (i += Len; i < Cols - 1; i++)
		Print(L" ");
	Print(L"%c", BOXDRAW_VERTICAL);

	SetTextPosition(0, 3);
	Print(L"%c", BOXDRAW_UP_RIGHT);
	for (i = 0; i < Cols - 2; i++)
		Print(L"%c", BOXDRAW_HORIZONTAL);
	Print(L"%c\n", BOXDRAW_UP_LEFT);
	DefText();

	FreePool(Line);
}

/**
  Print a hash entry that has failed processing.
  Do this over a specific section of the console we cycle over.

  @param[in]  Status     The Status code from the failed operation on the entry.
  @param[in]  Path       A pointer to the CHAR16 string with the Path of the entry.
  @param[in]  NumFailed  The current number of failed entries.
**/
STATIC VOID PrintFailedEntry(
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
	SetTextPosition(TEXT_POSITION_X, TEXT_POSITION_Y + 2 +
		(NumFailed % FAILED_ENTRIES_MAX));
	if (!IsTestMode) {
		gST->ConOut->OutputString(gST->ConOut, EmptyLine);
		SetTextPosition(TEXT_POSITION_X, TEXT_POSITION_Y + 2 +
			(NumFailed % FAILED_ENTRIES_MAX));
	}
	PrintError(L"File '%s'", Path);
}

/**
  Exit-specific processing for test/debug
**/
STATIC VOID ExitCheck(VOID)
{
#if defined(EFI_DEBUG)
	UINTN Index;
#endif

	// If running in test mode, shut down QEMU
	if (IsTestMode)
		ShutDown();

	// If running debug, wait for a user keystroke and shut down
#if defined(EFI_DEBUG)
	SetText(TEXT_YELLOW);
	Print(L"Press any key to exit.\n");
	DefText();
	gST->ConIn->Reset(gST->ConIn, FALSE);
	gST->BootServices->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
	ShutDown();
#endif
}

/**
  Obtain the root handle of the current volume.

  @param[out] Root              A pointer to the root file handle.

  @retval EFI_SUCCESS           The root file handle was successfully populated.
  @retval EFI_INVALID_PARAMETER The pointer to the root file handle is invalid.
**/
STATIC EFI_STATUS GetRootHandle(
	OUT EFI_FILE_HANDLE* Root
)
{
	EFI_STATUS Status;
	EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* Volume;

	if (Root == NULL)
		return EFI_INVALID_PARAMETER;

	// Access the loaded image so we can open the current volume
	Status = gBS->OpenProtocol(MainImageHandle, &gEfiLoadedImageProtocolGuid,
		(VOID**)&LoadedImage, MainImageHandle,
		NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status))
		return Status;

	// Open the the root directory on the boot volume
	Status = gBS->OpenProtocol(LoadedImage->DeviceHandle,
		&gEfiSimpleFileSystemProtocolGuid, (VOID**)&Volume,
		MainImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(Status))
		return Status;

	return Volume->OpenVolume(Volume, Root);
}

/*
 * Application entry-point
 * NB: This must be set to 'efi_main' for gnu-efi crt0 compatibility
 */
EFI_STATUS EFIAPI efi_main(
	IN EFI_HANDLE BaseImageHandle,
	IN EFI_SYSTEM_TABLE *SystemTable
)
{
	EFI_STATUS Status;
	EFI_FILE_HANDLE Root;
	EFI_INPUT_KEY Key;
	HASH_LIST HashList = { 0 };
	CHAR8 c;
	CHAR16 Path[PATH_MAX + 1], NumFailedString[32] = { 0 }, *PluralFiles;
	UINT8 ComputedHash[MD5_HASHSIZE], ExpectedHash[MD5_HASHSIZE];
	UINTN i, Index, Cols, Rows, NumFailed = 0;

	// Keep a global copy of the bootloader's image handle
	MainImageHandle = BaseImageHandle;

#if defined(_GNU_EFI)
	InitializeLib(BaseImageHandle, SystemTable);
#endif

	// Determine if we are running in test mode.
	// Note that test mode is no less secure than regular mode.
	// It only produces or removes extra onscreen output.
	IsTestMode = IsTestSystem();

	// Find the amount of console real-estate we have at out disposal
	Status = gST->ConOut->QueryMode(gST->ConOut, gST->ConOut->Mode->Mode,
		&Cols, &Rows);
	if (EFI_ERROR(Status)) {
		// Couldn't get the console dimensions
		Cols = 60;
		Rows = 20;
	}
	if (Cols >= STRING_MAX)
		Cols = STRING_MAX - 1;

	// Populate a blank line we can use to erase a line
	for (i = 0; i < Cols; i++)
		EmptyLine[i] = L' ';
	EmptyLine[i] = L'\0';

	// Display the static user-facing text if not in test mode
	if (!IsTestMode) {
		// Clear the input buffer
		gST->ConIn->Reset(gST->ConIn, FALSE);
		// Display the top banner
		DisplayBanner(Cols);
		SetTextPosition(TEXT_POSITION_X, TEXT_POSITION_Y);
		Print(L"Media verification:\n");
		SetTextPosition(TEXT_POSITION_X, TEXT_POSITION_Y + 1);
	}

	Status = GetRootHandle(&Root);
	if (EFI_ERROR(Status)) {
		PrintError(L"Could not open root directory\n");
		goto out;
	}

	// Parse md5sum.txt to construct a hash list
	Status = Parse(Root, HASH_FILE, &HashList);
	if (EFI_ERROR(Status))
		goto out;
	PluralFiles = (HashList.NumEntries == 1) ? L"" : L"s";

	if (IsTestMode) {
		// Print any extra data we want to validate
		Print(L"[TEST] TotalBytes = 0x%lX\n", HashList.TotalBytes);
	}

	// Now go through each entry we parsed
	for (Index = 0; Index < HashList.NumEntries; Index++) {
		// Check for user cancellation
		if (gST->ConIn->ReadKeyStroke(gST->ConIn, &Key) != EFI_NOT_READY)
			break;

		// Report progress
		if (!IsTestMode) {
			SetTextPosition(TEXT_POSITION_X, TEXT_POSITION_Y + 1);
			Print(L"%d/%d file%s processed%s\n", Index,
				HashList.NumEntries, PluralFiles, NumFailedString);
		}

		// Convert the expected hexascii hash to a binary value we can use
		ZeroMem(ExpectedHash, sizeof(ExpectedHash));
		for (i = 0; i < MD5_HASHSIZE * 2; i++) {
			c = HashList.Entry[Index].Hash[i];
			// The Parse() call should have filtered any invalid string
			V_ASSERT(IsValidHexAscii(c));
			ExpectedHash[i / 2] <<= 4;
			ExpectedHash[i / 2] |= c >= 'a' ? (c - 'a' + 0x0A) : c - '0';
		}

		// Convert the UTF-8 path to UCS-2
		Status = Utf8ToUcs2(HashList.Entry[Index].Path, Path, ARRAY_SIZE(Path));
		if (EFI_ERROR(Status)) {
			// Conversion failed but we want a UCS-2 Path for the failure
			// report so just filter out anything that is non lower ASCII.
			V_ASSERT(AsciiStrLen(HashList.Entry[Index].Path) < ARRAY_SIZE(Path));
			for (i = 0; i < AsciiStrLen(HashList.Entry[Index].Path); i++) {
				c = HashList.Entry[Index].Path[i];
				if (c < ' ' || c > 0x80)
					c = '?';
				Path[i] = (CHAR16)c;
			}
			Path[i] = L'\0';
		} else {
			// Hash the file and compare the result to the expected value
			// TODO: We should also handle progress & cancellation in HashFile()
			Status = HashFile(Root, Path, ComputedHash);
			if (Status == EFI_SUCCESS &&
				(CompareMem(ComputedHash, ExpectedHash, MD5_HASHSIZE) != 0))
				Status = EFI_CRC_ERROR;
		}

		// Report failures
		if (EFI_ERROR(Status)) {
			PrintFailedEntry(Status, Path, NumFailed++);
			UnicodeSPrint(NumFailedString, ARRAY_SIZE(NumFailedString),
				L" [%d failed]", NumFailed);
		}
	}

	// Final progress report
	SetTextPosition(TEXT_POSITION_X, TEXT_POSITION_Y + 1);
	Print(L"%d/%d file%s processed%s\n", Index,
		HashList.NumEntries, PluralFiles, NumFailedString);

	// Position subsequent console messages after our report
	SetTextPosition(TEXT_POSITION_X, TEXT_POSITION_Y + 3 +
		MIN(NumFailed, FAILED_ENTRIES_MAX));

out:
	SafeFree(HashList.Buffer);
	ExitCheck();

	return Status;
}
