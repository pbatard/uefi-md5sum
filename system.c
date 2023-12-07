/*
 * uefi-md5sum: UEFI MD5Sum validator - System Information
 * Copyright © 2014-2023 Pete Batard <pete@akeo.ie>
 * With parts from EDK © 1998  Intel Corporation
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
  Read a system configuration table from a TableGuid.
 
  @param[in]  TableGuid  The GUID of the system configuration table to seek.
  @param[out] Table      A pointer to the table structure to populate.
 
  @retval EFI_SUCCESS            The table was found and the Table pointer was set.
  @retval EFI_NOT_FOUND          The table could not be located in the system configuration.
  @retval EFI_INVALID_PARAMETER  One or more of the input parameters are invalid.
**/
STATIC EFI_STATUS GetSystemConfigurationTable(
	IN EFI_GUID* TableGuid,
	OUT VOID** Table
)
{
	UINTN Index;

	if (TableGuid == NULL || Table == NULL)
		return EFI_INVALID_PARAMETER;

	for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
		if (COMPARE_GUID(TableGuid, &(gST->ConfigurationTable[Index].VendorGuid))) {
			*Table = gST->ConfigurationTable[Index].VendorTable;
			return EFI_SUCCESS;
		}
	}
	return EFI_NOT_FOUND;
}

/**
  Return the SMBIOS string matching the provided string number.

  @param[in]  Smbios        Pointer to SMBIOS structure.
  @param[in]  StringNumber  String number to return. 0xFFFF can be used to skip all
                            strings and point to the next SMBIOS structure.
  @retval     Pointer to a string (may be NULL if the string is not populated) or a
              pointer to next SMBIOS structure if StringNumber is 0xFFFF.
**/
STATIC CHAR8* GetSmbiosString(
	IN SMBIOS_STRUCTURE_POINTER* Smbios,
	IN UINT16 StringNumber
)
{
	UINT16 Index;
	CHAR8* String;

	if (Smbios == NULL)
		return NULL;

	// Skip over formatted section
	String = (CHAR8*)(Smbios->Raw + Smbios->Hdr->Length);

	// Look through unformated section
	for (Index = 1; Index <= StringNumber; Index++) {
		if (StringNumber == Index)
			return String;

		// Skip string
		for (; *String != 0; String++);
		String++;

		if (*String == 0) {
			// If double NUL then we are done.
			// Return pointer to next structure in Smbios.
			// If you pass 0xFFFF for StringNumber you always get here.
			Smbios->Raw = (UINT8*)++String;
			return NULL;
		}
	}
	return NULL;
}

/**
  Detect if we are running a test system by querying the SMBIOS vendor string.

  @retval TRUE   A test system was detected.
  @retval FALSE  The SMBIOS could not be queried or a non test system is being used.
**/
BOOLEAN IsTestSystem(VOID)
{
	EFI_STATUS Status;
	SMBIOS_STRUCTURE_POINTER Smbios;
	SMBIOS_TABLE_ENTRY_POINT* SmbiosTable;
	SMBIOS_TABLE_3_0_ENTRY_POINT* Smbios3Table;
	CHAR8* VendorStr;
	UINT8* Raw;
	UINTN MaximumSize, ProcessedSize = 0;

	Status = GetSystemConfigurationTable(&gEfiSmbios3TableGuid, (VOID**)&Smbios3Table);
	if (Status == EFI_SUCCESS) {
		Smbios.Hdr = (SMBIOS_STRUCTURE*)(UINTN)Smbios3Table->TableAddress;
		MaximumSize = (UINTN)Smbios3Table->TableMaximumSize;
	} else {
		Status = GetSystemConfigurationTable(&gEfiSmbiosTableGuid, (VOID**)&SmbiosTable);
		if (EFI_ERROR(Status))
			return FALSE;
		Smbios.Hdr = (SMBIOS_STRUCTURE*)(UINTN)SmbiosTable->TableAddress;
		MaximumSize = (UINTN)SmbiosTable->TableLength;
	}
	// Sanity check
	if (MaximumSize > 1024 * 1024)
		return FALSE;

	while (Smbios.Hdr->Type != 0x7F) {
		Raw = Smbios.Raw;
		if (Smbios.Hdr->Type == 0) {
			// If we have a Vendor, compare it to the SMBIOS Vendor we set
			// in qemu in GitHub Actions during our tests.
			VendorStr = GetSmbiosString(&Smbios, Smbios.Type0->Vendor);
			if (VendorStr == NULL)
				return FALSE;
			return (CompareMem(VendorStr, TESTING_SMBIOS_NAME,
				MIN(sizeof(TESTING_SMBIOS_NAME) - 1, AsciiStrLen(VendorStr))) == 0);
		}
		GetSmbiosString(&Smbios, 0xFFFF);
		ProcessedSize += (UINTN)Smbios.Raw - (UINTN)Raw;
		if (ProcessedSize > MaximumSize)
			return FALSE;
	}

	return FALSE;
}
