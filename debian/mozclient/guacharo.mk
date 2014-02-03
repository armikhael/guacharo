# -*- mode: makefile; coding: utf-8 -*-

# Copyright (c) 2009 Guido Guenther <agx@sigxcpu.org>
# Description: Project Guacharo 3.0
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

BRANDING_DIR = mail/branding/guacharo/

MOZCLIENT_PROJECTNAME := guacharo
include debian/mozclient/thunderbird.mk

makebuilddir/$(DEB_MOZ_APPLICATION):: debian/stamp-guacharo-branding
debian/stamp-guacharo-branding:
	cp -af debian/guacharo-branding $(BRANDING_DIR)
	cp debian/app-icons/guacharo16.png $(BRANDING_DIR)/mailicon16.png
	cp debian/app-icons/guacharo22.png $(BRANDING_DIR)/mailicon22.png
	cp debian/app-icons/guacharo24.png $(BRANDING_DIR)/mailicon24.png
	cp debian/app-icons/guacharo32.png $(BRANDING_DIR)/mailicon32.png
	cp debian/app-icons/guacharo48.png $(BRANDING_DIR)/mailicon48.png
	cp debian/app-icons/guacharo256.png $(BRANDING_DIR)/mailicon256.png
	cp debian/app-icons/guacharo48.png $(BRANDING_DIR)/content/icon48.png
	cp debian/app-icons/guacharo64.png $(BRANDING_DIR)/content/icon64.png
	cp debian/preview.png mail/themes/gnomestripe/mail/preview.png
	touch $@

