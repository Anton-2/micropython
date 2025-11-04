import struct
import math
import fast_font
from binascii import crc32

from sdcard import SDCard

WAVE = "4sI8sIHHIIHH4sI"
WAVE_SIZE = struct.calcsize(WAVE)

TUNE = 440.0
BASE_NOTE = 69

SYSTEM_SAMPLING_RATE = 44100
ONE = 1<<30
ONE_PHASE = fast_font.ONE_PHASE

SECTOR_SIZE = 512

def midi2freq(note: int, cent:int=0):
    decnote = note+cent/100.0
    return TUNE*2**((decnote-BASE_NOTE)/12)

def freq2midi(freq: float):
    no = math.log2(freq/TUNE)*12
    note = math.floor(no)
    cent = round((no-note)*100)
    return BASE_NOTE+note, cent


class Sample:
    path: str
    lo_note: int
    hi_note: int

    first_sector: int      # sector# of first sample
    samples_offset: int    # offset in bytes of first sample in sector

    sampling_rate: int
    pitch: float
    n_samples: int

    loop_start: int|None
    loop_end: int|None

    def __init__(self, card:SDCard, path:str, lo_note:int, hi_note:int, pitch_note:int, loop_start:int|None=None, loop_end:int|None=None):
        self.card = card
        self.path = path

        self.lo_note = lo_note
        self.hi_note = hi_note
        self.first_sector, file_length = card.get_first_sector(path)

        self.loop_start = loop_start
        self.loop_end = loop_end

        with open(path, "rb") as ifile:
            riff, _, wave, _, fmt, n_channels, self.sampling_rate, _, _, n_bits, data, data_size = struct.unpack(WAVE, ifile.read(WAVE_SIZE))
            assert riff == b"RIFF" and wave == b"WAVEfmt " and data == b'data', "Bad wav file"
            assert n_channels == 1 and n_bits == 16 and fmt == 1, f"Bad wav format {n_channels=} {n_bits=} {fmt=}"

        self.samples_offset = WAVE_SIZE
        self.pitch = midi2freq(pitch_note)
        self.n_samples = data_size // 2

        assert (file_length-self.samples_offset)//2 == self.n_samples, "Sample size mismatch"

    def get_phase_inc(self, dest_note):
        dest_pitch = midi2freq(dest_note)
        phase_inc_float = (dest_pitch / self.pitch) * (self.sampling_rate / SYSTEM_SAMPLING_RATE)
        return int(ONE_PHASE*phase_inc_float)

    def fill_buf(self, buf, start:int, end:int):
        start_byte = self.samples_offset + start * 2
        nb_bytes = (end-start) * 2 + 1

        first_sector = self.first_sector + start_byte//SECTOR_SIZE
        nb_sectors = 1+(self.samples_offset + nb_bytes)//SECTOR_SIZE

        assert len(buf) >= nb_sectors*SECTOR_SIZE

        dest = memoryview(buf)
        for idx in range(nb_sectors):
            self.card.read_single_sector(first_sector+idx, dest[idx*SECTOR_SIZE:idx*SECTOR_SIZE+SECTOR_SIZE])
