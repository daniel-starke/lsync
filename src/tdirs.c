/**
 * @file tdirs.c
 * @author Daniel Starke
 * @see tdirs.h
 * @date 2012-12-15
 * @version 2017-05-23
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
#include <sys/stat.h>
#include <sys/types.h>
#endif /* PCF_IS_WIN */


/** Defines the additional size to allocate, if the path memory size is too small. */
#define PATH_LENGTH_GROWTH 256


/**
 * The function traverses the given path by the specified options
 * and notifies the passed visitor on each processed item.
 * It is used to handle the internal states in each recursion.
 * 
 * @param[in] path - base path to process
 * @param[in] maxLevel - maximal level to traverse to (-1 for no limit)
 * @param[in] curLevel - current level
 * @param[in] options - combination of tTdsOption elements by binary OR
 * @param[in] visitor - user defined callback function
 * @param[in,out] param - user defined parameter (passed to callback function)
 * @return 1 on success, 0 on user abort, -1 on error
 */
static int tds_traverseR(const char * path, const int maxLevel, const unsigned int curLevel,
	const int options, TraverseDirVisitorS visitor, void * param) {
#ifdef PCF_IS_WIN
	HANDLE dp = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATAA item;
	const size_t pathLength = strlen(path);
	const size_t myPathLength = pathLength + strlen(PCF_PATH_SEP) + 1;
	char * newPath = NULL;
	size_t maxPath = 256;
	int needPathSize;
	int result = 1;
	char * myPath = NULL;
	myPath = (char *)malloc(sizeof(char) * (myPathLength + 1));
	if (myPath == NULL) return -1;
	snprintf(myPath, myPathLength + 1, "%s"PCF_PATH_SEP"*", path);
	if (maxLevel >= 0 && curLevel > ((const unsigned int)maxLevel)) return 1;
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
					needPathSize = snprintf(newPath, maxPath, "%s"PCF_PATH_SEP"%s", path, item.cFileName);
				}
				if (needPathSize < 1) {
					result = -1;
					break;
				}
				if (((size_t)needPathSize) >= maxPath) {
					/* provided path string was too short -> increase its size to fit */
					maxPath = ((size_t)needPathSize) + PATH_LENGTH_GROWTH + 1;
					free(newPath);
					newPath = NULL;
					if (maxPath == 0) {
						/* number overflow (most unlikely) */
						result = -1;
						break;
					}
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
					if ((options & TDSO_DIRECTORY) != 0) {
						if ((*visitor)(newPath, itemName, itemExt, 1, curLevel, param) == 0) {
							result = 0;
						}
					}
					if ((item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 || (options & TDSO_FOLLOW_LINKS) != 0) {
						switch (tds_traverseR(newPath, maxLevel, curLevel + 1, options, visitor, param)) {
						case 0: result = 0; break;
						case 1: break;
						default: result = -1;
						}
					}
				} else if ((options & TDSO_ITEM) != 0) {
					/* normal item */
					if ((*visitor)(newPath, itemName, itemExt, 0, curLevel, param) == 0) {
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
	return result;
#else /* PCF_IS_NO_WIN */
	DIR * dp = NULL;
	struct dirent * item = NULL;
	struct stat itemStat;
	const size_t pathLength = strlen(path);
	char * newPath = NULL;
	size_t maxPath = 256;
	int needPathSize;
	int result = 1;
	if (maxLevel >= 0 && curLevel > ((const unsigned int)maxLevel)) return 1;
	if ((dp = opendir(path)) == NULL) result = -1;
	while (result == 1 && (item = readdir(dp)) != NULL) {
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
			if (needPathSize < 1) {
				result = -1;
				break;
			}
			if (((size_t)needPathSize) >= maxPath) {
				/* provided path string was too short -> increase its size to fit */
				maxPath = ((size_t)needPathSize) + PATH_LENGTH_GROWTH + 1;
				free(newPath);
				newPath = NULL;
				if (maxPath == 0) {
					/* number overflow (most unlikely) */
					result = -1;
					break;
				}
			}
		} while (newPath == NULL);
		stat(newPath, &itemStat);
		if (result == 1) {
			const char * itemName = newPath + strlen(newPath) - strlen(item->d_name);
			const char * itemExt = strrchr(itemName, '.');
			if (itemExt == NULL) {
				itemExt = itemName + strlen(item->d_name);
			}
			if ( S_ISDIR(itemStat.st_mode) ){
				/* directory */
				if ((options & TDSO_DIRECTORY) != 0) {
					if ((*visitor)(newPath, itemName, itemExt, 1, curLevel, param) == 0) {
						result = 0;
					}
				}
#ifdef S_ISLNK
				if (( ! S_ISLNK(itemStat.st_mode) ) || (options & TDSO_FOLLOW_LINKS) != 0) {
#endif
					switch (tds_traverseR(newPath, maxLevel, curLevel + 1, options, visitor, param)) {
					case 0: result = 0; break;
					case 1: break;
					default: result = -1; break;
					}
#ifdef S_ISLNK
				}
#endif
			} else if ((options & TDSO_ITEM) != 0) {
				/* normal item */
				if ((*visitor)(newPath, itemName, itemExt, 0, curLevel, param) == 0) {
					result = 0;
				}
			}
		}
	}
	if (dp != NULL) closedir(dp);
	if (newPath != NULL) free(newPath);
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
	int cleanOptions = options & TDSO_ALL;
	if (cleanOptions == 0) return -1;
	if (path == NULL) return -1;
	if (visitor == NULL) return -1;
	return tds_traverseR(path, maxLevel, 0, cleanOptions, visitor, param);
	
}
