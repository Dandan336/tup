#! /bin/sh -e
# tup - A file-based build system
#
# Copyright (C) 2009-2024  Mike Shal <marfey@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# Make sure a ghost variable and a regular variable don't compare as equal if
# the values are equal (ie: both blank).

. ./tup.sh
cat > Tupfile << HERE
file-y = foo.c
file-@(BAR) += bar.c
: foreach \$(file-y) |> cat %f > %o |> %B.o
HERE
echo hey > foo.c
echo yo > bar.c
update
tup_object_exist . "cat foo.c > foo.o"
tup_object_no_exist . "cat bar.c > bar.o"

varsetall BAR=
tup read
if tup flags_exists 2> /dev/null; then
	echo "*** BAR variable doesn't have flags set" 1>&2
	exit 1
fi

eotup
