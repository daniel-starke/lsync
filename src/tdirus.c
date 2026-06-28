/**
 * @file tdirus.c
 * @author Daniel Starke
 * @see tdirus.h
 * @date 2012-12-16
 * @version 2026-06-28
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tdirus.h"

#ifdef PCF_IS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else /* PCF_IS_NO_WIN */
/* not supported */
#endif /* PCF_IS_WIN */


/** Defines the additional size to allocate, if the path memory size is too small. */
#define PATH_LENGTH_GROWTH 256


/**
 * Defines a chain of ancestors to detect symbolic link cycles.
 */
typedef struct tTdusAncestor {
#ifdef PCF_IS_WIN
	DWORD volSerial; /**< volume serial number */
	DWORD idxHigh;   /**< high part of the file index */
	DWORD idxLow;    /**< low part of the file index */
#else /* PCF_IS_NO_WIN */
	int unused; /**< placeholder (wide char traversal is Windows only) */
#endif /* PCF_IS_WIN */
	const struct tTdusAncestor * parent; /**< enclosing directory (`NULL` for root) */
} tTdusAncestor;


/**
 * Invariant arguments passed unchanged through the traversal recursion.
 */
typedef struct tTdusCtx {
	int maxLevel;                 /**< maximal level to traverse to (`-1` for no limit) */
	int options;                  /**< combination of tTdusOption elements */
	TraverseDirVisitorUS visitor; /**< user defined callback function */
	void * param;                 /**< user defined callback parameter */
} tTdusCtx;


#ifdef PCF_IS_WIN
/**
 * Checks whether the given directory is already part of the ancestor chain.
 *
 * @param[in] chain - ancestor chain to search (may be `NULL`)
 * @param[in] info - directory to look for
 * @return 1 if a matching ancestor was found (cycle), else 0
 */
static int tdus_ancestorContains(const tTdusAncestor * chain, const BY_HANDLE_FILE_INFORMATION * info) {
	for (; chain != NULL; chain = chain->parent) {
		if (chain->volSerial == info->dwVolumeSerialNumber && chain->idxHigh == info->nFileIndexHigh && chain->idxLow == info->nFileIndexLow) {
			return 1;
		}
	}
	return 0;
}


/**
 * Retrieves the identity of the given directory by following links.
 *
 * @param[in] path - directory path
 * @param[out] info - receives the file information on success
 * @return 1 on success, 0 on error
 */
static int tdus_getDirId(const wchar_t * path, BY_HANDLE_FILE_INFORMATION * info) {
	int ok;
	HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	ok = (GetFileInformationByHandle(h, info) != 0) ? 1 : 0;
	CloseHandle(h);
	return ok;
}


/**
 * Reports an error entry to the visitor if error reporting is enabled.
 *
 * @param[in] ctx - traversal context
 * @param[in] path - full path
 * @param[in] item - item name
 * @param[in] ext - file extension
 * @param[in] flags - item flags (TDSUF_ERROR is added automatically)
 * @param[in] level - path depth
 * @return 0 if the visitor requested an abort, else 1
 */
static int tdus_reportError(const tTdusCtx * ctx, const wchar_t * path, const wchar_t * item,
	const wchar_t * ext, const int flags, const unsigned int level) {
	if ((ctx->options & TDUSO_ERRORS) != 0) {
		return (*ctx->visitor)(path, item, ext, flags | TDSUF_ERROR, level, ctx->param);
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
static void tdus_handleSub(const int rc, const tTdusCtx * ctx, const wchar_t * path,
	const wchar_t * item, const wchar_t * ext, const unsigned int level, int * result, int * subResult) {
	switch (rc) {
	case 0:
		*result = 0;
		break;
	case 1:
		break;
	default:
		if (tdus_reportError(ctx, path, item, ext, TDSUF_DIR, level) == 0) *result = 0;
		*subResult = -1;
		break;
	}
}
#endif /* PCF_IS_WIN */


/**
 * The function traverses the given path by the specified options
 * and notifies the passed visitor on each processed item.
 * It is used to handle the internal states in each recursion.
 *
 * @param[in] path - base path to process
 * @param[in] curLevel - current level
 * @param[in] ctx - traversal context (invariant arguments)
 * @param[in] ancestors - ancestor chain for cycle detection (NULL when not following links)
 * @return 1 on success, 0 on user abort, -1 on error
 */
static int tdus_traverseR(const wchar_t * path, const unsigned int curLevel,
	const tTdusCtx * ctx, const tTdusAncestor * ancestors) {
#ifdef PCF_IS_WIN
	HANDLE dp = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATAW item;
	const size_t pathLength = wcslen(path);
	const size_t myPathLength = pathLength + wcslen(PCF_PATH_SEPU) + 1;
	wchar_t * newPath = NULL;
	size_t maxPath = 256;
	int needPathSize;
	int result = 1, subResult = 1;
	wchar_t * myPath = NULL;
	if (ctx->maxLevel >= 0 && curLevel > ((const unsigned int)ctx->maxLevel)) return 1;
	myPath = (wchar_t *)malloc(sizeof(wchar_t) * (myPathLength + 1));
	if (myPath == NULL) return -1;
	snwprintf(myPath, myPathLength + 1, L"%ls" PCF_PATH_SEPU L"*", path);
	if ((dp = FindFirstFileW(myPath, &item)) == INVALID_HANDLE_VALUE) result = -1;
	while (result == 1) {
		if (wcscmp(item.cFileName, L".") != 0 && wcscmp(item.cFileName, L"..") != 0) {
			do {
				if (newPath == NULL) {
					if ((newPath = (wchar_t *)malloc(sizeof(wchar_t) * maxPath)) == NULL) {
						result = -1;
						break;
					}
				}
				if (path[pathLength - 1] == L'\\' || path[pathLength - 1] == L'/') {
					needPathSize = snwprintf(newPath, maxPath, L"%ls%ls", path, item.cFileName);
				} else {
					needPathSize = snwprintf(newPath, maxPath, L"%ls" PCF_PATH_SEPU L"%ls", path, item.cFileName);
				}
				if (needPathSize < 0 || ((size_t)needPathSize) >= maxPath) {
					/* path did not fit -> grow buffer and retry */
					if (needPathSize < 0) {
						maxPath += PATH_LENGTH_GROWTH;
					} else if (((size_t)needPathSize) > (SIZE_MAX - (PATH_LENGTH_GROWTH + 1))) {
						/* addition would overflow (most unlikely) */
						result = -1;
						break;
					} else {
						maxPath = ((size_t)needPathSize) + PATH_LENGTH_GROWTH + 1;
					}
					free(newPath);
					newPath = NULL;
				}
			} while (newPath == NULL);
			if (result == 1) {
				const wchar_t * itemName = newPath + wcslen(newPath) - wcslen(item.cFileName);
				const wchar_t * itemExt = wcsrchr(itemName, L'.');
				if (itemExt == NULL) {
					itemExt = itemName + wcslen(item.cFileName);
				}
				if ((item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0){
					/* directory */
					const int isLink = (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
					const int following = (ctx->options & TDUSO_FOLLOW_LINKS) != 0;
					if ((ctx->options & TDUSO_DIRECTORY) != 0) {
						if ((*ctx->visitor)(newPath, itemName, itemExt, TDSUF_DIR | (isLink ? TDSUF_LINK : 0), curLevel, ctx->param) == 0) {
							result = 0;
						}
					}
					if (isLink && ( ! following )) {
						/* reparse-point directory and not following links -> skip */
					} else if ( ! following ) {
						/* regular directory */
						tdus_handleSub(tdus_traverseR(newPath, curLevel + 1, ctx, NULL),
							ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
					} else {
						/* following links -> resolve identity for cycle detection */
						BY_HANDLE_FILE_INFORMATION info;
						if (tdus_getDirId(newPath, &info) == 0 || tdus_ancestorContains(ancestors, &info) != 0) {
							/* unresolved target or symbolic link cycle -> report and skip */
							if (tdus_reportError(ctx, newPath, itemName, itemExt, TDSUF_DIR, curLevel) == 0) {
								result = 0;
							}
						} else {
							tTdusAncestor node;
							node.volSerial = info.dwVolumeSerialNumber;
							node.idxHigh = info.nFileIndexHigh;
							node.idxLow = info.nFileIndexLow;
							node.parent = ancestors;
							tdus_handleSub(tdus_traverseR(newPath, curLevel + 1, ctx, &node),
								ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
						}
					}
				} else if ((ctx->options & TDUSO_ITEM) != 0) {
					/* normal item */
					const int isLink = (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
					if ((*ctx->visitor)(newPath, itemName, itemExt, TDSUF_FILE | (isLink ? TDSUF_LINK : 0), curLevel, ctx->param) == 0) {
						result = 0;
					}
				}
			}
		}
		if (FindNextFileW(dp, &item) == 0) {
			if (GetLastError() != ERROR_NO_MORE_FILES) result = -1;
			break;
		}
	}
	if (dp != INVALID_HANDLE_VALUE) FindClose(dp);
	if (newPath != NULL) free(newPath);
	if (myPath != NULL) free(myPath);
	if (result == 0) return 0;
	if (subResult != 1) return subResult;
	return result;
#else /* PCF_IS_NO_WIN */
#warning "Wide char directory listing is only supported for Windows."
	PCF_UNUSED(path)
	PCF_UNUSED(curLevel)
	PCF_UNUSED(ctx)
	PCF_UNUSED(ancestors)
	return -1;
#endif /* PCF_IS_WIN */
}


/**
 * The function traverses the given path by the specified options
 * and notifies the passed visitor on each processed item.
 *
 * @param[in] path - base path to process
 * @param[in] maxLevel - maximal level to traverse to (-1 for no limit)
 * @param[in] options - combination of tTdusOption elements by binary OR
 * @param[in] visitor - user defined callback function
 * @param[in,out] param - user defined parameter (passed to callback function)
 * @return 1 on success, 0 on user abort, -1 on error
 */
int tdus_traverse(const wchar_t * path, const int maxLevel, const int options, TraverseDirVisitorUS visitor, void * param) {
	tTdusCtx ctx;
	ctx.maxLevel = maxLevel;
	ctx.options = options & TDUSO_ALL;
	ctx.visitor = visitor;
	ctx.param = param;
	if (ctx.options == 0) return -1;
	if (path == NULL || *path == 0) return -1;
	if (visitor == NULL) return -1;
#ifdef PCF_IS_WIN
	if ((ctx.options & TDUSO_FOLLOW_LINKS) != 0) {
		/* seed the cycle detection chain with the root directory's identity */
		tTdusAncestor root;
		BY_HANDLE_FILE_INFORMATION info;
		root.parent = NULL;
		if (tdus_getDirId(path, &info) == 0) return -1;
		root.volSerial = info.dwVolumeSerialNumber;
		root.idxHigh = info.nFileIndexHigh;
		root.idxLow = info.nFileIndexLow;
		return tdus_traverseR(path, 0, &ctx, &root);
	}
#endif /* PCF_IS_WIN */
	return tdus_traverseR(path, 0, &ctx, NULL);
}
