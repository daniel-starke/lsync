/**
 * @file mingw-unicode.h
 * @author Daniel Starke
 * @date 2013-02-16
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
#ifndef __MINGW_UNICODE_H__
#define __MINGW_UNICODE_H__


#undef _tmain
#ifdef _UNICODE
#define _tmain wmain
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#define _tmain main
#endif

#if ((defined(__GNUC__) || defined(__TINYC__)) && defined(_UNICODE))

#ifndef __MSVCRT__
#error Unicode main function requires linking to MSVCRT
#endif

#include <wchar.h>
#include <stdlib.h>

#if ! defined(__MINGW64_VERSION_MAJOR)
# define stat _stat
# define wstat _wstat
#elif defined(_FILE_OFFSET_BITS) && (_FILE_OFFSET_BITS == 64)
# ifdef _USE_32BIT_TIME_T
#  define stat _stat32i64
#  define wstat _wstat32i64
# else /* _USE_32BIT_TIME_T*/
#  define stat _stat64
#  define wstat _wstat64
# endif /* _USE_32BIT_TIME_T */
#endif

#ifdef __TINYC__
int _CRT_glob = 0;
#else
extern int _CRT_glob;
#endif
extern void __wgetmainargs(int *, wchar_t ***, wchar_t ***, int, int *);

#ifdef MAIN_USE_ENVP
__attribute__((externally_visible))
int wmain(int argc, wchar_t * argv[], wchar_t * envp[]);
#else
__attribute__((externally_visible))
int wmain(int argc, wchar_t * argv[]);
#endif

int main() {
	wchar_t ** enpv, ** argv;
	int argc, si = 0;
	/* this also creates the global variable __wargv */
	__wgetmainargs(&argc, &argv, &enpv, _CRT_glob, &si);
#ifdef MAIN_USE_ENVP
	return wmain(argc, argv, enpv);
#else
	return wmain(argc, argv);
#endif
}


#endif /* defined(__GNUC__) && defined(_UNICODE) */

#endif /* __MINGW_UNICODE_H__ */
