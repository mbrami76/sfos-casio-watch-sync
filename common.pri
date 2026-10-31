# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

CONFIG += c++11 warn_on
QMAKE_CXXFLAGS += -Wextra

# Test programs are never installed into the package.
CONFIG += no_testcase_installs
