export ARCH=""
for ARCH in armv6m armv7m
do
    make clean && make && mv fast_font.mpy /Users/anton/Projects/lcd-figma/fw/fast_font_${ARCH}.mpy
done
