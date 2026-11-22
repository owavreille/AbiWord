
SUBDIRS = xp

if LLM_BUILTIN
noinst_LTLIBRARIES = llm.la

else

plugindir = $(ABIWORD_PLUGINSDIR)
plugin_LTLIBRARIES = llm.la

endif

llm_la_LIBADD = \
	xp/libxp.la \
	$(LLM_LIBS)

llm_la_LDFLAGS = \
	-avoid-version \
	-module \
	-no-undefined

llm_la_SOURCES =
nodist_EXTRA_llm_la_SOURCES = dummy.cpp

check_PROGRAMS = test_llm
TESTS = test_llm

test_llm_CPPFLAGS = \
	-DHAVE_CONFIG_H \
	$(LLM_CFLAGS)

test_llm_LDADD = \
	$(LLM_LIBS)

test_llm_SOURCES = \
	xp/ABI_LLM_Client.cpp \
	xp/ABI_LLM_Client.h \
	xp/t/test_LLM.cpp

EXTRA_DIST = \
	plugin.m4
