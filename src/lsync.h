/**
 * @file lsync.h
 * @author Daniel Starke
 * @date 2017-05-17
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
#ifndef __LSYNC_H__
#define __LSYNC_H__

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "target.h"
#include "tchar.h"
#include "dirstack.h"


#define PROGRAM_VERSION _T("2.1.0 2026-06-28")


#define BUFFER_SIZE 32768


/** Exit code for a backup that was interrupted by a signal. */
#define EXIT_SIGNAL 20


/** Exit code for a backup that finished but could not transfer everything. */
#define EXIT_PARTIAL 23


#ifdef UNICODE
#include "tdirus.h"
#define TDF_FILE TDSUF_FILE
#define TDF_DIR TDSUF_DIR
#define TDF_LINK TDSUF_LINK
#define TDF_ERROR TDSUF_ERROR
#define TDO_DIRECTORY TDUSO_DIRECTORY
#define TDO_ITEM TDUSO_ITEM
#define TDO_FOLLOW_LINKS TDUSO_FOLLOW_LINKS
#define TDO_ERRORS TDUSO_ERRORS
#define TDO_ALL TDUSO_ALL
#define td_traverse tdus_traverse
#else
#include "tdirs.h"
#define TDF_FILE TDSF_FILE
#define TDF_DIR TDSF_DIR
#define TDF_LINK TDSF_LINK
#define TDF_ERROR TDSF_ERROR
#define TDO_DIRECTORY TDSO_DIRECTORY
#define TDO_ITEM TDSO_ITEM
#define TDO_FOLLOW_LINKS TDSO_FOLLOW_LINKS
#define TDO_ERRORS TDSO_ERRORS
#define TDO_ALL TDSO_ALL
#define td_traverse tds_traverse
#endif


typedef enum {
	GETOPT_LINK_DEST = 1,
	GETOPT_VERSION,
} tLongOption;


typedef enum {
	AT_NONE  = 0x00,
	AT_GROUP = 0x01,
	AT_OWNER = 0x02,
	AT_PERMS = 0x04,
	AT_TIMES = 0x08,
	AT_ALL = AT_GROUP | AT_OWNER | AT_PERMS | AT_TIMES
} tAttrMask;


typedef enum {
	CP_NONE     = 0x00,
	CP_DEVICES  = 0x01,
	CP_LINKS    = 0x02,
	CP_SPECIALS = 0x04,
	CP_ALL = CP_DEVICES | CP_LINKS | CP_SPECIALS
} tCopyMask;


typedef struct {
	int devices;
	int group;
	int links;
	int owner;
	int perms;
	int recursive;
	int specials;
	int times;
	int verbose;
	TCHAR * linkDest;
	TCHAR ** srcArgs;
	int srcIndex;
	int srcCount;
	TCHAR * dstArg;
	int dstIsFile; /**< destination is a single explicit file path (rsync single-file semantics) */
	TCHAR * dst; /**< destination path string buffer to avoid allocations */
	TCHAR * ref; /**< reference path string buffer to avoid allocations */
	tAttrMask attrMask;
	tCopyMask copyMask;
	int hadError; /**< set when a recoverable error occurred (partial backup) */
	tDirStack dirStack; /**< stack of open directories for timestamp correction */
	int rootModified; /**< set when a top level child was written to update the root mtime */
} tContext;


extern volatile sig_atomic_t signalReceived;


void printHelp();
void handleSignal(int signum);
int joinPath(TCHAR * buf, const size_t len, const TCHAR * base, const TCHAR * item, const TCHAR * rel);
int destWithinSource(const TCHAR * src, const TCHAR * dst);
void dirStackFinalize(const tDirStackFrame * frame, void * param);
void dirStackMarkParent(tContext * ctx);
int backupVisitor(const TCHAR * src, const TCHAR * item, const TCHAR * ext, const int isDir,
	const unsigned int level, void * param);


/* I/O operations */
int isFile(const TCHAR * src);
int isDirectory(const TCHAR * src);
int isSymlink(const TCHAR * src);
int realPath(const TCHAR * path, TCHAR * buf, const size_t len);
int createDirectory(const TCHAR * dst, const int verbose);
int createTempName(const TCHAR * dst, TCHAR ** tmp, const int verbose);
int renameFile(const TCHAR * src, const TCHAR * dst, const int verbose);
int createHardLink(const TCHAR * src, const TCHAR * dst, const int verbose);
int copyFile(const TCHAR * src, const TCHAR * dst, const tCopyMask mask, const int verbose);
int copyAttributes(const TCHAR * src, const TCHAR * dst, const tAttrMask mask, const int verbose);
int isNewerFile(const TCHAR * src, const TCHAR * dst, const int verbose);


#endif /* __LSYNC_H__ */
