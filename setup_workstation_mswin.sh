#!/bin/sh
export SUFFIX=_mswin
export TARGET=x86_64-w64-mingw32

export CONFIG_SHELL=/bin/sh

export PREFIX=`realpath --no-symlinks "$PWD/../gcc-thomas-healy${SUFFIX}"`
export SYSROOT="/usr/${TARGET}/"

# System headers will be looked for in $SYSROOT/mingw/include instead
# of $SYSROOT/include, therefore we must create a symbolic link
if [ ! -e /usr/${TARGET}/mingw ]; then
  echo "Creating symbolic link from /usr/$TARGET/mingw to /usr/$TARGET/"
  sudo ln -sf /usr/${TARGET}/ /usr/${TARGET}/mingw
fi

# Must create symbolic links to tools like the assembler and linker
mkdir -p ../gcc-thomas-healy${SUFFIX}/${TARGET}
if [ ! -e ../gcc-thomas-healy${SUFFIX}/${TARGET}/bin ]; then
  echo "Creating symbolic link from ../gcc-thomas-healy${SUFFIX}/${TARGET}/bin to /usr/${TARGET}/bin/"
  ln -sf /usr/${TARGET}/bin/ ../gcc-thomas-healy${SUFFIX}/${TARGET}/bin
fi

mkdir -p ../gcc-thomas-healy${SUFFIX}_build
cd ../gcc-thomas-healy${SUFFIX}_build
if [ -z "$(find . -maxdepth 1 -type f -print -quit)" ]; then
  echo ===================================================================
  echo   Creating build directory and configuring build:
  echo      configure --prefix=`realpath --no-symlinks "$PWD/../gcc-thomas-healy${SUFFIX}"`/ --disable-multilib --enable-languages=c,c++ --prefix="$PREFIX" --target="$TARGET" --with-sysroot="$SYSROOT" --with-build-sysroot="$SYSROOT" --with-native-system-header-dir=/include/
  echo ===================================================================
  ../gcc-thomas-healy_source/configure --prefix=`realpath --no-symlinks "$PWD/../gcc-thomas-healy${SUFFIX}"`/ --disable-multilib --enable-languages=c,c++ --prefix="$PREFIX" --target="$TARGET" --with-sysroot="$SYSROOT" --with-build-sysroot="$SYSROOT"  --with-native-system-header-dir=/include/
fi
echo ===================================================================
echo   ALL DONE
echo ===================================================================
