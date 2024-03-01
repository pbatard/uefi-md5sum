/*
 * uefi-md5sum: UEFI MD5Sum validator
 * Copyright © 2023-2024 Pete Batard <pete@akeo.ie>
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

/*
 * When performing tests with GitHub Actions, we want to remove all
 * colour formatting as well force shutdown on exit (to exit qemu)
 * so we need a variable to tell us if we are running in test mode.
 */
BOOLEAN IsTestMode = FALSE;

/* Copy of the main Image Handle */
STATIC EFI_HANDLE MainImageHandle = NULL;

/* Strings used for platform identification */
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
#error Unsupported architecture
#endif

/**
  Obtain the device and root handle of the current volume.

  @param[out] DeviceHandle      A pointer to the device handle.
  @param[out] Root              A pointer to the root file handle.

  @retval EFI_SUCCESS           The root file handle was successfully populated.
  @retval EFI_INVALID_PARAMETER The pointer to the root file handle is invalid.
**/
STATIC EFI_STATUS GetRootHandle(
	OUT EFI_HANDLE* DeviceHandle,
	OUT EFI_FILE_HANDLE* RootHandle
)
{
	EFI_STATUS Status;
	EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* Volume;

	if (DeviceHandle == NULL || RootHandle == NULL)
		return EFI_INVALID_PARAMETER;

	// Access the loaded image so we can open the current volume
	Status = gBS->OpenProtocol(MainImageHandle, &gEfiLoadedImageProtocolGuid,
		(VOID**)&LoadedImage, MainImageHandle,
		NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status))
		return Status;
	*DeviceHandle = LoadedImage->DeviceHandle;

	// Open the the root directory on the boot volume
	Status = gBS->OpenProtocol(LoadedImage->DeviceHandle,
		&gEfiSimpleFileSystemProtocolGuid, (VOID**)&Volume,
		MainImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(Status))
		return Status;

	return Volume->OpenVolume(Volume, RootHandle);
}

/**
  Fix the casing of a path to match the actual case of the referenced elements.

  @param[in]     Root            A handle to the root partition.
  @param[in/out] Path            The path to update.

  @retval EFI_SUCCESS            The path was successfully updated.
  @retval EFI_INVALID_PARAMETER  One or more of the input parameters are invalid.
  @retval EFI_OUT_OF_RESOURCES   A memory allocation error occurred.
  @retval EFI_NOT_FOUND          One of the path elements was not found.
**/
STATIC EFI_STATUS SetPathCase(
	CONST IN EFI_FILE_HANDLE Root,
	IN CHAR16* Path
)
{
	CONST UINTN FileInfoSize = sizeof(EFI_FILE_INFO) + PATH_MAX * sizeof(CHAR16);
	EFI_FILE_HANDLE FileHandle = NULL;
	EFI_FILE_INFO* FileInfo;
	UINTN i, Len;
	UINTN Size;
	EFI_STATUS Status;

	if ((Root == NULL) || (Path == NULL) || (Path[0] != L'\\'))
		return EFI_INVALID_PARAMETER;

	FileInfo = (EFI_FILE_INFO*)AllocatePool(FileInfoSize);
	if (FileInfo == NULL)
		return EFI_OUT_OF_RESOURCES;

	Len = SafeStrLen(Path);
	/* The checks above ensure that Len is always >= 1, but just in case... */
	V_ASSERT(Len >= 1);

	// Find the last backslash in the path
	for (i = Len - 1; (i != 0) && (Path[i] != L'\\'); i--);

	if (i != 0) {
		Path[i] = 0;
		// Recursively fix the case
		Status = SetPathCase(Root, Path);
		if (EFI_ERROR(Status))
			goto out;
	}

	Status = Root->Open(Root, &FileHandle, (i == 0) ? L"\\" : Path, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(Status))
		goto out;

	do {
		Size = FileInfoSize;
		ZeroMem(FileInfo, Size);
		Status = FileHandle->Read(FileHandle, &Size, (VOID*)FileInfo);
		if (EFI_ERROR(Status))
			goto out;
		if (_StriCmp(&Path[i + 1], FileInfo->FileName) == 0) {
			SafeStrCpy(&Path[i + 1], PATH_MAX, FileInfo->FileName);
			Status = EFI_SUCCESS;
			goto out;
		}
		Status = EFI_NOT_FOUND;
	} while (Size != 0);

out:
	Path[i] = L'\\';
	if (FileHandle != NULL)
		FileHandle->Close(FileHandle);
	FreePool((VOID*)FileInfo);
	return Status;
}

/**
  Create a " (####.# <suffix>)" static string, e.g. " (133.7 MB)", from a 64-bit
  size value, that can be appended to a file path so as to report size to the
  user in an more human readable manner.
  @param[in]  Size            A 64-bit size.

  @retval     A static string with the human readable size or " (too large)" if
              the size is larger than 1 PB.
              The string is static and does not need to be freed. However it is
              expected that only *one* call is made to this function before the
              value is printed out and no longer needed.
**/
CHAR16* SizeToHumanReadable(
	CONST IN UINT64 Size
)
{
	STATIC CONST CHAR16* StrSufix[] = { L"bytes", L"KB", L"MB", L"GB", L"TB" };
	STATIC CHAR16 StrSize[32];
	INTN Suffix;
	UINT64 HrSize;

	// Size must be less than 1024 TB (1 PB)
	if (Size >= 1125899906842624ULL)
		return L" (too large)";

	// NB: (1 PB - 1) * 100 still won't overflow a 64-bit value
	HrSize = Size * 100ULL;

	for (Suffix = 0; Suffix < ARRAY_SIZE(StrSufix) - 1; Suffix++) {
		if (HrSize < 1024ULL * 100ULL)
			break;
		HrSize /= 1024ULL;
	}

	if (Suffix == 0 || (HrSize - (HrSize / 10ULL) * 10ULL) < 5ULL)
		UnicodeSPrint(StrSize, ARRAY_SIZE(StrSize), L" (%lld %s)", HrSize / 100ULL, StrSufix[Suffix]);
	else
		UnicodeSPrint(StrSize, ARRAY_SIZE(StrSize), L" (%lld.%lld %s)", HrSize / 100ULL, HrSize / 10ULL - (HrSize / 100ULL) * 10ULL, StrSufix[Suffix]);
	return StrSize;
}

/**
  Process exit according to the multiple scenarios we want to handle
  (Chain load the next bootloader, shutdown if test mode, etc.).

  @param[in]  Status      The current EFI Status of the application.
  @param[in]  DevicePath  (Optional) Device Path of a bootloader to chain load.
**/
STATIC EFI_STATUS ExitProcess(
	IN EFI_STATUS Status,
	IN OPTIONAL EFI_DEVICE_PATH* DevicePath
)
{
	UINTN Index;
	EFI_HANDLE ImageHandle;
	EFI_INPUT_KEY Key;
	BOOLEAN RunCountDown = TRUE;

	// If we have a bootloader to chain load, try to launch it
	if (DevicePath != NULL) {
		if (EFI_ERROR(Status) && Status != EFI_ABORTED && !IsTestMode) {
			// Ask the user if they want to continue, unless md5sum.txt could
			// not be found, in which case continue boot right away.
			if (Status != EFI_NOT_FOUND) {
				SetText(TEXT_YELLOW);
				// Give the user 1 hour to answer the question
				gBS->SetWatchdogTimer(3600, 0x11D5, 0, NULL);
				PrintCentered(L"Continue with boot? [y/N]", Console.Rows - 2);
				gST->ConIn->Reset(gST->ConIn, FALSE);
				while (gST->ConIn->ReadKeyStroke(gST->ConIn, &Key) == EFI_NOT_READY);
				if (Key.UnicodeChar != L'y' && Key.UnicodeChar != L'Y') {
					SafeFree(DevicePath);
					return Status;
				}
			}
			RunCountDown = FALSE;
		}
		// Reset the watchdog to the default 5 minutes timeout and system code
		gBS->SetWatchdogTimer(300, 0, 0, NULL);
		Status = gBS->LoadImage(FALSE, MainImageHandle, DevicePath, NULL, 0, &ImageHandle);
		SafeFree(DevicePath);
		if (Status == EFI_SUCCESS) {
			if (RunCountDown)
				CountDown(L"Continuing in", 3000);
			if (!IsTestMode)
				gST->ConOut->ClearScreen(gST->ConOut);
			Status = gBS->StartImage(ImageHandle, NULL, NULL);
		}
		if (EFI_ERROR(Status)) {
			SetTextPosition(MARGIN_H, Console.Rows / 2 + 1);
			PrintError(L"Could not launch original bootloader");
		}
	}

	// If running in test mode, shut down QEMU
	if (IsTestMode)
		ShutDown();

	// Wait for a user keystroke as needed
#if !defined(EFI_DEBUG)
	if (EFI_ERROR(Status)) {
#endif
		SetText(TEXT_YELLOW);
		PrintCentered(L"[Press any key to exit]", Console.Rows - 2);
		DefText();
		gST->ConIn->Reset(gST->ConIn, FALSE);
		gST->BootServices->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
#if defined(EFI_DEBUG)
		ShutDown();
# else
	}
#endif
	return Status;
}

/*
 * Application entry-point
 * NB: This must be set to 'efi_main' for gnu-efi crt0 compatibility
 */
EFI_STATUS EFIAPI efi_main(
	IN EFI_HANDLE BaseImageHandle,
	IN EFI_SYSTEM_TABLE* SystemTable
)
{
	EFI_STATUS Status;
	EFI_HANDLE DeviceHandle;
	EFI_FILE_HANDLE Root;
	EFI_DEVICE_PATH* DevicePath = NULL;
	HASH_LIST HashList = { 0 };
	CHAR8 c;
	CHAR16 Path[PATH_MAX + 1], Message[128], LoaderPath[64];
	UINT8 ComputedHash[MD5_HASHSIZE], ExpectedHash[MD5_HASHSIZE];
	UINTN i, Index, NumFailed = 0;
	PROGRESS_DATA Progress = { 0 };

	// Keep a global copy of the bootloader's image handle
	MainImageHandle = BaseImageHandle;

#if defined(_GNU_EFI)
	InitializeLib(BaseImageHandle, SystemTable);
#endif

	// Determine if we are running in test mode.
	// Note that test mode is no less secure than regular mode.
	// It only produces or removes extra onscreen output.
	IsTestMode = IsTestSystem();

	InitConsole();

	Status = GetRootHandle(&DeviceHandle, &Root);
	if (EFI_ERROR(Status)) {
		PrintError(L"Could not open root directory\n");
		goto out;
	}

	// Look up the original boot loader for chain loading
	UnicodeSPrint(LoaderPath, ARRAY_SIZE(LoaderPath),
		L"\\efi\\boot\\boot%s_original.efi", Arch);
	if (SetPathCase(Root, LoaderPath) == EFI_SUCCESS)
		DevicePath = FileDevicePath(DeviceHandle, LoaderPath);

	// Parse md5sum.txt to construct a hash list.
	// We parse the full file, rather than process it line by line so that we
	// can report progress and, unless md5sum_totalbytes is always specified at
	// the beginning, progress requires knowing how many files we have to hash.
	Status = Parse(Root, HASH_FILE, &HashList);
	if (EFI_ERROR(Status))
		goto out;

	// Print any extra data we want to validate
	PrintTest(L"TotalBytes = 0x%lX", HashList.TotalBytes);

	// Set up the progress bar data
	Progress.Type = (HashList.TotalBytes == 0) ? PROGRESS_TYPE_FILE : PROGRESS_TYPE_BYTE;
	Progress.Maximum = (HashList.TotalBytes == 0) ? HashList.NumEntries : HashList.TotalBytes;
	Progress.Message = L"Media verification";
	Progress.YPos = Console.Rows / 2 - 3;
	InitProgress(&Progress);
	SetText(TEXT_YELLOW);
	if (!IsTestMode)
		PrintCentered(L"[Press any key to skip]", Console.Rows - 2);
	DefText();

	// Now go through each entry we parsed
	for (Index = 0; Index < HashList.NumEntries; Index++) {
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
			Status = HashFile(Root, Path, &Progress, ComputedHash);
			if (Status == EFI_SUCCESS &&
				(CompareMem(ComputedHash, ExpectedHash, MD5_HASHSIZE) != 0))
				Status = EFI_CRC_ERROR;
		}

		// Check for user cancellation
		if (Status == EFI_ABORTED)
			break;

		// Report failures
		if (EFI_ERROR(Status))
			PrintFailedEntry(Status, Path, NumFailed++);
	}

	// Final report
	UnicodeSPrint(Message, ARRAY_SIZE(Message), L"%d/%d file%s processed",
		Index, HashList.NumEntries, (HashList.NumEntries == 1) ? L"" : L"s");
	V_ASSERT(SafeStrLen(Message) < ARRAY_SIZE(Message) / 2);
	UnicodeSPrint(&Message[SafeStrLen(Message)], ARRAY_SIZE(Message) - SafeStrLen(Message),
		L" [%d failed]", NumFailed);
	PrintCentered(Message, Progress.YPos + 2);

out:
	SafeFree(HashList.Buffer);
	if (Status == EFI_SUCCESS && NumFailed != 0)
		Status = EFI_CRC_ERROR;
	return ExitProcess(Status, DevicePath);
}
