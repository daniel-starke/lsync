/**
 * @file tdir.i
 * @author Daniel Starke
 * @see tdirs.h
 * @see tdirus.h
 * @date 2012-12-15
 * @version 2026-09-14
 * @internal This file is never used or compiled directly but only included.
 * @remarks Define TDIR_UNICODE for the wide character variant before including
 * this file. Defaults to the narrow character variant.
 * @remarks See TDIR_FUNC() for the remaining configuration macros.
 * @remarks Define TDIR_NO_VISTA to take the Windows XP code path even where the system
 * offers the later calls.
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "target.h"

#ifdef PCF_IS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else /* PCF_IS_NO_WIN */
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif /* PCF_IS_WIN */


#ifdef TDIR_UNICODE
#define TDIR_CHAR wchar_t
#define TDIR_T(x) L##x
#define TDIR_SLEN wcslen
#define TDIR_SCMP wcscmp
#define TDIR_SRCHR wcsrchr
#else /* ! TDIR_UNICODE */
#define TDIR_CHAR char
#define TDIR_T(x) x
#define TDIR_SLEN strlen
#define TDIR_SCMP strcmp
#define TDIR_SRCHR strrchr
#endif /* TDIR_UNICODE */


#ifdef PCF_IS_WIN
#define TDIR_SEP TDIR_T('\\')
/** Tests whether the given character ends a path element. */
#define TDIR_IS_SEP(c) ((c) == TDIR_T('\\') || (c) == TDIR_T('/'))
#else /* PCF_IS_NO_WIN */
#define TDIR_SEP TDIR_T('/')
/** Tests whether the given character ends a path element. A backslash is an
 * ordinary character in a POSIX name and must not pass for a separator here. */
#define TDIR_IS_SEP(c) ((c) == TDIR_T('/'))
#endif /* PCF_IS_WIN */


/** The wide character walk has no POSIX implementation. */
#if defined(TDIR_UNICODE) && ! defined(PCF_IS_WIN)
#define TDIR_UNSUPPORTED 1
#endif


#ifdef TDIR_UNSUPPORTED
#warning "Wide char directory listing is only supported for Windows."


/**
 * Traverses the given path by the specified options and notifies the passed
 * visitor on each processed item.
 *
 * @param[in] path - base path to process
 * @param[in] maxLevel - maximal level to traverse to (-1 for no limit)
 * @param[in] options - combination of the option enumeration elements by binary OR
 * @param[in] visitor - user defined callback function
 * @param[in,out] param - user defined parameter (passed to callback function)
 * @return 1 on success, 0 on user abort, -1 on error
 */
int TDIR_FUNC(const TDIR_CHAR * path, const int maxLevel, const int options, TDIR_VISITOR visitor, void * param) {
	PCF_UNUSED(path)
	PCF_UNUSED(maxLevel)
	PCF_UNUSED(options)
	PCF_UNUSED(visitor)
	PCF_UNUSED(param)
	return -1;
}
#else /* ! TDIR_UNSUPPORTED */


/** Defines the initial capacity of a path buffer in characters. */
#define TDIR_PATH_MIN 256


/**
 * Defines the number of levels to traverse at most. A tree this deep is past what the
 * targeted systems put in reach of an ordinary path, so the ceiling bounds the
 * recursion without shortening a walk that has real work below it. One level takes 304
 * bytes of stack on the POSIX walk and 656 and 896 bytes on the narrow and the wide
 * Windows walk, which holds the deepest walk below 188 KiB.
 */
#define TDIR_LEVEL_MAX 215


/** Defines the initial capacity of the link target set in elements. */
#define TDIR_SEEN_MIN 16


#if defined(DT_DIR) && defined(DT_UNKNOWN)
/** The directory entries carry a usable type field. */
#define TDIR_HAS_DTYPE 1
#endif


#ifdef PCF_IS_WIN
/** Defines the `FileIdInfo` element of `FILE_INFO_BY_HANDLE_CLASS`. */
#define TDIR_FILE_ID_INFO_CLASS 18

/** Defines the room `td_winPath()` keeps in front of a resolved path for its prefix. */
#define TDIR_LONG_ROOM 6


/**
 * Layout returned by GetFileInformationByHandleEx() for TDIR_FILE_ID_INFO_CLASS.
 * The build targets a Windows version whose headers still lack the type.
 */
typedef struct {
	ULONGLONG volumeSerialNumber; /**< volume serial number */
	BYTE fileId[16];              /**< 128-bit file ID */
} tTdirFileIdInfo;


/** Signature of `GetFileInformationByHandleEx()`, which Windows XP lacks. */
typedef BOOL(WINAPI * tTdirFileInfoExFn)(HANDLE, int, LPVOID, DWORD);


/**
 * Identity of a directory. The 64-bit file index of BY_HANDLE_FILE_INFORMATION is
 * not unique on ReFS, so the 128-bit file ID is preferred wherever the file system
 * provides one.
 */
typedef struct {
	uint64_t volSerial; /**< volume serial number */
	uint64_t idHigh;    /**< high part of the file ID (0 for a 64-bit file index) */
	uint64_t idLow;     /**< low part of the file ID */
} tTdirDirId;
#else /* PCF_IS_NO_WIN */
/** Identity of a directory. */
typedef struct {
	uint64_t dev; /**< device ID */
	uint64_t ino; /**< inode number */
} tTdirDirId;
#endif /* PCF_IS_WIN */


/**
 * Invariant arguments of the traversal recursion. Only the link target set changes. Each
 * link target is entered once, which bounds the walk and breaks cycles.
 */
typedef struct tTdirCtx {
	int maxLevel;         /**< maximal level to traverse to (`-1` for no limit) */
	int options;          /**< combination of the option enumeration elements */
	TDIR_VISITOR visitor; /**< user defined callback function */
	void * param;         /**< user defined callback parameter */
	tTdirDirId * seen;    /**< sorted identities of directories entered through a link */
	size_t seenCount;     /**< number of elements in `seen` */
	size_t seenCap;       /**< capacity of `seen` in elements */
#ifdef PCF_IS_WIN
	TDIR_CHAR * winPath; /**< conversion buffer of `td_winPath()` */
	size_t winCap;       /**< capacity of `winPath` in characters */
#endif /* PCF_IS_WIN */
} tTdirCtx;


/**
 * Compares two directory identities to give the link target set a total order.
 *
 * @param[in] lhs - left hand identity
 * @param[in] rhs - right hand identity
 * @return <0 if lhs < rhs, 0 if lhs == rhs, >0 if lhs > rhs
 */
static int td_idCmp(const tTdirDirId * lhs, const tTdirDirId * rhs) {
#ifdef PCF_IS_WIN
	if (lhs->volSerial != rhs->volSerial) return (lhs->volSerial < rhs->volSerial) ? -1 : 1;
	if (lhs->idHigh != rhs->idHigh) return (lhs->idHigh < rhs->idHigh) ? -1 : 1;
	if (lhs->idLow != rhs->idLow) return (lhs->idLow < rhs->idLow) ? -1 : 1;
#else /* PCF_IS_NO_WIN */
	if (lhs->dev != rhs->dev) return (lhs->dev < rhs->dev) ? -1 : 1;
	if (lhs->ino != rhs->ino) return (lhs->ino < rhs->ino) ? -1 : 1;
#endif /* PCF_IS_WIN */
	return 0;
}


#ifdef PCF_IS_WIN
/**
 * Retrieves the identity of the given directory by following links.
 *
 * @param[in] path - directory path
 * @param[out] id - receives the directory identity on success
 * @return 1 on success, 0 on error
 */
static int td_idFromPath(const TDIR_CHAR * path, tTdirDirId * id) {
	tTdirFileIdInfo fileId;
	BY_HANDLE_FILE_INFORMATION info;
	tTdirFileInfoExFn infoEx = NULL;
	int ok = 0;
	HANDLE h;
#ifndef TDIR_NO_VISTA
	/* resolved at runtime, since Windows XP lacks the function */
	const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
	if (kernel != NULL) infoEx = (tTdirFileInfoExFn)(void (*)(void))GetProcAddress(kernel, "GetFileInformationByHandleEx");
#endif /* not TDIR_NO_VISTA */
#ifdef TDIR_UNICODE
	h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#else /* ! TDIR_UNICODE */
	h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#endif /* TDIR_UNICODE */
	if (h == INVALID_HANDLE_VALUE) return 0;
	/* the file ID is opaque, only compared for equality and never interpreted */
	if (infoEx != NULL && infoEx(h, TDIR_FILE_ID_INFO_CLASS, &fileId, (DWORD)sizeof(fileId)) != 0) {
		id->volSerial = (uint64_t)(fileId.volumeSerialNumber);
		memcpy(&(id->idLow), fileId.fileId, sizeof(uint64_t));
		memcpy(&(id->idHigh), fileId.fileId + sizeof(uint64_t), sizeof(uint64_t));
		ok = 1;
	} else if (GetFileInformationByHandle(h, &info) != 0) {
		/* before Windows 8 and on a file system without a 128-bit ID -> 64-bit index */
		id->volSerial = (uint64_t)(info.dwVolumeSerialNumber);
		id->idHigh = 0;
		id->idLow = (((uint64_t)(info.nFileIndexHigh)) << 32) | ((uint64_t)(info.nFileIndexLow));
		ok = 1;
	}
	CloseHandle(h);
	return ok;
}
#else /* PCF_IS_NO_WIN */
/**
 * Converts the given file information into a directory identity.
 *
 * @param[in] st - file information of the directory
 * @param[out] id - receives the directory identity
 */
static void td_idFromStat(const struct stat * st, tTdirDirId * id) {
	id->dev = (uint64_t)(st->st_dev);
	id->ino = (uint64_t)(st->st_ino);
}

#endif /* PCF_IS_WIN */


/**
 * Tests whether the given level is still within the configured traversal limit. The
 * ceiling bounds the recursion, and with it the stack it needs, no matter which limit
 * the caller asked for.
 *
 * @param[in] ctx - traversal context
 * @param[in] level - level to test
 * @return 1 if the level may be traversed, else 0
 */
static int td_levelAllowed(const tTdirCtx * ctx, const unsigned int level) {
	if (level >= TDIR_LEVEL_MAX) return 0;
	return (ctx->maxLevel < 0 || level <= ((const unsigned int)(ctx->maxLevel))) ? 1 : 0;
}


/**
 * Returns the position the given identity has, or would have, in the link target set.
 *
 * @param[in] ctx - traversal context
 * @param[in] id - directory identity to look for
 * @return index of the first element not ordered before `id`
 */
static size_t td_seenLower(const tTdirCtx * ctx, const tTdirDirId * id) {
	size_t lo = 0;
	size_t hi = ctx->seenCount;
	while (lo < hi) {
		const size_t mid = lo + ((hi - lo) / 2);
		if (td_idCmp(ctx->seen + mid, id) < 0) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return lo;
}


/**
 * Claims the given directory for the walk. A link target is entered on the first
 * link that reaches it and refused on every later one, which bounds a link graph
 * and ends a cycle on its second lap.
 *
 * @param[in,out] ctx - traversal context
 * @param[in] id - identity of the link target
 * @return 1 if the directory may be entered
 * @return 0 if it was already entered through another link
 * @return -1 on allocation failure
 */
static int td_claimTarget(tTdirCtx * ctx, const tTdirDirId * id) {
	const size_t pos = td_seenLower(ctx, id);
	if (pos < ctx->seenCount && td_idCmp(ctx->seen + pos, id) == 0) return 0;
	if (ctx->seenCount >= ctx->seenCap) {
		const size_t newCap = (ctx->seenCap == 0) ? TDIR_SEEN_MIN : (ctx->seenCap * 2);
		if (newCap <= ctx->seenCap) return -1; /* number overflow (most unlikely) */
		tTdirDirId * grown = (tTdirDirId *)realloc(ctx->seen, newCap * sizeof(tTdirDirId));
		if (grown == NULL) return -1;
		ctx->seen = grown;
		ctx->seenCap = newCap;
	}
	if (pos < ctx->seenCount) {
		memmove(ctx->seen + pos + 1, ctx->seen + pos, (ctx->seenCount - pos) * sizeof(tTdirDirId));
	}
	ctx->seen[pos] = *id;
	ctx->seenCount++;
	return 1;
}


/**
 * Grows the given path buffer to hold at least the requested number of characters.
 * Only the recursion level owning the buffer ever grows it, and that level is
 * blocked while any descendant holds a pointer into it.
 *
 * @param[in,out] buf - path buffer to grow (may point to `NULL`)
 * @param[in,out] cap - capacity of `buf` in characters
 * @param[in] need - required capacity in characters
 * @return 1 on success, 0 on allocation failure or number overflow
 */
static int td_ensure(TDIR_CHAR ** buf, size_t * cap, const size_t need) {
	size_t newCap = (*cap == 0) ? TDIR_PATH_MIN : *cap;
	if (need <= *cap) return 1;
	while (newCap < need) {
		if (newCap > (SIZE_MAX / (2 * sizeof(TDIR_CHAR)))) return 0; /* number overflow (most unlikely) */
		newCap *= 2;
	}
	TDIR_CHAR * grown = (TDIR_CHAR *)realloc(*buf, newCap * sizeof(TDIR_CHAR));
	if (grown == NULL) return 0;
	*buf = grown;
	*cap = newCap;
	return 1;
}


#ifdef PCF_IS_WIN
/**
 * Returns the form of `path` a Win32 call accepts at any length. A path of `MAX_PATH` or
 * more becomes absolute with the `\\?\` or `\\?\UNC\` prefix. Others, and all narrow paths,
 * stay unchanged.
 *
 * @param[in,out] ctx - traversal context holding the conversion buffer
 * @param[in] path - path to convert
 * @return path to hand to the Win32 call, valid until the next call
 */
static const TDIR_CHAR * td_winPath(tTdirCtx * ctx, const TDIR_CHAR * path) {
#ifdef TDIR_UNICODE
	if (TDIR_IS_SEP(path[0]) && TDIR_IS_SEP(path[1]) && (path[2] == L'?' || path[2] == L'.') && TDIR_IS_SEP(path[3])) {
		return path; /* a name space that skips the resolution */
	}
	if (TDIR_SLEN(path) < MAX_PATH && ((TDIR_IS_SEP(path[0]) && TDIR_IS_SEP(path[1]))
		|| (path[0] != 0 && path[1] == L':' && TDIR_IS_SEP(path[2])))) {
		return path; /* an absolute path is as long as it is written */
	}
	const DWORD need = GetFullPathNameW(path, 0, NULL, NULL); /* size including the terminator */
	if (need <= MAX_PATH) return path; /* short enough or unresolvable -> left to the call */
	if (td_ensure(&(ctx->winPath), &(ctx->winCap), (size_t)need + TDIR_LONG_ROOM) == 0) return path;
	wchar_t * full = ctx->winPath + TDIR_LONG_ROOM;
	const DWORD got = GetFullPathNameW(path, need, full, NULL);
	if (got == 0 || got >= need || (TDIR_IS_SEP(full[0]) && TDIR_IS_SEP(full[1]) && (full[2] == L'?' || full[2] == L'.'))) {
		return path; /* unresolvable or a device name -> left to the call */
	}
	if (TDIR_IS_SEP(full[0]) && TDIR_IS_SEP(full[1])) {
		/* the prefix of a share takes the place of its first separator */
		memcpy(ctx->winPath, L"\\\\?\\UNC", 7 * sizeof(wchar_t));
		return ctx->winPath;
	}
	memcpy(full - 4, L"\\\\?\\", 4 * sizeof(wchar_t));
	return full - 4;
#else /* ! TDIR_UNICODE */
	PCF_UNUSED(ctx)
	return path;
#endif /* TDIR_UNICODE */
}
#endif /* PCF_IS_WIN */


/**
 * Reports an error entry to the visitor if error reporting is enabled.
 *
 * @param[in] ctx - traversal context
 * @param[in] path - full path
 * @param[in] item - item name
 * @param[in] ext - file extension
 * @param[in] flags - item flags (the error flag is added automatically)
 * @param[in] level - path depth
 * @return 0 if the visitor requested an abort, else 1
 */
static int td_reportError(const tTdirCtx * ctx, const TDIR_CHAR * path, const TDIR_CHAR * item,
	const TDIR_CHAR * ext, const int flags, const unsigned int level) {
	if ((ctx->options & TDIR_O_ERRORS) != 0) {
		return (*ctx->visitor)(path, item, ext, flags | TDIR_F_ERROR, level, ctx->param);
	}
	return 1;
}


/**
 * Handles the result of a recursion into a sub directory by updating the
 * caller's result and sub result state.
 *
 * @param[in] rc - return value of the recursive call
 * @param[in] ctx - traversal context
 * @param[in] path - full path
 * @param[in] item - item name
 * @param[in] ext - file extension
 * @param[in] level - path depth
 * @param[in,out] result - overall result state
 * @param[in,out] subResult - sub traversal result state
 */
static void td_handleSub(const int rc, const tTdirCtx * ctx, const TDIR_CHAR * path,
	const TDIR_CHAR * item, const TDIR_CHAR * ext, const unsigned int level, int * result, int * subResult) {
	switch (rc) {
	case 0:
		*result = 0;
		break;
	case 1:
		break;
	default:
		if (td_reportError(ctx, path, item, ext, TDIR_F_DIR, level) == 0) *result = 0;
		*subResult = -1;
		break;
	}
}


/**
 * Traverses the given path by the specified options and notifies the passed
 * visitor on each processed item. It carries the internal state of each
 * recursion.
 *
 * @param[in] path - base path to process
 * @param[in] curLevel - current level
 * @param[in,out] ctx - traversal context (invariant arguments and the link target set)
 * @return 1 on success, 0 on user abort, -1 on error
 */
static int td_traverseR(const TDIR_CHAR * path, const unsigned int curLevel, tTdirCtx * ctx) {
	const int following = (ctx->options & TDIR_O_FOLLOW_LINKS) != 0;
	const size_t pathLength = TDIR_SLEN(path);
	size_t prefixLength = pathLength;
	TDIR_CHAR * newPath = NULL;
	size_t maxPath = 0;
	int result = 1, subResult = 1;
#ifdef PCF_IS_WIN
	HANDLE dp = INVALID_HANDLE_VALUE;
#ifdef TDIR_UNICODE
	WIN32_FIND_DATAW item;
#else /* ! TDIR_UNICODE */
	WIN32_FIND_DATAA item;
#endif /* TDIR_UNICODE */
#else /* PCF_IS_NO_WIN */
	DIR * dp = NULL;
#endif /* PCF_IS_WIN */
	if (curLevel >= TDIR_LEVEL_MAX) return -1; /* the ceiling refuses, it does not cut off */
	if ( ! td_levelAllowed(ctx, curLevel) ) return 1;
	if ( ! TDIR_IS_SEP(path[pathLength - 1]) ) prefixLength++;
	/* the parent path is written once and only the item name changes per entry */
	if (td_ensure(&newPath, &maxPath, prefixLength + 2) == 0) return -1;
	memcpy(newPath, path, pathLength * sizeof(TDIR_CHAR));
	if (prefixLength != pathLength) newPath[pathLength] = TDIR_SEP;
	newPath[prefixLength] = 0;
#ifdef PCF_IS_WIN
	newPath[prefixLength] = TDIR_T('*');
	newPath[prefixLength + 1] = 0;
#ifdef TDIR_UNICODE
	dp = FindFirstFileW(td_winPath(ctx, newPath), &item);
#else /* ! TDIR_UNICODE */
	dp = FindFirstFileA(newPath, &item);
#endif /* TDIR_UNICODE */
	if (dp == INVALID_HANDLE_VALUE) result = -1;
	while (result == 1) {
		const TDIR_CHAR * name = item.cFileName;
		if (TDIR_SCMP(name, TDIR_T(".")) != 0 && TDIR_SCMP(name, TDIR_T("..")) != 0) {
			const size_t nameLength = TDIR_SLEN(name);
			if (td_ensure(&newPath, &maxPath, prefixLength + nameLength + 1) == 0) {
				result = -1;
				break;
			}
			memcpy(newPath + prefixLength, name, (nameLength + 1) * sizeof(TDIR_CHAR));
			const TDIR_CHAR * itemName = newPath + prefixLength;
			const TDIR_CHAR * itemExt = TDIR_SRCHR(itemName, TDIR_T('.'));
			const int isLink = (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
			const int isDir = (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			if (itemExt == NULL) itemExt = itemName + nameLength;
			if ( isDir ) {
				if ((ctx->options & TDIR_O_DIRECTORY) != 0) {
					if ((*ctx->visitor)(newPath, itemName, itemExt, TDIR_F_DIR | (isLink ? TDIR_F_LINK : 0), curLevel, ctx->param) == 0) {
						result = 0;
					}
				}
				if (isLink && ( ! following )) {
					/* reparse-point directory and not following links -> skip */
				} else if ( ! isLink ) {
					/* regular directory */
					td_handleSub(td_traverseR(newPath, curLevel + 1, ctx), ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
				} else if ( ! td_levelAllowed(ctx, curLevel + 1) ) {
					/* the level limit refuses the descent, so the target keeps its
					 * single claim for a link that can still be entered */
				} else {
					tTdirDirId id;
					if (td_idFromPath(td_winPath(ctx, newPath), &id) == 0) {
						/* unresolved target -> report and skip */
						if (td_reportError(ctx, newPath, itemName, itemExt, TDIR_F_DIR, curLevel) == 0) result = 0;
					} else {
						const int claim = td_claimTarget(ctx, &id);
						if (claim > 0) {
							td_handleSub(td_traverseR(newPath, curLevel + 1, ctx), ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
						} else if (claim == 0) {
							/* already entered through another link -> report and skip */
							if (td_reportError(ctx, newPath, itemName, itemExt, TDIR_F_DIR, curLevel) == 0) result = 0;
						} else {
							result = -1;
						}
					}
				}
			} else if ((ctx->options & TDIR_O_ITEM) != 0) {
				/* normal item */
				if ((*ctx->visitor)(newPath, itemName, itemExt, TDIR_F_FILE | (isLink ? TDIR_F_LINK : 0), curLevel, ctx->param) == 0) {
					result = 0;
				}
			}
		}
#ifdef TDIR_UNICODE
		if (FindNextFileW(dp, &item) == 0) {
#else /* ! TDIR_UNICODE */
		if (FindNextFileA(dp, &item) == 0) {
#endif /* TDIR_UNICODE */
			if (GetLastError() != ERROR_NO_MORE_FILES) result = -1;
			break;
		}
	}
	if (dp != INVALID_HANDLE_VALUE) FindClose(dp);
#else /* PCF_IS_NO_WIN */
	if ((dp = opendir(path)) == NULL) result = -1;
	while (result == 1) {
		const struct dirent * item;
		const TDIR_CHAR * name;
		size_t nameLength;
		errno = 0;
		item = readdir(dp);
		if (item == NULL) {
			/* Real error or end of list? */
			if (errno != 0) result = -1;
			break;
		}
		name = item->d_name;
		if (TDIR_SCMP(name, TDIR_T(".")) == 0 || TDIR_SCMP(name, TDIR_T("..")) == 0) continue;
		nameLength = TDIR_SLEN(name);
		if (td_ensure(&newPath, &maxPath, prefixLength + nameLength + 1) == 0) {
			result = -1;
			break;
		}
		memcpy(newPath + prefixLength, name, (nameLength + 1) * sizeof(TDIR_CHAR));
		const TDIR_CHAR * itemName = newPath + prefixLength;
		const TDIR_CHAR * itemExt = TDIR_SRCHR(itemName, TDIR_T('.'));
		int typeKnown = 0;
		int isLink = 0;
		int isDir = 0;
		if (itemExt == NULL) itemExt = itemName + nameLength;
#ifdef TDIR_HAS_DTYPE
		switch (item->d_type) {
		case DT_DIR:
			isDir = 1;
			typeKnown = 1;
			break;
		case DT_LNK:
			isLink = 1;
			typeKnown = 1;
			break;
		case DT_UNKNOWN:
			break;
		default:
			/* a regular file and every other non-directory type */
			typeKnown = 1;
			break;
		}
#endif /* TDIR_HAS_DTYPE */
		if ( ! typeKnown ) {
			struct stat itemStat;
			if (lstat(newPath, &itemStat) != 0) {
				if (td_reportError(ctx, newPath, itemName, itemExt, TDIR_F_FILE, curLevel) == 0) {
					result = 0;
				}
				continue;
			}
#ifdef S_ISLNK
			if ( S_ISLNK(itemStat.st_mode) ) isLink = 1;
#endif
			if ( ! isLink ) isDir = S_ISDIR(itemStat.st_mode) ? 1 : 0;
		}
		struct stat targetStat;
		if ( isLink ) {
			/* dereference the link to classify it (and identify its target) */
			if (stat(newPath, &targetStat) == 0) {
				isDir = S_ISDIR(targetStat.st_mode) ? 1 : 0;
			} else if ( following ) {
				/* dangling or unreadable link target -> report and skip */
				if (td_reportError(ctx, newPath, itemName, itemExt, TDIR_F_DIR, curLevel) == 0) {
					result = 0;
				}
				continue;
			}
			/* not following + unresolved target -> treated as item */
		}
		if ( isDir ) {
			/* directory (including a symbolic link to a directory) */
			if ((ctx->options & TDIR_O_DIRECTORY) != 0) {
				if ((*ctx->visitor)(newPath, itemName, itemExt, TDIR_F_DIR | (isLink ? TDIR_F_LINK : 0), curLevel, ctx->param) == 0) {
					result = 0;
				}
			}
			if (isLink && ( ! following )) {
				/* directory symbolic link and not following links -> reported, not descended */
			} else if ( ! isLink ) {
				/* regular directory */
				td_handleSub(td_traverseR(newPath, curLevel + 1, ctx), ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
			} else if ( ! td_levelAllowed(ctx, curLevel + 1) ) {
				/* the level limit refuses the descent, so the target keeps its single
				 * claim for a link that can still be entered */
			} else {
				/* the link target was already dereferenced to classify it */
				tTdirDirId id;
				td_idFromStat(&targetStat, &id);
				const int claim = td_claimTarget(ctx, &id);
				if (claim > 0) {
					td_handleSub(td_traverseR(newPath, curLevel + 1, ctx), ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
				} else if (claim == 0) {
					/* already entered through another link -> report and skip */
					if (td_reportError(ctx, newPath, itemName, itemExt, TDIR_F_DIR, curLevel) == 0) result = 0;
				} else {
					result = -1;
				}
			}
		} else if ((ctx->options & TDIR_O_ITEM) != 0) {
			/* normal item */
			if ((*ctx->visitor)(newPath, itemName, itemExt, TDIR_F_FILE | (isLink ? TDIR_F_LINK : 0), curLevel, ctx->param) == 0) {
				result = 0;
			}
		}
	}
	if (dp != NULL) closedir(dp);
#endif /* PCF_IS_WIN */
	if (newPath != NULL) free(newPath);
	if (result == 0) return 0;
	if (subResult != 1) return subResult;
	return result;
}


/**
 * Traverses the given path by the specified options and notifies the passed
 * visitor on each processed item.
 *
 * @param[in] path - base path to process
 * @param[in] maxLevel - maximal level to traverse to (-1 for no limit)
 * @param[in] options - combination of the option enumeration elements by binary OR
 * @param[in] visitor - user defined callback function
 * @param[in,out] param - user defined parameter (passed to callback function)
 * @return 1 on success, 0 on user abort, -1 on error
 * @remarks Define TDIR_FUNC for the name of this function.
 * @remarks Define TDIR_VISITOR for the visitor callback type.
 * @remarks Define TDIR_F_FILE, TDIR_F_DIR, TDIR_F_LINK and TDIR_F_ERROR for the item
 * flags.
 * @remarks Define TDIR_O_DIRECTORY, TDIR_O_ITEM, TDIR_O_FOLLOW_LINKS, TDIR_O_ERRORS
 * and TDIR_O_ALL for the traversal options.
 */
int TDIR_FUNC(const TDIR_CHAR * path, const int maxLevel, const int options, TDIR_VISITOR visitor, void * param) {
	tTdirCtx ctx;
	int result;
	ctx.maxLevel = maxLevel;
	ctx.options = options & TDIR_O_ALL;
	ctx.visitor = visitor;
	ctx.param = param;
	ctx.seen = NULL;
	ctx.seenCount = 0;
	ctx.seenCap = 0;
#ifdef PCF_IS_WIN
	ctx.winPath = NULL;
	ctx.winCap = 0;
#endif /* PCF_IS_WIN */
	if (ctx.options == 0) return -1;
	if (path == NULL || *path == 0) return -1;
	if (visitor == NULL) return -1;
	result = td_traverseR(path, 0, &ctx);
	if (ctx.seen != NULL) free(ctx.seen);
#ifdef PCF_IS_WIN
	if (ctx.winPath != NULL) free(ctx.winPath);
#endif /* PCF_IS_WIN */
	return result;
}
#endif /* TDIR_UNSUPPORTED */
