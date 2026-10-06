-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA512

Format: 3.0 (quilt)
Source: libusb-1.0
Binary: libusb-1.0-0, libusb-1.0-0-dev, libusb-1.0-doc, libusb-1.0-0-udeb
Architecture: linux-any hurd-any all
Version: 2:1.0.27-1
Maintainer: Aurelien Jarno <aurel32@debian.org>
Homepage: http://www.libusb.info
Standards-Version: 4.6.2
Testsuite: autopkgtest
Testsuite-Triggers: autoconf, automake, build-essential, libudev-dev, libumockdev-dev, pkg-config, umockdev
Build-Depends: debhelper-compat (= 13), libudev-dev [linux-any], pkg-config, umockdev <!nocheck>, libumockdev-dev <!nocheck>
Build-Depends-Indep: doxygen
Package-List:
 libusb-1.0-0 deb libs optional arch=linux-any,hurd-any
 libusb-1.0-0-dev deb libdevel optional arch=linux-any,hurd-any
 libusb-1.0-0-udeb udeb debian-installer optional arch=linux-any,hurd-any profile=!noudeb
 libusb-1.0-doc deb doc optional arch=all
Checksums-Sha1:
 7c169047f5970e767d937b598f60979de5f036e3 643680 libusb-1.0_1.0.27.orig.tar.bz2
 ffb7e3e03c672d154bfc9e8aa840cd9c1db656a7 833 libusb-1.0_1.0.27.orig.tar.bz2.asc
 9eee768d76ee4a490beeeade2e17a5cb0b334cb3 17044 libusb-1.0_1.0.27-1.debian.tar.xz
Checksums-Sha256:
 ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575 643680 libusb-1.0_1.0.27.orig.tar.bz2
 1cd22bbfe4ce382ca9b091e2a6275c48f1c776253815cbb615da295ae0bfe687 833 libusb-1.0_1.0.27.orig.tar.bz2.asc
 560bc02e704b8f28b04be3e6a551ccbf2b5c5cb0850864d6a5416ad05723f0b4 17044 libusb-1.0_1.0.27-1.debian.tar.xz
Files:
 1fb61afe370e94f902a67e03eb39c51f 643680 libusb-1.0_1.0.27.orig.tar.bz2
 be3b4265eb0ba77ea2a4bbe78539e1d7 833 libusb-1.0_1.0.27.orig.tar.bz2.asc
 9ab0e4b8a46ea6c3456343bba278398a 17044 libusb-1.0_1.0.27-1.debian.tar.xz

-----BEGIN PGP SIGNATURE-----

iQIzBAEBCgAdFiEEUryGlb40+QrX1Ay4E4jA+JnoM2sFAmW75icACgkQE4jA+Jno
M2vR2g/8CAvEt2Ea1iT/cc/HmAm6InO5RdG82FbqpQABSK9JNgN+EuuIA861GfSf
jCjrLE2B55oioAKSx6zKxadAC59dP9VdFMmfTBvfpDJrYlXyYLlQpKDUrok23nT2
C9w2EI7lnQLpqbuuefxXzpmTqoGja1pSsX8BSmQxb2oqo8W516YPswItBhY7v6WQ
yBILaPGlrdUibqxIw0Qw+jbbi4VT1XIE7l+30XtZ7NK5nvBTVK83vi1OU7+yjPop
tVoFp99LWQS3M3q6LK94UtRYpi2XQqL+0ZgoomhkZZ/NUOzDehGguiyLUBlvv3yD
yq4DQsi8EmaWQzuofqtEO4BpdMZT2A3pZ17LU3LBx4AmHSdrSiJIK/nXMWtn3r+1
qUsFzMQi0DufpyYCU+JUOvLoSg+v1ab42/yupvDzpbp0gQcO72/av+c8Y5u6XAIC
fZSmCast8kqLsFY9GACEe5def3Ua2UT9M5Hm6fMoR33+AQJhcURPVC/qzeykEoSP
K2Uq5PAO0qJa2frGD0Op3vyfpETIjWXylj725tGFLw87+Ckug5tYyGPh/rZ5Oh/k
1xRVApEeiu6ppsis5cJwo6yXSewCS0aqo71KGdurcR4/kqoRa+DjZJqzdkOh+cSF
ClCWGsD3BAL8fHB9vrkeWsuv0PvQ0EItXX0fmBQdlc/AprzMqCk=
=Uhhm
-----END PGP SIGNATURE-----
