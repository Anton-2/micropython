import machine
import neopixel

# dot clock shoiuld be >= 25Mhz
machine.freq(round(30 * 5) * 1_000_000)
np = neopixel.NeoPixel(machine.Pin(21), 1)

out = machine.Pin(5, machine.Pin.OUT)


from machine import mem32


def pincf(pin, drive, slew):
    addr = 0x40038000 + 0x04 + 0x04 * pin
    mem32[addr] = 0b1000010 | (drive << 4) | slew


def setcf(drive, slew):
    for pin in range(12, 20):
        pincf(pin, drive, slew)


# setcf(1, 0)


def led(r, g, b):
    np.buf[0] = g
    np.buf[1] = r
    np.buf[2] = b

    np.write()


led(12, 0, 14)

# This demo is derived from the pico-example:
# https://github.com/raspberrypi/pico-examples/blob/master/hstx/dvi_out_hstx_encoder/dvi_out_hstx_encoder.c
# However this only outputs a test pattern, not a full image. MicroPython
# interrupts take too long to update the DMAs like the C example does. Instead,
# this creates an entire frame's worth of HSTX commands, which are sent to the
# HSTX by a DMA. A second DMA re-triggers the first DMA each time it finishes,
# because again, MicroPython interrupts are too slow. That second DMA does get
# re-configured by an interrupt, because there is plenty of time for that while
# the first DMA is running.

# Imports
import array

import machine
import rp2
import uctypes

# Pin assignments for DVI output. This pinout is typical for devices like the
# SparkFun IoT RedBoard RP2350, Pico-DVI-Sock, Adafruit Feather RP2350 with HSTX
# Port, etc. These can be changed, but must remain in pairs for this demo.
dvi_clk_p = 14
dvi_clk_n = dvi_clk_p + 1
dvi_d0_p = 12
dvi_d0_n = dvi_d0_p + 1
dvi_d1_p = 18
dvi_d1_n = dvi_d1_p + 1
dvi_d2_p = 16
dvi_d2_n = dvi_d2_p + 1

# Create HSTX and DMA objects
hstx = rp2.HSTX()
dma0 = rp2.DMA()
dma1 = rp2.DMA()

assert dma0.channel == 0
assert dma1.channel == 1

if False:
    import time

    csr = hstx.pack_csr(
        expand_enable=0,
        enable=0,
    )
    hstx.csr(csr)
    dma0.active(False)
    dma1.active(False)
    time.sleep(0.5)

# ----------------------------------------------------------------------------
# DVI constants

TMDS_CTRL_00 = 0x354
TMDS_CTRL_01 = 0x0AB
TMDS_CTRL_10 = 0x154
TMDS_CTRL_11 = 0x2AB

SYNC_V0_H0 = TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20)
SYNC_V0_H1 = TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20)
SYNC_V1_H0 = TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20)
SYNC_V1_H1 = TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20)

H_SYNC_POLARITY = 0
H_FRONT_PORCH = 8
H_SYNC_WIDTH = 32
H_BACK_PORCH = 40
H_ACTIVE_PIXELS = 640

V_SYNC_POLARITY = 0
V_FRONT_PORCH = 1
V_SYNC_WIDTH = 8
V_BACK_PORCH = 6 + 181
V_ACTIVE_LINES = 400

H_TOTAL_PIXELS = H_FRONT_PORCH + H_SYNC_WIDTH + H_BACK_PORCH + H_ACTIVE_PIXELS
V_TOTAL_LINES = V_FRONT_PORCH + V_SYNC_WIDTH + V_BACK_PORCH + V_ACTIVE_LINES

HSTX_CMD_RAW = 0x0 << 12
HSTX_CMD_RAW_REPEAT = 0x1 << 12
HSTX_CMD_TMDS = 0x2 << 12
HSTX_CMD_TMDS_REPEAT = 0x3 << 12
HSTX_CMD_NOP = 0xF << 12

# ----------------------------------------------------------------------------
# HSTX command lists

vblank_line_vsync_off = array.array(
    "I",
    [
        HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH,
        SYNC_V1_H1,
        HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH,
        SYNC_V1_H0,
        HSTX_CMD_RAW_REPEAT | (H_BACK_PORCH + H_ACTIVE_PIXELS),
        SYNC_V1_H1,
    ],
)

vblank_line_vsync_on = array.array(
    "I",
    [
        HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH,
        SYNC_V0_H1,
        HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH,
        SYNC_V0_H0,
        HSTX_CMD_RAW_REPEAT | (H_BACK_PORCH + H_ACTIVE_PIXELS),
        SYNC_V0_H1,
    ],
)

vactive_line = array.array(
    "I",
    [
        HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH,
        SYNC_V1_H1,
        HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH,
        SYNC_V1_H0,
        HSTX_CMD_RAW_REPEAT | H_BACK_PORCH,
        SYNC_V1_H1,
        # Modified to repeat single pixel group for all active pixels
        HSTX_CMD_TMDS | 2,
        0x00FFFFFF,
        0x00FF0000,
        HSTX_CMD_TMDS_REPEAT | (H_ACTIVE_PIXELS - 4),
        0x00000000,
        HSTX_CMD_TMDS | 2,
        0x00FF0000,
        0x00FFFFFF,
    ],
)

white_line = array.array(
    "I",
    [
        HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH,
        SYNC_V1_H1,
        HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH,
        SYNC_V1_H0,
        HSTX_CMD_RAW_REPEAT | H_BACK_PORCH,
        SYNC_V1_H1,
        # Modified to repeat single pixel group for all active pixels
        HSTX_CMD_TMDS_REPEAT | H_ACTIVE_PIXELS,
        0x00FFFFFF,
    ],
)

green_line = array.array(
    "I",
    [
        HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH,
        SYNC_V1_H1,
        HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH,
        SYNC_V1_H0,
        HSTX_CMD_RAW_REPEAT | H_BACK_PORCH,
        SYNC_V1_H1,
        # Modified to repeat single pixel group for all active pixels
        HSTX_CMD_TMDS | 1,
        0x00FFFFFF,
        HSTX_CMD_TMDS_REPEAT | (H_ACTIVE_PIXELS - 2),
        0x00FF0000,
        HSTX_CMD_TMDS | 1,
        0x00FFFFFF,
    ],
)


# ----------------------------------------------------------------------------
# DMA logic

# Here we create a full frame's worth of HSTX commands in a single array. The
# pixel_group is a collection of 4 pixels (RGB332) that is repeated to fill
# the active area of each active line, resulting in vertical lines of solid
# color.
pixel_group = [0xFF000000, 0xFF00FF00, 0x00003E0, 0x55005500]

V_TOTAL_LINES = V_FRONT_PORCH + V_SYNC_WIDTH + V_BACK_PORCH + V_ACTIVE_LINES


frontporch_start = V_TOTAL_LINES - V_FRONT_PORCH
frontporch_end = frontporch_start + V_FRONT_PORCH

vsync_start = 0
vsync_end = vsync_start + V_SYNC_WIDTH

backporch_start = vsync_end
backporch_end = backporch_start + V_BACK_PORCH

active_start = backporch_end

print(f"frontporch_start: {frontporch_start}")
print(f"frontporch_end: {frontporch_end}")
print(f"vsync_start: {vsync_start}")
print(f"vsync_end: {vsync_end}")
print(f"backporch_start: {backporch_start}")
print(f"backporch_end: {backporch_end}")
print(f"active_start: {active_start}")

data = array.array("I")
lof7 = []
for v_scanline in range(V_TOTAL_LINES):
    if vsync_start <= v_scanline < vsync_end:
        data += vblank_line_vsync_on
    elif backporch_start <= v_scanline < backporch_end:
        data += vblank_line_vsync_off
    elif frontporch_start <= v_scanline < frontporch_end:
        data += vblank_line_vsync_off
    else:
        assert v_scanline >= active_start
        line = v_scanline - active_start
        if line == 0 or line == 399:
            data += white_line
        elif line == 1 or line == 398:
            data += green_line
        else:
            data += vactive_line
            lof7.append(len(data) - 4)
            data[-4] = pixel_group[(line // 40) % 4]

hstx_clock = machine.freq()
dot_clock = hstx_clock / 5
tot_pix = V_TOTAL_LINES * H_TOTAL_PIXELS
active_pix = V_ACTIVE_LINES * H_ACTIVE_PIXELS

print(
    f"dot: {dot_clock/1e6}Mhz line: {dot_clock/V_TOTAL_LINES/1e3}Khz frame: {dot_clock/tot_pix}hz "
)
print(f"{len(data)=} active ratio {active_pix/tot_pix:.1f}")

# ----------------------------------------------------------------------------
# Main program


a = """

  if (COLOR_DEPTH == 32) {
    // Configure HSTX's TMDS encoder for RGB888
    hstx_ctrl_hw->expand_tmds = 7 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
                                16 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
                                7 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
                                8 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
                                7 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
                                0 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
  } else if (COLOR_DEPTH == 16) {
    // Configure HSTX's TMDS encoder for RGB565
    hstx_ctrl_hw->expand_tmds = 4 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
                                0 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
                                5 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
                                27 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
                                4 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
                                21 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
  } else if (COLOR_DEPTH == 8) {
    // Configure HSTX's TMDS encoder for RGB332
    hstx_ctrl_hw->expand_tmds = 2 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
                                0 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
                                2 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
                                29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
                                1 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
                                26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
"""

# Configure HSTX's TMDS encoder for RGB332
# expand_tmds = hstx.pack_expand_tmds(
#    l2_nbits=2, l2_rot=0, l1_nbits=2, l1_rot=29, l0_nbits=1, l0_rot=26
# )

# Configure HSTX's TMDS encoder for RGB565
expand_tmds = hstx.pack_expand_tmds(
    l2_nbits=7, l2_rot=16, l1_nbits=7, l1_rot=8, l0_nbits=7, l0_rot=0
)

hstx.expand_tmds(expand_tmds)

# Pixels (TMDS) come in 4 8-bit chunks. Control symbols (RAW) are an
# entire 32-bit word.
# expand_shift = hstx.pack_expand_shift(enc_n_shifts=4, enc_shift=8, raw_n_shifts=1, raw_shift=0)

# Pixels (TMDS) come in 2 16-bit chunks. Control symbols (RAW) are an
# entire 32-bit word.
expand_shift = hstx.pack_expand_shift(enc_n_shifts=1, enc_shift=0, raw_n_shifts=1, raw_shift=0)

hstx.expand_shift(expand_shift)

# Serial output config: clock period of 5 cycles, pop from command
# expander every 5 cycles, shift the output shiftreg by 2 every cycle.
csr = hstx.pack_csr(
    clk_div=5,
    clk_phase=0,
    n_shifts=5,
    shift=2,
    coupled_select=0,
    coupled_mode=0,
    expand_enable=1,
    enable=1,
)
hstx.csr(csr)

# Assign clock pair to two neighbouring pins:
hstx.bit(dvi_clk_p, hstx.pack_bit(clk=1))
hstx.bit(dvi_clk_n, hstx.pack_bit(clk=1, inv=1))
for lane in range(3):
    # For each TMDS lane, assign it to the correct GPIO pair based on the
    # desired pinout:
    lane_to_output_bit = [dvi_d0_p, dvi_d1_p, dvi_d2_p]
    bit = lane_to_output_bit[lane]
    # Output even bits during first half of each HSTX cycle, and odd bits
    # during second half. The shifter advances by two bits each cycle.
    # uint32_t lane_data_sel_bits =
    #     (lane * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
    #     (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
    # The two halves of each pair get identical data, but one pin is inverted.
    hstx.bit(bit, hstx.pack_bit(sel_p=lane * 10, sel_n=lane * 10 + 1))
    hstx.bit(bit + 1, hstx.pack_bit(sel_p=lane * 10, sel_n=lane * 10 + 1, inv=1))

for i in range(12, 20):
    # Pin.ALT_HSTX not yet supported, set alt function with direct register access
    # machine.Pin(i, mode=machine.Pin.ALT, alt=machine.Pin.ALT_HSTX)
    machine.Pin(i, mode=machine.Pin.ALT)
    machine.mem32[0x40028000 + 4 * 2 * i + 4 * 1] = 0

# We need the second DMA to write to the first DMA's registers, which is not
# readily available in MicroPython. The address is calculated here. This uses
# the alias3 registers to easily reset the transfer count and read address,
# while also re-triggering the first DMA (see section 12.6.3.1 of the RP2350
# datasheet).
DMA_BASE = 0x50000000
dma_channel_hw_t_size = 0x40
dma0_ch_addr = DMA_BASE + dma_channel_hw_t_size * dma0.channel
DMA_CH0_AL3_TRANS_COUNT_OFFSET = 0x38
dma0_al3_trans_count_addr = dma0_ch_addr + DMA_CH0_AL3_TRANS_COUNT_OFFSET
dma0_al3_trans_count_and_read_addr = array.array("I", [len(data), uctypes.addressof(data)])


i = 0


# IRQ handler for DMA1 completion, which simply resets it
@micropython.native
def my_irq(ch):
    global dma1, dma0_al3_trans_count_addr, dma0_al3_trans_count_and_read_addr, i
    dma1.read = dma0_al3_trans_count_and_read_addr
    dma1.write = dma0_al3_trans_count_addr
    dma1.count = 2
    # led(i, 0, 128)
    # i += 1
    # if i == 256:
    #    i = 0
    out.value(1 - out.value())


# Configure the two DMAs
DREQ_HSTX = 52
dma_ctrl = dma0.pack_ctrl(
    enable=True,
    size=2,
    inc_read=True,
    inc_write=False,
    chain_to=dma1.channel,
    treq_sel=DREQ_HSTX,
    bswap=False,
)
dma0.config(read=data, write=hstx, count=len(data), ctrl=dma_ctrl, trigger=False)
dma_ctrl = dma1.pack_ctrl(
    enable=True,
    size=2,
    inc_read=True,
    inc_write=True,
    irq_quiet=False,
    bswap=False,
)
dma1.config(
    read=dma0_al3_trans_count_and_read_addr,
    write=dma0_al3_trans_count_addr,
    count=2,
    ctrl=dma_ctrl,
    trigger=False,
)

# Set up IRQ handler for DMA1 so it can be re-configured when done
dma1.irq(handler=my_irq, hard=True)

# Start the first DMA, which will trigger the second DMA when done, which will
# re-trigger the first DMA, and so on...
dma0.active(True)


led(12, 12, 0)


def rgb(r, g, b):
    r >>= 3
    g >>= 2
    b >>= 3
    return (r << 11) | (g << 5) | b


def drgb(r, g, b):
    v = rgb(r, g, b)
    return (v << 16) | v


def set(l, r, g, b):
    v = rgb(r, g, b)
    data[lof7[l]] = (v << 16) | v


def rset(start, end, v):
    for line in range(start, end):
        data[lof7[line]] = v
