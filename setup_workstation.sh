#!/bin/sh
export CONFIG_SHELL=/bin/sh

echo "Testing internet connection..."
if nc -zw1 google.com 443; then
echo "We can access the Google HTTPS server."
echo ===================================================================
echo   Installing pre-requisites
echo ===================================================================
sudo apt update
sudo apt install -y build-essential gcc-multilib git make gawk flex bison libgmp-dev libmpfr-dev libmpc-dev python3 binutils perl libisl-dev libzstd-dev tar gzip bzip2 autotools-dev automake
sudo apt install -y meld geany
./contrib/download_prerequisites
fi

mkdir -p ../gcc-thomas-healy_build
cd ../gcc-thomas-healy_build
if [ -z "$(find . -maxdepth 1 -type f -print -quit)" ]; then
  echo ===================================================================
  echo   Creating build directory and configuring build:
  echo      configure --prefix=`realpath --no-symlinks "$PWD/../gcc-thomas-healy"`/ --enable-languages=c,c++ --with-multilib-list=m32,m64,mx32
  echo ===================================================================
  ../gcc-thomas-healy_source/configure --prefix=`realpath --no-symlinks "$PWD/../gcc-thomas-healy"`/ --enable-languages=c,c++ --with-multilib-list=m32,m64,mx32
fi

echo ===================================================================
echo   Configuring git at the commandline with username and password
echo ===================================================================
git config --global user.email "thomaspkhealy@yahoo.com"
git config --global user.name "Thomas PK Healy"

echo "Testing internet connection..."
if nc -zw1 google.com 443; then
echo "We can access the Google HTTPS server."
echo ===================================================================
echo   Setting up bikeshed
echo ===================================================================
sudo apt-get install -y python3 python3-venv pipx
pipx ensurepath
. "$HOME/.profile"
pipx install bikeshed
bikeshed update
fi

echo ===================================================================
echo   ALL DONE
echo ===================================================================
