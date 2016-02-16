#!/usr/bin/env bash

autoscan
sed -f- configure.scan > >(tee configure.ac) <<HERE
s/^AC_INIT(.*)/AC_INIT([socket_multiplexer],[0.0.1],[takei.yuya@gmail.com])/
s/^AC_CONFIG_SRCDIR(.*)/AC_CONFIG_SRCDIR([config.h.in])/
/^# Checks for programs/iAC_CONFIG_MACRO_DIR([m4])
/^# Checks for programs/iAC_CONFIG_AUX_DIR([build-aux])
/^# Checks for programs/iAM_INIT_AUTOMAKE([foreign])
/^# Checks for programs/i
/^AC_PROG_CXX/aAX_CHECK_COMPILE_FLAG([-std=c++11], [CXXFLAGS="$CXXFLAGS -std=c++11"])
/^# Checks for libraries./aAC_CHECK_LIB([pthread], [pthread_create])
HERE
./autogen.sh
