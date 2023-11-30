/*
 * uefi-md5sum: UEFI MD5Sum validator - MD5 Hash functions
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

/*
 * This code was derived from various public domain sources
 * sharing the following copyright declaration:
 *
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 */

#include "boot.h"

/*
 * Prefetch 64 bytes at address m, for read-only operation
 * We account for these built-in calls doing nothing if the
 * line has already been fetched, or if the address is invalid.
 */
#if defined(__GNUC__) || defined(__clang__)
#define PREFETCH64(m) do { __builtin_prefetch(m, 0, 0); __builtin_prefetch(m + 32, 0, 0); } while(0)
#elif defined(_MSC_VER)
#if defined(_M_IX86) || defined (_M_X64)
#define PREFETCH64(m) do { _m_prefetch(m); _m_prefetch(m + 32); } while(0)
#else
// _m_prefetch() doesn't seem to exist for MSVC/ARM
#define PREFETCH64(m)
#endif
#endif

/* Hash context initialisation */
STATIC VOID Md5Init(HASH_CONTEXT* Context)
{
	ZeroMem(Context, sizeof(*Context));
	Context->State[0] = 0x67452301;
	Context->State[1] = 0xefcdab89;
	Context->State[2] = 0x98badcfe;
	Context->State[3] = 0x10325476;
}

/* Transform the message X which consists of 16 32-bit-words (MD5) */
STATIC VOID Md5Transform(HASH_CONTEXT* Context, CONST UINT8* Data)
{
	UINT32 a, b, c, d, x[16];

	a = Context->State[0];
	b = Context->State[1];
	c = Context->State[2];
	d = Context->State[3];

#ifdef BIG_ENDIAN_HOST
	{
		UINT32 k;
		for (k = 0; k < 16; k += 4) {
			CONST UINT8* p2 = Data + k * 4;
			x[k] = SwapBytes32(p2);
			x[k + 1] = SwapBytes32(p2 + 4);
			x[k + 2] = SwapBytes32(p2 + 8);
			x[k + 3] = SwapBytes32(p2 + 12);
		}
	}
#else
	CopyMem(x, Data, sizeof(x));
#endif

#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

#define MD5STEP(f, w, x, y, z, Data, s) do { \
	( w += f(x, y, z) + Data,  w = w<<s | w>>(32-s),  w += x ); } while(0)

	MD5STEP(F1, a, b, c, d, x[0] + 0xd76aa478, 7);
	MD5STEP(F1, d, a, b, c, x[1] + 0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, x[2] + 0x242070db, 17);
	MD5STEP(F1, b, c, d, a, x[3] + 0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, x[4] + 0xf57c0faf, 7);
	MD5STEP(F1, d, a, b, c, x[5] + 0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, x[6] + 0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, x[7] + 0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, x[8] + 0x698098d8, 7);
	MD5STEP(F1, d, a, b, c, x[9] + 0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, x[10] + 0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, x[11] + 0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, x[12] + 0x6b901122, 7);
	MD5STEP(F1, d, a, b, c, x[13] + 0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, x[14] + 0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, x[15] + 0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, x[1] + 0xf61e2562, 5);
	MD5STEP(F2, d, a, b, c, x[6] + 0xc040b340, 9);
	MD5STEP(F2, c, d, a, b, x[11] + 0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, x[0] + 0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, x[5] + 0xd62f105d, 5);
	MD5STEP(F2, d, a, b, c, x[10] + 0x02441453, 9);
	MD5STEP(F2, c, d, a, b, x[15] + 0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, x[4] + 0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, x[9] + 0x21e1cde6, 5);
	MD5STEP(F2, d, a, b, c, x[14] + 0xc33707d6, 9);
	MD5STEP(F2, c, d, a, b, x[3] + 0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, x[8] + 0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, x[13] + 0xa9e3e905, 5);
	MD5STEP(F2, d, a, b, c, x[2] + 0xfcefa3f8, 9);
	MD5STEP(F2, c, d, a, b, x[7] + 0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, x[12] + 0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, x[5] + 0xfffa3942, 4);
	MD5STEP(F3, d, a, b, c, x[8] + 0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, x[11] + 0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, x[14] + 0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, x[1] + 0xa4beea44, 4);
	MD5STEP(F3, d, a, b, c, x[4] + 0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, x[7] + 0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, x[10] + 0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, x[13] + 0x289b7ec6, 4);
	MD5STEP(F3, d, a, b, c, x[0] + 0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, x[3] + 0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, x[6] + 0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, x[9] + 0xd9d4d039, 4);
	MD5STEP(F3, d, a, b, c, x[12] + 0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, x[15] + 0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, x[2] + 0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, x[0] + 0xf4292244, 6);
	MD5STEP(F4, d, a, b, c, x[7] + 0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, x[14] + 0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, x[5] + 0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, x[12] + 0x655b59c3, 6);
	MD5STEP(F4, d, a, b, c, x[3] + 0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, x[10] + 0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, x[1] + 0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, x[8] + 0x6fa87e4f, 6);
	MD5STEP(F4, d, a, b, c, x[15] + 0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, x[6] + 0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, x[13] + 0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, x[4] + 0xf7537e82, 6);
	MD5STEP(F4, d, a, b, c, x[11] + 0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, x[2] + 0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, x[9] + 0xeb86d391, 21);

	#undef F1
	#undef F2
	#undef F3
	#undef F4

	/* Update chaining vars */
	Context->State[0] += a;
	Context->State[1] += b;
	Context->State[2] += c;
	Context->State[3] += d;
}

/* Update the message digest with the contents of the buffer (MD5) */
STATIC VOID Md5Write(HASH_CONTEXT* Context, CONST UINT8* Buffer, UINTN Length)
{
	UINTN Num = Context->ByteCount & (MD5_BLOCKSIZE - 1);

	/* Update bytecount */
	Context->ByteCount += Length;

	/* Handle any leading odd-sized chunks */
	if (Num) {
		UINT8* p = Context->Buffer + Num;

		Num = MD5_BLOCKSIZE - Num;
		if (Length < Num) {
			CopyMem(p, Buffer, Num);
			return;
		}
		CopyMem(p, Buffer, Num);
		Md5Transform(Context, Context->Buffer);
		Buffer += Num;
		Length -= Num;
	}

	/* Process Data in blocksize chunks */
	while (Length >= MD5_BLOCKSIZE) {
		PREFETCH64(Buffer + MD5_BLOCKSIZE);
		Md5Transform(Context, Buffer);
		Buffer += MD5_BLOCKSIZE;
		Length -= MD5_BLOCKSIZE;
	}

	/* Handle any remaining bytes of Data. */
	CopyMem(Context->Buffer, Buffer, Length);
}

/* Finalize the computation and write the digest in Context->State[] */
STATIC VOID Md5Final(HASH_CONTEXT* Context)
{
	UINTN Count = ((UINTN)Context->ByteCount) & (MD5_BLOCKSIZE - 1);
	UINT64 BitCount = Context->ByteCount << 3;
	UINT8* p;

	/* Set the first char of padding to 0x80.
	 * This is safe since there is always at least one byte free
	 */
	p = Context->Buffer + Count;
	*p++ = 0x80;

	/* Bytes of padding needed to make blocksize */
	Count = (MD5_BLOCKSIZE - 1) - Count;

	/* Pad out to 56 mod 64 */
	if (Count < 8) {
		/* Two lots of padding: Pad the first block to blocksize */
		ZeroMem(p, Count);
		Md5Transform(Context, Context->Buffer);

		/* Now fill the next block */
		ZeroMem(Context->Buffer, MD5_BLOCKSIZE - 8);
	} else {
		/* Pad block to blocksize */
		ZeroMem(p, Count - 8);
	}

	/* append the 64 bit Count (little endian) */
	Context->Buffer[MD5_BLOCKSIZE - 8] = (UINT8)BitCount;
	Context->Buffer[MD5_BLOCKSIZE - 7] = (UINT8)(BitCount >> 8);
	Context->Buffer[MD5_BLOCKSIZE - 6] = (UINT8)(BitCount >> 16);
	Context->Buffer[MD5_BLOCKSIZE - 5] = (UINT8)(BitCount >> 24);
	Context->Buffer[MD5_BLOCKSIZE - 4] = (UINT8)(BitCount >> 32);
	Context->Buffer[MD5_BLOCKSIZE - 3] = (UINT8)(BitCount >> 40);
	Context->Buffer[MD5_BLOCKSIZE - 2] = (UINT8)(BitCount >> 48);
	Context->Buffer[MD5_BLOCKSIZE - 1] = (UINT8)(BitCount >> 56);

	Md5Transform(Context, Context->Buffer);

	p = Context->Buffer;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { SwapBytes32(p, (UINT32)Context->State[a]); p += 4; } while(0);
#else
#define X(a) do { *(UINT32*)p = (UINT32)Context->State[a]; p += 4; } while(0)
#endif
	X(0);
	X(1);
	X(2);
	X(3);
#undef X
}

/**
  Compute the MD5 hash of a single file.

  @param[in]     Root   A file handle to the root directory.
  @param[in]     Path   A pointer to the CHAR16 string with the target path.
  @param[out]    Hash   A pointer to the 16-byte array that is to receive the hash.

  @retval EFI_SUCCESS           The file was successfully processed and the hash has been populated.
  @retval EFI_INVALID_PARAMETER One or more of the input parameters are invalid or one of the paths
                                from the hash list points to a directory.
  @retval EFI_OUT_OF_RESOURCES  A memory allocation error occurred.
  @retval EFI_NOT_FOUND         The target file could not be found on the media.
**/
EFI_STATUS HashFile(
	IN CONST EFI_FILE_HANDLE Root,
	IN CONST CHAR16* Path,
	OUT UINT8* Hash
)
{
	EFI_STATUS Status = EFI_INVALID_PARAMETER;
	EFI_FILE_HANDLE File = NULL;
	EFI_FILE_INFO* Info = NULL;
	HASH_CONTEXT Context = { {0} };
	UINTN Size, ReadSize = 0;
	UINT64 ReadBytes;
	UINT8 Buffer[MD5_BUFFERSIZE];

	if ((Root == NULL) || (Path == NULL) || (Hash == NULL))
		goto out;

	ZeroMem(Hash, MD5_HASHSIZE);

	// Open the target
	Status = Root->Open(Root, &File, (CHAR16*)Path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
	if (EFI_ERROR(Status))
		goto out;

	// Validate that it's a file and not a directory
	Size = FILE_INFO_SIZE;
	Info = AllocateZeroPool(Size);
	if (Info == NULL) {
		Status = EFI_OUT_OF_RESOURCES;
		goto out;
	}
	Status = File->GetInfo(File, &gEfiFileInfoGuid, &Size, Info);
	if (EFI_ERROR(Status))
		goto out;

	if (Info->Attribute & EFI_FILE_DIRECTORY) {
		Status = EFI_INVALID_PARAMETER;
		goto out;
	}

	// Compute the MD5 Hash
	Md5Init(&Context);
	for (ReadBytes = 0; ; ReadBytes += ReadSize) {
		ReadSize = sizeof(Buffer);
		Status = File->Read(File, &ReadSize, Buffer);
		if (EFI_ERROR(Status))
			goto out;
		if (ReadSize == 0)
			break;
		Md5Write(&Context, Buffer, (UINTN)ReadSize);
	}
	Md5Final(&Context);

	CopyMem(Hash, Context.Buffer, MD5_HASHSIZE);
	Status = EFI_SUCCESS;

out:
	if (File != NULL)
		File->Close(File);
	SafeFree(Info);
	return Status;
}
