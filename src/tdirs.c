/**
 * @file tdirs.c
 * @author Daniel Starke
 * @see tdirs.h
 * @date 2012-12-15
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
#include "tdirs.h"

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


/** Defines the additional size to allocate, if the path memory size is too small. */
#define PATH_LENGTH_GROWTH 256


/**
 * Defines a chain of ancestors to detect symbolic link cycles.
 */
typedef struct tTdsAncestor {
#ifdef PCF_IS_WIN
	DWORD volSerial; /**< volume serial number */
	DWORD idxHigh;   /**< high part of the file index */
	DWORD idxLow;    /**< low part of the file index */
#else /* PCF_IS_NO_WIN */
	dev_t dev; /**< device ID */
	ino_t ino; /**< inode number */
#endif /* PCF_IS_WIN */
	const struct tTdsAncestor * parent; /**< enclosing directory (`NULL` for root) */
} tTdsAncestor;


/**
 * Invariant arguments passed unchanged through the traversal recursion.
 */
typedef struct tTdsCtx {
	int maxLevel;                /**< maximal level to traverse to (`-1` for no limit) */
	int options;                 /**< combination of tTdsOption elements */
	TraverseDirVisitorS visitor; /**< user defined callback function */
	void * param;                /**< user defined callback parameter */
} tTdsCtx;


#ifdef PCF_IS_WIN
/**
 * Checks whether the given directory is already part of the ancestor chain.
 *
 * @param[in] chain - ancestor chain to search (may be `NULL`)
 * @param[in] info - directory to look for
 * @return 1 if a matching ancestor was found (cycle), else 0
 */
static int tds_ancestorContains(const tTdsAncestor * chain, const BY_HANDLE_FILE_INFORMATION * info) {
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
static int tds_getDirId(const char * path, BY_HANDLE_FILE_INFORMATION * info) {
	int ok;
	HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	ok = (GetFileInformationByHandle(h, info) != 0) ? 1 : 0;
	CloseHandle(h);
	return ok;
}
#else /* PCF_IS_NO_WIN */
/**
 * Checks whether the given directory identity is already part of the ancestor chain.
 *
 * @param[in] chain - ancestor chain to search (may be `NULL`)
 * @param[in] st - directory identity to look for
 * @return 1 if a matching ancestor was found (cycle), else 0
 */
static int tds_ancestorContains(const tTdsAncestor * chain, const struct stat * st) {
	for (; chain != NULL; chain = chain->parent) {
		if (chain->dev == st->st_dev && chain->ino == st->st_ino) {
			return 1;
		}
	}
	return 0;
}
#endif /* PCF_IS_WIN */


/**
 * Reports an error entry to the visitor if error reporting is enabled.
 *
 * @param[in] ctx - traversal context
 * @param[in] path - full path
 * @param[in] item - item name
 * @param[in] ext - file extension
 * @param[in] flags - item flags (TDSF_ERROR is added automatically)
 * @param[in] level - path depth
 * @return 0 if the visitor requested an abort, else 1
 */
static int tds_reportError(const tTdsCtx * ctx, const char * path, const char * item,
	const char * ext, const int flags, const unsigned int level) {
	if ((ctx->options & TDSO_ERRORS) != 0) {
		return (*ctx->visitor)(path, item, ext, flags | TDSF_ERROR, level, ctx->param);
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
static void tds_handleSub(const int rc, const tTdsCtx * ctx, const char * path,
	const char * item, const char * ext, const unsigned int level, int * result, int * subResult) {
	switch (rc) {
	case 0:
		*result = 0;
		break;
	case 1:
		break;
	default:
		if (tds_reportError(ctx, path, item, ext, TDSF_DIR, level) == 0) *result = 0;
		*subResult = -1;
		break;
	}
}


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
static int tds_traverseR(const char * path, const unsigned int curLevel,
	const tTdsCtx * ctx, const tTdsAncestor * ancestors) {
#ifdef PCF_IS_WIN
	HANDLE dp = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATAA item;
	const size_t pathLength = strlen(path);
	const size_t myPathLength = pathLength + strlen(PCF_PATH_SEP) + 1;
	char * newPath = NULL;
	size_t maxPath = 256;
	int needPathSize;
	int result = 1, subResult = 1;
	char * myPath = NULL;
	if (ctx->maxLevel >= 0 && curLevel > ((const unsigned int)ctx->maxLevel)) return 1;
	myPath = (char *)malloc(sizeof(char) * (myPathLength + 1));
	if (myPath == NULL) return -1;
	snprintf(myPath, myPathLength + 1, "%s" PCF_PATH_SEP "*", path);
	if ((dp = FindFirstFileA(myPath, &item)) == INVALID_HANDLE_VALUE) result = -1;
	while (result == 1) {
		if (strcmp(item.cFileName, ".") != 0 && strcmp(item.cFileName, "..") != 0) {
			do {
				if (newPath == NULL) {
					if ((newPath = (char *)malloc(sizeof(char) * maxPath)) == NULL) {
						result = -1;
						break;
					}
				}
				if (path[pathLength - 1] == '\\' || path[pathLength - 1] == '/') {
					needPathSize = snprintf(newPath, maxPath, "%s%s", path, item.cFileName);
				} else {
					needPathSize = snprintf(newPath, maxPath, "%s" PCF_PATH_SEP "%s", path, item.cFileName);
				}
				if (needPathSize < 0 || ((size_t)needPathSize) >= maxPath) {
					/* path did not fit -> grow buffer and retry */
					if (needPathSize < 0) {
						maxPath += PATH_LENGTH_GROWTH;
					} else if (((size_t)needPathSize) > (SIZE_MAX - (PATH_LENGTH_GROWTH + 1))) {
						/* number overflow (most unlikely) */
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
				const char * itemName = newPath + strlen(newPath) - strlen(item.cFileName);
				const char * itemExt = strrchr(itemName, '.');
				if (itemExt == NULL) {
					itemExt = itemName + strlen(item.cFileName);
				}
				if ((item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0){
					/* directory */
					const int isLink = (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
					const int following = (ctx->options & TDSO_FOLLOW_LINKS) != 0;
					if ((ctx->options & TDSO_DIRECTORY) != 0) {
						if ((*ctx->visitor)(newPath, itemName, itemExt, TDSF_DIR | (isLink ? TDSF_LINK : 0), curLevel, ctx->param) == 0) {
							result = 0;
						}
					}
					if (isLink && ( ! following )) {
						/* reparse-point directory and not following links -> skip */
					} else if ( ! following ) {
						/* regular directory */
						const int res = tds_traverseR(newPath, curLevel + 1, ctx, NULL);
						tds_handleSub(res, ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
					} else {
						/* following links -> resolve identity for cycle detection */
						BY_HANDLE_FILE_INFORMATION info;
						if (tds_getDirId(newPath, &info) == 0 || tds_ancestorContains(ancestors, &info) != 0) {
							/* unresolved target or symbolic link cycle -> report and skip */
							if (tds_reportError(ctx, newPath, itemName, itemExt, TDSF_DIR, curLevel) == 0) {
								result = 0;
							}
						} else {
							tTdsAncestor node;
							node.volSerial = info.dwVolumeSerialNumber;
							node.idxHigh = info.nFileIndexHigh;
							node.idxLow = info.nFileIndexLow;
							node.parent = ancestors;
							const int res = tds_traverseR(newPath, curLevel + 1, ctx, &node);
							tds_handleSub(res, ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
						}
					}
				} else if ((ctx->options & TDSO_ITEM) != 0) {
					/* normal item */
					const int isLink = (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
					if ((*ctx->visitor)(newPath, itemName, itemExt, TDSF_FILE | (isLink ? TDSF_LINK : 0), curLevel, ctx->param) == 0) {
						result = 0;
					}
				}
			}
		}
		if (FindNextFileA(dp, &item) == 0) {
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
	DIR * dp = NULL;
	struct dirent * item = NULL;
	struct stat itemStat;
	const size_t pathLength = strlen(path);
	char * newPath = NULL;
	size_t maxPath = 256;
	int needPathSize;
	int result = 1, subResult = 1;
	if (ctx->maxLevel >= 0 && curLevel > ((const unsigned int)ctx->maxLevel)) return 1;
	if ((dp = opendir(path)) == NULL) result = -1;
	while (result == 1) {
		errno = 0;
		item = readdir(dp);
		if (item == NULL) {
			/* real error or end of list? */
			if (errno != 0) result = -1;
			break;
		}
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
		do {
			if (newPath == NULL) {
				if ((newPath = (char *)malloc(sizeof(char) * maxPath)) == NULL) {
					result = -1;
					break;
				}
			}
			if (path[pathLength - 1] == '\\' || path[pathLength - 1] == '/') {
				needPathSize = snprintf(newPath, maxPath, "%s%s", path, item->d_name);
			} else {
				needPathSize = snprintf(newPath, maxPath, "%s"PCF_PATH_SEP"%s", path, item->d_name);
			}
			if (needPathSize < 0 || ((size_t)needPathSize) >= maxPath) {
				/* error or path did not fit -> grow buffer and retry */
				if (needPathSize < 0) {
					maxPath += PATH_LENGTH_GROWTH;
				} else if (((size_t)needPathSize) > SIZE_MAX - (PATH_LENGTH_GROWTH + 1)) {
					/* number overflow (most unlikely) */
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
			const char * itemName = newPath + strlen(newPath) - strlen(item->d_name);
			const char * itemExt = strrchr(itemName, '.');
			int isLink = 0;
			int isDir;
			const int following = (ctx->options & TDSO_FOLLOW_LINKS) != 0;
			struct stat idStat;
			if (itemExt == NULL) {
				itemExt = itemName + strlen(item->d_name);
			}
			if (lstat(newPath, &itemStat) != 0) {
				if (tds_reportError(ctx, newPath, itemName, itemExt, TDSF_FILE, curLevel) == 0) {
					result = 0;
				}
				continue;
			}
			idStat = itemStat;
#ifdef S_ISLNK
			if (S_ISLNK(itemStat.st_mode)) isLink = 1;
#endif
			if (isLink) {
				/* dereference the link to classify it (and identify its target) */
				struct stat targetStat;
				if (stat(newPath, &targetStat) == 0) {
					idStat = targetStat;
				} else if (following) {
					/* dangling or unreadable link target -> report and skip */
					if (tds_reportError(ctx, newPath, itemName, itemExt, TDSF_DIR, curLevel) == 0) {
						result = 0;
					}
					continue;
				}
				/* not following + unresolved target -> keep lstat (treated as item) */
			}
			isDir = S_ISDIR(idStat.st_mode) ? 1 : 0;
			if (isDir) {
				/* directory (including a symlink to a directory) */
				if ((ctx->options & TDSO_DIRECTORY) != 0) {
					if ((*ctx->visitor)(newPath, itemName, itemExt, TDSF_DIR | (isLink ? TDSF_LINK : 0), curLevel, ctx->param) == 0) {
						result = 0;
					}
				}
				if (isLink && ( ! following )) {
					/* directory symlink and not following links -> reported, not descended */
				} else if ( following ) {
					/* cycle detection is active while following links */
					if (tds_ancestorContains(ancestors, &idStat) != 0) {
						/* symbolic link cycle -> report and skip */
						if (tds_reportError(ctx, newPath, itemName, itemExt, TDSF_DIR, curLevel) == 0) {
							result = 0;
						}
					} else {
						tTdsAncestor node;
						node.dev = idStat.st_dev;
						node.ino = idStat.st_ino;
						node.parent = ancestors;
						const int res = tds_traverseR(newPath, curLevel + 1, ctx, &node);
						tds_handleSub(res, ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
					}
				} else {
					const int res = tds_traverseR(newPath, curLevel + 1, ctx, NULL);
					tds_handleSub(res, ctx, newPath, itemName, itemExt, curLevel, &result, &subResult);
				}
			} else if ((ctx->options & TDSO_ITEM) != 0) {
				/* normal item */
				if ((*ctx->visitor)(newPath, itemName, itemExt, TDSF_FILE | (isLink ? TDSF_LINK : 0), curLevel, ctx->param) == 0) {
					result = 0;
				}
			}
		}
	}
	if (dp != NULL) closedir(dp);
	if (newPath != NULL) free(newPath);
	if (result == 0) return 0;
	if (subResult != 1) return subResult;
	return result;
#endif /* PCF_IS_WIN */
}


/**
 * The function traverses the given path by the specified options
 * and notifies the passed visitor on each processed item.
 *
 * @param[in] path - base path to process
 * @param[in] maxLevel - maximal level to traverse to (-1 for no limit)
 * @param[in] options - combination of tTdsOption elements by binary OR
 * @param[in] visitor - user defined callback function
 * @param[in,out] param - user defined parameter (passed to callback function)
 * @return 1 on success, 0 on user abort, -1 on error
 */
int tds_traverse(const char * path, const int maxLevel, const int options, TraverseDirVisitorS visitor, void * param) {
	tTdsCtx ctx;
	ctx.maxLevel = maxLevel;
	ctx.options = options & TDSO_ALL;
	ctx.visitor = visitor;
	ctx.param = param;
	if (ctx.options == 0) return -1;
	if (path == NULL || *path == 0) return -1;
	if (visitor == NULL) return -1;
	if ((ctx.options & TDSO_FOLLOW_LINKS) != 0) {
		/* seed the cycle detection chain with the root directory's identity */
		tTdsAncestor root;
		root.parent = NULL;
#ifdef PCF_IS_WIN
		{
			BY_HANDLE_FILE_INFORMATION info;
			if (tds_getDirId(path, &info) == 0) return -1;
			root.volSerial = info.dwVolumeSerialNumber;
			root.idxHigh = info.nFileIndexHigh;
			root.idxLow = info.nFileIndexLow;
		}
#else /* PCF_IS_NO_WIN */
		{
			struct stat st;
			if (stat(path, &st) != 0) return -1;
			root.dev = st.st_dev;
			root.ino = st.st_ino;
		}
#endif /* PCF_IS_WIN */
		return tds_traverseR(path, 0, &ctx, &root);
	}
	return tds_traverseR(path, 0, &ctx, NULL);
}
