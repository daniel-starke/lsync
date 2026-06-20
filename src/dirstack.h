/**
 * @file dirstack.h
 * @author Daniel Starke
 * @see dirstack.c
 * @date 2026-06-19
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
#ifndef __DIRSTACK_H__
#define __DIRSTACK_H__

#include <stddef.h>
#include "target.h"
#include "tchar.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Single directory frame.
 */
typedef struct {
	unsigned int level; /**< nesting depth of the directory */
	int modified;       /**< marked as modified? */
	TCHAR * src;        /**< first path (owned copy) */
	TCHAR * dst;        /**< second path (owned copy) */
} tDirStackFrame;


/**
 * Growable stack of directory frames with sparse level.
 */
typedef struct {
	tDirStackFrame * frames; /**< frame array, NULL if empty */
	size_t size;             /**< frame count */
	size_t capacity;         /**< allocated capacity in `frames` */
} tDirStack;


/**
 * Defines the callback function for directory frame traversing.
 *
 * @param[in] frame - directory frame
 * @param[in,out] param - user defined parameter
 */
typedef void (* tDirStackVisitor)(const tDirStackFrame * frame, void * param);


int ds_push(tDirStack * stack, const TCHAR * src, const TCHAR * dst, const unsigned int level);
int ds_markTop(tDirStack * stack);
void ds_consume(tDirStack * stack, const unsigned int level, tDirStackVisitor visitor, void * param);
void ds_clear(tDirStack * stack);


#ifdef __cplusplus
}
#endif


#endif /* __DIRSTACK_H__ */
