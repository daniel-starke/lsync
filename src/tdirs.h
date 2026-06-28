/**
 * @file tdirs.h
 * @author Daniel Starke
 * @see tdirs.c
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
#ifndef __LIBPCF_TDIRS_H__
#define __LIBPCF_TDIRS_H__

#include "target.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Flags passed to TraverseDirVisitorS() by tds_traverse().
 */
typedef enum tTdsFlag {
	TDSF_FILE = 0,
	TDSF_DIR = 1,
	TDSF_LINK = TDSF_DIR << 1,
	TDSF_ERROR = TDSF_LINK << 1
} tTdsFlag;


/**
 * Defines the callback function for directory traversing.
 * It is recommended to make the callback function inline
 * for speed increase. The path, item and ext parameter share
 * the same memory, which makes the following example
 * possible.
 * <br><br>Example:<pre>
 * inline int processDir(const char * path, const char * item, const char * ext,
 *  const int flags, const unsigned int level, void * param) {
 *  switch (flags & ~TDSF_LINK) {
 *  case TDSF_FILE:
 *   if (*ext != 0) {
 *    printf("%u:%.*s: %s (%s)\n", level, (int)(item - path), path, item, ext);
 *   } else {
 *    printf("%u:%.*s: %s\n", level, (int)(item - path), path, item);
 *   }
 *   break;
 *  case TDSF_DIR:
 *   printf("%u:%.*s: %s (DIR)\n", level, (int)(item - path), path, item);
 *   break;
 *  default:
 *   printf("%u:%.*s: %s (ERROR)\n", level, (int)(item - path), path, item);
 *   break;
 *  }
 *  return 1;
 * }
 * </pre> 
 * 
 * @param[in] path - full path
 * @param[in] item - item name
 * @param[in] ext - file extension
 * @param[in] flags - item flags
 * @param[in] level - path depth calculated from the base path
 * @param[in,out] param - user defined parameter
 * @return 0 to abort
 * @return 1 to continue
 */
typedef int (* TraverseDirVisitorS)(const char * path, const char * item, const char * ext,
	const int flags, const unsigned int level, void * param);


/**
 * These options are used to control the traversing process of
 * tds_traverse().
 */
typedef enum tTdsOption {
	TDSO_DIRECTORY = 1,
	TDSO_ITEM = TDSO_DIRECTORY << 1,
	TDSO_FOLLOW_LINKS = TDSO_ITEM << 1,
	TDSO_ERRORS = TDSO_FOLLOW_LINKS << 1,
	TDSO_ALL = TDSO_DIRECTORY | TDSO_ITEM | TDSO_FOLLOW_LINKS | TDSO_ERRORS
} tTdsOption;


int tds_traverse(const char * path, const int maxLevel, const int options, TraverseDirVisitorS visitor, void * param);


#ifdef __cplusplus
}
#endif


#endif /* __LIBPCF_TDIRS_H__ */
