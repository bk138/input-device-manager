AC_PREREQ([2.57])
AC_INIT(gnome-device-setup, [0.2.1], [peter.hutterer@who-t.net])
AM_INIT_AUTOMAKE([dist-bzip2])

AM_CONFIG_HEADER(config.h)
AC_PROG_CC
AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG([0.20]) 

# check for gtk
PKG_CHECK_MODULES([GTK3], [gtk+-3.0 >= 2.99.3])
AC_SUBST(GTK3_CFLAGS)
AC_SUBST(GTK3_LIBS)

# check for XInput
PKG_CHECK_MODULES([XINPUT2], [xi >= 1.3])
AC_SUBST(XINPUT2_CFLAGS)
AC_SUBST(XINPUT2_LIBS)

AC_OUTPUT([Makefile
           src/Makefile])
