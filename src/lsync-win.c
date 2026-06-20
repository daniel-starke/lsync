/**
 * @file lsync-win.c
 * @author Daniel Starke
 * @date 2017-05-22
 * @version 2026-06-19
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


#ifndef COPY_FILE_FAIL_IF_EXISTS
#define COPY_FILE_FAIL_IF_EXISTS 0x00000001
#endif


#ifndef COPY_FILE_COPY_SYMLINK
#define COPY_FILE_COPY_SYMLINK 0x00000800
#endif


#ifndef COPY_FILE_NO_BUFFERING
#define COPY_FILE_NO_BUFFERING 0x00001000
#endif


#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK 0xA000000CL
#endif


#ifndef FSCTL_GET_REPARSE_POINT
#define FSCTL_GET_REPARSE_POINT 0x000900A8
#endif


#ifndef FILE_FLAG_OPEN_REPARSE_POINT
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000
#endif


#ifndef MAXIMUM_REPARSE_DATA_BUFFER_SIZE
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE 16384
#endif


#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#endif


#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif


/** Layout of REPARSE_DATA_BUFFER for an IO_REPARSE_TAG_SYMLINK reparse point. */
typedef struct {
	DWORD ReparseTag;
	WORD  ReparseDataLength;
	WORD  Reserved;
	WORD  SubstituteNameOffset;
	WORD  SubstituteNameLength;
	WORD  PrintNameOffset;
	WORD  PrintNameLength;
	ULONG Flags;
	WCHAR PathBuffer[1];
} tSymlinkReparse;


/**
 * Writes the last Windows API error messages to standard error.
 * 
 * @param[in] obj - object that would had been created/modified
 * @param[in] msg - prefix output with this message
 */
static void printLastError(const TCHAR * obj, const TCHAR * msg) {
	DWORD lastError;
	DWORD bytesReturned;
	TCHAR outBuf[2048];
	lastError = GetLastError();
	bytesReturned = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lastError, LANG_USER_DEFAULT, outBuf, 2048, NULL);
	if (bytesReturned > 0) {
		_ftprintf(stderr, _T("%s:%s: %s"), obj, msg, outBuf);
	} else {
		_ftprintf(stderr, _T("%s:%s: Error %i while formatting error %i.\n"), obj, msg, (int)GetLastError(), (int)lastError);
	}
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
 * Checks if the given path is a reparse point (symlink or junction).
 *
 * @param[in] src - check this path
 * @return 1 if src is a reparse point, else 0
 */
int isSymlink(const TCHAR * src) {
	DWORD dwAttrib = GetFileAttributes(src);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_REPARSE_POINT) != 0) ? 1 : 0;
}


/**
 * Resolves the given path in its canonical absolute form.
 *
 * @param[in] path - path to resolve
 * @param[out] buf - buffer receiving the canonical path
 * @param[in] len - size of buf in characters
 * @return 1 on success, 0 on failure
 */
int realPath(const TCHAR * path, TCHAR * buf, const size_t len) {
	const DWORD n = GetFullPathName(path, (DWORD)len, buf, NULL);
	return (n > 0 && n < (DWORD)len) ? 1 : 0;
}


/**
 * Skip the "server\share" portion of a UNC path. The given pointer must point just past
 * the leading "\\" (or the "\\?\UNC\") prefix.
 *
 * @param[in] ptr - pointer to behind the UNC prefix
 * @return pointer behind the share component (and its trailing separator, if present)
 */
static const TCHAR * skipUncShare(const TCHAR * ptr) {
	int i;
	for (i = 0; i < 2; i++) {
		while (*ptr != 0 && *ptr != _T('\\') && *ptr != _T('/')) ptr++; /* server / share component */
		if (*ptr != 0) ptr++; /* skip separator */
	}
	return ptr;
}


/**
 * Returns the length of the root prefix of a Windows path, i.e. drive specifier, UNC share
 * or extended length / device prefix.
 *
 * @param[in] path - path to check
 * @return number of root prefix characters
 */
static size_t rootPrefixLen(const TCHAR * path) {
	const TCHAR * p = path;
	if ((p[0] == _T('\\') || p[0] == _T('/')) && (p[1] == _T('\\') || p[1] == _T('/'))) {
		/* extended length / device prefix: \\?\ or \\.\ */
		if ((p[2] == _T('?') || p[2] == _T('.')) && (p[3] == _T('\\') || p[3] == _T('/'))) {
			p += 4;
			/* \\?\UNC\server\share */
			if ((p[0] == _T('U') || p[0] == _T('u')) && (p[1] == _T('N') || p[1] == _T('n'))
				&& (p[2] == _T('C') || p[2] == _T('c')) && (p[3] == _T('\\') || p[3] == _T('/'))) {
				return (size_t)(skipUncShare(p + 4) - path);
			}
			/* \\?\C:\ */
			if (p[0] != 0 && p[1] == _T(':')) {
				p += 2;
				if (*p == _T('\\') || *p == _T('/')) p++;
			}
			return (size_t)(p - path);
		}
		/* UNC path: \\server\share */
		return (size_t)(skipUncShare(p + 2) - path);
	}
	/* drive specifier: X:\ or X: (drive-relative) */
	if (p[0] != 0 && p[1] == _T(':')) {
		p += 2;
		if (*p == _T('\\') || *p == _T('/')) p++;
		return (size_t)(p - path);
	}
	/* rooted on the current drive: \foo */
	if (p[0] == _T('\\') || p[0] == _T('/')) return 1;
	/* relative path */
	return 0;
}


/**
 * Removes the given path. Directories are deleted recursively. Directory
 * reparse point (junction or symlink) is unlinked without touching its target.
 *
 * @param[in] path - path to remove
 * @param[in] verbose - verbosity level
 * @return 1 on success (or if the path does not exist), 0 on failure
 */
static int removePath(const TCHAR * path, const int verbose) {
	const DWORD attr = GetFileAttributes(path);
	if (attr == INVALID_FILE_ATTRIBUTES) return 1; /* nothing to remove */
	if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		if ((attr & FILE_ATTRIBUTE_READONLY) != 0) {
			SetFileAttributes(path, attr & (~((DWORD)FILE_ATTRIBUTE_READONLY)));
		}
		if (DeleteFile(path) == 0) {
			if (verbose > 0) printLastError(path, _T("DeleteFile():")_T2(TO_STR2(__LINE__)));
			return 0;
		}
		return 1;
	}
	/* recurse into a real directory to remove reparse point */
	if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
		HANDLE dp;
		WIN32_FIND_DATA item;
		int result = 1;
		const size_t len = _tcslen(path);
		const size_t patLen = len + 3; /* separator + '*' + NUL */
		TCHAR * pattern = (TCHAR *)malloc(sizeof(TCHAR) * patLen);
		if (pattern == NULL) return 0;
		_sntprintf(pattern, patLen, _T("%s")_T2(PCF_PATH_SEP)_T("*"), path);
		pattern[patLen - 1] = 0;
		dp = FindFirstFile(pattern, &item);
		free(pattern);
		if (dp == INVALID_HANDLE_VALUE) {
			if (verbose > 0) printLastError(path, _T("FindFirstFile():")_T2(TO_STR2(__LINE__)));
			return 0;
		}
		do {
			if (_tcscmp(item.cFileName, _T(".")) == 0 || _tcscmp(item.cFileName, _T("..")) == 0) continue;
			const size_t childLen = len + 1 + _tcslen(item.cFileName) + 1;
			TCHAR * child = (TCHAR *)malloc(sizeof(TCHAR) * childLen);
			if (child == NULL) {
				result = 0;
				break;
			}
			_sntprintf(child, childLen, _T("%s")_T2(PCF_PATH_SEP)_T("%s"), path, item.cFileName);
			child[childLen - 1] = 0;
			if (removePath(child, verbose) == 0) result = 0;
			free(child);
			if (result == 0) break;
		} while (FindNextFile(dp, &item) != 0);
		FindClose(dp);
		if (result == 0) return 0;
	}
	if (RemoveDirectory(path) == 0) {
		if (verbose > 0) printLastError(path, _T("RemoveDirectory():")_T2(TO_STR2(__LINE__)));
		return 0;
	}
	return 1;
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
	/* nothing to create if the path is only a root prefix (drive, UNC share, ...) */
	if (rootPrefixLen(dst) >= len) return 1;
	TCHAR * end = NULL; /* pointer to the end of the current path part */
	TCHAR * dir = (TCHAR *)malloc(sizeof(TCHAR) * (len + 1));
	if (dir == NULL) {
		if (verbose > 0) {
			_ftprintf(
				stderr,
				_T("Error: Failed to allocate %u bytes of memory.\n"),
				(unsigned)(sizeof(TCHAR) * (len + 1))
			);
		}
		goto onError;
	}
	memcpy(dir, dst, sizeof(TCHAR) * len);
	dir[len] = 0;
	for (;;) {
		if (end == NULL) {
			/* skip the root prefix of an absolute path */
			end = _tcspbrk(dir + rootPrefixLen(dir), PATH_SEPS);
		} else {
			*end = _T('\\'); /* restore the separator from previous iteration */
			end = _tcspbrk(end + 1, PATH_SEPS);
		}
		if (end != NULL) *end = 0; /* limit `dir` to current path prefix */
		if (isDirectory(dir) == 0) {
			/* replace non-directory at this path */
			if (GetFileAttributes(dir) != INVALID_FILE_ATTRIBUTES) {
				if (removePath(dir, verbose) == 0) goto onError;
			}
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
 * Reserves a unique temporary file name next to `dst` (same directory and volume) so it can later
 * be renamed into place atomically. The allocated name is stored in `tmp` for the caller to free.
 *
 * @param[in] dst - final destination path the temporary belongs to
 * @param[out] tmp - receives the allocated temporary path (NULL on failure)
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int createTempName(const TCHAR * dst, TCHAR ** tmp, const int verbose) {
	const size_t len = _tcslen(dst);
	const size_t bufLen = len + 16; /* dst + ".tmpXXXXXXXX" + NUL */
	TCHAR * tmpPath;
	unsigned int seed;
	int attempt;
	*tmp = NULL;
	tmpPath = (TCHAR *)malloc(sizeof(TCHAR) * bufLen);
	if (tmpPath == NULL) {
		if (verbose > 0) _ftprintf(stderr, _T("Error: Failed to allocate %u bytes.\n"), (unsigned)(sizeof(TCHAR) * bufLen));
		return 0;
	}
	seed = (unsigned int)(GetTickCount() ^ (GetCurrentProcessId() << 16) ^ GetCurrentThreadId());
	for (attempt = 0; attempt < 32768; attempt++) {
		seed = (seed * 1103515245u) + 12345u;
		_sntprintf(tmpPath, bufLen, _T("%s.tmp%08x"), dst, (unsigned)seed);
		tmpPath[bufLen - 1] = 0;
		if (GetFileAttributes(tmpPath) == INVALID_FILE_ATTRIBUTES) {
			const DWORD err = GetLastError();
			if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
				*tmp = tmpPath;
				return 1;
			}
		}
	}
	if (verbose > 0) _ftprintf(stderr, _T("Error: Failed to find a free temporary name for \"%s\".\n"), dst);
	free(tmpPath);
	return 0;
}


/**
 * Atomically rename src to dst, replacing any existing destination.
 *
 * @param[in] src - path to rename
 * @param[in] dst - destination path
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int renameFile(const TCHAR * src, const TCHAR * dst, const int verbose) {
	if (MoveFileEx(src, dst, MOVEFILE_REPLACE_EXISTING) == 0) {
		if (verbose > 0) printLastError(dst, _T("MoveFileEx():")_T2(TO_STR2(__LINE__)));
		return 0;
	}
	return 1;
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
	int result = 0;
	TCHAR * tmpPath = NULL;
	/* link under a temporary name and rename it into place so a failed link never
	 * destroys the existing destination (atomic replace, like the copy path) */
	if (createTempName(dst, &tmpPath, verbose) == 0) return 0;
	if (CreateHardLink(tmpPath, src, NULL) == 0) {
		if (verbose > 0) printLastError(tmpPath, _T("CreateHardLink():")_T2(TO_STR2(__LINE__)));
		goto onError;
	}
	if (renameFile(tmpPath, dst, verbose) == 0) goto onError;
	result = 1;
	if (verbose > 1) {
		_ftprintf(stdout, _T("Created hardlink \"%s\" pointing to \"%s\".\n"), dst, src);
	}
onError:
	if (result == 0) DeleteFile(tmpPath); /* discard the temp link on failure */
	free(tmpPath);
	return result;
}


/**
 * Recreates the symlink at `src` as a symlink at `dst` (best effort). Reading the
 * reparse point works on Windows 2000 and newer, but creating a symlink needs Windows
 * Vista or newer plus the create symlink privilege (or developer mode). The link is skipped
 * with a notice rather than copying the link target's content if this is unavailable.
 *
 * @param[in] src - source symlink
 * @param[in] dst - destination path
 * @param[in] verbose - verbosity level
 * @return 1 on success or skip, 0 on failure
 */
static int copySymbolicLink(const TCHAR * src, const TCHAR * dst, const int verbose) {
	typedef BOOLEAN (WINAPI * tCreateSymbolicLink)(LPCTSTR, LPCTSTR, DWORD);
	/* resolved dynamically so the executable still loads on Windows XP */
	static tCreateSymbolicLink createSymbolicLink = NULL;
	static int resolved = 0;
	HANDLE file;
	DWORD got;
	BY_HANDLE_FILE_INFORMATION info;
	int isDir;
	union {
		tSymlinkReparse data;
		char raw[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
	} buf;
	WCHAR target[MAX_PATH];
	WORD nameLen, nameOff;
	if (resolved == 0) {
		union {
			FARPROC proc;
			tCreateSymbolicLink func;
		} conv;
		resolved = 1;
		conv.proc = GetProcAddress(
			GetModuleHandle(_T("kernel32.dll")),
#ifdef UNICODE
			"CreateSymbolicLinkW"
#else
			"CreateSymbolicLinkA"
#endif
		);
		createSymbolicLink = conv.func;
	}
	/* read the reparse point to obtain the link target without following it */
	file = CreateFile(src, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		if (verbose > 0) printLastError(src, _T("CreateFile():")_T2(TO_STR2(__LINE__)));
		return 0;
	}
	/* avoid resolving possibly dangling or cross type target */
	if (GetFileInformationByHandle(file, &info) == 0) {
		if (verbose > 0) printLastError(src, _T("GetFileInformationByHandle():")_T2(TO_STR2(__LINE__)));
		CloseHandle(file);
		return 0;
	}
	isDir = ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) ? 1 : 0;
	if (DeviceIoControl(file, FSCTL_GET_REPARSE_POINT, NULL, 0, &buf, sizeof(buf), &got, NULL) == 0) {
		if (verbose > 0) printLastError(src, _T("DeviceIoControl():")_T2(TO_STR2(__LINE__)));
		CloseHandle(file);
		return 0;
	}
	CloseHandle(file);
	if (buf.data.ReparseTag != IO_REPARSE_TAG_SYMLINK) {
		/* not a symlink (e.g. a junction) -> skip */
		if (verbose > 0) _ftprintf(stderr, _T("Skipping reparse point \"%s\" (not a symbolic link).\n"), src);
		return 1;
	}
	if (buf.data.PrintNameLength > 0) {
		nameLen = buf.data.PrintNameLength;
		nameOff = buf.data.PrintNameOffset;
	} else {
		nameLen = buf.data.SubstituteNameLength;
		nameOff = buf.data.SubstituteNameOffset;
	}
	/* validate nameOff/nameLen against the returned data to prevent an out-of-bounds read */
	const size_t pathBufOff = (size_t)((const char *)buf.data.PathBuffer - (const char *)&buf);
	if ((size_t)got < pathBufOff || ((size_t)nameOff + (size_t)nameLen) > ((size_t)got - pathBufOff)) {
		if (verbose > 0) _ftprintf(stderr, _T("Error: Malformed reparse point \"%s\".\n"), src);
		return 0;
	}
	if ((((size_t)nameLen) / sizeof(WCHAR)) >= MAX_PATH) {
		if (verbose > 0) _ftprintf(stderr, _T("Error: Symbolic link target too long: \"%s\".\n"), src);
		return 0;
	}
	memcpy(target, ((const char *)buf.data.PathBuffer) + nameOff, nameLen);
	target[nameLen / sizeof(WCHAR)] = 0;
	/* strip NT object prefix "\??\" from SubstituteName */
	if (target[0] == L'\\' && target[1] == L'?' && target[2] == L'?' && target[3] == L'\\') {
		memmove(target, target + 4, (wcslen(target + 4) + 1) * sizeof(WCHAR));
	}
	if (createSymbolicLink == NULL) {
		if (verbose > 0) _ftprintf(stderr, _T("Skipping symbolic link \"%s\" (creating symbolic links requires Windows Vista or newer).\n"), src);
		return 1;
	}
	/* create the link under a temporary name and rename it so that failed creation
	 * never destroys the existing destination (atomic replace) */
	const DWORD dwFlags = (isDir != 0) ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
	TCHAR * tmpPath = NULL;
	if (createTempName(dst, &tmpPath, verbose) == 0) return 0;
	/* try the unprivileged create flag first (Windows 10 1703+ with developer mode), then
	 * without it for older systems that reject the unknown flag */
	if ((*createSymbolicLink)(tmpPath, target, (DWORD)(dwFlags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) == 0
		&& (*createSymbolicLink)(tmpPath, target, dwFlags) == 0) {
		if (verbose > 0) {
			printLastError(tmpPath, _T("CreateSymbolicLink():")_T2(TO_STR2(__LINE__)));
			_ftprintf(stderr, _T("Skipping symbolic link \"%s\" (could not be created; may require privileges).\n"), src);
		}
		free(tmpPath);
		return 1; /* graceful skip, destination left untouched */
	}
	/* new link exists -> safe to replace the destination */
	if (GetFileAttributes(dst) != INVALID_FILE_ATTRIBUTES) {
		if (removePath(dst, verbose) == 0) {
			if (isDir != 0) RemoveDirectory(tmpPath); else DeleteFile(tmpPath);
			free(tmpPath);
			return 0;
		}
	}
	if (renameFile(tmpPath, dst, verbose) == 0) {
		if (isDir != 0) RemoveDirectory(tmpPath); else DeleteFile(tmpPath);
		free(tmpPath);
		return 0;
	}
	free(tmpPath);
	if (verbose > 1) {
		_ftprintf(stdout, _T("Copied symbolic link \"%s\" to \"%s\".\n"), src, dst);
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
	if (isSymlink(src) != 0) {
		if ((mask & CP_LINKS) != 0) {
			return copySymbolicLink(src, dst, verbose);
		}
		if (verbose > 0) _ftprintf(stderr, _T("Skipping symbolic link \"%s\" (requires --links).\n"), src);
		return 1;
	}
	int result = 0;
	TCHAR * tmpPath = NULL;
	/* copy to a temporary file first then atomically replace the destination so
	 * a failed copy never destroys the existing destination file */
	if (createTempName(dst, &tmpPath, verbose) == 0) return 0;
	/* symlinks are handled above -> only regular files reach this point */
	/* COPY_FILE_NO_BUFFERING is Vista+; COPY_FILE_FAIL_IF_EXISTS makes the copy fail-closed
	 * so a process racing for the temporary name cannot have its file silently overwritten */
	DWORD dwCopyFlags = COPY_FILE_FAIL_IF_EXISTS | ((LOBYTE(LOWORD(GetVersion())) < 6) ? 0 : COPY_FILE_NO_BUFFERING);
	if (CopyFileEx(src, tmpPath, NULL, NULL, FALSE, dwCopyFlags) == 0) {
		if (verbose > 0) printLastError(tmpPath, _T("CopyFileEx():")_T2(TO_STR2(__LINE__)));
		goto onError;
	}
	/* replace a directory at destination path (type change) */
	if (isDirectory(dst) != 0) {
		if (removePath(dst, verbose) == 0) goto onError;
	}
	if (renameFile(tmpPath, dst, verbose) == 0) goto onError;
	result = 1;
	if (verbose > 1) {
		_ftprintf(stdout, _T("Copied file \"%s\" to \"%s\".\n"), src, dst);
	}
onError:
	if (result == 0) DeleteFile(tmpPath);
	free(tmpPath);
	return result;
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
	if (src == NULL || dst == NULL) return 0;
	if (mask == AT_NONE) return 1;
	int result = 0;
	SECURITY_INFORMATION flags = 0;
	PSID owner, group;
	PACL dacl;
	HANDLE file = INVALID_HANDLE_VALUE;
	FILETIME times[3];
	const int isLink = (isSymlink(src) != 0);
	const DWORD reparseFlag = (isLink != 0) ? FILE_FLAG_OPEN_REPARSE_POINT : 0;
	const size_t len = _tcslen(dst);
	char buffer[4096];
	DWORD neededLength = 0;
	PSECURITY_DESCRIPTOR sd = (PSECURITY_DESCRIPTOR)buffer;
	void * sdAlloc = NULL;
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
	/* copy security information and avoid rewriting link target's ACLs */
	if (flags != 0 && isLink == 0) {
		if (GetFileSecurity(src, flags, sd, sizeof(buffer), &neededLength) == 0) {
			/* descriptor did not fit into stack buffer: retry with exact heap buffer size */
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
				if (verbose > 0) printLastError(src, _T("GetFileSecurity():")_T2(TO_STR2(__LINE__)));
				goto onError;
			}
			sdAlloc = malloc(neededLength);
			if (sdAlloc == NULL) {
				if (verbose > 0) _ftprintf(stderr, _T("Error: Failed to allocate %u bytes.\n"), (unsigned)neededLength);
				goto onError;
			}
			sd = (PSECURITY_DESCRIPTOR)sdAlloc;
			if (GetFileSecurity(src, flags, sd, neededLength, &neededLength) == 0) {
				if (verbose > 0) printLastError(src, _T("GetFileSecurity():")_T2(TO_STR2(__LINE__)));
				goto onError;
			}
		}
		if (SetFileSecurity(dstCpy, flags, sd) == 0) {
			if (verbose > 0) printLastError(dstCpy, _T("SetFileSecurity():")_T2(TO_STR2(__LINE__)));
			goto onError;
		}
	}
	/* copy file times */
	if ((mask & AT_TIMES) != 0) {
		file = CreateFile(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | reparseFlag, NULL);
		if (file == INVALID_HANDLE_VALUE) {
			if (verbose > 0) printLastError(src, _T("CreateFile():")_T2(TO_STR2(__LINE__)));
			goto onError;
		}
		if (GetFileTime(file, times, times + 1, times + 2) == 0) {
			if (verbose > 0) printLastError(src, _T("GetFileTime():")_T2(TO_STR2(__LINE__)));
			goto onError;
		}
		CloseHandle(file);
		file = CreateFile(dst, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | reparseFlag, NULL);
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
	if (sdAlloc != NULL) free(sdAlloc);
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
 * @return 1 if `dst` differs from `src` (changed), 0 if unchanged and -1 on error
 */
int isNewerFile(const TCHAR * src, const TCHAR * dst, const int verbose) {
	if (src == NULL || dst == NULL) return -1;
	int result = -1;
	HANDLE file = INVALID_HANDLE_VALUE;
	FILETIME srcTime, dstTime;
	LARGE_INTEGER srcSize, dstSize;
	ULARGE_INTEGER srcStamp, dstStamp;
	const DWORD srcFlags = (isSymlink(src) != 0) ? (FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS) : FILE_ATTRIBUTE_NORMAL;
	file = CreateFile(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, srcFlags, NULL);
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
	const DWORD dstFlags = (isSymlink(dst) != 0) ? (FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS) : FILE_ATTRIBUTE_NORMAL;
	file = CreateFile(dst, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, dstFlags, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		const DWORD err = GetLastError();
		if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
			if (verbose > 0) printLastError(dst, _T("CreateFile():")_T2(TO_STR2(__LINE__)));
			result = 1;
		} else {
			result = 0;
		}
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
	srcStamp.LowPart = srcTime.dwLowDateTime;
	srcStamp.HighPart = srcTime.dwHighDateTime;
	dstStamp.LowPart = dstTime.dwLowDateTime;
	dstStamp.HighPart = dstTime.dwHighDateTime;
	result = 0;
	if (srcSize.QuadPart == dstSize.QuadPart) {
		/* compare modification time with one second granularity (100ns FILETIME units per
		 * second) to match the POSIX backend and tolerate coarser filesystem timestamps */
		if ((srcStamp.QuadPart / 10000000ULL) != (dstStamp.QuadPart / 10000000ULL)) {
			result = 1;
		}
	} else {
		result = 1;
	}
onError:
	if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
	return result;
}
