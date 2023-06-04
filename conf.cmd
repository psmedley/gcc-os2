set LDFLAGS=-Zomf -Zstack 8192 -Zhigh-mem
set cc=gcc
set grep=grep.exe
set path=u:\dev\gcc-os2\host-i386-pc-os2-emx\gcc;%path%
set LIBS= -lpthread
dash ./configure --disable-bootstrap --enable-languages=c,c++ --build=i686-pc-os2-emx --with-sysroot=/@unixroot --prefix=/@unixroot/usr/local1230 --mandir=/@unixroot/usr/local1230/share/man --infodir=/@unixroot/usr/local1230/share/info --enable-shared --enable-threads --enable-checking=release --disable-multilib --with-system-zlib --with-gcc-major-version-only --without-isl --with-tune=generic --with-arch=i686 --with-gnu-as --disable-libstdcxx-pch
