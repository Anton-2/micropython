from sdcard import get_card
from samples import SYSTEM_SAMPLING_RATE, ONE, ONE_PHASE, Sample




SAMPLES = (
    ('Harpsichord F5-[62, 65].wav', 62, 65),
    ('Harpsichord B5-[66, 71].wav', 66, 71),
    ('Harpsichord F6-[72, 77].wav', 72, 77),
    ('Harpsichord D7-[78, 86].wav', 78, 86),
)


SAMPLES = (
    (62, 65, "/rom/Harpsichord F5-[62, 65].wav", 65, 36618, 36997),
    (66, 71, "/rom/Harpsichord B5-[66, 71].wav", 71, 37383, 39081),
    (72, 77, "/rom/Harpsichord F6-[72, 77].wav", 77, 30673, 31873),
    (78, 86, "/rom/Harpsichord D7-[78, 86].wav", 86, 30172, 30397),
)



class Voice:

    def __init__(self, note, eg):
        good_sample = None
        loop_start = loop_end = 0
        for ((low, high), sample, loop_start, loop_end) in SAMPLES:
            if low<=note<=high:
                good_sample  = sample
                break

        assert good_sample, f'bad note {note}'

        self.wave = fast_font.Wave(good_sample._buf, loop_start, loop_end)
        self.wave.play(good_sample.get_phase_inc(note))
        self.wave.eg_trigger(array.array('i', eg), 0)
        self.wave.loop(True)
        self.in_decay = False

    def off(self, eg):
        #val = self.wave.eg_value()/ONE
        #print(val)
        self.wave.eg_trigger(array.array('i', eg))
        self.wave.loop(False)
        self.in_decay = True


    @micropython.native
    def fill(self, tmp_amp, dest, is_first):
        end_of_eg = self.wave.eg_fill(tmp_amp)
        self.wave.fill(tmp_amp, dest, is_first)
        return not (end_of_eg and self.in_decay)




# ======= I2S CONFIGURATION =======

CHUNK_SIZE = 256
BUF_CHUNK_NB = 8

I2S_ID = 0
SD_PIN = 3
SCK_PIN = 4
WS_PIN = 5

mute = Pin(2, Pin.OUT, value=0)

def i2s_setup():

    # ======= I2S CONFIGURATION =======
    audio_out = I2S(
        I2S_ID,
        sck=Pin(SCK_PIN),
        ws=Pin(WS_PIN),
        sd=Pin(SD_PIN),
        mode=I2S.TX,
        bits=32,
        format=I2S.MONO,
        rate=SYSTEM_SAMPLING_RATE,
        ibuf=CHUNK_SIZE*BUF_CHUNK_NB,
    )

    return audio_out

@micropython.native
def play():
    audio_out = i2s_setup()
    mute.value(1)

    dest = bytearray(4*CHUNK_SIZE)
    amp = array.array("I")
    for _ in range(CHUNK_SIZE):
        amp.append(0)

    eg1 = (
        4, ONE,  ONE//4,
    )

    decay = SYSTEM_SAMPLING_RATE//4
    eg2 = (
        decay, ONE, -(ONE//decay),
        decay, 0, 0,

    )

    playing = []
    play_next = []

    u=UART(0, tx=16, rx=17, baudrate=115200)

    notes_on = {}

    try:
        while True:

            msg = decode_msg(u)
            if msg is not None:
                note, is_on = msg

                if is_on:
                    assert note not in notes_on, f"retrig {note} ?"
                    voice = Voice(note, eg1)
                    notes_on[note] = voice
                    playing.append(voice)
                else:
                    voice = notes_on.pop(note)
                    assert voice, f"off {note}, not on ?"
                    voice.off(eg2)

            first = True
            play_next.clear()
            for voice in playing:
                if voice.fill(amp, dest, first):
                    play_next.append(voice)
                first = False

            I2S.shift(buf=dest, bits=32, shift=10)
            audio_out.write(bytes(dest))
            playing, play_next = play_next, playing

    finally:
        mute.value(0)
        audio_out.deinit()


def decode_msg(u):
    r=u.read(4)

    if not r:
        return None

    if not len(r) == 4:
        print(f"ign {r}")
        return None

    cmd, note, vel, end = r

    if end != 0xFF:
        print(f"bad {r}")
        return None

    if cmd == 128:
        #print(f"note on {note}")
        return (note, True)
    elif cmd == 129:
        #print(f"note off {note}")
        return (note, False)
    else:
        print(f"bad cmd {cmd} {note} {vel}")

    return None


if __name__ == "__main__":
    play()
