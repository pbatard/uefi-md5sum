/*
 * uefi-md5sum: UEFI MD5Sum validator - md5sum.txt parser
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

/**
  Parse a hash sum list file and populate a HASH_LIST structure from it.

  @param[in]  Root   A file handle to the root directory.
  @param[in]  Path   A pointer to the CHAR16 string.
  @param[out] List   A pointer to the HASH_LIST structure to populate.

  @retval EFI_SUCCESS           The file was successfully parsed and the hash list is populated.
  @retval EFI_INVALID_PARAMETER One or more of the input parameters are invalid.
  @retval EFI_OUT_OF_RESOURCES  A memory allocation error occurred.
  @retval EFI_UNSUPPORTED       The hash list file is too small or too large.
  @retval EFI_END_OF_FILE       The hash list file could not be read.
  @retval EFI_ABORTED           The hash list file contains invalid data.
**/
EFI_STATUS Parse(CONST EFI_FILE_HANDLE Root, CONST CHAR16* Path, HASH_LIST* List)
{
	EFI_STATUS Status;
	EFI_FILE_HANDLE File;
	EFI_FILE_INFO* Info = NULL;
	CHAR8* HashFile = NULL;
	HASH_ENTRY* HashList = NULL;
	UINTN i, c, Size, HashFileSize, NumLines, HashListSize;

	if (Root == NULL || Path == NULL || List == NULL)
		return EFI_INVALID_PARAMETER;

	// Look for the hash file on the boot partition
	Status = Root->Open(Root, &File, (CHAR16*)Path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
	if (EFI_ERROR(Status)) {
		PrintError(L"Unable to locate '%s'", Path);
		goto out;
	}

	// Read the whole file into a memory buffer
	Size = FILE_INFO_SIZE;
	Info = AllocateZeroPool(Size);
	if (Info == NULL) {
		Status = EFI_OUT_OF_RESOURCES;
		PrintError(L"Unable to allocate memory");
		goto out;
	}
	Status = File->GetInfo(File, &gEfiFileInfoGuid, &Size, Info);
	if (EFI_ERROR(Status)) {
		PrintError(L"Unable to get '%s' size", HASH_FILE);
		goto out;
	}
	if (Info->FileSize < HASH_HEXASCII_SIZE + 2) {
		Status = EFI_UNSUPPORTED;
		PrintError(L"'%s' is too small", HASH_FILE);
		goto out;
	}
	if (Info->FileSize > HASH_FILE_SIZE_MAX) {
		Status = EFI_UNSUPPORTED;
		PrintError(L"'%s' is too large", HASH_FILE);
		goto out;
	}
	HashFileSize = (UINTN)Info->FileSize;
	// +1 so we can add a newline at the end
	HashFile = AllocatePool(HashFileSize + 1);
	if (HashFile == NULL) {
		Status = EFI_OUT_OF_RESOURCES;
		PrintError(L"Unable to allocate memory");
		goto out;
	}
	Size = HashFileSize;
	Status = File->Read(File, &Size, HashFile);
	if (!EFI_ERROR(Status) && Size != HashFileSize)
		Status = EFI_END_OF_FILE;
	if (EFI_ERROR(Status)) {
		PrintError(L"Unable to read '%s'", HASH_FILE);
		goto out;
	}
	// Correct to the actual size of our buffer and add a newline
	HashFile[HashFileSize++] = 0x0A;

	// Compute the maximum number of lines/entries we need to allocate
	NumLines = 1;	// We added a line break
	for (i = 0; i < HashFileSize - 1; i++) {
		if (HashFile[i] == 0x0A) {
			NumLines++;
		} else if (HashFile[i] == 0x0D) {
			// Convert to UNIX style
			HashFile[i] = 0x0A;
			// Don't double count lines with DOS style ending
			if (HashFile[i + 1] != 0x0A)
				NumLines++;
		} else if (HashFile[i] < ' ' && HashFile[i] != '\t') {
			// Do not allow any NUL or control characters besides TAB
			Status = EFI_ABORTED;
			PrintError(L"'%s' contains invalid data", HASH_FILE);
			goto out;
		}
	}

	// Don't allow files with more than a specific set of entries
	if (NumLines > HASH_FILE_LINES_MAX) {
		Status = EFI_UNSUPPORTED;
		PrintError(L"'%s' contains too many lines", HASH_FILE);
		goto out;
	}

	// Allocate a array of hash entries
	HashList = AllocateZeroPool(NumLines * sizeof(HASH_ENTRY));
	if (HashList == NULL) {
		Status = EFI_OUT_OF_RESOURCES;
		PrintError(L"Unable to allocate memory");
		goto out;
	}

	// Now parse the file to populate the array
	HashListSize = 0;
	for (i = 0; i < HashFileSize; ) {
		// Ignore whitespaces, control characters or anything non-ASCII
		// (such as BOMs) that may precede a hash entry or a comment.
		while ((HashFile[i] <= 0x20 || HashFile[i] >= 0x80) && i < HashFileSize)
			i++;
		if (i >= HashFileSize)
			break;

		// Ignore comments
		if (HashFile[i] == '#') {
			// Note that because we added a terminating 0x0A to the file,
			// we cannot overflow on the while loop below.
			while (HashFile[i++] != 0x0A);
			continue;
		}

		// Check for a valid hash, which should be HASH_HEXASCII_SIZE
		// hexascii followed by whitespace.
		if (i + HASH_HEXASCII_SIZE >= HashFileSize ||
			(!IsWhiteSpace(HashFile[i + HASH_HEXASCII_SIZE]))) {
			HashFile[MIN(HashFileSize - 1, i + HASH_HEXASCII_SIZE)] = 0x00;
			Status = EFI_ABORTED;
			PrintError(L"Invalid hash data after '%a'", &HashFile[i]);
			goto out;
		}

		// NUL-terminate the hash value, add it to our array and validate it
		HashFile[i + HASH_HEXASCII_SIZE] = 0x00;
		HashList[HashListSize].Hash = &HashFile[i];
		for (; HashFile[i] != 0x00; i++) {
			// Convert A-F to lowercase
			if (HashFile[i] >= 'A' && HashFile[i] <= 'F')
				HashFile[i] += 0x20;
			if (!IsValidHexAscii(HashFile[i])) {
				Status = EFI_ABORTED;
				PrintError(L"Invalid hash data in '%a'", HashList[HashListSize].Hash);
				goto out;
			}
		}

		// Skip data between hash and path
		while (++i < HashFileSize && HashFile[i] < 0x21) {
			// Anything other than whitespace is illegal
			if (!IsWhiteSpace(HashFile[i])) {
				Status = EFI_ABORTED;
				PrintError(L"Invalid hash data after '%a'", HashList[HashListSize].Hash);
				goto out;
			}
		}

		// Start of path value
		c = i;
		while (i < HashFileSize && HashFile[i] != 0x0A) {
			if (HashFile[i] == '/') {
				// Convert slashes to backslashes
				HashFile[i] = '\\';
			} else if (HashFile[i] < ' ') {
				// Anything lower than space (including TAB) is illegal
				i = c;
				break;
			}
			i++;
		}
		// Check for a path parsing error above or an illegal path length
		if (i == c || i > c + PATH_MAX) {
			Status = EFI_ABORTED;
			PrintError(L"Invalid path after '%a'", HashList[HashListSize].Hash);
			goto out;
		}
		// NUL-terminate the path.
		// Note that we can't overflow here since we added an extra 0x0A to our file.
		HashFile[i++] = 0x00;
		HashList[HashListSize].Path = &HashFile[c];
		HashListSize++;
	}

	List->Size = HashListSize;
	List->Buffer = HashFile;
	List->Entry = HashList;

out:
	SafeFree(Info);
	if (EFI_ERROR(Status)) {
		SafeFree(HashFile);
		SafeFree(HashList);
	}

	return Status;
}
