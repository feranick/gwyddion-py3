# gwyddion-py3
Test Gwyddion with  Python3 support for pygwy

Two configure.ac files:

1. configure.ac.original
2. configure.ac

The second forces the compilation of pygw under Python3. Of course at least initially compilation will fail, but that is the point, so we can work to bring the script to be compatible to python3.

Versions of python3 that can be used: 3.6, 3.7, 3.8, 3.9, 3.10

Every time configure.ac is switched to the test version, rerun as indicated below.
./autogen.sh
./configure
make

test pygwy inside: 
gwyddion-2.61/debian/gwyddion/usr/lib/x86_64-linux-gnu/gwyddion/modules


