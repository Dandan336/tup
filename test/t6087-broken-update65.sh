#! /bin/sh -e
# tup - A file-based build system
#
# Copyright (C) 2021-2024  Mike Shal <marfey@gmail.com>
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

# Make sure we can have a dependency on a ghost, and then convert the ghost to
# a directory & remove the command at the same time.
. ./tup.sh

cat > Tupfile << HERE
: foreach *.c |> gcc -c %f -o %o -Ighost |> %B.o
: *.o |> gcc %f -o %o |> main.exe
HERE

echo 'int main(void) {return 0;}' > ok.c
update

mkdir ghost
touch ghost/foo.txt

cat > Tupfile << HERE
HERE
update

eotup
