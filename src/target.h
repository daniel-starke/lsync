/**
 * @file target.h
 * @author Daniel Starke
 * @date 2012-12-08
 * @version 2017-05-23
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
#ifndef __LIBPCF_TARGET_H__
#define __LIBPCF_TARGET_H__

#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


#ifndef TO_STR
/** Converts the passed argument to a string literal. */
# define TO_STR(x) #x
#endif /* to string */

#ifndef TO_STR2
/** Converts the passed argument to a string literal. */
# define TO_STR2(x) TO_STR(x)
#endif /* to string */


#if defined(__WIN32__) || defined(__WIN64__) || defined(WIN32) \
 || defined(WINNT) || defined(_WIN32) || defined(__WIN32) || defined(__WINNT) \
 || defined(__MINGW32__) || defined(__MINGW64__)
# ifndef PCF_IS_WIN
/** Defined if compiler target is windows. */
#  define PCF_IS_WIN 1
# endif
# undef PCF_IS_NO_WIN
#else /* no windows */
# ifndef PCF_IS_NO_WIN
/** Defined if compiler target is _not_ windows. */
#  define PCF_IS_NO_WIN 1
# endif
# undef PCF_IS_WIN
#endif /* windows */


#if !defined(PCF_IS_WIN) && (defined(unix) || defined(__unix) || defined(__unix__) \
 || defined(__gnu_linux__) || defined(linux) || defined(__linux) \
 || defined(__linux__))
# ifndef PCF_IS_LINUX
/** Defined if compiler target is linux/unix. */
# define PCF_IS_LINUX 1
# endif
# undef PCF_IS_NO_LINUX
#else /* no linux */
# ifndef PCF_IS_NO_LINUX
/** Defined if compiler target is _not_ linux/unix. */
#  define PCF_IS_NO_LINUX 1
# endif
# undef PCF_IS_LINUX
#endif /* linux */


#ifdef PCF_PATH_SEP
# undef PCF_PATH_SEP
#endif
#ifdef PCF_IS_WIN
/** Defines the Windows path separator. */
# define PCF_PATH_SEP "\\"
# define PCF_PATH_SEPU L"\\"
#else /* PCF_IS_NO_WIN */
/** Defines the non-Windows path separator. */
# define PCF_PATH_SEP "/"
# define PCF_PATH_SEPU L"/"
#endif /* PCF_IS_WIN */


/* conditional OpenMP pragma */
#ifdef _OPENMP
# if defined(__clang__)
#  define PCF_DO_OMP(x) _Pragma(TO_STR(omp x))
# elif defined(__ICC) && __ICC > 1110
#  define PCF_DO_OMP(x) _Pragma(TO_STR(omp x))
# elif defined(__GNUC__) && ((__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
#  define PCF_DO_OMP(x) _Pragma(TO_STR(omp x))
# elif defined(_MSC_VER) && _MSC_VER >= 1400
#  define PCF_DO_OMP(x) __pragma(omp x)
# else
#  define PCF_DO_OMP(x)
# endif
/* version specific OpenMP pragmas */
# if _OPENMP >= 199810 /* 1.0 */
#  define PCF_DO_OMPv10(x) PCF_DO_OMP(x)
# endif
# if _OPENMP >= 200203 /* 2.0 */
#  define PCF_DO_OMPv20(x) PCF_DO_OMP(x)
# endif
# if _OPENMP >= 200505 /* 2.5 */
#  define PCF_DO_OMPv25(x) PCF_DO_OMP(x)
# endif
# if _OPENMP >= 200805 /* 3.0 */
#  define PCF_DO_OMPv30(x) PCF_DO_OMP(x)
# endif
# if _OPENMP >= 201107 /* 3.1 */
#  define PCF_DO_OMPv31(x) PCF_DO_OMP(x)
# endif
# if _OPENMP >= 201307 /* 4.0 */
#  define PCF_DO_OMPv40(x) PCF_DO_OMP(x)
# endif
#else
# define PCF_DO_OMP(x)
#endif

/* version specific OpenMP pragmas */
#ifndef PCF_DO_OMPv10
# define PCF_DO_OMPv10(x)
#endif
#ifndef PCF_DO_OMPv20
# define PCF_DO_OMPv20(x)
#endif
#ifndef PCF_DO_OMPv25
# define PCF_DO_OMPv25(x)
#endif
#ifndef PCF_DO_OMPv30
# define PCF_DO_OMPv30(x)
#endif
#ifndef PCF_DO_OMPv31
# define PCF_DO_OMPv31(x)
#endif
#ifndef PCF_DO_OMPv40
# define PCF_DO_OMPv40(x)
#endif


#define PCF_MIN(x, y) ((x) > (y) ? (y) : (x))
#define PCF_MAX(x, y) ((x) >= (y) ? (x) : (y))


/* suppress unused parameter warning */
#ifdef _MSC_VER
# define PCF_UNUSED(x)
#else /* not _MSC_VER */
# define PCF_UNUSED(x) (void)x;
#endif /* not _MSC_VER */


/* Windows workarounds */
#ifdef PCF_IS_WIN
# define fileno _fileno
# define fdopen _fdopen
# define setmode _setmode
# define get_osfhandle _get_osfhandle
# define open_osfhandle _open_osfhandle
# define O_TEXT _O_TEXT
# define O_WTEXT _O_WTEXT
# define O_U8TEXT _O_U8TEXT
# define O_U16TEXT _O_U16TEXT
# define O_BINARY _O_BINARY
# define O_RDONLY _O_RDONLY
#endif


/* MSVS workarounds */
#ifdef _MSC_VER
# define snprintf _snprintf
# define snwprintf _snwprintf
# define vsnprintf _vsnprintf
# define vsnwprintf _vsnwprintf
# define va_copy(dest, src) (dest = src)
#endif /* _MSC_VER */


/* Cygwin workaround */
#ifdef __CYGWIN__
# define O_U8TEXT 0x00040000
# define O_U16TEXT 0x00020000
#endif /* __CYGWIN__ */


/* Define vsnwprintf from vswprintf on Linux systems. */
#ifdef PCF_IS_NO_WIN
# define vsnwprintf vswprintf
#endif


#ifdef __cplusplus
}
#endif


#endif /* __LIBPCF_TARGET_H__ */
