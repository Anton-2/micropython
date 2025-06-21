from machine import Pin
from rp2 import PIO, asm_pio, StateMachine, DMA

sd = Pin(9, Pin.OUT, value=0)

@asm_pio(
    out_init = PIO.OUT_LOW,
    sideset_init = (PIO.OUT_LOW, PIO.OUT_LOW),
    autopull=True,
    # pull_thresh=16,
    # fifo_join=PIO.JOIN_TX,
)

def i2s_write():
    set(x, 14)                  .side(0b10) [1]

    label('left_channel')
    out(pins, 1)                .side(0b00) [1]
    jmp(x_dec, "left_channel")  .side(0b10) [1]
    out(pins, 1)                .side(0b01) [1]

    mov(osr, null)              .side(0b11)
    set(x, 14)                  .side(0b11)

    label('right_channel')
    out(pins, 1)                .side(0b01) [1]
    jmp(x_dec, "right_channel") .side(0b11) [1]
    out(pins, 1)                .side(0b00) [1]


PIO_IDX = 1

FS = 32_000
NBITS = 16
NCHAN = 2
CLOCK_PER_BITS = 2

FREQ = FS * NBITS * NCHAN * CLOCK_PER_BITS

def i2s():
    sm = StateMachine(PIO_IDX, i2s_write, freq=FREQ, out_base=Pin(12), sideset_base=Pin(10), pull_thresh=16, out_shiftdir=PIO.SHIFT_LEFT)
    sm.active(0)
    sm.restart()
    sm.active(1)

    BUF_REPEAT = 4
    BUF_SIZE = 64 * 2 * BUF_REPEAT

    dmas = [DMA() for _ in range(2)]
    bufs = [bytearray(BUF_SIZE) for _ in range(2)]

    for idx, dma in enumerate(dmas):
        odma = dmas[1-idx]
        buf = bufs[idx]

        control = dma.pack_ctrl(size=1, inc_write=False, inc_read=True, treq_sel=PIO_IDX, chain_to=odma.channel)
        dma.config(
            read=buf,
            write=sm,
            count=len(buf)//2,
            ctrl=control,
            trigger=True
        )

    return sm, dmas, bufs

sm, dmas, bufs = i2s()
d0 = dmas[0]
d1 = dmas[1]

print(d0.count, d1.count, DMA.unpack_ctrl(d0.ctrl), DMA.unpack_ctrl(d1.ctrl))
