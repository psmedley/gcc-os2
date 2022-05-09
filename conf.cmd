set config_site=u:/moztools/config.site-gcc
set path=u:\dev\gcc-11-os2\host-i386-pc-os2-emx\gcc;%path%
set LIBS= -lpthread
set grep=grep.exe
ash ./configure --prefix=/usr/local1210 --enable-shared --enable-languages=c,c++,fortran --with-gnu-as --enable-frame-pointer --disable-bootstrap --disable-libstdcxx-pch  --enable-threads --enable-decimal-float=yes --build=i686-pc-os2-emx --target=i686-pc-os2-emx 2>&1 | tee configure.log

# fix failed assertion in gcc\gcc.c