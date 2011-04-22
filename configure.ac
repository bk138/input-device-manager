AC_PREREQ([2.57])
AC_INIT(gnome-device-setup, [0.2.1], [peter.hutterer@who-t.net])
AM_INIT_AUTOMAKE([dist-bzip2])

AM_CONFIG_HEADER(config.h)
AC_PROG_CC
AC_PROG_INSTALL

PKG_CHECK_MODULES(GTK, gtk+-2.0)
PKG_CHECK_MODULES(XI, [inputproto >= 1.4.99] [xi >= 1.1.99])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)
AC_SUBST(XI_CFLAGS)
AC_SUBST(XI_LIBS)
AC_SUBST(VERSION)

AC_OUTPUT([Makefile
           src/Makefile])
