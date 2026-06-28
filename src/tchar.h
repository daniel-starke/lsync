/**
 * @file tchar.h
 * @author Daniel Starke
 * @date 2014-05-04
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
#ifndef __TCHAR_H__
#define __TCHAR_H__

#if defined(__MINGW32__) || defined(_MSC_VER)
# if defined(_UNICODE) || defined(UNICODE)
#  undef _UNICODE
#  define _UNICODE 1
#  undef UNICODE
#  define UNICODE 1
#  include <wchar.h>
# endif /* defined(_UNICODE) || defined(UNICODE) */
# ifdef __MINGW32__
#  include <sys/stat.h>
# endif
#else /* not __MINGW32__ */
# include <string.h>
#endif


#ifdef UNICODE
typedef wchar_t TCHAR;
#define _T2(x) _T(x)
#define _T(x) L##x
/* std C */
#define _totupper towupper
#define _istprint iswprint
#define _tcslen wcslen
#define _tcscmp wcscmp
#define _tcsncmp wcsncmp
#define _tcsnicmp _wcsnicmp
#define _tcsrchr wcsrchr
#define _tcspbrk wcspbrk
#define _tcschr wcschr
#define _ttoi _wtoi
#define _fgetts fgetws
#define _fputts _fputws
#define _putts _putws
#define _tprintf wprintf
#define _ftprintf fwprintf
#define _sntprintf snwprintf
#define _tfopen _wfopen
#define _tstat wstat
#define _tcserror _wcserror
#define PCF_PATH_SEPT PCF_PATH_SEPU

#else /* not UNICODE */
typedef char TCHAR;
#define _T2(x) _T(x)
#define _T(x) x
/* std C */
#define _totupper toupper
#define _istprint isprint
#define _tcslen strlen
#define _tcscmp strcmp
#define _tcsncmp strncmp
#if defined(__MINGW32__) || defined(_MSC_VER)
#define _tcsnicmp _strnicmp
#else
#define _tcsnicmp strncasecmp
#endif
#define _tcsrchr strrchr
#define _tcspbrk strpbrk
#define _tcschr strchr
#define _ttoi atoi
#define _fgetts fgets
#define _fputts fputs
#define _putts puts
#define _tprintf printf
#define _ftprintf fprintf
#define _sntprintf snprintf
#define _tfopen fopen
#define _tstat stat
#define _tcserror strerror
#define PCF_PATH_SEPT PCF_PATH_SEP

#endif /* not UNICODE */


const TCHAR * _tcsrpbrk(const TCHAR * str1, const TCHAR * str2);


#ifdef _MSC_VER
# define wstat _wstat
# define stat _stat
# define fseeko64 _fseeki64
# define ftello64(x) _telli64(_fileno(x))
# define S_ISREG(x) ((x) & _S_IFREG) == _S_IFREG
# define S_ISDIR(x) ((x) & _S_IFDIR) == _S_IFDIR
# define UINT64_FMT _T("%I64u")
#else /* not _MSC_VER */
#if ! defined(__MINGW64__) && defined(__MINGW32__)
extern __attribute__((__dllimport__)) FILE * _fdopen(int, const char *);
extern __attribute__((__dllimport__)) FILE * _wfdopen(int, const wchar_t *);
extern __attribute__((__dllimport__)) int _wtoi(const wchar_t *);
extern __attribute__((__dllimport__)) FILE * _fopen(const char *, const char *);
extern __attribute__((__dllimport__)) FILE * _wfopen(const wchar_t *, const wchar_t *);
#endif
# ifdef __MINGW32__
#  if ! defined(__MINGW64_VERSION_MAJOR)
#   define wstat _wstat
#   define stat _stat
#   ifdef UNICODE
#    ifndef _O_U16TEXT
#     define _O_U16TEXT 0x20000
#    endif /* _O_U16TEXT */
#    ifndef _O_U8TEXT
#     define _O_U8TEXT 0x40000
#    endif /* _O_U8TEXT */
#   endif /* UNICODE */
#  elif defined(_FILE_OFFSET_BITS) && (_FILE_OFFSET_BITS == 64)
#   ifdef _USE_32BIT_TIME_T
#    define stat _stat32i64
#    define wstat _wstat32i64
#   else /* _USE_32BIT_TIME_T */
#    define stat _stat64
#    define wstat _wstat64
#   endif /* _USE_32BIT_TIME_T */
#  endif
# endif /* __MINGW32__ */
# define UINT64_FMT _T("%" PRIu64)
#endif /* not _MSC_VER */


#endif /* __TCHAR_H__ */
