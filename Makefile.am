ACLOCAL_AMFLAGS =	-I m4

lib_LTLIBRARIES =	libudev.la
include_HEADERS =	libudev.h

libudev_la_SOURCES =	udev.c			\
			udev-device.c		\
			udev-device.h		\
			udev-enumerate.c	\
			udev-filter.c		\
			udev-filter.h		\
			udev-list.c		\
			udev-list.h		\
			udev-monitor.c		\
			udev-utils.c		\
			udev-utils.h		\
			utils.c			\
			utils.h

libudev_la_LDFLAGS =	-pthread
libudev_la_CFLAGS =	-I$(top_srcdir) -Wall -Werror -fvisibility=hidden

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libudev.pc
