AC_INIT([fusepod], [0.5.2],
        [Keegan Carruthers-Smith <keegan.csmith@gmail.com>])
AM_INIT_AUTOMAKE

AC_PROG_CXX
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

PKG_CHECK_MODULES(FUSE, [fuse >= 2.5])
PKG_CHECK_MODULES(libgpod, [libgpod-1.0])

AC_PATH_PROG(TAGLIB_CONFIG, taglib-config, no)
if test "x$TAGLIB_CONFIG" = "xno" ; then
    PKG_CHECK_MODULES(taglib, [taglib >= 1.0])
else
    taglib_CFLAGS=`$TAGLIB_CONFIG --cflags`
    taglib_LIBS=[`$TAGLIB_CONFIG --libs`]
fi

CPPFLAGS="$CPPFLAGS -Wall -O2 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 $FUSE_CFLAGS $libgpod_CFLAGS $taglib_CFLAGS"
LIBS="$FUSE_LIBS $libgpod_LIBS $taglib_LIBS"

AC_CONFIG_FILES([Makefile
		src/Makefile
	  	man/Makefile])
AC_OUTPUT 
