/**
 * @file lsync-linux.c
 * @author Daniel Starke
 * @date 2017-05-22
 * @version 2026-06-20
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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "lsync.h"


#ifdef UNICODE
#error "Build configuration not supported. Please undefine UNICODE."
#endif


/**
 * Writes the last error message (errno) to standard error.
 *
 * @param[in] obj - object that would had been created/modified
 * @param[in] msg - prefix output with this message
 */
static void printLastError(const TCHAR * obj, const TCHAR * msg) {
	fprintf(stderr, "%s:", obj);
	perror(msg);
}


/**
 * Removes the given path. Directories are removed recursively. Symlinks are
 * unlinked without following them.
 *
 * @param[in] path - path to remove
 * @param[in] verbose - verbosity level
 * @return 1 on success (or if the path does not exist), 0 on failure
 */
static int removePath(const TCHAR * path, const int verbose) {
	struct stat stats;
	if (lstat(path, &stats) < 0) {
		if (errno == ENOENT) return 1; /* nothing to remove */
		if (verbose > 0) printLastError(path, "lstat():"TO_STR2(__LINE__));
		return 0;
	}
	if (S_ISDIR(stats.st_mode)) {
		DIR * dp = opendir(path);
		struct dirent * item;
		int result = 1;
		const size_t pathLen = strlen(path);
		if (dp == NULL) {
			if (verbose > 0) printLastError(path, "opendir():"TO_STR2(__LINE__));
			return 0;
		}
		for (;;) {
			errno = 0;
			item = readdir(dp);
			if (item == NULL) {
				if (errno != 0) {
					if (verbose > 0) printLastError(path, "readdir():"TO_STR2(__LINE__));
					result = 0;
				}
				break;
			}
			if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
			const size_t childLen = pathLen + 1 + strlen(item->d_name) + 1;
			char * child = (char *)malloc(childLen);
			if (child == NULL) {
				result = 0;
				break;
			}
			snprintf(child, childLen, "%s/%s", path, item->d_name);
			if (removePath(child, verbose) == 0) result = 0;
			free(child);
			if (result == 0) break;
		}
		closedir(dp);
		if (result == 0) return 0;
		if (rmdir(path) < 0) {
			if (verbose > 0) printLastError(path, "rmdir():"TO_STR2(__LINE__));
			return 0;
		}
		return 1;
	}
	if (unlink(path) < 0) {
		if (verbose > 0) printLastError(path, "unlink():"TO_STR2(__LINE__));
		return 0;
	}
	return 1;
}


/**
 * Checks if the given path exists and is not a directory.
 * 
 * @param[in] src - check this path
 * @return 1 if src exists and is not a directory, else 0
 */
int isFile(const TCHAR * src) {
	struct stat stats;
	if (stat(src, &stats) == 0 && !S_ISDIR(stats.st_mode)) return 1;
	return 0;
}


/**
 * Checks if the given path exists and is a directory.
 * 
 * @param[in] src - check this path
 * @return 1 if src is a directory, else 0
 */
int isDirectory(const TCHAR * src) {
	struct stat stats;
	if (stat(src, &stats) == 0 && S_ISDIR(stats.st_mode)) return 1;
	return 0;
}


/**
 * Checks if the given path is a symlink (a trailing separator resolves the target).
 *
 * @param[in] src - check this path
 * @return 1 if src is a symlink, else 0
 */
int isSymlink(const TCHAR * src) {
	struct stat stats;
	if (lstat(src, &stats) == 0 && S_ISLNK(stats.st_mode)) return 1;
	return 0;
}


/**
 * Resolves the given path to its canonical absolute form.
 *
 * @param[in] path - path to resolve (must exist)
 * @param[out] buf - buffer receiving the canonical path
 * @param[in] len - size of buf in characters
 * @return 1 on success, 0 on failure
 */
int realPath(const TCHAR * path, TCHAR * buf, const size_t len) {
	char resolved[PATH_MAX];
	if (realpath(path, resolved) == NULL || strlen(resolved) >= len) return 0;
	strcpy(buf, resolved);
	return 1;
}


/**
 * Creates the passed directory path recursively.
 * 
 * @param[in] dst - destination path
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int createDirectory(const TCHAR * dst, const int verbose) {
	if (dst == NULL || *dst == 0) return 0;
	int result = 0;
	const size_t len = _tcslen(dst);
	TCHAR * end = NULL; /* pointer to the end of the current path part */
	TCHAR * dir = malloc(sizeof(TCHAR) * (len + 1));
	if (dir == NULL) {
		if (verbose > 0) {
			fprintf(
				stderr,
				"Error: Failed to allocate %u bytes of memory.\n",
				(unsigned)(sizeof(TCHAR) * (len + 1))
			);
		}
		goto onError;
	}
	memcpy(dir, dst, sizeof(TCHAR) * len);
	dir[len] = 0;
	for (;;) {
		if (end == NULL) {
			/* skip the leading separator of an absolute path */
			end = _tcspbrk(((*dir == '/') ? dir + 1 : dir), PATH_SEPS);
		} else {
			*end = '/'; /* restore the separator removed before */
			end = _tcspbrk(end + 1, PATH_SEPS);
		}
		if (end != NULL) *end = 0; /* trim to current path prefix */
		if (isDirectory(dir) == 0) {
			/* replace a non-directory at same path */
			struct stat st;
			if (lstat(dir, &st) == 0 && !S_ISDIR(st.st_mode)) {
				if (removePath(dir, verbose) == 0) goto onError;
			}
			if (mkdir(dir, 0777) < 0) {
				if (verbose > 0) printLastError(dir, "mkdir():"TO_STR2(__LINE__));
				goto onError;
			}
			if (verbose > 1) {
				fprintf(stdout, "Created directory \"%s\".\n", dir);
			}
		}
		if (end == NULL) break;
	}
	result = 1;
onError:
	if (dir != NULL) free(dir);
	return result;
}


/**
 * Reserves a unique temporary file name next to `dst` (same directory and volume) so it can later
 * be renamed into place atomically. The allocated name is stored in `tmp` for the caller to free.
 *
 * @param[in] dst - final destination path the temporary belongs to
 * @param[out] tmp - receives the allocated temporary path (NULL on failure)
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int createTempName(const TCHAR * dst, TCHAR ** tmp, const int verbose) {
	const size_t dstLen = strlen(dst);
	int fd;
	TCHAR * name = (TCHAR *)malloc(dstLen + 8); /* dst + ".XXXXXX" + NUL */
	*tmp = NULL;
	if (name == NULL) {
		if (verbose > 0) fprintf(stderr, "Failed to allocate %u bytes.\n", (unsigned)(dstLen + 8));
		return 0;
	}
	memcpy(name, dst, dstLen);
	memcpy(name + dstLen, ".XXXXXX", 8); /* copies trailing NUL too */
	fd = mkstemp(name);
	if (fd < 0) {
		if (verbose > 0) printLastError(name, "mkstemp():"TO_STR2(__LINE__));
		free(name);
		return 0;
	}
	close(fd);
	unlink(name); /* only the file name is needed, not the file itself */
	*tmp = name;
	return 1;
}


/**
 * Atomically rename `src` to `dst` and replace any existing destination.
 *
 * @param[in] src - path to rename
 * @param[in] dst - destination path
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int renameFile(const TCHAR * src, const TCHAR * dst, const int verbose) {
	if (rename(src, dst) < 0) {
		if (verbose > 0) printLastError(dst, "rename():"TO_STR2(__LINE__));
		return 0;
	}
	return 1;
}


/**
 * Creates a hardlink for the destination pointing to the given path at source.
 * The function overwrites the destination file or hardlink.
 *
 * @param[in] src - source path
 * @param[in] dst - destination path
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int createHardLink(const TCHAR * src, const TCHAR * dst, const int verbose) {
	if (src == NULL || dst == NULL) return 0;
	int result = 0;
	TCHAR * tmp = NULL;
	/* link under a temporary name and rename() it so a failed link never
	 * destroys the existing destination (atomic replace) */
	if (createTempName(dst, &tmp, verbose) == 0) return 0;
	if (link(src, tmp) < 0) {
		if (verbose > 0) printLastError(tmp, "link():"TO_STR2(__LINE__));
		goto onError;
	}
	if (renameFile(tmp, dst, verbose) == 0) goto onError;
	result = 1;
	if (verbose > 1) {
		_ftprintf(stdout, "Created hardlink \"%s\" pointing to \"%s\".\n", dst, src);
	}
onError:
	if (result == 0 && tmp != NULL) unlink(tmp);
	free(tmp);
	return result;
}


/**
 * Copies the source file to the destination file. The destination needs to be a file path.
 * The function overwrites the destination file or hardlink.
 * 
 * @param[in] src - source file
 * @param[in] dst - destination file
 * @param[in] mask - copy mask
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int copyFile(const TCHAR * src, const TCHAR * dst, const tCopyMask mask, const int verbose) {
	if (src == NULL || dst == NULL) return 0;
	int result = 0;
	int in = -1;
	int out = -1;
	char buffer[16384];
	char * outPtr;
	char * tmp = NULL;
	ssize_t got, done;
	struct stat stats;
	if (lstat(src, &stats) < 0) {
		if (verbose > 0) printLastError(src, "lstat():"TO_STR2(__LINE__));
		return 0;
	}
	/* handle character and block devices */
	if (S_ISCHR(stats.st_mode) || S_ISBLK(stats.st_mode)) {
		if ((mask & CP_DEVICES) == 0) {
			if (verbose > 0) fprintf(stderr, "Skipping device \"%s\" (requires --devices).\n", src);
			return 1;
		}
		/* create under a temporary name and rename() it so a failed create never
		 * destroys the existing destination (atomic replace) */
		if (createTempName(dst, &tmp, verbose) == 0) goto onError;
		if (mknod(tmp, stats.st_mode, stats.st_rdev) < 0) {
			if (verbose > 0) printLastError(tmp, "mknod():"TO_STR2(__LINE__));
			goto onError;
		}
		if (isDirectory(dst) != 0 && removePath(dst, verbose) == 0) goto onError;
		if (renameFile(tmp, dst, verbose) == 0) goto onError;
		result = 1;
		if (verbose > 1) {
			fprintf(stdout, "Copied device \"%s\" to \"%s\".\n", src, dst);
		}
		goto onError;
	}
	/* handle symlinks */
	if ( S_ISLNK(stats.st_mode) ) {
		char * buf = NULL;
		size_t bufSize;
		ssize_t linkLen;
		if ((mask & CP_LINKS) == 0) {
			if (verbose > 0) fprintf(stderr, "Skipping symbolic link \"%s\" (requires --links).\n", src);
			return 1;
		}
		/* st_size holds the target length, but magic symlinks (e.g. under /proc) report 0;
		 * don't trust it -> grow the buffer until readlink no longer fills it completely */
		bufSize = (stats.st_size > 0) ? ((size_t)stats.st_size + 1) : 1024;
		for (;;) {
			char * newBuf = (char *)realloc(buf, bufSize);
			if (newBuf == NULL) {
				if (verbose > 0) fprintf(stderr, "Failed to allocate %u bytes.\n", (unsigned)bufSize);
				free(buf);
				return 0;
			}
			buf = newBuf;
			errno = 0;
			linkLen = readlink(src, buf, bufSize);
			if (linkLen < 0) {
				if (verbose > 0) printLastError(src, "readlink():"TO_STR2(__LINE__));
				free(buf);
				return 0;
			}
			if (((size_t)linkLen) < bufSize) break;
			bufSize *= 2;
		}
		buf[linkLen] = 0;
		/* create under a temporary name and rename() it so a failed create never
		 * destroys the existing destination (atomic replace) */
		if (createTempName(dst, &tmp, verbose) == 0) {
			free(buf);
			goto onError;
		}
		if (symlink(buf, tmp) < 0) {
			if (verbose > 0) printLastError(tmp, "symlink():"TO_STR2(__LINE__));
			free(buf);
			goto onError;
		}
		free(buf);
		if (isDirectory(dst) != 0 && removePath(dst, verbose) == 0) goto onError;
		if (renameFile(tmp, dst, verbose) == 0) goto onError;
		result = 1;
		if (verbose > 1) {
			fprintf(stdout, "Copied symbolic link \"%s\" to \"%s\".\n", src, dst);
		}
		goto onError;
	}
	/* handle special files: named pipes and sockets */
	if ( S_ISFIFO(stats.st_mode) || S_ISSOCK(stats.st_mode) ) {
		if ((mask & CP_SPECIALS) == 0) {
			if (verbose > 0) fprintf(stderr, "Skipping special file \"%s\" (requires --specials).\n", src);
			return 1;
		}
		/* create under a temporary name and rename() it so a failed create never
		 * destroys the existing destination (atomic replace) */
		if (createTempName(dst, &tmp, verbose) == 0) goto onError;
		/* sockets have no device number -> recreate inode with mknod (no privilege
		 * needed for FIFO/socket types, unlike block/character devices) */
		if (S_ISFIFO(stats.st_mode) ? (mkfifo(tmp, 0777) < 0) : (mknod(tmp, stats.st_mode, 0) < 0)) {
			if (verbose > 0) printLastError(tmp, S_ISFIFO(stats.st_mode) ? "mkfifo():"TO_STR2(__LINE__) : "mknod():"TO_STR2(__LINE__));
			goto onError;
		}
		if (isDirectory(dst) != 0 && removePath(dst, verbose) == 0) goto onError;
		if (renameFile(tmp, dst, verbose) == 0) goto onError;
		result = 1;
		if (verbose > 1) {
			fprintf(stdout, "Copied special file \"%s\" to \"%s\".\n", src, dst);
		}
		goto onError;
	}
	/* handle regular files: copy to a temporary file in the destination
	 * directory and rename() it so a failed copy never destroys the existing
	 * destination file (atomic replace). */
	in = open(src, O_RDONLY);
	if (in < 0) {
		if (verbose > 0) printLastError(src, "open():"TO_STR2(__LINE__));
		goto onError;
	}
	if (createTempName(dst, &tmp, verbose) == 0) goto onError;
	out = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (out < 0) {
		if (verbose > 0) printLastError(tmp, "open():"TO_STR2(__LINE__));
		goto onError;
	}
	/* the file is created with mode 0600 -> set it to 0777 masked by current umask */
	const mode_t fileMask = umask(0);
	umask(fileMask);
	if (fchmod(out, (mode_t)(0777 & ~fileMask)) < 0) {
		if (verbose > 0) printLastError(tmp, "fchmod():"TO_STR2(__LINE__));
		goto onError;
	}
	for (;;) {
		got = read(in, buffer, sizeof(buffer));
		if (got < 0) {
			if (errno == EINTR) continue; /* interrupted by a signal -> retry */
			if (verbose > 0) printLastError(src, "read():"TO_STR2(__LINE__));
			goto onError;
		}
		if (got == 0) break; /* end of file */
		outPtr = buffer;
		while (got > 0) {
			done = write(out, outPtr, (size_t)got);
			if (done < 0) {
				if (errno == EINTR) continue; /* interrupted by a signal -> retry */
				if (verbose > 0) printLastError(dst, "write():"TO_STR2(__LINE__));
				goto onError;
			}
			outPtr += done;
			got -= done;
		}
	}
	/* flush and close the temporary file before swapping it */
	if (close(out) < 0) {
		out = -1;
		if (verbose > 0) printLastError(tmp, "close():"TO_STR2(__LINE__));
		goto onError;
	}
	out = -1;
	/* replace a directory at destination path */
	if (isDirectory(dst) != 0) {
		if (removePath(dst, verbose) == 0) goto onError;
	}
	/* atomically replace the destination with the newly written copy */
	if (renameFile(tmp, dst, verbose) == 0) goto onError;
	result = 1;
	if (verbose > 1) {
		fprintf(stdout, "Copied file \"%s\" to \"%s\".\n", src, dst);
	}
onError:
	if (in >= 0) close(in);
	if (out >= 0) close(out);
	if (result == 0 && tmp != NULL) unlink(tmp);
	free(tmp);
	return result;
}


/**
 * Copies the path attributes and security settings for the given path to the destination path
 * according to the mask passed.
 * 
 * @param[in] src - source path
 * @param[in] dst - destination path
 * @param[in] mask - copy mask
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int copyAttributes(const TCHAR * src, const TCHAR * dst, const tAttrMask mask, const int verbose) {
	if (src == NULL || dst == NULL) return 0;
	if (mask == AT_NONE) return 1;
	int result = 0;
	struct stat stats;
	if (lstat(src, &stats) < 0) {
		if (verbose > 0) printLastError(src, "lstat():"TO_STR2(__LINE__));
		return 0;
	}
	if ((mask & (AT_GROUP | AT_OWNER)) != 0) {
		if (lchown(dst, ((mask & AT_OWNER) != 0) ? stats.st_uid : (uid_t)(-1), ((mask & AT_GROUP) != 0) ? stats.st_gid : (gid_t)(-1)) < 0) {
			if (verbose > 0) printLastError(dst, "lchown():"TO_STR2(__LINE__));
			goto onError;
		}
	}
	/* symlinks have no permission bits on Linux -> skip here */
	if ((mask & AT_PERMS) != 0 && ! S_ISLNK(stats.st_mode)) {
		if (chmod(dst, stats.st_mode) < 0) {
			if (verbose > 0) printLastError(dst, "chmod():"TO_STR2(__LINE__));
			goto onError;
		}
	}
	if ((mask & AT_TIMES) != 0) {
#if defined(_BSD_SOURCE) || defined(_SVID_SOURCE) || (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L) || (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 700)
		/* copy times with nanosecond precision */
		struct timespec times[2];
		/* last access time */
		times[0] = stats.st_atim;
		/* last modification time */
		times[1] = stats.st_mtim;
		if (utimensat(AT_FDCWD, dst, times, S_ISLNK(stats.st_mode) ? AT_SYMLINK_NOFOLLOW : 0) < 0) {
			if (verbose > 0) printLastError(dst, "utimensat():"TO_STR2(__LINE__));
			goto onError;
		}
#else
		/* copy times with second precision (sub second fields are not portable here) */
		struct timeval times[2];
		/* last access time */
		times[0].tv_sec = stats.st_atime;
		times[0].tv_usec = 0;
		/* last modification time */
		times[1].tv_sec = stats.st_mtime;
		times[1].tv_usec = 0;
		if (utimes(dst, times) < 0) {
			if (verbose > 0) printLastError(dst, "utimes():"TO_STR2(__LINE__));
			goto onError;
		}
#endif
	}
	if (verbose > 1) {
		fprintf(stdout, "Copied attributes from file \"%s\" to \"%s\".\n", src, dst);
	}
	result = 1;
onError:
	return result;
}


/**
 * Compares source file against destination file to find out which file was
 * modified more recently.
 * 
 * @param[in] src - older file
 * @param[in] dst - newer file
 * @param[in] verbose - verbosity level
 * @return 1 if `dst` differs from `src` (changed), 0 if unchanged and -1 on error
 */
int isNewerFile(const TCHAR * src, const TCHAR * dst, const int verbose) {
	if (src == NULL || dst == NULL) return -1;
	int result = -1;
	struct stat srcStats, dstStats;
	/* matched symlinks by link */
	if (lstat(src, &srcStats) < 0) {
		if (verbose > 0) printLastError(src, "lstat():"TO_STR2(__LINE__));
		goto onError;
	}
	if (lstat(dst, &dstStats) < 0) {
		if (errno != ENOENT) {
			if (verbose > 0) printLastError(dst, "lstat():"TO_STR2(__LINE__));
			result = 1;
		} else {
			result = 0;
		}
		goto onError;
	}
	result = 0;
	if (srcStats.st_size == dstStats.st_size) {
		/* ctime is ignored so that metadata only change still do --link-dest hardlink deduplication */
		if (srcStats.st_mtime != dstStats.st_mtime) {
			result = 1;
		}
	} else {
		result = 1;
	}
onError:
	return result;
}
