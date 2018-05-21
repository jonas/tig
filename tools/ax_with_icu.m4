dnl Copyright (c) 2006-2017 Jonas Fonseca <jonas.fonseca@gmail.com>
dnl
dnl This program is free software; you can redistribute it and/or
dnl modify it under the terms of the GNU General Public License as
dnl published by the Free Software Foundation; either version 2 of
dnl the License, or (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl SYNOPSIS
dnl
dnl	AX_WITH_ICU([version, [components]])
dnl
dnl	  version default value is 0
dnl
dnl	  components default value is icu-uc
dnl
dnl DESCRIPTION
dnl
dnl	Defines
dnl
dnl	  ICU_CFLAGS
dnl	  ICU_LDFLAGS
dnl	  ICU_LIBS
dnl
dnl	If pkg-config fails, falls back to the deprecated icu-config
dnl	utility.  icu-config is not capable of selecting a subset of
dnl	components to link, and will ignore the "components" argument
dnl	to the macro.
dnl
dnl	Components recognized by pkg-config are:
dnl
dnl	  icu-uc	Common (uc) and Data (dt/data) libraries
dnl	  icu-i18n	Internationalization (in/i18n) library
dnl	  icu-lx	Paragraph Layout library
dnl	  icu-io	Ustdio/iostream library (icuio)
dnl
dnl	The user may override the location of pkg-config or icu-config
dnl	via environment variables $PKG_CONFIG and $ICU_CONFIG.
dnl
dnl EXAMPLE
dnl
dnl	AX_WITH_ICU([50.0], [icu-uc icu-i18n])
dnl

AC_DEFUN([AX_WITH_ICU], [
	AC_ARG_WITH([icu], AS_HELP_STRING([--with-icu], [Build with libicu support]))

	_libicu_min_version="$1"
	_libicu_components="$2"
	test x = x"$_libicu_min_version" && _libicu_min_version=0
	test x = x"$_libicu_components"  && _libicu_components=icu-uc

	_libicu_pkg_config="${PKG_CONFIG:-pkg-config}"
	_libicu_icu_config="${ICU_CONFIG:-icu-config}"

	AC_SUBST(ICU_CFLAGS)
	AC_SUBST(ICU_LDFLAGS)
	AC_SUBST(ICU_LIBS)

	AS_IF([test "x$with_icu" = "xyes"], [

		AC_MSG_CHECKING([whether pkg-config can query libicu])
		AS_IF([test x != x"$("$_libicu_pkg_config" --modversion icu-uc 2>/dev/null)"], [
			AC_MSG_RESULT([yes])
		], [ dnl else
			AC_MSG_RESULT([no])
			AC_MSG_CHECKING([whether icu-config can query libicu])
			AS_IF([test x != x"$("$_libicu_icu_config" --version 2>/dev/null)"], [
				AC_MSG_RESULT([yes])
			], [ dnl else
				AC_MSG_RESULT([no])
				AC_MSG_ERROR([neither pkg-config nor icu-config is able to query local libicu configuration])
			])
		])

		AC_MSG_CHECKING([for libicu version >= $_libicu_min_version])
		ICU_VERSION="$(("$_libicu_pkg_config" --modversion "$_libicu_components" 2>/dev/null || "$_libicu_icu_config" --version || printf "%s\n" "-1") | head -1)"
		AS_IF([test x"$(expr "$ICU_VERSION" ">=" "$_libicu_min_version")" = x1], [

			# libicu success
			AC_MSG_RESULT([$ICU_VERSION])
			AC_DEFINE(HAVE_ICU, [1], [Define to 1 if libicu is present])
			ICU_CFLAGS="$( "$_libicu_pkg_config" --cflags      "$_libicu_components" 2>/dev/null || "$_libicu_icu_config" --cflags)"
			ICU_LDFLAGS="$("$_libicu_pkg_config" --libs-only-L "$_libicu_components" 2>/dev/null || "$_libicu_icu_config" --ldflags-searchpath)"
			ICU_LIBS="$(   "$_libicu_pkg_config" --libs-only-l "$_libicu_components" 2>/dev/null || "$_libicu_icu_config" --ldflags-libsonly)"

		], [ dnl else
			AC_MSG_RESULT([$ICU_VERSION])
			AC_MSG_ERROR([required libicu version not found])
		])

	])
])
