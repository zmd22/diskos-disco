# musl-cross-make config for the diskOS cross compiler (used by ../Dockerfile; also works for a manual build).
# musl-cross-make: https://github.com/richfelker/musl-cross-make at commit 227df8b99103f9c59f6570babf892978e293082f
# (its hashes/ directory pins every source tarball). Default component versions at that commit are used:
# GMP 6.3.0, MPC 1.3.1, MPFR 4.2.2, Linux headers 4.19.88-2.
#
# Target ABI = the stock firmware's own: MIPS32r2, 64-bit FPU registers (FP64), IEEE 754-2008 NaN. Binaries built
# for the older MIPS-I / FP32 / legacy-NaN ABI still run on the Snowsky Disc, but its kernel then emulates every
# floating-point instruction in software, which is many times slower.
TARGET = mipsel-linux-musl
OUTPUT = /opt/mipsel-n2008-musl-cross
GCC_VER = 11.2.0
MUSL_VER = 1.2.6
BINUTILS_VER = 2.44
GCC_CONFIG += --with-arch=mips32r2 --with-nan=2008 --with-fp-32=64 --with-odd-spreg-32
GCC_CONFIG += --disable-libquadmath --disable-libsanitizer --enable-languages=c
COMMON_CONFIG += MAKEINFO=true
