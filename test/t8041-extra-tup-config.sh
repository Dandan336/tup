#! /bin/sh -e
# tup - A file-based build system
#
# Copyright (C) 2012-2021  Mike Shal <marfey@gmail.com>
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

# Try to create an extra tup.config in the root directory after a variant.
. ./tup.sh

tmkdir build
tmkdir sub

cat > Tupfile << HERE
.gitignore
: foreach *.c |> gcc -c %f -o %o |> %B.o
: *.o sub/*.o |> gcc %f -o %o |> prog.exe
HERE
cat > sub/Tupfile << HERE
.gitignore
: foreach bar.c |> gcc -c %f -o %o |> %B.o
HERE
echo "int main(void) {return 0;}" > foo.c
tup touch Tupfile foo.c build/tup.config sub/bar.c
update

check_exist build/foo.o build/sub/bar.o build/prog.exe build/.gitignore build/sub/.gitignore
check_not_exist foo.o sub/bar.o prog.exe

tup touch tup.config
update

check_exist build/foo.o build/sub/bar.o build/prog.exe build/.gitignore build/sub/.gitignore
check_not_exist foo.o sub/bar.o prog.exe

eotup
