# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# One test executable per module, so module branches do not collide.
TEMPLATE = subdirs
# The device kits have no testlib: tests are built only where it exists.
qtHaveModule(testlib) {
    SUBDIRS += watchproto
    SUBDIRS += notiffilter
    SUBDIRS += settings
    SUBDIRS += scheduler
    SUBDIRS += watchlink
    SUBDIRS += notifmonitor
    SUBDIRS += service
    SUBDIRS += uibridge
}

# On-phone probe for the wake-ups (needs libkeepalive); not part of the host build.
!hostbuild: SUBDIRS += wakeprobe

# On-phone test program for watchlink (needs BlueZ on the system bus); run manually on a phone.
SUBDIRS += watchlink-cli

# On-phone test program for notifmonitor; run manually on a phone.
SUBDIRS += notifmonitor-cli
