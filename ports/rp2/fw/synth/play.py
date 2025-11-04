from sdcard import init_card
from samples import SYSTEM_SAMPLING_RATE, ONE, ONE_PHASE, Sample
import time

SAMPLES = (
    (62, 65, "/sd/Harpsicord/Harpsichord F5-[62, 65].wav", 65, 36618, 36997),
    (66, 71, "/sd/Harpsicord/Harpsichord B5-[66, 71].wav", 71, 37383, 39081),
    (72, 77, "/sd/Harpsicord/Harpsichord F6-[72, 77].wav", 77, 30673, 31873),
    (78, 86, "/sd/Harpsicord/Harpsichord D7-[78, 86].wav", 86, 30172, 30397),
)

def get_samples(card):
    ret = []
    for lo, hi, path, pitch, loop_start, loop_end in SAMPLES:
        print(path)
        sample = Sample(card, path, lo, hi, pitch, loop_start, loop_end)
        ret.append(sample)
    ret.sort(key=lambda sample:sample.lo_note)
    return ret

def test_samples(card):
    samples = get_samples(card)
    s = samples[0]

    buf = bytearray(4096)
    s.fill_buf(buf, 0, 1023)
    print(buf[s.samples_offset], buf[s.samples_offset+1023*2])
    return samples

def feed(card, buffer, sector):
    start=time.ticks_us()
    card.read_trigger(sector, buffer)
    while not card.read_done():
        pass
    delta = time.ticks_us()-start
    return delta, delta/(len(buffer)//512)


res = []

def basic():
    card = init_card()
    buffer = bytearray(4096)

    res.append((0, feed(card, buffer, 0)))
    res.append((8, feed(card, buffer, 8)))
    res.append((16, feed(card, buffer, 16)))
    res.append((1, feed(card, buffer, 1)))
    res.append((9, feed(card, buffer, 9)))

    return res

if __name__ == "__main__":
    #
    # samples = get_samples(card)
    #

    for r in basic():
        print(r)
