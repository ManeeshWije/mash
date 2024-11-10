#ifndef __OS_DETECTION_DEFS_HEADER__
#define __OS_DETECTION_DEFS_HEADER__

/* System include files. */
#ifndef MAKEDEPEND
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif


/*
 * Compilation Environment Identification
 */
#if defined( __NetBSD__ ) || defined( __OpenBSD__ ) || defined( __FreeBSD__ )

#   define OS_BSD



#elif defined( __linux__ )

#   define OS_LINUX



#elif defined( __APPLE__ )

#   define OS_DARWIN



#elif defined( _WIN32 )

#   define OS_WINDOWS



#else
#   error Unknown operating system -- need more defines in os_defs.h!
#endif



/** if we are on one of the Unix type platforms, define OS_UNIX */
#if defined( OS_FREEBSD ) || defined( OS_LINUX ) || defined( OS_DARWIN )

#  define OS_UNIX

#endif


/**
 **	Set up for common requirements, including or defining things
 ** that make portability hard if they are missing.
 **/
#ifndef MAKEDEPEND
#include <limits.h>
#ifdef OS_WINDOWS_NT
#include <stddef.h>
#endif
#endif

#endif /* __OS_DETECTION_DEFS_HEADER__ */
