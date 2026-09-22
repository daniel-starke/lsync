/**
 * @file tdirus.c
 * @author Daniel Starke
 * @see tdirus.h
 * @date 2012-12-16
 * @version 2026-09-14
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
#include <wchar.h>
#include "tdirus.h"


#define TDIR_FUNC tdus_traverse
#define TDIR_VISITOR TraverseDirVisitorUS

#define TDIR_F_FILE TDSUF_FILE
#define TDIR_F_DIR TDSUF_DIR
#define TDIR_F_LINK TDSUF_LINK
#define TDIR_F_ERROR TDSUF_ERROR

#define TDIR_O_DIRECTORY TDUSO_DIRECTORY
#define TDIR_O_ITEM TDUSO_ITEM
#define TDIR_O_FOLLOW_LINKS TDUSO_FOLLOW_LINKS
#define TDIR_O_ERRORS TDUSO_ERRORS
#define TDIR_O_ALL TDUSO_ALL

#define TDIR_UNICODE


/* include template function */
#include "tdir.i"
