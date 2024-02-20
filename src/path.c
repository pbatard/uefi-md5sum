/*
 * uefi-md5sum: UEFI MD5Sum validator - Path related functions
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

/**
  Fix the casing of a path to match the actual case of the referenced elements.

  @param[in]     Root            A handle to the root partition.
  @param[in/out] Path            The path to update.

  @retval EFI_SUCCESS            The path was successfully updated.
  @retval EFI_INVALID_PARAMETER  One or more of the input parameters are invalid.
  @retval EFI_OUT_OF_RESOURCES   A memory allocation error occurred.
  @retval EFI_NOT_FOUND          One of the path elements was not found.
**/
EFI_STATUS SetPathCase(CONST EFI_FILE_HANDLE Root, CHAR16* Path)
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
