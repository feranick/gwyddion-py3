dnl Create simple --enable option $1, creating enable_$2 with help text $3,
dnl Default for $2 is $1.  Default option value is $4 (yes if not given).
AC_DEFUN([GWY_ENABLE],
[
AC_ARG_ENABLE([$1],
  AS_HELP_STRING([--enable-$1],
                 [$3 @<:@default=ifelse($4,,yes,$4)@:>@]),
  [case "${enableval}" in
     yes|no) ifelse([$2],,[enable_$1="$enableval"],[enable_$2="$enableval"]) ;;
     *) AC_MSG_ERROR(bad value ${enableval} for --enable-$1) ;;
   esac],
  [ifelse($2,,[enable_$1=ifelse($4,,yes,$4)],[enable_$2=ifelse($4,,yes,$4)])])
])

dnl Create simple --with option $1, creating enable_$2 with help text $3,
dnl Default for $2 is $1.  Default option value is $4 (yes if not given).
AC_DEFUN([GWY_WITH],
[
AC_ARG_WITH([$1],
  AS_HELP_STRING([--with-$1],
                 [$3 @<:@default=ifelse($4,,yes,$4)@:>@]),
  [case "${withval}" in
     yes|no) ifelse([$2],,[enable_$1="$withval"],[enable_$2="$withval"]) ;;
     *) AC_MSG_ERROR(bad value ${withval} for --with-$1) ;;
   esac],
  [ifelse($2,,[enable_$1=ifelse($4,,yes,$4)],[enable_$2=ifelse($4,,yes,$4)])])
])
