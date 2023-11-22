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

/*
 * When performing tests with GitHub Actions, we want to remove all
 * colour formatting as well force shutdown on exit (to exit qemu)
 * so we need a variable to tell us if we are running in test mode.
 */
BOOLEAN IsTestMode = FALSE;

/* Copies of the global image handle and system table for the current executable */
EFI_SYSTEM_TABLE*   MainSystemTable = NULL;
EFI_HANDLE          MainImageHandle = NULL;

/*
 * Application entry-point
 * NB: This must be set to 'efi_main' for gnu-efi crt0 compatibility
 */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE BaseImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status = EFI_SUCCESS;
#if defined(EFI_DEBUG)
	UINTN Event;
#endif

	MainSystemTable = SystemTable;
	MainImageHandle = BaseImageHandle;

#if defined(_GNU_EFI)
	InitializeLib(BaseImageHandle, SystemTable);
#endif

	IsTestMode = IsTestSystem();

	PrintInfo(L"UEFI MD5Sum");
	PrintInfo(L"Copyright © 2023 Pete Batard");

	// If running in test mode, close QEMU by invoking shutdown
	if (IsTestMode)
		SHUTDOWN;

#if defined(EFI_DEBUG)
	// If running debug, wait for a user keystroke and shutdown
	SetText(TEXT_YELLOW);
	Print(L"\nPress any key to exit.\n");
	DefText();
	gST->ConIn->Reset(gST->ConIn, FALSE);
	gST->BootServices->WaitForEvent(1, &gST->ConIn->WaitForKey, &Event);
	SHUTDOWN;
#endif

	return Status;
}
