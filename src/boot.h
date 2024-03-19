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
#include <Protocol/DiskIo.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/LoadedImage.h>

#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/SmBios.h>

#include <IndustryStandard/SmBios.h>

#endif /* __MAKEWITH_GNUEFI */

/* For use with static analysers */
#if defined(__GNUC__)
#define NO_RETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define NO_RETURN __declspec(noreturn)
#else
#define NO_RETURN
#endif

/*
 * Global variables
 */

 /* Copy of the main Image Handle */
extern EFI_HANDLE           gMainImageHandle;

/* Set to true when we are running the GitHub Actions tests */
extern BOOLEAN              gIsTestMode;

/* Amount of time to pause after a read (in μs) */
extern UINTN                gPauseAfterRead;

/* Dimensions of the UEFI text console */
typedef struct {
	UINTN Cols;
	UINTN Rows;
} CONSOLE_DIMENSIONS;
extern CONSOLE_DIMENSIONS   gConsole;

/* Incremental vertical position at which we display alert messages */
extern UINTN                gAlertYPos;

/* SMBIOS vendor name used by GitHub Actions' qemu when running the tests */
#define TESTING_SMBIOS_NAME "GitHub Actions Test"

/* Name of the file containing the list of hashes */
#define HASH_FILE           L"md5sum.txt"

/* Minimum dimensions we expect the console to accomodate */
#define COLS_MIN            50
#define ROWS_MIN            20

/* Size of an MD5 hash */
#define MD5_HASHSIZE        16

/* Block size used for MD5 hash computation */
#define MD5_BLOCKSIZE       64

/* Size of the hexascii representation of a hash */
#define HASH_HEXASCII_SIZE  (MD5_HASHSIZE * 2)

/* Buffer size for file reads and MD5 hashing */
#define READ_BUFFERSIZE     (1024 * 1024)

/* Number of bytes to process between watchdog resets */
#define WATCHDOG_RESETSIZE  (128 * 1024 * 1024)

/* Maximum size to be used for paths */
#ifndef PATH_MAX
#define PATH_MAX            512
#endif

/* For safety, we set a maximum size that strings shall not outgrow */
#define STRING_MAX          (PATH_MAX + 2)

/* Shorthand for Unicode strings */
typedef UINT32              CHAR32;

/* Maximum size allowed for the hash file we process */
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

/* Standard MIN/MAX macro */
#ifndef MIN
#define MIN(X, Y)            (((X) < (Y)) ? (X) : (Y))
#endif

#ifndef MAX
#define MAX(X, Y)            (((X) > (Y)) ? (X) : (Y))
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
#define TEXT_DARKGRAY        EFI_TEXT_ATTR(EFI_DARKGRAY, EFI_BLACK)

/*
 * Set and restore the console text colour
 */
#define SetText(attr)        do { if (!gIsTestMode) gST->ConOut->SetAttribute(gST->ConOut, (attr)); } while(0)
#define DefText()            do { if (!gIsTestMode) gST->ConOut->SetAttribute(gST->ConOut, TEXT_DEFAULT); } while(0)

/* Types of progress */
#define PROGRESS_TYPE_FILE 0
#define PROGRESS_TYPE_BYTE 1

/* Structure used for progress report */
typedef struct {
	UINT8         Type;      /* One of the types defined above */
	BOOLEAN       Active;    /* Indicates that progress bar is active and can be updated */
	UINTN         YPos;      /* Vertical position of the progress bar on the console */
	UINTN         PPos;      /* Horizontal position of the percentage */
	UINTN         LastCol;   /* Current horizontal position of the progress bar */
	UINT64        Current;   /* Current progres value */
	UINT64        Maximum;   /* Maximum progress value */
	CONST CHAR16* Message;   /* Message that should be displayed above the progress bar */
} PROGRESS_DATA;

/*
 * Convenience macros to print informational, warning or error messages.
 */
#define PrintInfo(fmt, ...)     do { SetTextPosition(0, gAlertYPos++); \
                                     SetText(TEXT_WHITE); Print(L"[INFO]"); DefText(); \
                                     Print(L" " fmt L"\n", ##__VA_ARGS__); } while(0)
#define PrintWarning(fmt, ...)  do { SetTextPosition(0, gAlertYPos++); \
                                     SetText(TEXT_YELLOW); Print(L"[WARN]"); DefText(); \
                                     Print(L" " fmt L"\n", ##__VA_ARGS__); } while(0)
#define PrintError(fmt, ...)    do { SetTextPosition(0, gAlertYPos++); \
                                     SetText(TEXT_RED); Print(L"[FAIL]"); DefText(); \
                                     Print(L" " fmt L": [%d] %r\n", ##__VA_ARGS__, (Status&0x7FFFFFFF), Status); } while (0)
#define PrintTest(fmt, ...)     do { if (gIsTestMode) Print(L"[TEST] " fmt L"\n", ##__VA_ARGS__); } while(0)

/* Convenience macro to position text on screen (when not running in test mode). */
#define SetTextPosition(x, y)   do { if (!gIsTestMode) gST->ConOut->SetCursorPosition(gST->ConOut, x, y);} while (0)

/* Convenience assertion macro */
#define P_ASSERT(f, l, a)   do { if(!(a)) { Print(L"\n*** ASSERT FAILED: %a(%d): %a ***\n", f, l, #a); \
                                          if (gIsTestMode) ShutDown(); else Halt(); } } while(0)
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
	return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'));
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

/* Reset the system immediately */
NO_RETURN STATIC __inline VOID Reset()
{
	gRT->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
	while (1);
}

/* Shut down the system immediately */
NO_RETURN STATIC __inline VOID ShutDown()
{
	gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
	while (1);
}

/* Freeze the system with current screen output, then shut it down after one hour */
NO_RETURN STATIC __inline VOID Halt()
{
	// Disable the watchdog timer, since we don't want an early reset
	gBS->SetWatchdogTimer(0, 0, 0, NULL);
	Sleep((UINTN)3600 * 1000 * 1000);
	gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
	while (1);
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

/*
 * Some UEFI firmwares have a *BROKEN* Unicode collation implementation
 * so we must provide our own version of StriCmp for ASCII comparison...
 */
static __inline CHAR16 _tolower(CONST CHAR16 c)
{
	if (('A' <= c) && (c <= 'Z'))
		return 'a' + (c - 'A');
	return c;
}

STATIC __inline INTN _StriCmp(CONST CHAR16* s1, CONST CHAR16* s2)
{
	/* NB: SafeStrLen() will already have asserted if these condition are met */
	if ((SafeStrLen(s1) >= STRING_MAX) || (SafeStrLen(s2) >= STRING_MAX))
		return -1;
	while ((*s1 != L'\0') && (_tolower(*s1) == _tolower(*s2)))
		s1++, s2++;
	return (INTN)(*s1 - *s2);
}

/*
 * Secure string copy, that either uses the already secure version from
 * EDK2, or duplicates it for gnu-efi and asserts on any error.
 */
STATIC __inline VOID _SafeStrCpy(CHAR16* Destination, UINTN DestMax,
	CONST CHAR16* Source, CONST CHAR8* File, CONST UINTN Line) {
#ifdef _GNU_EFI
	P_ASSERT(File, Line, Destination != NULL);
	P_ASSERT(File, Line, Source != NULL);
	P_ASSERT(File, Line, DestMax != 0);
	/*
	 * EDK2 would use RSIZE_MAX, but we use the smaller PATH_MAX for
	 * gnu-efi as it can help detect path overflows while debugging.
	 */
	P_ASSERT(File, Line, DestMax <= PATH_MAX);
	P_ASSERT(File, Line, DestMax > StrLen(Source));
	while (*Source != 0)
		*(Destination++) = *(Source++);
	*Destination = 0;
#else
	P_ASSERT(File, Line, StrCpyS(Destination, DestMax, Source) == 0);
#endif
}

#define SafeStrCpy(d, l, s) _SafeStrCpy(d, l, s, __FILE__, __LINE__)

/*
 * Secure string cat, that either uses the already secure version from
 * EDK2, or duplicates its functionality for gnu-efi.
 */
STATIC __inline VOID _SafeStrCat(CHAR16* Destination, UINTN DestMax,
	CONST CHAR16* Source, CONST CHAR8* File, CONST UINTN Line) {
#ifdef _GNU_EFI
	P_ASSERT(File, Line, Destination != NULL);
	P_ASSERT(File, Line, Source != NULL);
	P_ASSERT(File, Line, DestMax != 0);
	/*
	 * EDK2 would use RSIZE_MAX, but we use the smaller PATH_MAX for
	 * gnu-efi as it can help detect path overflows while debugging.
	 */
	P_ASSERT(File, Line, DestMax <= PATH_MAX);
	P_ASSERT(File, Line, DestMax > StrLen(Source) + StrLen(Destination));
	Destination += StrLen(Destination);
	while (*Source != 0)
		*(Destination++) = *(Source++);
	*Destination = 0;
#else
	P_ASSERT(File, Line, StrCatS(Destination, DestMax, Source) == 0);
#endif
}

#define SafeStrCat(d, l, s) _SafeStrCat(d, l, s, __FILE__, __LINE__)

/**
  Detect if we are running a test system by querying the SMBIOS vendor string.

  @retval TRUE   A test system was detected.
  @retval FALSE  A regular plaform is being used.
**/
BOOLEAN IsTestSystem(VOID);

/**
  Detect if we are running an early AMI UEFI v2.0 system, that can't process USB
  keyboard inputs unless we give it time to breathe.

  @retval TRUE   An AMI v2.0 UEFI firmware was detected.
  @retval FALSE  The system is not AMI v2.0 UEFI.
**/
BOOLEAN IsEarlyAmiUefi(VOID);

/**
  Detect if we are running of an NTFS partition with the buggy AMI NTFS file system
  driver (See: https://github.com/pbatard/AmiNtfsBug)

  @param[in]  DeviceHandle  A handle to the device running our boot image.

  @retval     TRUE          The boot partition is serviced by the AMI NTFS driver.
  @retval     FALSE         A non AMI NTFS file system driver is being used.
**/
BOOLEAN IsProblematicNtfsDriver(
	CONST EFI_HANDLE DeviceHandle
);

/**
  Parse a hash sum list file and populate a HASH_LIST structure from it.

  @param[in]  Root   A file handle to the root directory.
  @param[in]  Path   A pointer to the CHAR16 string.
  @param[out] List   A pointer to the HASH_LIST structure to populate.

  @retval EFI_SUCCESS           The file was successfully parsed and the hash list is populated.
  @retval EFI_INVALID_PARAMETER One or more of the input parameters are invalid.
  @retval EFI_OUT_OF_RESOURCES  A memory allocation error occurred.
  @retval EFI_NOT_FOUND         The hash list file does not exist.
  @retval EFI_UNSUPPORTED       The hash list file is too small or too large.
  @retval EFI_END_OF_FILE       The hash list file could not be read.
  @retval EFI_ABORTED           The hash list file contains invalid data.
**/
EFI_STATUS Parse(
	IN CONST EFI_FILE_HANDLE Root,
	IN CONST CHAR16* Path,
	OUT HASH_LIST* List
);

/**
  Compute the MD5 hash of a single file.

  @param[in]   Root             A file handle to the root directory.
  @param[in]   Path             A pointer to the CHAR16 string with the target path.
  @param[in]   Progress         (Optional) A pointer to a PROGRESS_DATA structure. If provided then
                                the current progress value will be updated by this call.
  @param[out]  Hash             A pointer to the 16-byte array that is to receive the hash.

  @retval EFI_SUCCESS           The file was successfully processed and the hash has been populated.
  @retval EFI_INVALID_PARAMETER One or more of the input parameters are invalid or one of the paths
                                from the hash list points to a directory.
  @retval EFI_OUT_OF_RESOURCES  A memory allocation error occurred.
  @retval EFI_NOT_FOUND         The target file could not be found on the media.
  @retval EFI_END_OF_FILE       The file could not be read in full.
  @retval EFI_ABORTED           User cancelled the operation.
**/
EFI_STATUS HashFile(
	IN CONST EFI_FILE_HANDLE Root,
	IN CONST CHAR16* Path,
	OPTIONAL IN PROGRESS_DATA* Progress,
	OUT UINT8* Hash
);

/**
  Convert a UTF-8 encoded string to a UCS-2 encoded string.

  @param[in]  Utf8String      A pointer to the input UTF-8 encoded string.
  @param[out] Ucs2String      A pointer to the output UCS-2 encoded string.
  @param[in]  Ucs2StringSize  The size of the Ucs2String buffer (in CHAR16).

  @retval EFI_SUCCESS            The conversion was successful.
  @retval EFI_INVALID_PARAMETER  One or more of the input parameters are invalid.
  @retval EFI_BUFFER_TOO_SMALL   The output buffer is too small to hold the result.
**/
EFI_STATUS Utf8ToUcs2(
	IN CONST CHAR8* Utf8String,
	OUT CHAR16* Ucs2String,
	IN CONST UINTN Ucs2StringSize
);

/**
  Console initialisation.
**/
VOID InitConsole(VOID);

/**
  Flush the keyboard input buffers.
**/
VOID FlushKeyboardInput(VOID);

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
);

/**
  Scroll section teardown.
**/
VOID ExitScrollSection(VOID);

/**
  Print a centered message on the console.

  @param[in]  Message    The text message to print.
  @param[in]  YPos       The vertical position to print the message to.
**/
VOID PrintCentered(
	IN CONST CHAR16* Message,
	IN CONST UINTN YPos
);

/**
  Print a hash entry that has failed processing.
  Do this over a specific section of the console we cycle over.

  @param[in]  Status     The Status code from the failed operation on the entry.
  @param[in]  Path       A pointer to the CHAR16 string with the Path of the entry.
  @param[in]  NumFailed  The current number of failed entries.
**/
VOID PrintFailedEntry(
	IN CONST EFI_STATUS Status,
	IN CONST CHAR16* Path
);

/**
  Display a countdown on screen.

  @param[in]  Message   A message to display with the countdown.
  @param[in]  Duration  The duration of the countdown (in ms).
**/
VOID CountDown(
	IN CONST CHAR16* Message,
	IN CONST UINTN Duration
);

/**
  Initialize a progress bar.

  @param[in]  Progress   A pointer to a PROGRESS_DATA structure.
**/
VOID InitProgress(
	IN PROGRESS_DATA* Progress
);

/**
  Update a progress bar.

  @param[in]  Progress   A pointer to a PROGRESS_DATA structure.
**/
VOID UpdateProgress(
	IN PROGRESS_DATA* Progress
);

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
);
