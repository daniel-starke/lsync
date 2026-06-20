/**
 * @file dirstack.c
 * @author Daniel Starke
 * @see dirstack.h
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
#include <stdlib.h>
#include <string.h>
#include "dirstack.h"


/**
 * Duplicates the given string into a newly allocated buffer.
 *
 * @param[in] str - string to duplicate
 * @return allocated copy or NULL on allocation failure
 */
static TCHAR * ds_dup(const TCHAR * str) {
	const size_t len = _tcslen(str) + 1;
	TCHAR * res = (TCHAR *)malloc(sizeof(TCHAR) * len);
	if (res != NULL) memcpy(res, str, sizeof(TCHAR) * len);
	return res;
}


/**
 * Pushes a directory frame onto the stack. The frame stores private copies of
 * both paths.
 *
 * @param[in,out] stack - stack handle
 * @param[in] src - first path
 * @param[in] dst - second path
 * @param[in] level - nesting depth of the directory
 * @return 1 on success, 0 on allocation failure
 */
int ds_push(tDirStack * stack, const TCHAR * src, const TCHAR * dst, const unsigned int level) {
	if (stack->size >= stack->capacity) {
		const size_t newCap = (stack->capacity == 0) ? 16 : (stack->capacity * 2);
		tDirStackFrame * newFrames = (tDirStackFrame *)realloc(stack->frames, newCap * sizeof(tDirStackFrame));
		if (newFrames == NULL) return 0;
		stack->frames = newFrames;
		stack->capacity = newCap;
	}
	tDirStackFrame * frame = stack->frames + stack->size;
	frame->level = level;
	frame->modified = 0;
	frame->src = ds_dup(src);
	frame->dst = ds_dup(dst);
	if (frame->src == NULL || frame->dst == NULL) {
		free(frame->src);
		free(frame->dst);
		return 0;
	}
	stack->size++;
	return 1;
}


/**
 * Marks the topmost frame as modified.
 *
 * @param[in,out] stack - stack handle
 * @return 1 if a frame was marked, 0 if the stack was empty
 */
int ds_markTop(tDirStack * stack) {
	if (stack->size > 0) {
		stack->frames[stack->size - 1].modified = 1;
		return 1;
	}
	return 0;
}


/**
 * Visits and then pops every frame at or below the given level, top to
 * bottom.
 *
 * @param[in,out] stack - stack handle
 * @param[in] level - consume every frame with this level or deeper
 * @param[in] visitor - callback invoked per frame before it is freed, or NULL to skip
 * @param[in,out] param - user defined parameter passed to the visitor
 */
void ds_consume(tDirStack * stack, const unsigned int level, tDirStackVisitor visitor, void * param) {
	while (stack->size > 0 && stack->frames[stack->size - 1].level >= level) {
		tDirStackFrame * frame = stack->frames + (--stack->size);
		if (visitor != NULL) visitor(frame, param);
		free(frame->src);
		free(frame->dst);
	}
}


/**
 * Clear all stack items. Safe to re-use afterwards.
 *
 * @param[in,out] stack - stack handle
 */
void ds_clear(tDirStack * stack) {
	while (stack->size > 0) {
		tDirStackFrame * frame = stack->frames + (--stack->size);
		free(frame->src);
		free(frame->dst);
	}
	free(stack->frames);
	stack->frames = NULL;
	stack->capacity = 0;
}
