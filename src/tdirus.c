/**
 * @file tdirus.c
 * @author Daniel Starke
 * @see tdirus.h
 * @date 2012-12-16
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
static int tdus_traverseR(const wchar_t * path, const int maxLevel, const unsigned int curLevel,
	const int options, TraverseDirVisitorUS visitor, void * param) {
#ifdef PCF_IS_WIN
	HANDLE dp = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATAW item;
	const size_t pathLength = wcslen(path);
	const size_t myPathLength = pathLength + strlen(PCF_PATH_SEP) + 1;
	wchar_t * newPath = NULL;
	size_t maxPath = 256;
	int needPathSize;
	int result = 1;
	wchar_t * myPath = NULL;
	myPath = (wchar_t *)malloc(sizeof(wchar_t) * (myPathLength + 1));
	if (myPath == NULL) return -1;
	snwprintf(myPath, myPathLength + 1, L"%ls" PCF_PATH_SEPU L"*", path);
	if (maxLevel >= 0 && curLevel > ((const unsigned int)maxLevel)) return 1;
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
				const wchar_t * itemName = newPath + wcslen(newPath) - wcslen(item.cFileName);
				const wchar_t * itemExt = wcsrchr(itemName, L'.');
				if (itemExt == NULL) {
					itemExt = itemName + wcslen(item.cFileName);
				}
				if ((item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0){
					/* directory */
					if ((options & TDUSO_DIRECTORY) != 0) {
						if ((*visitor)(newPath, itemName, itemExt, 1, curLevel, param) == 0) {
							result = 0;
						}
					}
					if ((item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 || (options & TDUSO_FOLLOW_LINKS) != 0) {
						switch (tdus_traverseR(newPath, maxLevel, curLevel + 1, options, visitor, param)) {
						case 0: result = 0; break;
						case 1: break;
						default: result = -1; break;
						}
					}
				} else if ((options & TDUSO_ITEM) != 0) {
					/* normal item */
					if ((*visitor)(newPath, itemName, itemExt, 0, curLevel, param) == 0) {
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
	return result;
#else /* PCF_IS_NO_WIN */
#warning "Wide char directory listing is only supported for Windows."
	PCF_UNUSED(path)
	PCF_UNUSED(maxLevel)
	PCF_UNUSED(curLevel)
	PCF_UNUSED(options)
	PCF_UNUSED(visitor)
	PCF_UNUSED(param)
	return -1;
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
int tdus_traverse(const wchar_t * path, const int maxLevel, const int options, TraverseDirVisitorUS visitor, void * param) {
	int cleanOptions = options & TDUSO_ALL;
	if (cleanOptions == 0) return -1;
	if (path == NULL) return -1;
	if (visitor == NULL) return -1;
	return tdus_traverseR(path, maxLevel, 0, cleanOptions, visitor, param);
	
}
