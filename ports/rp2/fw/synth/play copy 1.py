from sdcard import init_card
from samples import SYSTEM_SAMPLING_RATE, ONE, ONE_PHASE, Sample


SAMPLES = (
    (62, 65, "/rom/Harpsichord F5-[62, 65].wav", 65, 36618, 36997),
    (66, 71, "/rom/Harpsichord B5-[66, 71].wav", 71, 37383, 39081),
    (72, 77, "/rom/Harpsichord F6-[72, 77].wav", 77, 30673, 31873),
    (78, 86, "/rom/Harpsichord D7-[78, 86].wav", 86, 30172, 30397),
)

def get_samples(card):
    ret = []
    for lo, hi, path, pitch, loop_start, loop_end in SAMPLES:
        sample = Sample(path, lo, hi, pitch, loop_start, loop_end)
        sample.find_first_sector(card)
        ret.append(sample)
    ret.sort(key=lambda sample:sample.lo_note)
    return ret

if __name__ == "__main__":
    card = init_card()
    samples = get_samples(card)
    print(samples)
