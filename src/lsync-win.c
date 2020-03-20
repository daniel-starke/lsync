/**
 * @file lsync_win.c
 * @author Daniel Starke
 * @date 2017-05-22
 * @version 2017-05-25
 * 
 * DISCLAIMER
 * This file has no copyright assigned and is placed in the Public Domain.
 * All contributions are also assumed to be in the Public Domain.
 * Other contributions are not permitted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <aclapi.h>
#include <windows.h>
#include "lsync.h"


#ifndef COPY_FILE_COPY_SYMLINK
#define COPY_FILE_COPY_SYMLINK 0x00000800
#endif


#ifndef COPY_FILE_NO_BUFFERING
#define COPY_FILE_NO_BUFFERING 0x00001000
#endif


#ifndef BACKUP_SECURITY_INFORMATION
#define BACKUP_SECURITY_INFORMATION 0x00010000
#endif


/**
 * Writes the last Windows API error messages to standard output.
 * 
 * @param[in] obj - object that would had been created/modified
 * @param[in] msg - prefix output with this message
 */
static void printLastError(const TCHAR * obj, const TCHAR * msg) {
	DWORD lastError;
	DWORD bytesReturned;
	TCHAR outBuf[2048];

	lastError = GetLastError();

	bytesReturned = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, lastError, LANG_USER_DEFAULT, outBuf, 2048, NULL);

	if (bytesReturned > 0) {
		_ftprintf(stderr, _T("%s:%s: %s"), obj, msg, outBuf);
	} else {
		_ftprintf(stderr, _T("%s:%s: Error %i while formatting error %i.\n"), obj, msg, (int)GetLastError(), (int)lastError);
	}
}


/**
 * Enables or disables the given security privilege for the current process.
 * 
 * @param[in] privilege - privilege to modify
 * @param[in] bEnablePrivilege - TRUE to enable, FALSE to disable
 * @return TRUE on success, else false
 */
static BOOL setCurrentPrivilege(LPCTSTR privilege, BOOL bEnablePrivilege) {
	HANDLE hToken;
	TOKEN_PRIVILEGES tp;
	LUID luid;
	TOKEN_PRIVILEGES tpPrevious;
	DWORD cbPrevious = sizeof(TOKEN_PRIVILEGES);
	BOOL bSuccess = FALSE;

	if (LookupPrivilegeValue(NULL, privilege, &luid) == 0) return FALSE;	
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken) == 0) return FALSE;

	tp.PrivilegeCount           = 1;
	tp.Privileges[0].Luid       = luid;
	tp.Privileges[0].Attributes = 0;

	AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &tpPrevious, &cbPrevious);

	if (GetLastError() == ERROR_SUCCESS) {
		/* try again with privilege based on previous setting */
		tpPrevious.PrivilegeCount     = 1;
		tpPrevious.Privileges[0].Luid = luid;

		if (bEnablePrivilege != FALSE) {
			tpPrevious.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
		} else {
			tpPrevious.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED & tpPrevious.Privileges[0].Attributes);
		}

		AdjustTokenPrivileges(hToken, FALSE, &tpPrevious, cbPrevious, NULL, NULL);
		if (GetLastError() == ERROR_SUCCESS) bSuccess=TRUE;
	}

	CloseHandle(hToken);

	return bSuccess;
}


/**
 * Checks if the given path exists and is not a directory.
 * 
 * @param[in] src - check this path
 * @return 1 if src exists and is not a directory, else 0
 */
int isFile(const TCHAR * src) {
	DWORD dwAttrib = GetFileAttributes(src);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY) == 0) ? 1 : 0;
}


/**
 * Checks if the given path exists and is a directory.
 * 
 * @param[in] src - check this path
 * @return 1 if src is a directory, else 0
 */
int isDirectory(const TCHAR * src) {
	DWORD dwAttrib = GetFileAttributes(src);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY) != 0) ? 1 : 0;
}


/**
 * Creates the passed directory path recursively.
 * 
 * @param[in] dst - destination path
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int createDirectory(const TCHAR * dst, const int verbose) {
	if (dst == NULL || *dst == 0) return 0;
	int result = 0;
	const size_t len = _tcslen(dst);
	if (dst[1] == _T(':') && len < 4) return 1;
	TCHAR * end = NULL; /* pointer to the end of the current path part */
	TCHAR * dir = (TCHAR *)malloc(sizeof(TCHAR) * (len + 1));
	memcpy(dir, dst, sizeof(TCHAR) * len);
	dir[len] = 0;
	if (dir == NULL) {
		if (verbose > 0) {
			_ftprintf(
				stderr,
				_T("Error: Failed to allocate %u bytes of memory.\n"),
				(unsigned)(sizeof(TCHAR) * _tcslen(dst))
			);
		}
		goto onError;
	}
	for (;;) {
		if (end == NULL) {
			if (dir[1] == _T(':')) {
				end = _tcspbrk(dir + 4, PATH_SEPS); /* absolute path */
			} else {
				end = _tcspbrk(dir, PATH_SEPS); /* relative path */
			}
		} else {
			*end = _T('\\');
			end = _tcspbrk(end + 1, PATH_SEPS);
		}
		if (isDirectory(dir) == 0) {
			if (CreateDirectory(dir, NULL) == 0) {
				if (verbose > 0) printLastError(dir, _T("CreateDirectory():")_T2(TO_STR2(__LINE__)));
				goto onError;
			}
			if (verbose > 1) {
				_ftprintf(stdout, _T("Created directory \"%s\".\n"), dir);
			}
		}
		if (end == NULL) break;
	}
	result = 1;
onError:
	if (dir != NULL) free(dir);
	return result;
}


/**
 * Creates a hard-link for the destination pointing to the given path at source.
 * The function overwrites the destination file or hardlink.
 * 
 * @param[in] src - source path
 * @param[in] dst - destination path
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int createHardLink(const TCHAR * src, const TCHAR * dst, const int verbose) {
	if (src == NULL || dst == NULL) return 0;
	if (isFile(dst) != 0) {
		if (DeleteFile(dst) == 0) {
			if (verbose > 0) printLastError(dst, _T("DeleteFile():")_T2(TO_STR2(__LINE__)));
			return 0;
		}
	}
	if (CreateHardLink(dst, src, NULL) == 0) {
		if (verbose > 0) printLastError(dst, _T("CreateHardLink():")_T2(TO_STR2(__LINE__)));
		return 0;
	}
	if (verbose > 1) {
		_ftprintf(stdout, _T("Created hardlink \"%s\" pointing to \"%s\".\n"), dst, src);
	}
	return 1;
}


/**
 * Copies the source file to the destination file. The destination needs to be a file path.
 * The function overwrites the destination file or hardlink.
 * 
 * @param[in] src - source file
 * @param[in] dst - destination file
 * @param[in] mask - copy mask
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int copyFile(const TCHAR * src, const TCHAR * dst, const tCopyMask mask, const int verbose) {
	if (src == NULL || dst == NULL) return 0;
	if (isFile(dst) != 0) {
		if (DeleteFile(dst) == 0) {
			if (verbose > 0) printLastError(dst, _T("DeleteFile():")_T2(TO_STR2(__LINE__)));
			return 0;
		}
	}
	DWORD dwCopyFlags = (((mask & CP_LINKS) != 0) ? COPY_FILE_COPY_SYMLINK : 0) | COPY_FILE_NO_BUFFERING;
	if (LOBYTE(LOWORD(GetVersion())) < 6) dwCopyFlags = 0; /* before Vista */
	if (CopyFileEx(src, dst, NULL, NULL, FALSE, dwCopyFlags) == 0) {
		if (verbose > 0) printLastError(dst, _T("CopyFileEx():")_T2(TO_STR2(__LINE__)));
		return 0;
	}
	if (verbose > 1) {
		_ftprintf(stdout, _T("Copied file \"%s\" to \"%s\".\n"), src, dst);
	}
	return 1;
}


/**
 * Copies the path attributes and security settings for the given path to the destination path
 * according to the mask passed.
 * 
 * @param[in] src - source path
 * @param[in] dst - destination path
 * @param[in] mask - copy mask
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int copyAttributes(const TCHAR * src, const TCHAR * dst, const tAttrMask mask, const int verbose) {
	static BOOL hasSecurityPrivilege = FALSE;
	if (src == NULL || dst == NULL) return 0;
	if (mask == AT_NONE) return 1;
	int result = 0;
	SECURITY_INFORMATION flags = 0;
	PSID owner, group;
	PACL dacl;
	HANDLE file = INVALID_HANDLE_VALUE;
	FILETIME times[3];
	const size_t len = _tcslen(dst);
	char buffer[4096];
	DWORD neededLength;
	TCHAR * dstCpy = (TCHAR *)malloc(sizeof(TCHAR) * (len + 1));
	if (dstCpy == NULL) {
		if (verbose > 0) _ftprintf(stderr, _T("Error: Failed to allocate %u bytes.\n"), (unsigned)(sizeof(TCHAR) * (len + 1)));
		goto onError;
	}
	memcpy(dstCpy, dst, sizeof(TCHAR) * len);
	dstCpy[len] = 0;
	ZeroMemory(&owner, sizeof(owner));
	ZeroMemory(&group, sizeof(group));
	ZeroMemory(&dacl, sizeof(dacl));
	if ((mask & AT_GROUP) != 0) flags = flags | GROUP_SECURITY_INFORMATION;
	if ((mask & AT_OWNER) != 0) flags = flags | OWNER_SECURITY_INFORMATION;
	if ((mask & AT_PERMS) != 0) flags = flags | DACL_SECURITY_INFORMATION;
	if ((mask & AT_ALL) == AT_ALL) flags = BACKUP_SECURITY_INFORMATION;
	/* copy security information */
	if (hasSecurityPrivilege != TRUE) {
		hasSecurityPrivilege = setCurrentPrivilege(SE_SECURITY_NAME, TRUE);
	}
	if (GetFileSecurity(src, flags, buffer, sizeof(buffer), &neededLength) != ERROR_SUCCESS) {
		if (verbose > 0) printLastError(src, _T("GetFileSecurity():")_T2(TO_STR2(__LINE__)));
		goto onError;
	}
	if (SetFileSecurity(dstCpy, flags, buffer) != ERROR_SUCCESS) {
		if (verbose > 0) printLastError(dstCpy, _T("SetFileSecurity():")_T2(TO_STR2(__LINE__)));
		goto onError;
	}
	/* copy file times */
	if (isDirectory(src) == 0) {
		file = CreateFile(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE) {
			if (verbose > 0) printLastError(src, _T("CreateFile():")_T2(TO_STR2(__LINE__)));
			goto onError;
		}
		if (GetFileTime(file, times, times + 1, times + 2) == 0) {
			if (verbose > 0) printLastError(src, _T("GetFileTime():")_T2(TO_STR2(__LINE__)));
			goto onError;
		}
		CloseHandle(file);
		file = CreateFile(dst, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE) {
			if (verbose > 0) printLastError(dst, _T("CreateFile():")_T2(TO_STR2(__LINE__)));
			goto onError;
		}
		if (SetFileTime(file, times, times + 1, times + 2) == 0) {
			if (verbose > 0) printLastError(dst, _T("SetFileTime():")_T2(TO_STR2(__LINE__)));
			goto onError;
		}
	}
	if (verbose > 1) {
		_ftprintf(stdout, _T("Copied attributes from file \"%s\" to \"%s\".\n"), src, dst);
	}
	result = 1;
onError:
	if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
	if (dstCpy != NULL) free(dstCpy);
	return result;
}


/**
 * Compares source file against destination file to find out which file was
 * modified more recently.
 * 
 * @param[in] src - older file
 * @param[in] dst - newer file
 * @param[in] verbose - verbosity level
 * @return 1 if dst exists and is newer than src, 0 else and -1 on error
 */
int isNewerFile(const TCHAR * src, const TCHAR * dst, const int verbose) {
	if (src == NULL || dst == NULL) return -1;
	int result = -1;
	HANDLE file = INVALID_HANDLE_VALUE;
	FILETIME srcTime, dstTime;
	LARGE_INTEGER srcSize, dstSize;
	file = CreateFile(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		if (verbose > 0) printLastError(src, _T("CreateFile():")_T2(TO_STR2(__LINE__)));
		goto onError;
	}
	if (GetFileTime(file, NULL, NULL, &srcTime) == 0) {
		if (verbose > 0) printLastError(src, _T("GetFileTime():")_T2(TO_STR2(__LINE__)));
		goto onError;
	}
	if (GetFileSizeEx(file, &srcSize) == 0) {
		if (verbose > 0) printLastError(src, _T("GetFileSizeEx():")_T2(TO_STR2(__LINE__)));
		goto onError;
	}
	CloseHandle(file);
	file = CreateFile(dst, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		result = 0;
		goto onError;
	}
	if (GetFileTime(file, NULL, NULL, &dstTime) == 0) {
		if (verbose > 0) printLastError(dst, _T("GetFileTime():")_T2(TO_STR2(__LINE__)));
		goto onError;
	}
	if (GetFileSizeEx(file, &dstSize) == 0) {
		if (verbose > 0) printLastError(dst, _T("GetFileSizeEx():")_T2(TO_STR2(__LINE__)));
		goto onError;
	}
	result = 0;
	if (srcSize.QuadPart == dstSize.QuadPart) {
		if (srcTime.dwHighDateTime < dstTime.dwHighDateTime) {
			result = 1;
		} else if (srcTime.dwHighDateTime == dstTime.dwHighDateTime) {
			if (srcTime.dwLowDateTime < dstTime.dwLowDateTime) result = 1;
		}
	} else {
		result = 1;
	}
onError:
	if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
	return result;
}
