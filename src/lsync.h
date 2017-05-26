/**
 * @file lsync.h
 * @author Daniel Starke
 * @date 2017-05-17
 * @version 2017-05-25
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


#define PROGRAM_VERSION _T("1.0.0 2017-05-25")


#define BUFFER_SIZE 32768


#ifdef UNICODE
#include "tdirus.h"
#define TDO_DIRECTORY TDUSO_DIRECTORY
#define TDO_ITEM TDUSO_ITEM
#define TDO_FOLLOW_LINKS TDUSO_FOLLOW_LINKS
#define TDO_ALL TDUSO_ALL
#define td_traverse tdus_traverse
#else
#include "tdirs.h"
#define TDO_DIRECTORY TDSO_DIRECTORY
#define TDO_ITEM TDSO_ITEM
#define TDO_FOLLOW_LINKS TDSO_FOLLOW_LINKS
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
	AT_ALL = AT_GROUP | AT_OWNER | AT_PERMS
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
	int verbose;
	TCHAR * linkDest;
	TCHAR ** srcArgs;
	int srcIndex;
	int srcCount;
	TCHAR * dstArg;
	TCHAR * dst; /* destination path string buffer to avoid allocations */
	TCHAR * ref; /* reference path string buffer to avoid allocations */
	tAttrMask attrMask;
	tCopyMask copyMask;
} tOptions;


extern volatile int signalReceived;


void printHelp();
void handleSignal(int signum);
int backupVisitor(const TCHAR * src, const TCHAR * item, const TCHAR * ext, const int isDir,
	const unsigned int level, void * param);


/* I/O operations */
int isFile(const TCHAR * src);
int isDirectory(const TCHAR * src);
int createDirectory(const TCHAR * dst, const int verbose);
int createHardLink(const TCHAR * src, const TCHAR * dst, const int verbose);
int copyFile(const TCHAR * src, const TCHAR * dst, const tCopyMask mask, const int verbose);
int copyAttributes(const TCHAR * src, const TCHAR * dst, const tAttrMask mask, const int verbose);
int isNewerFile(const TCHAR * src, const TCHAR * dst, const int verbose);


#endif /* __LSYNC_H__ */
