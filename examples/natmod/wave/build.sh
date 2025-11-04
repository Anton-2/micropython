export ARCH=armv7m
make clean && \
    make -j 10 && \
    cp wave.mpy ../../../ports/rp2/fw/wave.mpy && \
    mv wave.mpy /Users/anton/Projects/SndFontPlayer/pico/fw/wave.mpy
