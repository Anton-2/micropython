from sdcard import SDCard, init_card
from samples import ONE, ONE_PHASE, Sample, midi2freq
import time
import array
import struct

SYSTEM_SAMPLING_RATE = 44100

WAVE = "4sI8sIHHIIHH4sI"
WAVE_SIZE = struct.calcsize(WAVE)


def write_wav_header(file, sample_size, length, sr=SYSTEM_SAMPLING_RATE):
    file.seek(0)
    file.write(
        struct.pack(
            WAVE,
            b"RIFF",
            36 + length,
            b"WAVEfmt ",
            16,
            1,
            1,
            sr,
            sr * sample_size,
            sample_size,
            sample_size * 8,
            b"data",
            length,
        )
    )


SFZ = (
    ("Harpsichord_D3.wav", 38, 0, 39, "loop_continuous", 167374, 170994),
    ("Harpsichord_G3.wav", 43, 40, 44, "loop_continuous", 82026, 86527),
    ("Harpsichord_C4.wav", 48, 45, 48, "loop_continuous", 99441, 102476),
    ("Harpsichord_F4.wav", 53, 49, 54, "loop_continuous", 47196, 47447),
    ("Harpsichord_C5.wav", 60, 55, 61, "loop_continuous", 91398, 93248),
    ("Harpsichord_F5.wav", 65, 62, 65, "loop_continuous", 36618, 36996),
    ("Harpsichord_B5.wav", 71, 66, 71, "loop_continuous", 37383, 39080),
    ("Harpsichord_F6.wav", 77, 72, 77, "loop_continuous", 30673, 31872),
    ("Harpsichord_D7.wav", 86, 78, 86, "loop_continuous", 30172, 30396),
    ("Harpsichord_B7.wav", 95, 87, 95, "loop_continuous", 20130, 20575),
    (
        "Harpsichord_C8.wav",
        96,
        96,
        127,
        "loop_continuous",
        35993,
        36752,
    ),  # oversample >x2 after 108
)

if True:
    SAMPLES = (
        (45, 48, "/sd/Harpsichord/Samples/Harpsichord_C4.wav", 48, 99441, 102476),
        (62, 65, "/sd/Harpsicord/Harpsichord F5-[62, 65].wav", 65, 36618, 36997),
        (66, 71, "/sd/Harpsicord/Harpsichord B5-[66, 71].wav", 71, 37383, 39081),
        (72, 77, "/sd/Harpsicord/Harpsichord F6-[72, 77].wav", 77, 30673, 31873),
        (78, 86, "/sd/Harpsicord/Harpsichord D7-[78, 86].wav", 86, 30172, 30397),
    )
else:
    SAMPLES = (
        (62, 65, "/sd/Harpsicord/Harpsichord F5-[62, 65].wav", 65, 0, 0),
        (66, 71, "/sd/Harpsicord/Harpsichord B5-[66, 71].wav", 71, 0, 0),
        (72, 77, "/sd/Harpsicord/Harpsichord F6-[72, 77].wav", 77, 0, 0),
        (78, 86, "/sd/Harpsicord/Harpsichord D7-[78, 86].wav", 86, 0, 0),
    )


def get_samples(card) -> list[Sample]:
    ret = []
    for lo, hi, path, pitch, loop_start, loop_end in SAMPLES:
        print(path)
        sample = Sample(card, path, lo, hi, pitch, loop_start, loop_end)
        ret.append(sample)
    ret.sort(key=lambda sample: sample.lo_note)
    return ret


SAMPLES_PER_BLOC = 256
BLOC_PER_HALF_RING = 4
HALF_RING_SIZE = SAMPLES_PER_BLOC * BLOC_PER_HALF_RING
RING_MASK = (HALF_RING_SIZE * 2) - 1


class HalfRing:
    idx: int
    buf: memoryview[array.array[int]]
    first_sample: int | None
    last_sample: int | None
    state: str

    def __init__(self, idx: int, ring_mv: memoryview[array.array[int]]):
        self.idx = idx
        self.buf = ring_mv[idx * HALF_RING_SIZE : idx * HALF_RING_SIZE + HALF_RING_SIZE]
        self.first_sample = None
        self.last_sample = None
        self.state = "NULL"

    def req(self, first_sample: int):
        assert self.state != "REQ", "Request on request ?"
        self.first_sample = first_sample
        self.last_sample = first_sample + HALF_RING_SIZE - 1
        self.state = "REQ"

    def ready(self):
        assert self.state == "REQ"
        self.state = "READY"

    def __repr__(self):
        return f"HalfRing#{self.idx} {self.first_sample}-{self.last_sample}:{self.state}"


# current
# for each voice
#   h-ring update
#   interpolate (assuming all is fine)
# start prefetch
#
# BADS:
# delay between h-ring update and prefetch
# h-ring set REQ one chunk later
#
# next arch ?
#
# for each voice
#   interpolate (assuming all is fine)
# send to i2s / effects
#
# for each voice (fair round robin)
#   h-ring update
#   and one start prefetce
#


#
# detect loop start and cache an half-ring of data starting with first sector of loop start
# on loop end,
#   - interpolate until loop end
#   - recall cached half ring over current
#   - jump phase to first sample of loop start
#   - interpolate until end of buffer
#
#
class Voice:
    sample: Sample

    half_rings: tuple[HalfRing, HalfRing]
    current_hr: int

    loop_start: int  #
    loop_end: int  # 0 -> no loop

    loop_hring_cache: array.array[int] | None
    loop_hring_first_sample: int | None
    loop_hring_last_sample: int | None

    phase: float
    phase_inc: float

    state: str

    def __init__(self, sample: Sample, dest_pitch: float):
        self.sample = sample
        self.ring = array.array("h", [0] * (HALF_RING_SIZE * 2))
        ring_mv = memoryview(self.ring)

        self.half_rings = (HalfRing(0, ring_mv), HalfRing(1, ring_mv))

        self.phase = sample.samples_offset // 2  # skip wave header (44 bytes)
        self.phase_inc = (dest_pitch / sample.pitch) * (
            sample.sampling_rate / SYSTEM_SAMPLING_RATE
        )

        self.current_hr = 0
        self.half_rings[self.current_hr].req(0)

        self.state = "INIT"

        self.loop_hring_first_sample = None
        self.loop_hring_last_sample = None

        if self.sample.loop_start:
            self.loop_start = self.sample.loop_start
            self.loop_end = self.sample.loop_end
            self.loop_hring_cache = array.array("h", [0] * HALF_RING_SIZE)
        else:
            self.loop_start = 0
            self.loop_end = 0
            self.loop_hring_cache = None

    def loop_off(self):
        self.loop_start = 0
        self.loop_end = 0

    def bootstrap(self):
        if self.state == "INIT" and self.half_rings[self.current_hr].state == "READY":
            current_hr = self.half_rings[self.current_hr]
            next_hr = self.half_rings[1 - self.current_hr]

            assert current_hr.last_sample is not None
            next_hr.req(current_hr.last_sample + 1)

            self.state = "RUNNING"

    #
    # we expect current hring to be ready and contain the start sample here
    #
    # @micropython.native
    def fill_bloc(self, bloc: array.array[int], dest_offset: int, dest_nb: int, first: bool):
        first_sample = int(self.phase)

        # how many samples to read at this rate ?
        adv = int(dest_nb * self.phase_inc)
        full_bloc_last_sample = first_sample + adv - 1  # -1 for sample index

        # cut it short if we're looping here
        # or cut it short if this is the end of the sample

        sample_last_sample = self.sample.samples_offset // 2 + self.sample.n_samples - 1
        loop = False

        if self.loop_end and full_bloc_last_sample > self.loop_end:
            last_sample = self.loop_end
            loop = True
        elif full_bloc_last_sample >= sample_last_sample:
            # >= and -1 cause we need to read full_bloc_last_sample+1 for interpolation
            last_sample = sample_last_sample - 1
            self.state = "DONE"
        else:
            last_sample = full_bloc_last_sample

        remain = 0
        if last_sample != full_bloc_last_sample:
            chunk_nb = int((last_sample - first_sample) / self.phase_inc)
            remain = (dest_nb - chunk_nb) if loop else 0
            dest_nb = chunk_nb

        # interpolation needs one sample more
        last_sample += 1

        current_hr_idx = self.current_hr
        current_hr = self.half_rings[current_hr_idx]
        next_hr = None

        # sanity checks
        print(
            f"INTERPOLATE {dest_nb} samples, {remain=}. Rate={self.phase_inc:.3f}x from {first_sample} to {last_sample} current hring {current_hr_idx} fs={current_hr.first_sample} ls={current_hr.last_sample}"
        )

        assert (
            current_hr.state == "READY"
            and current_hr.first_sample is not None
            and current_hr.last_sample is not None
        ), "Fill bloc, current hr is not ready"

        assert (
            current_hr.first_sample <= first_sample <= current_hr.last_sample
        ), "Fill bloc, current hr incorrect"

        # are we crossing over to next ring ?
        if last_sample > current_hr.last_sample:
            # next will be current next time
            self.current_hr = 1 - current_hr_idx
            next_hr = self.half_rings[self.current_hr]

            # check that next hr is ready
            assert next_hr.last_sample and next_hr.state == "READY", "next hr is not ready"
            assert (
                next_hr.first_sample == current_hr.last_sample + 1
            ), "current -> next link failed"
            assert next_hr.last_sample >= last_sample, "next hr too small"

            # we leave the current h-ring
            # what will become current is called next during this transition

            # Got current and next filled in. Time to grap start loop cache if loop starts in current
            if (
                self.loop_start
                and current_hr.first_sample <= self.loop_start <= current_hr.last_sample
                and self.loop_hring_first_sample is None
            ):
                assert self.loop_hring_cache is not None

                # find first bloc with loop start
                offset = self.loop_start - current_hr.first_sample
                bloc_offset = offset // SAMPLES_PER_BLOC

                # cache loop start
                self.loop_hring_first_sample = (
                    current_hr.first_sample + bloc_offset * SAMPLES_PER_BLOC
                )
                self.loop_hring_last_sample = self.loop_hring_first_sample + HALF_RING_SIZE - 1

                # copy from bloc_offset to end of hring, take missing bloc in next hring.
                chunk_start = bloc_offset * SAMPLES_PER_BLOC
                chunk_len = (BLOC_PER_HALF_RING - bloc_offset) * SAMPLES_PER_BLOC
                self.loop_hring_cache[:chunk_len] = current_hr.buf[chunk_start:]

                print(f"LOOP fill cache, first chunk {chunk_len}")

                if bloc_offset != 0:
                    self.loop_hring_cache[chunk_len:] = next_hr.buf[
                        : (SAMPLES_PER_BLOC * BLOC_PER_HALF_RING) - chunk_len
                    ]
                    print(
                        f"LOOP fill cache, second chunk {(SAMPLES_PER_BLOC * BLOC_PER_HALF_RING) - chunk_len}"
                    )

            # won't grab data from current after this, can prefetch "next next" data
            # but don't do this if we are going to loop
            req_first_sample = next_hr.last_sample + 1
            if not self.loop_end or req_first_sample <= self.loop_end:
                print(f"mark hring#{current_hr_idx} for prefetch from {req_first_sample}")
                current_hr.req(req_first_sample)
            else:
                print(
                    f"DONT mark hring#{current_hr_idx} for prefetch from {req_first_sample} (loop)"
                )

        pos, frac = divmod(self.phase, 1)

        local_phase = (pos - current_hr.first_sample) + frac
        if current_hr_idx == 1:
            local_phase += HALF_RING_SIZE

        delta_phase = self.phase - local_phase
        assert delta_phase == int(delta_phase), "frac in delta_phase ?"

        for idx in range(dest_offset, dest_offset + dest_nb):
            pos, frac = divmod(local_phase, 1)

            s1 = self.ring[int(pos) & RING_MASK]
            s2 = self.ring[int(pos + 1) & RING_MASK]

            v = round(s1 + (s2 - s1) * frac)
            bloc[idx] += v

            local_phase += self.phase_inc

        # not looping
        if remain == 0:
            self.phase = local_phase + delta_phase
            return dest_nb, 0

        # we are looping, update phase to start again at loop start
        frac = local_phase % 1
        self.phase = self.loop_start + frac

        if first:
            # needs to overwrite current with loop cache.

            # stay in current (dont cross to next_hr)
            self.current_hr = current_hr_idx

            # check we've got the start of loop cached
            assert (
                self.loop_hring_cache
                and self.loop_hring_first_sample is not None
                and self.loop_hring_last_sample is not None
            ), "missing loop cache"

            # copy cached loop start over current
            current_hr.buf[:] = self.loop_hring_cache[:]
            current_hr.first_sample = self.loop_hring_first_sample
            current_hr.last_sample = self.loop_hring_last_sample

            # need to prefetch next ...
            req_first_sample = current_hr.last_sample + 1
            if not self.loop_end or req_first_sample <= self.loop_end:
                next_hr = self.half_rings[1 - current_hr_idx]
                print(f"mark hring#{1 - current_hr_idx} for LOOP prefetch from {req_first_sample}")
                next_hr.req(req_first_sample)
            else:
                print(
                    f"DONT mark hring#{1 - current_hr_idx} for LOOP prefetch from {req_first_sample} (loop)"
                )

        return dest_nb, remain


def get_voice_hr(voices: list[Voice | None], idx: int):
    voice_idx, hr_idx = divmod(idx, 2)
    if voice_idx >= len(voices):
        return None, None
    voice = voices[voice_idx]
    if voice is not None:
        hr = voice.half_rings[hr_idx]
        return voice, hr
    else:
        return None, None


class Prefetch:
    def __init__(self, card: SDCard):
        self.card = card
        self.cur_pos = 0
        self.state = "IDLE"

    # @micropython.native
    def process(self, voices: list[Voice | None]):
        if self.state == "FETCHING" and self.card.read_done():
            _, hr = get_voice_hr(voices, self.cur_pos)
            assert hr is not None and hr.state == "REQ"
            hr.ready()
            self.state = "IDLE"
            self.cur_pos += 1

        if self.state != "IDLE":
            return

        start = self.cur_pos
        max_pos = len(voices) * 2
        if start >= max_pos:
            start = max_pos - 1

        while voices:
            if self.cur_pos >= max_pos:
                self.cur_pos = 0

            voice, hr = get_voice_hr(voices, self.cur_pos)
            print(f"try prefetch in {self.cur_pos} {voice=} {hr=} = ", end="")
            if voice is not None and hr is not None and hr.state == "REQ":
                assert hr.first_sample is not None
                sector = hr.first_sample // 256
                assert sector == hr.first_sample / 256
                self.card.read_trigger(sector + voice.sample.first_sector, hr.buf)
                self.state = "FETCHING"
                print("FETCHING")
                break
            else:
                print("SKIP")

            self.cur_pos += 1

            if self.cur_pos == start:
                break


class Voices:
    def __init__(self, card: SDCard):
        self.prefetch = Prefetch(card)
        self.voices: list[Voice | None] = []
        self.buf = array.array("i", [0] * SAMPLES_PER_BLOC)

    def add_voice(self, voice: Voice):
        for idx, item in enumerate(self.voices):
            if item is None:
                self.voices[idx] = voice
        else:
            self.voices.append(voice)

    def del_voice(self, voice: Voice):
        idx = self.voices.index(voice)
        self.voices[idx] = None

    # @micropython.native
    def loop(self):
        self.prefetch.process(self.voices)
        # time.sleep(0.008)
        # self.prefetch.process(self.voices)

        for i in range(len(self.buf)):
            self.buf[i] = 0

        max_done = 0

        for idx, voice in enumerate(self.voices):
            if voice is None:
                continue

            if voice.state == "INIT":
                voice.bootstrap()

            if voice.state != "RUNNING":
                continue

            remain = SAMPLES_PER_BLOC
            first = True
            tot_done = 0
            while remain:
                print(f"v{idx}: fill_bloc {tot_done=} {remain=} {first=}")
                done, remain = voice.fill_bloc(self.buf, tot_done, remain, first)
                tot_done += done
                first = False

            if tot_done > max_done:
                max_done = tot_done

        return max_done

    def display(self):
        for idx, voice in enumerate(self.voices):
            if voice is None:
                continue
            print(f"Voice #{idx}")
            for hr in voice.half_rings:
                print(f"  {hr}")


# @micropython.native
def main(p=0, pitch=0):
    card = init_card()
    samples = get_samples(card)

    voices = Voices(card)
    v1 = Voice(samples[p], midi2freq(samples[p].hi_note + pitch))
    voices.add_voice(v1)

    voices.display()

    WAVE_SAMPLE_SIZE = 4
    with open("/remote/bloc_out.wav", "wb") as ofile:
        ofile.seek(44)

        tot = 0
        for idx in range(250 * 4):
            if idx == 200 * 4:
                print("LOOP OFF")
                for voice in voices.voices:
                    if voice:
                        voice.loop_off()

            n = voices.loop()
            if n:
                tot += n
                for i, value in enumerate(voices.buf):
                    voices.buf[i] = value * 65535
                ofile.write(bytes(voices.buf)[: WAVE_SAMPLE_SIZE * n])
                if n and n != 256:
                    break

        write_wav_header(ofile, WAVE_SAMPLE_SIZE, tot * WAVE_SAMPLE_SIZE)


""" SYSTEM_SRATE = 22050 -> strange

v0: 256
try prefetch in 1 voice=<Voice object at 20027500> hr=HalfRing#1 35840-36863:READY
try prefetch in 0 voice=<Voice object at 20027500> hr=HalfRing#0 36864-37887:READY
interpolate 256 samples at 2.000x from 36886 to 37142
v0: 256
try prefetch in 1 voice=<Voice object at 20027500> hr=HalfRing#1 37888-38911:REQ
interpolate -60 samples at 2.000x from 37398 to 37279
v0: -60
try prefetch in 0 voice=<Voice object at 20027500> hr=HalfRing#0 36864-37887:READY
v0: 0
try prefetch in 1 voice=<Voice object at 20027500> hr=HalfRing#1 37888-38911:READY
try prefetch in 0 voice=<Voice object at 20027500> hr=HalfRing#0 36864-37887:READY
v0: 0

"""


""" ROUND ROBIN

class TaskScheduler:
    def __init__(self):
        self.tasks = []
        self.index = 0

    def add_task(self, task):
        if task not in self.tasks:
            self.tasks.append(task)

    def remove_task(self, task):
        if task in self.tasks:
            pos = self.tasks.index(task)
            self.tasks.remove(task)
            # Ajuste l'index si on retire avant la position courante
            if pos < self.index:
                self.index -= 1

    def process_next(self):
        if not self.tasks:
            return None

        # Reboucle au début si fin atteinte
        if self.index >= len(self.tasks):
            self.index = 0

        task = self.tasks[self.index]
        self.index += 1
        return task

# Usage
s = TaskScheduler()
s.add_task("A")
s.add_task("B")
s.process_next()  # "A"
s.add_task("C")
s.process_next()  # "B"
s.process_next()  # "C"
s.process_next()  # "A" (reboucle)


"""

if __name__ == "__main__":
    main(0, -2)
