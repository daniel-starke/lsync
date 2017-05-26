/**
 * @file lsync.c
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
#include "getopt.h"
#include "mingw-unicode.h"
#include "lsync.h"

#if defined(PCF_IS_WIN)
#define PATH_SEPS _T("\\/")
#include "lsync_win.c"
#elif defined(PCF_IS_LINUX)
#define PATH_SEPS _T("/")
#include "lsync_linux.c"
#else
#error "Unsupported target platform."
#endif


volatile int signalReceived = 0;


/**
 * Main entry point.
 */
int _tmain(int argc, TCHAR ** argv) {
	int res, i;
	TCHAR * buffer = NULL;
	tOptions opt = { 0 }; /* initialize all options with zero */
	struct option longOptions[] = {
		{_T("link-dest"), required_argument, NULL,           GETOPT_LINK_DEST},
		{_T("version"),   no_argument,       NULL,           GETOPT_VERSION},
		{_T("devices"),   no_argument,       &opt.devices,       0 },
		{_T("specials"),  no_argument,       &opt.specials,      0 },
		{_T("archive"),   no_argument,       NULL,           _T('a')},
		{_T(""),          no_argument,       NULL,           _T('D')},
		{_T("group"),     no_argument,       NULL,           _T('g')},
		{_T("help"),      no_argument,       NULL,           _T('h')},
		{_T("links"),     no_argument,       NULL,           _T('l')},
		{_T("owner"),     no_argument,       &opt.owner,     _T('o')},
		{_T("perms"),     no_argument,       &opt.perms,     _T('p')},
		{_T("recursive"), no_argument,       &opt.recursive, _T('r')},
		{_T("verbose"),   no_argument,       NULL,           _T('v')},
		{NULL, 0, NULL, 0}
	};

	/* ensure that the environment does not change the argument parser behavior */
	putenv("POSIXLY_CORRECT=");
	
#ifdef UNICODE
	/* http://msdn.microsoft.com/en-us/library/z0kc8e3z(v=vs.80).aspx */
	_setmode(_fileno(stdout), _O_U16TEXT);
	_setmode(_fileno(stderr), _O_U16TEXT);
#endif

	if (argc < 2) {
		printHelp();
		return EXIT_FAILURE;
	}

	while (1) {
		res = getopt_long(argc, argv, _T(":aDghloprv"), longOptions, NULL);

		if (res == -1) break;
		switch (res) {
		case GETOPT_LINK_DEST:
			opt.linkDest = optarg;
			break;
		case GETOPT_VERSION:
			_putts(PROGRAM_VERSION);
			return EXIT_SUCCESS;
			break;
		case _T('a'):
			opt.devices   = 1;
			opt.group     = 1;
			opt.links     = 1;
			opt.owner     = 1;
			opt.perms     = 1;
			opt.recursive = 1;
			opt.specials  = 1;
			break;
		case 0:
		case _T('D'):
		case _T('g'):
		case _T('l'):
		case _T('o'):
		case _T('p'):
		case _T('r'):
			break; /* already handled by flags */
		case _T('v'):
			opt.verbose++;
			break;
		case _T('h'):
			printHelp();
			return EXIT_SUCCESS;
			break;
		case _T(':'):
			_ftprintf(stderr, _T("Error: Option argument is missing for '%s'.\n"), argv[optind - 1]);
			return EXIT_FAILURE;
			break;
		case _T('?'):
			if (_istprint(optopt) != 0) {
				_ftprintf(stderr, _T("Error: Unknown or ambiguous option '-%c'.\n"), optopt);
			} else if (optopt == 0) {
				_ftprintf(stderr, _T("Error: Unknown or ambiguous option '%s'.\n"), argv[optind - 1]);
			} else {
				_ftprintf(stderr, _T("Error: Unknown option character '0x%0X'.\n"), (int)optopt);
			}
			return EXIT_FAILURE;
			break;
		default:
			abort();
		}
	}
	
	if (optind >= argc) {
		_ftprintf(stderr, _T("Error: Missing source and destination path.\n"));
		return EXIT_FAILURE;
	} else if ((optind + 1) >= argc) {
		_ftprintf(stderr, _T("Error: Missing destination path.\n"));
		return EXIT_FAILURE;
	}
	
	res = EXIT_FAILURE; /* goto onError only occurs from this point onwards */
	
	/* prepare options */
	opt.srcArgs = argv + optind;
	opt.srcCount = argc - optind - 1;
	opt.dstArg = argv[argc - 1];
	buffer = (TCHAR *)malloc(sizeof(TCHAR) * (BUFFER_SIZE * 2));
	if (buffer == NULL) {
		_ftprintf(stderr, _T("Error: Failed to allocate %i bytes.\n"), sizeof(TCHAR) * (BUFFER_SIZE * 2));
		goto onError;
	}
	opt.dst = buffer;
	opt.ref = buffer + BUFFER_SIZE;
	opt.attrMask = (tAttrMask)(
		  ((opt.group != 0) ? AT_GROUP : AT_NONE)
		| ((opt.owner != 0) ? AT_OWNER : AT_NONE)
		| ((opt.perms != 0) ? AT_PERMS : AT_NONE)
	);
	opt.copyMask = (tCopyMask)(
		  ((opt.devices  != 0) ? CP_DEVICES  : CP_NONE)
		| ((opt.links    != 0) ? CP_LINKS    : CP_NONE)
		| ((opt.specials != 0) ? CP_SPECIALS : CP_NONE)
	);
	opt.verbose++;
	
	/* install signal handlers */
	signalReceived = 0;
	signal(SIGINT, handleSignal);
	signal(SIGTERM, handleSignal);
	
	if (createDirectory(opt.dstArg, opt.verbose) == 0) goto onError;
	for (i = optind, opt.srcIndex = 0; signalReceived == 0 && (i + 1) < argc; i++, opt.srcIndex++) {
		if (isFile(argv[i]) != 0) {
			/* process file */
			if (backupVisitor(argv[i], NULL, NULL, 0, 0, &opt) != 0) {
				if (opt.verbose > 1) _ftprintf(stderr, _T("Finished backing up \"%s\".\n"), argv[i]);
			} else {
				_ftprintf(stderr, _T("Error: Failed to backup \"%s\".\n"), argv[i]);
				goto onError;
			}
		} else if (isDirectory(argv[i]) != 0) {
			/* needed to create output folder */
			if (backupVisitor(argv[i], NULL, NULL, 1, 0, &opt) == 0) {
				_ftprintf(stderr, _T("Error: Failed creating destination path \"%s\".\n"), argv[i]);
				goto onError;
			}
			/* process directory tree */
			switch (td_traverse(argv[i], (opt.recursive == 0)? 0 : -1, TDO_DIRECTORY | TDO_ITEM, backupVisitor, &opt)) {
			case 1:
				if (opt.verbose > 1) _ftprintf(stderr, _T("Finished backing up \"%s\".\n"), argv[i]);
				break;
			case 0:
			case -1:
				_ftprintf(stderr, _T("Error: Failed to backup \"%s\".\n"), argv[i]);
				goto onError;
				break;
			default: /* unreachable */ break;
			}
		} else {
			_ftprintf(stderr, _T("Error: Could not find source \"%s\".\n"), argv[i]);
			goto onError;
		}
	}

	res = EXIT_SUCCESS;
onError:
	if (buffer != NULL) free(buffer);
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
	_ftprintf(stdout, _T("Received signal. Finishing current operation.\n"));
	signalReceived++;
}


/**
 * Traversing visitor to back-up a single path object.
 * 
 * @param[in] src - full path to the object to backup
 * @param[in] item - file name of the object to backup
 * @param[in] ext - file extension of the object to backup
 * @param[in] isDir - 1 if path is a directory, 0 else
 * @param[in] level - current recursion depth
 * @param[in] param - backup parameters (see tOptions)
 * @return 1 on success, 0 else
 */
int backupVisitor(const TCHAR * src, const TCHAR * item, const TCHAR * ext, const int isDir,
	const unsigned int level, void * param) {
	if (signalReceived != 0) return 0;
	PCF_UNUSED(item)
	PCF_UNUSED(ext)
	PCF_UNUSED(level)
	tOptions * opt = (tOptions *)param;
	size_t srcLen = _tcslen(opt->srcArgs[opt->srcIndex]);
	/* construct destination path */
	const TCHAR * srcElement = _tcsrpbrk(opt->srcArgs[opt->srcIndex], PATH_SEPS);
	if (srcElement == NULL) {
		srcElement = opt->srcArgs[opt->srcIndex];
	} else {
		srcElement++; /* skip path separator */
	}
	if (_tcslen(src) > srcLen && _tcschr(PATH_SEPS, opt->srcArgs[opt->srcIndex][srcLen - 1]) == NULL) {
		srcLen++; /* skip path separator in src */
	}
	if (isDir == 0 && item == NULL) {
		/* single file */
		if (_sntprintf(opt->dst, BUFFER_SIZE, _T("%s") _T2(PCF_PATH_SEP) _T("%s"), opt->dstArg, srcElement) >= BUFFER_SIZE) {
			if (opt->verbose > 0) _ftprintf(stderr, _T("Error: Destination path \"%s\" is too long.\n"), opt->dst);
			return 0;
		};
	} else {
		/* element in directory tree */
		if (_sntprintf(opt->dst, BUFFER_SIZE, _T("%s") _T2(PCF_PATH_SEP) _T("%s") _T2(PCF_PATH_SEP) _T("%s"), opt->dstArg, srcElement, src + srcLen) >= BUFFER_SIZE) {
			if (opt->verbose > 0) _ftprintf(stderr, _T("Error: Destination path \"%s\" is too long.\n"), opt->dst);
			return 0;
		};
	}
	/* backup source to destination */
	if (isDir != 0) {
		/* create target directory */
		if (createDirectory(opt->dst, opt->verbose) == 0) return 0;
	} else {
		/* backup file */
		if (opt->linkDest == NULL) {
			/* no reference directory given to link against; just copy */
			if (copyFile(src, opt->dst, opt->copyMask, opt->verbose) == 0) return 0;
		} else {
			/* construct reference file path */
			if (isDir == 0 && item == NULL) {
				/* single file */
				if (_sntprintf(opt->ref, BUFFER_SIZE, _T("%s") _T2(PCF_PATH_SEP) _T("%s"), opt->linkDest, srcElement) >= BUFFER_SIZE) {
					if (opt->verbose > 0) _ftprintf(stderr, _T("Error: Destination path \"%s\" is too long.\n"), opt->dst);
					return 0;
				};
			} else {
				/* element in directory tree */
				if (_sntprintf(opt->ref, BUFFER_SIZE, _T("%s") _T2(PCF_PATH_SEP) _T("%s") _T2(PCF_PATH_SEP) _T("%s"), opt->linkDest, srcElement, src + srcLen) >= BUFFER_SIZE) {
					if (opt->verbose > 0) _ftprintf(stderr, _T("Error: Reference path \"%s\" is too long.\n"), opt->ref);
					return 0;
				}
			}
			switch (isNewerFile(opt->ref, src, 0)) {
			case 0: /* destination file is NOT newer than path */
				if (createHardLink(opt->ref, opt->dst, opt->verbose) == 0) {
					/* fall-back to copy on hardlink error */
					if (opt->verbose > 0) {
						_ftprintf(stderr, _T("Warning: Hardlink at \"%s\" failed. Falling back to copy.\n"), opt->dst);
					}
					if (copyFile(src, opt->dst, opt->copyMask, opt->verbose) == 0) return 0;
				}
				break;
			case 1: /* destination file is newer than path */
			default: /* some file does not exist */
				if (isNewerFile(opt->dst, src, 0) != 0) { /* only copy if destination file does not exist or if source is newer */
					if (copyFile(src, opt->dst, opt->copyMask, opt->verbose) == 0) return 0;
				}
				break;
			}
		}
	}
	copyAttributes(src, opt->dst, opt->attrMask, opt->verbose);
	return 1;
}
