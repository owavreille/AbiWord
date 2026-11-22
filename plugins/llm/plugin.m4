
LLM_CFLAGS=
LLM_LIBS=

llm_deps="no"

if test "$enable_llm" != ""; then

PKG_CHECK_EXISTS([libcurl],
[
	llm_deps="yes"
], [
	if test "$enable_llm" = "yes"; then
		AC_MSG_ERROR([llm plugin: libcurl est requis])
	else
		AC_MSG_WARN([llm plugin: libcurl est absent, le plugin est ignoré])
	fi
])

fi

if test "$enable_llm" = "yes" || \
   test "$llm_deps" = "yes"; then

PKG_CHECK_MODULES(LLM,[libcurl])

test "$enable_llm" = "auto" && PLUGINS="$PLUGINS llm"

LLM_CFLAGS="$LLM_CFLAGS "'${PLUGIN_CFLAGS}'
LLM_LIBS="$LLM_LIBS "'${PLUGIN_LIBS}'

if test "$enable_llm_builtin" = "yes"; then
	LLM_CFLAGS="$LLM_CFLAGS -DABI_PLUGIN_BUILTIN"
fi

fi

AC_SUBST([LLM_CFLAGS])
AC_SUBST([LLM_LIBS])
