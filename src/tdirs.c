/**
 * @file tdirs.c
 * @author Daniel Starke
 * @see tdirs.h
 * @date 2012-12-15
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
#include "tdirs.h"


#define TDIR_FUNC tds_traverse
#define TDIR_VISITOR TraverseDirVisitorS

#define TDIR_F_FILE TDSF_FILE
#define TDIR_F_DIR TDSF_DIR
#define TDIR_F_LINK TDSF_LINK
#define TDIR_F_ERROR TDSF_ERROR

#define TDIR_O_DIRECTORY TDSO_DIRECTORY
#define TDIR_O_ITEM TDSO_ITEM
#define TDIR_O_FOLLOW_LINKS TDSO_FOLLOW_LINKS
#define TDIR_O_ERRORS TDSO_ERRORS
#define TDIR_O_ALL TDSO_ALL

#undef TDIR_UNICODE


/* include template function */
#include "tdir.i"
