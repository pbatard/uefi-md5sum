/*
 * uefi-md5sum: UEFI MD5Sum validator
 * Copyright © 2014-2023 Pete Batard <pete@akeo.ie>
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

#ifdef __MAKEWITH_GNUEFI

#include <efi.h>
#include <efilib.h>
#include <libsmbios.h>

#else /* EDK2 */

#include <Base.h>
#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/LoadedImage.h>

#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/SmBios.h>

#include <IndustryStandard/SmBios.h>

#endif /* __MAKEWITH_GNUEFI */

/*
 * Global variables
 */

/* Set to true when we are running the GitHub Actions tests */
extern BOOLEAN              IsTestMode;

/* SMBIOS vendor name used by GitHub Actions' qemu when running the tests */
#define TESTING_SMBIOS_NAME "GitHub Actions Test"

/* Name of the file containing the list of hashes */
#define HASH_FILE           L"md5sum.txt"

/* Used to center our output on screen */
#define TEXT_POSITION_X     2
#define TEXT_POSITION_Y     5

/* Number of failed entries we display beneath the validation line before looping back */
#define FAILED_ENTRIES_MAX  5

/* Size of an MD5 hash */
#define MD5_HASHSIZE        16

/* Block size used for MD5 hash computation */
#define MD5_BLOCKSIZE       64

/* Buffer size used for MD5 hashing */
#define MD5_BUFFERSIZE      4096

/* Size of the hexascii representation of a hash */
#define HASH_HEXASCII_SIZE  (MD5_HASHSIZE * 2)

/* Maximum size to be used for paths */
#ifndef PATH_MAX
#define PATH_MAX            512
#endif

/* For safety, we set a maximum size that strings shall not outgrow */
#define STRING_MAX          (PATH_MAX + 2)

/* Shorthand for Unicode strings */
typedef UINT32              CHAR32;

/* Maximum size allowed for hash file we process */
#define HASH_FILE_SIZE_MAX  (64 * 1024 * 1024)

/* Maximum number of lines allowed in a hash file */
#define HASH_FILE_LINES_MAX 100000

/* Maximum size for the File Info structure we query */
#define FILE_INFO_SIZE      (SIZE_OF_EFI_FILE_INFO + PATH_MAX * sizeof(CHAR16))

/* Data alignment macros */
#if defined(__GNUC__)
#define ALIGNED(m)          __attribute__ ((__aligned__(m)))
#elif defined(_MSC_VER)
#define ALIGNED(m)          __declspec(align(m))
#endif

/* Macro used to compute the size of an array */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(Array)   (sizeof(Array) / sizeof((Array)[0]))
#endif

/* Standard MIN() macro */
#ifndef MIN
#define MIN(X, Y)            (((X) < (Y)) ? (X) : (Y))
#endif

/* FreePool() replacement, that NULLs the freed pointer. */
#define SafeFree(p)          do { FreePool(p); p = NULL;} while(0)

/* Maximum line size for our banner */
#define BANNER_LINE_SIZE     79

/*
 * Console colours we will be using
 */
#define TEXT_DEFAULT         EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK)
#define TEXT_REVERSED        EFI_TEXT_ATTR(EFI_BLACK, EFI_LIGHTGRAY)
#define TEXT_YELLOW          EFI_TEXT_ATTR(EFI_YELLOW, EFI_BLACK)
#define TEXT_RED             EFI_TEXT_ATTR(EFI_LIGHTRED, EFI_BLACK)
#define TEXT_GREEN           EFI_TEXT_ATTR(EFI_LIGHTGREEN, EFI_BLACK)
#define TEXT_WHITE           EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK)

/*
 * Set and restore the console text colour
 */
#define SetText(attr)        do { if (!IsTestMode) gST->ConOut->SetAttribute(gST->ConOut, (attr)); } while(0)
#define DefText()            do { if (!IsTestMode) gST->ConOut->SetAttribute(gST->ConOut, TEXT_DEFAULT); } while(0)

/*
 * Convenience macros to print informational, warning or error messages.
 */
#define PrintInfo(fmt, ...)     do { SetText(TEXT_WHITE); Print(L"[INFO]"); DefText(); \
                                     Print(L" " fmt L"\n", ##__VA_ARGS__); } while(0)
#define PrintWarning(fmt, ...)  do { SetText(TEXT_YELLOW); Print(L"[WARN]"); DefText(); \
                                     Print(L" " fmt L"\n", ##__VA_ARGS__); } while(0)
#define PrintError(fmt, ...)    do { SetText(TEXT_RED); Print(L"[FAIL]"); DefText(); \
                                     Print(L" " fmt L": [%d] %r\n", ##__VA_ARGS__, (Status&0x7FFFFFFF), Status); } while (0)

/* Convenience macro to position text on screen (when not running in test mode). */
#define SetTextPosition(x, y)   do { if (!IsTestMode) gST->ConOut->SetCursorPosition(gST->ConOut, x, y);} while (0)

/* Convenience assertion macro */
#define P_ASSERT(f, l, a)   do { if(!(a)) { Print(L"*** ASSERT FAILED: %a(%d): %a ***\n", f, l, #a); \
                                          if (IsTestMode) ShutDown(); else Halt(); } } while(0)
#define V_ASSERT(a)         P_ASSERT(__FILE__, __LINE__, a)

/*
 * EDK2 and gnu-efi's CompareGuid() return opposite values for a match!
 * EDK2 returns boolean TRUE, whereas gnu-efi returns INTN 0, so we
 * define a common boolean macro that follows EDK2 convention always.
 */
#if defined(_GNU_EFI)
#define COMPARE_GUID(a, b) (CompareGuid(a, b) == 0)
#else
#define COMPARE_GUID CompareGuid
#endif

/* Context that is used to hash data */
typedef struct ALIGNED(64) {
	UINT8       Buffer[MD5_BLOCKSIZE];
	UINT32      State[4];
	UINT64      ByteCount;
} HASH_CONTEXT;

/* Hash entry, comprised of the (hexascii) hash value and the path it applies to */
typedef struct {
	CHAR8*      Hash;
	CHAR8*      Path;
} HASH_ENTRY;

/* Hash list of <Size> Hash entries */
typedef struct {
	HASH_ENTRY* Entry;
	UINTN       NumEntries;
	UINT8*      Buffer;
	UINT64      TotalBytes;
} HASH_LIST;

/* Check for a valid lowercase hex ASCII value */
STATIC __inline BOOLEAN IsValidHexAscii(CHAR8 c)
{
	return ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
}

/* Check for a valid whitespace character */
STATIC __inline BOOLEAN IsWhiteSpace(CHAR8 c)
{
	return (c == ' ' || c == '\t');
}

/* Pause the system for a specific duration (in us) */
STATIC __inline EFI_STATUS Sleep(UINTN MicroSeconds)
{
	return gBS->Stall(MicroSeconds);
}

/* Shut down the system immediately */
STATIC __inline VOID ShutDown()
{
	gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

/* Freeze the system with current screen output, then shut it down after one hour */
STATIC __inline VOID Halt()
{
	Sleep((UINTN)3600 * 1000 * 1000);
	gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

/*
 * Secure string length, that asserts if the string is NULL or if
 * the length is larger than a predetermined value (STRING_MAX)
 */
STATIC __inline UINTN _SafeStrLen(CONST CHAR16 * String, CONST CHAR8 * File, CONST UINTN Line) {
	UINTN Len = 0;
	P_ASSERT(File, Line, String != NULL);
	Len = StrLen(String);
	P_ASSERT(File, Line, Len < STRING_MAX);
	return Len;
}

#define SafeStrLen(s) _SafeStrLen(s, __FILE__, __LINE__)

/**
  Detect if we are running a test system by querying the SMBIOS vendor string.

  @retval TRUE   A test system was detected.
  @retval FALSE  A regular plaform is being used.
**/
BOOLEAN IsTestSystem(VOID);

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
EFI_STATUS Parse(IN CONST EFI_FILE_HANDLE Root, IN CONST CHAR16* Path, OUT HASH_LIST* List);

/**
  Compute the MD5 hash of a single file.

  @param[in]     Root   A file handle to the root directory.
  @param[in]     Path   A pointer to the CHAR16 string with the target path.
  @param[in/out] Hash   A pointer to the 16-byte array that is to receive the hash.

  @retval EFI_SUCCESS           The file was successfully processed and the hash has been populated.
  @retval EFI_INVALID_PARAMETER One or more of the input parameters are invalid or one of the paths
								from the hash list points to a directory.
  @retval EFI_OUT_OF_RESOURCES  A memory allocation error occurred.
  @retval EFI_NOT_FOUND         The target file could not be found on the media.
**/
EFI_STATUS HashFile(IN CONST EFI_FILE_HANDLE Root, IN CONST CHAR16* Path, OUT UINT8* Hash);

/**
  Convert a UTF-8 encoded string to a UCS-2 encoded string.

  @param[in]  Utf8String      A pointer to the input UTF-8 encoded string.
  @param[out] Ucs2String      A pointer to the output UCS-2 encoded string.
  @param[in]  Ucs2StringSize  The size of the Ucs2String buffer (in CHAR16).

  @retval EFI_SUCCESS            The conversion was successful.
  @retval EFI_INVALID_PARAMETER  One or more of the input parameters are invalid.
  @retval EFI_BUFFER_TOO_SMALL   The output buffer is too small to hold the result.
**/
EFI_STATUS Utf8ToUcs2(IN CONST CHAR8* Utf8String, OUT CHAR16* Ucs2String, IN CONST UINTN Ucs2StringSize);
