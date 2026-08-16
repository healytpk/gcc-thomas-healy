#!/bin/sh
# Provide <bits/c++config.h> for the m32df multilib.
#
# libstdc++ is not built for m32df -- it needs a hosted C library -- so its
# configure never generates the per-multilib <bits/c++config.h> that every
# libstdc++ header includes.  Without it, even the header-only freestanding
# subset (<cstdint>, <type_traits>, <limits>, <new>) cannot be used and
# -nostdinc++ is required.
#
# m32df is i386/ILP32, so the m32 multilib's copy is correct for it: the file
# records the data model, type widths and feature macros, none of which differ
# between the two.  What differs -- the C library -- affects the hosted parts
# of libstdc++, which are unavailable here in any case.
#
# Run after 'make install', passing the install prefix.
set -e
prefix=${1:?usage: install-c++config.sh <install-prefix> [target-triplet]}
target=${2:-x86_64-pc-linux-gnu}

base="$prefix/include/c++"
ver=$(ls "$base" 2>/dev/null | head -1)
[ -n "$ver" ] || { echo "no C++ headers under $base" >&2; exit 1; }
dir="$base/$ver/$target"

src="$dir/32"
dst="$dir/32df"

[ -f "$src/bits/c++config.h" ] || {
	echo "$src/bits/c++config.h not found." >&2
	echo "The m32 multilib must be built for this to work; configure with" >&2
	echo "--with-multilib-list=m64,m32 (or the default) and rebuild." >&2
	exit 1
}

mkdir -p "$dst"
cp -r "$src/." "$dst/"
echo "installed $dst/bits/c++config.h (copied from the m32 multilib)"

# The substitute <iostream> and friends.  CC1PLUS_SPEC adds this directory
# ahead of the standard C++ ones for -m32df (via -iwithprefixbefore), so
# nothing has to be passed on the command line; a directory that is not there
# is silently ignored, which is why this is a separate, optional step.
here=$(cd "$(dirname "$0")" && pwd)
if [ -d "$here/include" ]; then
	gccdir="$prefix/lib/gcc/$target"
	gccver=$(ls "$gccdir" 2>/dev/null | head -1)
	if [ -n "$gccver" ]; then
		hdr="$gccdir/$gccver/include/c++/m32df"
		mkdir -p "$hdr"
		cp -r "$here/include/." "$hdr/"
		echo "installed $hdr/{iostream,ios,ostream,iomanip}"
	else
		echo "warning: $gccdir has no version directory; skipped <iostream>" >&2
	fi
fi
echo
echo "Now this should compile without -nostdinc++:"
echo "    #include <cstdint>"
echo "    static_assert(sizeof(std::uintptr_t)  <  sizeof(void(*)()));"
echo "    static_assert(sizeof(std::uintfptr_t) >= sizeof(void(*)()));"
echo
echo "and std::cout is available with no extra flags:"
echo "    g++ -m32df prog.cpp -o prog"
