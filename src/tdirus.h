/**
 * @file tdirus.h
 * @author Daniel Starke
 * @see tdirus.c
 * @date 2012-12-16
 * @version 2016-05-01
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
#ifndef __LIBPCF_TDIRUS_H__
#define __LIBPCF_TDIRUS_H__

#include <wchar.h>
#include "target.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Defines the callback function for directory traversing.
 * It is recommended to make the callback function inline
 * for speed increase. The path, item and ext parameter share
 * the same memory, which makes the following example
 * possible.
 * <br><br>Expample:<pre>
 * inline int processDir(const wchar_t * path, const wchar_t * item, const wchar_t * ext,
 *  const int isDir, const unsigned int level, void * param) {
 *  if (isDir != 0) {
 *   wprintf(L"%u:%.*s: %s (DIR)\n", level, item - path, path, item);
 *  } else {
 *   if (*ext != 0) {
 *    wprintf(L"%u:%.*s: %s (%s)\n", level, item - path, path, item, ext);
 *   } else {
 *    wprintf(L"%u:%.*s: %s\n", level, item - path, path, item);
 *   }
 *  }
 *  return 1;
 * }
 * </pre> 
 * 
 * @param[in] path - full path
 * @param[in] item - item name
 * @param[in] ext - file extension
 * @param[in] isDir - 0 if not a directory, else 1
 * @param[in] level - path depth calculated from the base path
 * @param[in,out] param - user defined parameter
 * @return 0 to abort
 * @return 1 to continue
 */
typedef int (* TraverseDirVisitorUS)(const wchar_t * path, const wchar_t * item, const wchar_t * ext, const int isDir,
	const unsigned int level, void * param);


/**
 * These options are used to control the traversing process of
 * tdus_traverse().
 */
typedef enum tTdusOption {
	TDUSO_DIRECTORY = 1,
	TDUSO_ITEM = TDUSO_DIRECTORY << 1,
	TDUSO_FOLLOW_LINKS = TDUSO_ITEM << 1,
	TDUSO_ALL = TDUSO_DIRECTORY | TDUSO_ITEM | TDUSO_FOLLOW_LINKS
} tTdusOption;


int tdus_traverse(const wchar_t * path, const int maxLevel, const int options, TraverseDirVisitorUS visitor, void * param);


#ifdef __cplusplus
}
#endif


#endif /* __LIBPCF_TDIRUS_H__ */
