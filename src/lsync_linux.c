/**
 * @file lsync_linux.c
 * @author Daniel Starke
 * @date 2017-05-22
 * @version 2017-05-24
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
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "lsync.h"


#ifdef UNICODE
#error "Build configuration not supported. Please undefine UNICODE."
#endif


/**
 * Writes the last Windows API error messages to standard output.
 * 
 * @param[in] obj - object that would had been created/modified
 * @param[in] msg - prefix output with this message
 */
static void printLastError(const TCHAR * obj, const TCHAR * msg) {
	fprintf(stderr, "%s:", obj);
	perror(msg);
}


/**
 * Deletes the given object if it exists and is not a directory.
 * 
 * @param[in] dst - path element to remove
 * @return 1 on success, 0 else
 */
static int deleteIfFile(const TCHAR * dst, const int verbose) {
	if (isFile(dst) != 0) {
		if (unlink(dst) < 0) {
			if (verbose > 0) printLastError(dst, "unlink():"TO_STR2(__LINE__));
			return 0;
		}
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
	if (dst[1] == ':' && len < 4) return 1;
	TCHAR * end = NULL; /* pointer to the end of the current path part */
	TCHAR * dir = malloc(sizeof(TCHAR) * (len + 1));
	memcpy(dir, dst, sizeof(TCHAR) * len);
	dir[len] = 0;
	if (dir == NULL) {
		if (verbose > 0) {
			fprintf(
				stderr,
				"Error: Failed to allocate %u bytes of memory.\n",
				(unsigned)(sizeof(TCHAR) * _tcslen(dst))
			);
		}
		goto onError;
	}
	for (;;) {
		if (end == NULL) {
			if (*dir == '/') {
				end = _tcspbrk(dir + 4, PATH_SEPS); /* absolute path */
			} else {
				end = _tcspbrk(dir, PATH_SEPS); /* relative path */
			}
		} else {
			*end = '/';
			end = _tcspbrk(end + 1, PATH_SEPS);
		}
		if (isDirectory(dir) == 0) {
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
 * Creates a hard-link for the destination pointing to the given path at source.
 * The function overwrites the destination file or hardlink.
 * 
 * @param[in] src - source path
 * @param[in] dst - destination path
 * @param[in] verbose - verbosity level
 * @return 1 on success, 0 on failure
 */
int createHardLink(const TCHAR * src, const TCHAR * dst, const int verbose) {
	if (src == NULL || dst == NULL) return 0;
	if (deleteIfFile(dst, verbose) == 0) return 0;
	if (link(src, dst) < 0) {
		if (verbose > 0) printLastError(dst, "link():"TO_STR2(__LINE__));
		return 0;
	}
	if (verbose > 1) {
		_ftprintf(stdout, "Created hardlink \"%s\" pointing to \"%s\".\n", dst, src);
	}
	return 1;
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
	int in = 0;
	int out = 0;
	char buffer[16384];
	char * outPtr;
	ssize_t got, done;
	struct stat stats;
	if (stat(src, &stats) < 0) {
		if (verbose > 0) printLastError(src, "stat():"TO_STR2(__LINE__));
		return 0;
	}
	/* handle devices */
	if (S_ISCHR(stats.st_mode) || S_ISBLK(stats.st_mode) || S_ISSOCK(stats.st_mode)) {
		if ((mask & CP_DEVICES) != 0) {
			if (deleteIfFile(dst, verbose) == 0) return 0;
			if (mknod(dst, stats.st_mode, stats.st_rdev) < 0) {
				if (verbose > 0) printLastError(dst, "mknod():"TO_STR2(__LINE__));
				return 0;
			}
			if (verbose > 1) {
				fprintf(stdout, "Copied device \"%s\" to \"%s\".\n", src, dst);
			}
		}
		return 1;
	}
	/* handle symbolic links */
	if ( S_ISLNK(stats.st_mode) ) {
		if ((mask & CP_LINKS) != 0) {
			if (stats.st_size <= 0) {
				if (verbose > 0) fprintf(stderr, "%s:stat():"TO_STR2(__LINE__)": Failed to get link name size.\n", src);
				return 0;
			}
			if (deleteIfFile(dst, verbose) == 0) return 0;
			char * buf = malloc(sizeof(char) * (stats.st_size + 1));
			if (buf == NULL) {
				if (verbose > 0) fprintf(stderr, "Failed to allocate %u bytes.\n", (unsigned)(sizeof(char) * (stats.st_size + 1)));
				return 0;
			}
			errno = 0;
			if (readlink(src, buf, sizeof(char) * stats.st_size) < stats.st_size) {
				if (verbose > 0) {
					if (errno != 0) {
						printLastError(src, "readlink():"TO_STR2(__LINE__));
					} else {
						fprintf(stderr, "%s:readlink():"TO_STR2(__LINE__)": Failed to read link name.\n", src);
					}
				}
				free(buf);
				return 0;
			}
			buf[stats.st_size] = 0;
			if (symlink(dst, buf) < 0) {
				if (verbose > 0) printLastError(dst, "symlink():"TO_STR2(__LINE__));
				free(buf);
				return 0;
			}
			free(buf);
			if (verbose > 1) {
				fprintf(stdout, "Copied symbolic link \"%s\" to \"%s\".\n", src, dst);
			}
		}
		return 1;
	}
	/* handle special types */
	if ( S_ISFIFO(stats.st_mode) ) {
		if ((mask & CP_SPECIALS) != 0) {
			if (mkfifo(dst, 0777) < 0) {
				if (verbose > 0) printLastError(dst, "mkfifo():"TO_STR2(__LINE__));
				return 0;
			}
			if (verbose > 1) {
				fprintf(stdout, "Copied fifo \"%s\" to \"%s\".\n", src, dst);
			}
		}
		return 1;
	}
	/* handle regular files */
	if (deleteIfFile(dst, verbose) == 0) return 0;
	
	in = open(src, O_RDONLY | O_LARGEFILE);
	if (in < 0) {
		if (verbose > 0) printLastError(src, "open():"TO_STR2(__LINE__));
		goto onError;
	}
	
	out = open(dst, O_WRONLY | O_CREAT | O_LARGEFILE | O_EXCL, 0777);
	if (out < 0) {
		if (verbose > 0) printLastError(dst, "open():"TO_STR2(__LINE__));
		goto onError;
	}
	
	do {
		errno = 0;
		while ((got = read(in, buffer, sizeof(buffer))) > 0) {
			if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
				if (verbose > 0) printLastError(src, "read():"TO_STR2(__LINE__));
				goto onError;
			}
			outPtr = buffer;
			while ((outPtr - buffer) < got) {
				errno = 0;
				done = write(out, outPtr, got - (outPtr - buffer));
				if (done < 0) done = 0;
				outPtr += done;
				if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
					if (verbose > 0) printLastError(dst, "write():"TO_STR2(__LINE__));
					goto onError;
				}
			}
		}
	} while ((errno == 0 || errno == EAGAIN || errno == EWOULDBLOCK) && got != 0);
	
	result = 1;
	if (verbose > 1) {
		fprintf(stdout, "Copied file \"%s\" to \"%s\".\n", src, dst);
	}
onError:
	if (in > 0) close(in);
	if (out > 0) close(out);
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
	if (stat(src, &stats) < 0) {
		if (verbose > 0) printLastError(src, "stat():"TO_STR2(__LINE__));
		return 0;
	}
	if ((mask & (AT_GROUP | AT_OWNER)) != 0) {
		if (chown(dst, ((mask & AT_OWNER) != 0) ? stats.st_uid : (uid_t)(-1), ((mask & AT_GROUP) != 0) ? stats.st_gid : (gid_t)(-1)) < 0) {
			if (verbose > 0) printLastError(dst, "chown():"TO_STR2(__LINE__));
			goto onError;
		}
	}
	if ((mask & AT_PERMS) != 0) {
		if (chmod(dst, stats.st_mode) < 0) {
			if (verbose > 0) printLastError(dst, "chmod():"TO_STR2(__LINE__));
			goto onError;
		}
	}
#if defined(_BSD_SOURCE) || defined(_SVID_SOURCE) || (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L) || (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 700)
	/* copy times with nanosecond precision */
	struct timespec times[2];
	/* last access time */
	times[0] = stats.st_atim;
	/* last modification time */
	times[1] = stats.st_mtim;
	if (utimensat(AT_FDCWD, dst, times, 0) < 0) {
		if (verbose > 0) printLastError(dst, "utimensat():"TO_STR2(__LINE__));
		goto onError;
	}
#else
	/* copy times with microsecond precision */
	struct timeval times[2];
	/* last access time */
	times[0].tv_sec = stats.st_atime;
	times[0].tv_usec = stats.st_atimensec / 1000;
	/* last modification time */
	times[1].tv_sec = stats.st_mtime;
	times[1].tv_usec = stats.st_mtimensec / 1000;
	if (utimes(dst, times) < 0) {
		if (verbose > 0) printLastError(dst, "utimes():"TO_STR2(__LINE__));
		goto onError;
	}
#endif
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
 * @return 1 if dst is newer than src, 0 else and -1 on error
 */
int isNewerFile(const TCHAR * src, const TCHAR * dst, const int verbose) {
	if (src == NULL || dst == NULL) return -1;
	int result = -1;
	struct stat srcStats, dstStats;
	if (stat(src, &srcStats) < 0) {
		if (verbose > 0) printLastError(src, "stat():"TO_STR2(__LINE__));
		goto onError;
	}
	if (stat(dst, &dstStats) < 0) {
		result = 0;
		goto onError;
	}
	result = 0;
	if (srcStats.st_size == dstStats.st_size) {
		if (srcStats.st_mtime < dstStats.st_mtime || srcStats.st_ctime < dstStats.st_ctime) {
			result = 1;
		}
	} else {
		result = 1;
	}
onError:
	return result;
}
