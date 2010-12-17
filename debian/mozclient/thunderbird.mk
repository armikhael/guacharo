# -*- mode: makefile; coding: utf-8 -*-

# Copyright (c) 2007-2009 Fabien Tassin <fta@sofaraway.org>
# Description: Project Thunderbird 3.0
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2, or (at
# your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

MOZCLIENT_PROJECTNAME := thunderbird

-include /usr/share/mozilla-devscripts/mozclient.mk

COMPARE_FILTER_PRE_IN := sed \
	-e 's,foo,foo,' \
	$(NULL)

COMPARE_FILTER_PRE_OUT := sed \
	-e 's,^usr/lib/thunderbird[^/]*/,,' \
	-e 's,^usr/share/pixmaps/.*,,' \
	$(NULL)

COMPARE_FILTER_IN    := sed \
	-e 's,foo,foo,' \
	$(NULL)

COMPARE_FILTER_OUT   := sed \
	-e 's,^DEBIAN/.*,,' \
	-e 's,^usr/lib/debug/.*,,' \
	-e 's,^usr/share/doc/.*,,' \
	-e 's,^usr/share/menu/.*,,' \
	-e 's,^usr/share/applications/.*,,' \
	$(NULL)
