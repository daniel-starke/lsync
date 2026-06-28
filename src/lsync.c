/**
 * @file lsync.c
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
#include "getopt.h"
#include "mingw-unicode.h"
#include "lsync.h"

#if defined(PCF_IS_WIN)
#define PATH_SEPS _T("\\/")
#define PATH_NCMP _tcsnicmp /* Windows paths are case-insensitive */
#include "lsync-win.c"
#elif defined(PCF_IS_LINUX)
#define PATH_SEPS _T("/")
#define PATH_NCMP _tcsncmp
#include "lsync-linux.c"
#else
#error "Unsupported target platform."
#endif


volatile sig_atomic_t signalReceived = 0;


/**
 * Main entry point.
 */
int _tmain(int argc, TCHAR ** argv) {
	int res = EXIT_FAILURE;
	TCHAR * buffer = NULL;
	char POSIXLY_CORRECT[] = "POSIXLY_CORRECT=";
	tContext ctx;
	memset(&ctx, 0, sizeof(ctx));
	struct option longOptions[] = {
		{_T("link-dest"), required_argument, NULL,           GETOPT_LINK_DEST},
		{_T("version"),   no_argument,       NULL,           GETOPT_VERSION},
		{_T("devices"),   no_argument,       &ctx.devices,       0 },
		{_T("specials"),  no_argument,       &ctx.specials,      0 },
		{_T("archive"),   no_argument,       NULL,           _T('a')},
		{_T(""),          no_argument,       NULL,           _T('D')},
		{_T("group"),     no_argument,       NULL,           _T('g')},
		{_T("help"),      no_argument,       NULL,           _T('h')},
		{_T("links"),     no_argument,       &ctx.links,     _T('l')},
		{_T("owner"),     no_argument,       &ctx.owner,     _T('o')},
		{_T("perms"),     no_argument,       &ctx.perms,     _T('p')},
		{_T("recursive"), no_argument,       &ctx.recursive, _T('r')},
		{_T("times"),     no_argument,       &ctx.times,     _T('t')},
		{_T("verbose"),   no_argument,       NULL,           _T('v')},
		{NULL, 0, NULL, 0}
	};

	/* ensure that the environment does not change the argument parser behavior */
	putenv(POSIXLY_CORRECT);
	
#ifdef UNICODE
	/* http://msdn.microsoft.com/en-us/library/z0kc8e3z(v=vs.80).aspx */
	_setmode(_fileno(stdout), _O_U16TEXT);
	_setmode(_fileno(stderr), _O_U16TEXT);
#endif

	if (argc < 2) {
		printHelp();
		return EXIT_FAILURE;
	}

	for (;;) {
		res = getopt_long(argc, argv, _T(":aDghloprtv"), longOptions, NULL);

		if (res == -1) break;
		switch (res) {
		case GETOPT_LINK_DEST:
			ctx.linkDest = optarg;
			break;
		case GETOPT_VERSION:
			_putts(PROGRAM_VERSION);
			res = EXIT_SUCCESS;
			goto onError;
		case _T('a'):
			ctx.devices   = 1;
			ctx.group     = 1;
			ctx.links     = 1;
			ctx.owner     = 1;
			ctx.perms     = 1;
			ctx.recursive = 1;
			ctx.specials  = 1;
			ctx.times     = 1;
			break;
		case _T('D'):
			ctx.devices  = 1;
			ctx.specials = 1;
			break;
		case _T('g'):
			ctx.group = 1;
			break;
		case 0:
		case _T('l'):
		case _T('o'):
		case _T('p'):
		case _T('r'):
		case _T('t'):
			break; /* already handled by flags */
		case _T('v'):
			ctx.verbose++;
			break;
		case _T('h'):
			printHelp();
			res = EXIT_SUCCESS;
			goto onError;
		case _T(':'):
			_ftprintf(stderr, _T("Error: Option argument is missing for '%s'.\n"), argv[optind - 1]);
			res = EXIT_FAILURE;
			goto onError;
		case _T('?'):
			if (_istprint(optopt) != 0) {
				_ftprintf(stderr, _T("Error: Unknown or ambiguous option '-%c'.\n"), optopt);
			} else if (optopt == 0) {
				_ftprintf(stderr, _T("Error: Unknown or ambiguous option '%s'.\n"), argv[optind - 1]);
			} else {
				_ftprintf(stderr, _T("Error: Unknown option character '0x%02X'.\n"), (int)optopt);
			}
			res = EXIT_FAILURE;
			goto onError;
		default:
			abort();
		}
	}

	res = EXIT_FAILURE; /* default on goto onError from here on */

	if (optind >= argc) {
		_ftprintf(stderr, _T("Error: Missing source and destination path.\n"));
		goto onError;
	} else if ((optind + 1) >= argc) {
		_ftprintf(stderr, _T("Error: Missing destination path.\n"));
		goto onError;
	}

	/* prepare options */
	ctx.srcArgs = argv + optind;
	ctx.srcCount = argc - optind - 1;
	ctx.dstArg = argv[argc - 1];
	buffer = (TCHAR *)malloc(sizeof(TCHAR) * (BUFFER_SIZE * 2));
	if (buffer == NULL) {
		_ftprintf(stderr, _T("Error: Failed to allocate %u bytes.\n"), (unsigned)(sizeof(TCHAR) * (BUFFER_SIZE * 2)));
		goto onError;
	}
	ctx.dst = buffer;
	ctx.ref = buffer + BUFFER_SIZE;
	ctx.attrMask = (tAttrMask)(
		  ((ctx.group != 0) ? AT_GROUP : AT_NONE)
		| ((ctx.owner != 0) ? AT_OWNER : AT_NONE)
		| ((ctx.perms != 0) ? AT_PERMS : AT_NONE)
		| ((ctx.times != 0) ? AT_TIMES : AT_NONE)
	);
	ctx.copyMask = (tCopyMask)(
		  ((ctx.devices  != 0) ? CP_DEVICES  : CP_NONE)
		| ((ctx.links    != 0) ? CP_LINKS    : CP_NONE)
		| ((ctx.specials != 0) ? CP_SPECIALS : CP_NONE)
	);
	/* be verbose by default */
	ctx.verbose++;

	/* single file destination check */
	{
		const size_t argLen = _tcslen(ctx.dstArg);
		const int hasTrailingSep = (argLen > 0 && _tcschr(PATH_SEPS, ctx.dstArg[argLen - 1]) != NULL);
		if (ctx.srcCount == 1 && hasTrailingSep == 0 && isDirectory(ctx.dstArg) == 0 && (isSymlink(ctx.srcArgs[0]) != 0 || isFile(ctx.srcArgs[0]) != 0)) {
			ctx.dstIsFile = 1;
		}
	}

	/* install signal handlers */
	signalReceived = 0;
	signal(SIGINT, handleSignal);
	signal(SIGTERM, handleSignal);

	if (ctx.dstIsFile != 0) {
		/* create parent directory of the destination file */
		const TCHAR * sep = _tcsrpbrk(ctx.dstArg, PATH_SEPS);
		if (sep != NULL && sep != ctx.dstArg) {
			const size_t parentLen = (size_t)(sep - ctx.dstArg);
			if (parentLen < BUFFER_SIZE) {
				memcpy(ctx.dst, ctx.dstArg, sizeof(TCHAR) * parentLen);
				ctx.dst[parentLen] = 0;
				if (createDirectory(ctx.dst, ctx.verbose) == 0) goto onError;
			}
		}
	} else {
		if (createDirectory(ctx.dstArg, ctx.verbose) == 0) goto onError;
	}
	for (ctx.srcIndex = 0; signalReceived == 0 && ctx.srcIndex < ctx.srcCount; ctx.srcIndex++) {
		TCHAR * src = ctx.srcArgs[ctx.srcIndex];
		if (isSymlink(src) != 0 || isFile(src) != 0) {
			/* file / symbolic link copied as a link (or skipped) and never followed */
			/* separator after a link resolves to the target (handled below)  */
			if (backupVisitor(src, NULL, NULL, 0, 0, &ctx) == 0) goto onError; /* signal */
			if (ctx.verbose > 1) _ftprintf(stderr, _T("Finished backing up \"%s\".\n"), src);
		} else if (isDirectory(src) != 0) {
			if (destWithinSource(src, ctx.dstArg) != 0) {
				/* endless recursion case */
				_ftprintf(stderr, _T("Error: Cannot back up directory \"%s\" into itself \"%s\".\n"), src, ctx.dstArg);
				ctx.hadError = 1;
				continue;
			}
			/* needed to create output folder (errors are flagged inside, not fatal) */
			ctx.rootModified = 0;
			backupVisitor(src, NULL, NULL, 1, 0, &ctx);
			/* process directory tree */
			const int visited = td_traverse(src, (ctx.recursive == 0) ? 0 : -1, TDO_DIRECTORY | TDO_ITEM | TDO_ERRORS, backupVisitor, &ctx);
			ds_consume(&ctx.dirStack, 0, dirStackFinalize, &ctx);
			if (visited != 1 && visited != -1) goto onError; /* visitor aborted (signal) */
			if (visited == -1) {
				/* partial backup due to errors -> keep going */
				ctx.hadError = 1;
			}
			/* correct the root directory timestamp if any top-level child was written */
			if (ctx.rootModified != 0 && (ctx.attrMask & AT_TIMES) != 0 && signalReceived == 0) {
				backupVisitor(src, NULL, NULL, 1, 0, &ctx);
			}
			if (ctx.verbose > 1) _ftprintf(stderr, _T("Finished backing up \"%s\".\n"), src);
		} else {
			_ftprintf(stderr, _T("Error: Could not find source \"%s\".\n"), src);
			ctx.hadError = 1;
		}
	}

	res = (signalReceived != 0) ? EXIT_SIGNAL : ((ctx.hadError != 0) ? EXIT_PARTIAL : EXIT_SUCCESS);
onError:
	if (buffer != NULL) free(buffer);
	ds_clear(&ctx.dirStack);
	return res;
}


/**
 * Write the help for this application to standard out.
 */
void printHelp() {
	_tprintf(
	_T("lsync [options] [<source> ...] <destination>\n")
	_T("\n")
	_T("This is free and unencumbered software released into the public domain.\n")
	_T("\n")
	_T("-a, --archive\n")
	_T("      Archive mode (same as -rlptgoD).\n")
	_T("    --devices\n")
	_T("      Preserves device files.\n")
	_T("-D\n")
	_T("      Same as --devices --specials.\n")
	_T("-g, --group\n")
	_T("      Preserves group.\n")
	_T("-h, --help\n")
	_T("      Print short usage instruction.\n")
	_T("    --link-dest <reference>\n")
	_T("      Hardlink to files from reference in destination if unchanged.\n")
	_T("-l, --links\n")
	_T("      Copy symlinks as symlinks.\n")
	_T("-o, --owner\n")
	_T("      Preserves owner.\n")
	_T("-p  --perms\n")
	_T("      Preserves permissions.\n")
	_T("-r, --recursive\n")
	_T("      Traverses given directories recursive.\n")
	_T("    --specials\n")
	_T("      Preserves special files.\n")
	_T("-t, --times\n")
	_T("      Preserves modification times.\n")
	_T("-v\n")
	_T("      Increases verbosity.\n")
	_T("    --version\n")
	_T("      Outputs the program version.\n")
	_T("\n")
	_T("lsync %s\n")
	_T("https://github.com/daniel-starke/lsync\n")
	, PROGRAM_VERSION);
}


/**
 * Handles external signals.
 * 
 * @param[in] signum - received signal number
 */
void handleSignal(int signum) {
	PCF_UNUSED(signum)
	signalReceived = 1;
	/* signal handlers may only call async-signal-safe functions -> avoid non-reentrant stdio */
	static const char msg[] = "Received signal. Finishing current operation.\n";
#if defined(PCF_IS_WIN)
	DWORD written;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)(sizeof(msg) - 1), &written, NULL);
#else
	write(STDERR_FILENO, msg, sizeof(msg) - 1);
#endif
}


/**
 * Joins "base/item" or, if rel is not NULL, "base/item/rel" into the given buffer.
 *
 * @param[out] buf - destination buffer
 * @param[in] len - destination buffer length in characters
 * @param[in] base - leading path part
 * @param[in] item - path element appended to base
 * @param[in] rel - trailing path part appended after item, or NULL to omit
 * @return 1 on success, 0 on truncation
 */
int joinPath(TCHAR * buf, const size_t len, const TCHAR * base, const TCHAR * item, const TCHAR * rel) {
	const int n = (rel == NULL)
		? _sntprintf(buf, len, _T("%s") _T2(PCF_PATH_SEP) _T("%s"), base, item)
		: _sntprintf(buf, len, _T("%s") _T2(PCF_PATH_SEP) _T("%s") _T2(PCF_PATH_SEP) _T("%s"), base, item, rel);
	buf[len - 1] = 0;
	return (n >= 0 && (size_t)n < len) ? 1 : 0;
}


/**
 * Tests whether the destination is at or below the source directory. Such an overlap would
 * make the backup copy the source into its own subtree and recurse infinitely.
 *
 * @param[in] src - source directory
 * @param[in] dst - destination directory
 * @return 1 if dst is src or within it, 0 otherwise or on error
 */
int destWithinSource(const TCHAR * src, const TCHAR * dst) {
	TCHAR * realSrc = (TCHAR *)malloc(sizeof(TCHAR) * BUFFER_SIZE * 2);
	int res = 0;
	if (realSrc == NULL) return 0;
	TCHAR * realDst = realSrc + BUFFER_SIZE;
	if (realPath(src, realSrc, BUFFER_SIZE) != 0 && realPath(dst, realDst, BUFFER_SIZE) != 0) {
		const size_t len = _tcslen(realSrc);
		if (PATH_NCMP(realSrc, realDst, len) == 0 && (realDst[len] == 0 || _tcschr(PATH_SEPS, realDst[len]) != NULL)) {
			res = 1;
		}
	}
	free(realSrc);
	return res;
}


/**
 * Directory stack finalizer. Re-applies the modification time if the subtree changed.
 *
 * @param[in] frame - directory frame being finalized
 * @param[in,out] param - backup processing context
 */
void dirStackFinalize(const tDirStackFrame * frame, void * param) {
	tContext * ctx = (tContext *)param;
	if (frame->modified != 0 && signalReceived == 0 && (ctx->attrMask & AT_TIMES) != 0) {
		if (copyAttributes(frame->src, frame->dst, (tAttrMask)(ctx->attrMask & AT_TIMES), ctx->verbose) == 0) {
			if (ctx->verbose > 0) _ftprintf(stderr, _T("Warning: Failed to correct timestamps on \"%s\".\n"), frame->dst);
			ctx->hadError = 1;
		}
	}
}


/**
 * Marks the parent directory of the item currently processed as modified so its
 * timestamps are corrected when its subtree completes.
 *
 * @param[in,out] ctx - backup processing context
 */
void dirStackMarkParent(tContext * ctx) {
	if (ds_markTop(&ctx->dirStack) == 0) {
		ctx->rootModified = 1;
	}
}


/**
 * Traversing visitor to back-up a single path object.
 *
 * @param[in] src - full path to the object to backup
 * @param[in] item - file name of the object to backup (NULL for root/single-file calls from main)
 * @param[in] ext - file extension of the object to backup
 * @param[in] flags - item flags
 * @param[in] level - current recursion depth
 * @param[in] param - backup parameters (see tContext)
 * @return 1 on success, 0 else
 */
int backupVisitor(const TCHAR * src, const TCHAR * item, const TCHAR * ext, const int flags,
	const unsigned int level, void * param) {
	if (signalReceived != 0) return 0;
	PCF_UNUSED(ext)
	tContext * ctx = (tContext *)param;
	/* ignore root and single file calls */
	const int fromTraversal = (item != NULL);
	if ((flags & TDF_ERROR) != 0) {
		if ( fromTraversal ) ds_consume(&ctx->dirStack, level, dirStackFinalize, ctx);
		_ftprintf(stderr, _T("Error: Failed to read directory \"%s\".\n"), src);
		ctx->hadError = 1;
		return 1;
	}
	/* symlinks (file or directory) are reported but never descended -> copy as a link */
	int itemFlags = flags;
	if (fromTraversal != 0 && (flags & TDF_LINK) != 0) {
		itemFlags = TDF_FILE;
	}
	/* without --recursive sub directories are skipped entirely */
	if (itemFlags == TDF_DIR && fromTraversal && ctx->recursive == 0) {
		ds_consume(&ctx->dirStack, level, dirStackFinalize, ctx);
		return 1;
	}
	/* finalize directories whose subtree is now complete before handling this item */
	if ( fromTraversal ) ds_consume(&ctx->dirStack, level, dirStackFinalize, ctx);
	const TCHAR * srcArg = ctx->srcArgs[ctx->srcIndex];
	size_t srcLen = _tcslen(srcArg);
	int ok;
	const int trailingSep = (srcLen > 0 && _tcschr(PATH_SEPS, srcArg[srcLen - 1]) != NULL);
	const TCHAR * srcBase = _tcsrpbrk(srcArg, PATH_SEPS);
	if (srcBase == NULL) {
		srcBase = srcArg;
	} else {
		srcBase++; /* skip path separator */
	}
	if (_tcslen(src) > srcLen && trailingSep == 0) {
		srcLen++; /* skip separator between source and relative path */
	}
	const TCHAR * rel = src + srcLen; /* path relative to source ("" for the root) */
	/* construct destination path */
	if (ctx->dstIsFile != 0) {
		/* single file copied to an explicit destination file path */
		const size_t dstArgLen = _tcslen(ctx->dstArg);
		ok = (dstArgLen < BUFFER_SIZE) ? 1 : 0;
		if (ok != 0) memcpy(ctx->dst, ctx->dstArg, sizeof(TCHAR) * (dstArgLen + 1));
	} else if (itemFlags == TDF_FILE && item == NULL) {
		/* single file */
		ok = joinPath(ctx->dst, BUFFER_SIZE, ctx->dstArg, srcBase, NULL);
	} else if (trailingSep != 0) {
		/* "src/" copies the contents of src into the destination */
		ok = joinPath(ctx->dst, BUFFER_SIZE, ctx->dstArg, rel, NULL);
	} else {
		/* "src" copies the src directory itself into the destination */
		ok = joinPath(ctx->dst, BUFFER_SIZE, ctx->dstArg, srcBase, rel);
	}
	if (ok == 0) {
		if (ctx->verbose > 0) _ftprintf(stderr, _T("Error: Destination path \"%s\" is too long.\n"), ctx->dst);
		ctx->hadError = 1;
		return 1; /* ignore this path */
	}
	/* backup source to destination */
	if (itemFlags == TDF_DIR) {
		if (createDirectory(ctx->dst, ctx->verbose) == 0) {
			/* recoverable single directory failure -> partial backup, keep going */
			_ftprintf(stderr, _T("Error: Failed creating destination path \"%s\".\n"), ctx->dst);
			ctx->hadError = 1;
			return 1;
		}
		/* update parent's mtime on sub directory creation */
		if ( fromTraversal ) dirStackMarkParent(ctx);
		/* "src/" copies the contents into the destination root and leaves the
		 * root's own attributes/timestamps untouched (like rsync) */
		if ((item != NULL || trailingSep == 0)
			&& copyAttributes(src, ctx->dst, ctx->attrMask, ctx->verbose) == 0) {
			if (ctx->verbose > 0) {
				_ftprintf(stderr, _T("Warning: Failed to copy attributes to \"%s\".\n"), ctx->dst);
			}
			ctx->hadError = 1;
		}
		/* defer directory timestamp until subtree is done */
		if (fromTraversal && (ctx->attrMask & AT_TIMES) != 0) {
			if (ds_push(&ctx->dirStack, src, ctx->dst, level) == 0) ctx->hadError = 1;
		}
		return 1;
	} else if (itemFlags == TDF_FILE) {
		int wrote = 0;
		int hardlinked = 0;
		if (ctx->linkDest == NULL) {
			/* no reference directory: copy only when missing or changed */
			if (isNewerFile(ctx->dst, src, 0) != 0) {
				if (copyFile(src, ctx->dst, ctx->copyMask, ctx->verbose) == 0) {
					ctx->hadError = 1;
					return 1;
				}
				wrote = 1;
			}
		} else {
			/* construct reference file path (same layout as destination) */
			if (ctx->dstIsFile != 0) {
				/* reference same name under reference directory */
				const TCHAR * dstBase = _tcsrpbrk(ctx->dstArg, PATH_SEPS);
				dstBase = (dstBase == NULL) ? ctx->dstArg : (dstBase + 1);
				ok = joinPath(ctx->ref, BUFFER_SIZE, ctx->linkDest, dstBase, NULL);
			} else if (itemFlags == TDF_FILE && item == NULL) {
				ok = joinPath(ctx->ref, BUFFER_SIZE, ctx->linkDest, srcBase, NULL);
			} else if (trailingSep != 0) {
				ok = joinPath(ctx->ref, BUFFER_SIZE, ctx->linkDest, rel, NULL);
			} else {
				ok = joinPath(ctx->ref, BUFFER_SIZE, ctx->linkDest, srcBase, rel);
			}
			if (ok == 0) {
				if (ctx->verbose > 0) _ftprintf(stderr, _T("Error: Reference path \"%s\" is too long.\n"), ctx->ref);
				ctx->hadError = 1;
				return 1; /* ignore this path */
			}
			switch (isNewerFile(ctx->ref, src, 0)) {
			case 0: /* source matches reference */
				if (createHardLink(ctx->ref, ctx->dst, ctx->verbose) == 0) {
					/* fallback to copy on hardlink error */
					if (ctx->verbose > 0) {
						_ftprintf(stderr, _T("Warning: Hardlink at \"%s\" failed. Falling back to copy.\n"), ctx->dst);
					}
					if (isNewerFile(ctx->dst, src, 0) != 0) {
						if (copyFile(src, ctx->dst, ctx->copyMask, ctx->verbose) == 0) {
							ctx->hadError = 1;
							return 1;
						}
					}
				} else {
					hardlinked = 1;
				}
				wrote = 1;
				break;
			case 1: /* source differs from reference */
			default: /* reference or source does not exist */
				if (isNewerFile(ctx->dst, src, 0) != 0) {
					if (copyFile(src, ctx->dst, ctx->copyMask, ctx->verbose) == 0) {
						ctx->hadError = 1;
						return 1;
					}
					wrote = 1;
				}
				break;
			}
		}
		/* never copy attributes to a hardlinked destination */
		if (hardlinked == 0) {
			if (copyAttributes(src, ctx->dst, ctx->attrMask, ctx->verbose) == 0) {
				if (ctx->verbose > 0) {
					_ftprintf(stderr, _T("Warning: Failed to copy attributes to \"%s\".\n"), ctx->dst);
				}
				ctx->hadError = 1; /* attributes not fully preserved -> partial backup */
			}
		}
		if (wrote != 0 && fromTraversal) dirStackMarkParent(ctx);
		return 1;
	}
	return 1;
}
