#!/bin/bash

#      Copyright (C) 2005-2008 Team XBMC
#      http://www.xbmc.org
#
#  This Program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2, or (at your option)
#  any later version.
#
#  This Program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with XBMC; see the file COPYING.  If not, write to
#  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
#  http://www.gnu.org/copyleft/gpl.html

THISDIR=$(pwd)
WORKDIR=workarea

. $THISDIR/getInstallers.sh
. $THISDIR/mkConfig.sh

build()
{
	cd $THISDIR/$WORKDIR

	lh bootstrap
	lh chroot

	# safeguard against crashes
	lh chroot_devpts remove
	lh chroot_proc remove
	lh chroot_sysfs remove
	
	for modulesdir in chroot/lib/modules/*
	do
		umount $modulesdir/volatile &> /dev/null
	done

	cd $THISDIR
}

if ! which lh > /dev/null ; then
	echo "A required package (live-helper) is not available, exiting..."
	exit 1
fi

#
#
#
mkdir -p Files/chroot_local-includes/root &> /dev/null

# Get drivers if files are not already available
if [ ! -f crystalhd-HEAD.tar.gz ]; then
	getBCDriversSources
else
	mv crystalhd-HEAD.tar.gz Files/chroot_local-includes/root
fi

if [ -z "$DONOTBUILDRESTRICTEDDRIVERS" ]; then
	if ! ls NVIDIA*.run > /dev/null 2>&1 ; then
		getNVIDIAInstaller
	else
		mv NVIDIA*.run Files/chroot_local-includes/root
	fi

	if ! ls ati*.run > /dev/null 2>&1 ; then
		getAMDInstaller
	else
		mv ati*.run Files/chroot_local-includes/root
	fi
else
	rm Files/chroot_local-hooks/20-buildAMD.sh
	rm Files/chroot_local-hooks/30-buildNVIDIA.sh
fi


# Clean any previous run
rm -rf *.ext3 &> /dev/null

rm -rf $WORKDIR &> /dev/null
mkdir -p "$THISDIR/$WORKDIR"
cd "$THISDIR/$WORKDIR"

# Create config tree
makeConfig

# Create chroot and build drivers
build

# Get files from chroot
cp $WORKDIR/chroot/tmp/*.ext3 . &> /dev/null
cp $WORKDIR/chroot/tmp/crystalhd.tar .
