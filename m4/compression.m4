AC_DEFUN([kdump_COMPRESSION], [dnl
AC_ARG_WITH([$1],
  [AS_HELP_STRING([--with-$1],
    [support for $1 compression @<:@default=check@:>@])],
  [], [with_$1=check])
AS_IF([test "x$with_$1" != xno],
  [PKG_CHECK_MODULES([$2], [$1],
     [AS_VAR_SET([$2][_REQUIRES],[$1])
      have_$1=yes
     ],[dnl Fall back to searching if there is no pkg-config file
      saved_LIBS="$LIBS"
      AC_SEARCH_LIBS([$4],[$3],[dnl
        AS_VAR_SET([$2][_LIBS], [-l$3])
        AS_VAR_SET([$2][_PC_LIBS], [-l$3])
        have_$1=yes
      ],[dnl
        have_$1=no
      ])
      LIBS="$saved_LIBS"
     ])
  ],[dnl
   have_$1=no
  ])
AS_IF([test "x$have_$1" = xyes],
  [AC_DEFINE(USE_[$2], 1, [Define to enable support for $1 compression])
  ],[
   AS_IF([test "x$with_$1" = xyes],
     [AC_MSG_ERROR([$1 requested but neither pkg-config nor -l$3 found])
     ])
  ])
AC_SUBST([$2][_REQUIRES])
AC_SUBST([$2][_CFLAGS])
AC_SUBST([$2][_LIBS])
AC_SUBST([$2][_PC_LIBS])
])
