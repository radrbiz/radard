#ifndef RIPPLE_THRIFT_H_INCLUDED
#define RIPPLE_THRIFT_H_INCLUDED

#if RIPPLE_THRIFT_AVAILABLE

#include <beast/Config.h>

# define HAVE_CONFIG_H

# if BEAST_LINUX
#  define HAVE_CLOCK_GETTIME 1
#  define HAVE_GETHOSTBYNAME_R 1
#  define HAVE_LIBINTL_H 1
#  define HAVE_LIBRT 1
#  define HAVE_MALLOC_H 1
#  define HAVE_STDBOOL_H 1
#  define LSTAT_FOLLOWS_SLASHED_SYMLINK 1
#  define STRERROR_R_CHAR_P 1
# endif

#include <thrift/Thrift.h>

#endif

#endif
