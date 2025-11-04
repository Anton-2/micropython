import time
import ae_sdio as sdio
from micropython import const
import random
import vfs
from binascii import crc32
import gc

# cmds
# GO_IDLE_STATE - init card in spi mode if CS low
CMD0 = const(0X00)
# ALL_SEND_CID - Asks any card to send the CID.
CMD2 = const(0X02)
# SEND_RELATIVE_ADDR - Ask the card to publish a new RCA.
CMD3 = const(0X03)
# SWITCH_FUNC - Switch Function Command
CMD6 = const(0X06)
# SELECT/DESELECT_CARD - toggles between the stand-by and transfer states.
CMD7 = const(0X07)
# SEND_IF_COND - verify SD Memory Card interface operating condition.*/
CMD8 = const(0X08)
# SEND_CSD - read the Card Specific Data (CSD register)
CMD9 = const(0X09)
# SEND_CID - read the card identification information (CID register)
CMD10 = const(0X0A)
# VOLTAGE_SWITCH -Switch to 1.8V bus signaling level.
CMD11 = const(0X0B)
# STOP_TRANSMISSION - end multiple sector read sequence
CMD12 = const(0X0C)
# SEND_STATUS - read the card status register
CMD13 = const(0X0D)
# READ_SINGLE_SECTOR - read a single data sector from the card
CMD17 = const(0X11)
# READ_MULTIPLE_SECTOR - read multiple data sectors from the card
CMD18 = const(0X12)
# WRITE_SECTOR - write a single data sector to the card
CMD24 = const(0X18)
# WRITE_MULTIPLE_SECTOR - write sectors of data until a STOP_TRANSMISSION
CMD25 = const(0X19)
# ERASE_WR_BLK_START - sets the address of the first sector to be erased
CMD32 = const(0X20)
# ERASE_WR_BLK_END - sets the address of the last sector of the continuous range to be erased
CMD33 = const(0X21)
# ERASE - erase all previously selected sectors
CMD38 = const(0X26)
# APP_CMD - escape for application specific command
CMD55 = const(0X37)
# READ_OCR - read the OCR register of a card
CMD58 = const(0X3A)
# CRC_ON_OFF - enable or disable CRC checking
CMD59 = const(0X3B)
# SET_BUS_WIDTH - Defines the data bus width for data transfer.
ACMD6 = const(0X06)
# SD_STATUS - Send the SD Status.
ACMD13 = const(0X0D)
# SET_WR_BLK_ERASE_COUNT - Set the number of write sectors to be pre-erased before writing
ACMD23 = const(0X17)
# SD_SEND_OP_COMD - Sends host capacity support information and activates the card's initialization process
ACMD41 = const(0X29)
# Reads the SD Configuration Register (SCR).
ACMD51 = const(0X33)


# flags
FLAG_NO_CRC      = const(0x0001)
FLAG_NO_LOGMSG   = const(0x0002)
FLAG_NO_CMD_TAG  = const(0x0004)
FLAG_STOP_CLK    = const(0x0008)


# status
SDIO_OK = const(0)
SDIO_BUSY = const(1)
SDIO_ERR_RESPONSE_TIMEOUT = const(2) # Timed out waiting for response from card
SDIO_ERR_RESPONSE_CRC = const(3)     # Response CRC is wrong
SDIO_ERR_RESPONSE_CODE = const(4)    # Response command code does not match what was sent
SDIO_ERR_DATA_TIMEOUT = const(5)     # Timed out waiting for data block
SDIO_ERR_DATA_CRC = const(6)         # CRC for data packet is wrong
SDIO_ERR_WRITE_CRC = const(7)        # Card reports bad CRC for write
SDIO_ERR_WRITE_FAIL = const(8)       # Card reports write failure
SDIO_ERR_STOP_TIMEOUT = const(9)     # Timeout waiting for card to be idle
SDIO_ERR_INVALID_PARAM = const(10)   # Invalid parameters to function

# mode/speed
MODE_INITIALIZE             = const(0) # Initialization 300 kHz
MODE_MMC                    = const(1) # Old MMC cards, 20 MHz
MODE_STANDARD               = const(2) # Standard 25 MHz
MODE_HIGHSPEED              = const(3) # High-speed 50 MHz
MODE_HIGHSPEED_OVERCLOCK    = const(4) # High-speed 75 MHz, experimental

# misc
CARD_OCR_MODE = const((1 << 30) | (1 << 28) | (1 << 20))

# states
# SD is in idle state.
IDLE_STATE = const(0)
# SD is in multi-sector read state.
READ_STATE = const(1)
# SD is in multi-sector write state.
WRITE_STATE = const(2)

# timeouts
SDIO_CMD_TIMEOUT_US = const(50)

# Timeout for read/write transfers, total
SDIO_TRANSFER_TIMEOUT_US = const(1000 * 1000)

# Timeout for card initialization
SDIO_INIT_TIMEOUT_US = const(1000 * 1000)

SDIO_BLOCK_SIZE = const(512)

class SDIO_Error(Exception):
    pass

class SDCard:

    cur_state:int = IDLE_STATE
    cur_sector: int = -1
    error:int = SDIO_OK
    error_line:int = 0
    sdio_clk_hz:int = 0

    cid: bytearray = bytearray(16)
    csd: bytearray = bytearray(16)
    rca: int

    type: str
    sector_count: int

    # for traced load
    last_sector: int|None = None
    last_crc: int|None = None

    @micropython.native
    def command(self, cmd, args, buf, flags):
        status = sdio.command(cmd, args, buf, flags)
        if status != SDIO_OK:
            raise SDIO_Error(status)

    @micropython.native
    def command_u32(self, cmd, args, flags):
        status = sdio.command_u32(cmd, args, flags)
        if status != SDIO_OK:
            raise SDIO_Error(status)
        return sdio.result()

    def get_type(self, ocr):
        return "SDHC" if ocr & (1 << 30) else "SD2"

    def get_sector_count(self):
        ver = self.csd[0] >> 6
        if ver == 0:
            c_size = (self.csd[6] & 3) << 10
            c_size |= (self.csd[7]<< 2) | (self.csd[8] >> 6)
            c_size_mult = ((self.csd[9] & 3) << 1) | (self.csd[10] >> 7)
            read_bl_len = self.csd[5] & 15
            return (c_size + 1) << (c_size_mult + read_bl_len + 2 - 9)
        elif ver == 1:
            c_size = (self.csd[7] & 63) << 16
            c_size |= self.csd[8] << 8
            c_size |= self.csd[9]
            return (c_size + 1) << 10
        else:
            return 0

    def begin(self):

        sdio.init(MODE_INITIALIZE)
        time.sleep_us(1000)

        for retries in range(5):
            time.sleep_us(1000)
            _ = sdio.command(CMD0, 0, None, FLAG_NO_LOGMSG)
            time.sleep_us(1000)
            status = sdio.command_u32(CMD8, 0x1AA, FLAG_NO_LOGMSG)
            reply = sdio.result()
            if status == SDIO_OK and reply == 0x1AA:
                break;

        if reply != 0x1AA or status != SDIO_OK:
            print(f"No response to CMD8 SEND_IF_COND {status=} {reply=} {retries=}")
            raise SDIO_Error(status)

        # Send ACMD41 to begin card initialization and wait for it to complete
        start = time.ticks_us()
        ocr = 0
        while not (ocr&(1 << 31)):
            self.command_u32(CMD55, 0, 0)
            ocr = self.command_u32(ACMD41, CARD_OCR_MODE, FLAG_NO_CRC | FLAG_NO_CMD_TAG)

            if time.ticks_diff(time.ticks_us(), start) > SDIO_INIT_TIMEOUT_US:
                raise SDIO_Error(f"SD card initialization timeout, {ocr:08X}")

        self.type = self.get_type(ocr)

        # Get CID
        self.command(CMD2, 0, self.cid, FLAG_NO_CRC | FLAG_NO_CMD_TAG)

        # Get relative card address
        self.rca =  self.command_u32(CMD3, 0, 0)

        # Get CSD
        self.command(CMD9, self.rca, self.csd, FLAG_NO_CRC | FLAG_NO_CMD_TAG)
        self.sector_count = self.get_sector_count()

        # Select card
        self.command_u32(CMD7, self.rca, 0)

        # Set 4-bit bus mode
        self.command_u32(CMD55, self.rca, 0)
        self.command_u32(ACMD6, 2, 0)

        self.cur_state = IDLE_STATE

    @micropython.native
    def stop_transmission(self, blocking=False):
        sdio.command_u32(CMD12, 0, FLAG_NO_LOGMSG)
        self.cur_state = IDLE_STATE
        self.cur_sector = -1

        if not blocking:
            return

        start = time.ticks_us()
        status = -1
        while (time.ticks_us() - start) < SDIO_TRANSFER_TIMEOUT_US:
            if sdio.is_busy():
                continue

            status = sdio.command_u32(CMD13, self.rca, 0)
            if status != SDIO_OK:
                continue

            state = (sdio.result() >> 9) & 0x0F
            if state != 5:
                return

        raise SDIO_Error(f"TimeOut {status}")



    @micropython.native
    def read_start(self, sector):
        if self.cur_state == READ_STATE and self.cur_sector == sector:
            return

        if self.cur_state != IDLE_STATE:
            self.stop_transmission(True)

        address = sector if (self.type == "SDHC") else sector * 512;

        # Send the multiple read command and then stop clock before first block
        status = sdio.command_u32(CMD18, address, FLAG_STOP_CLK)
        if status != SDIO_OK:
            print("CMD18 failed, stopping")
            self.stop_transmission(False)
            raise SDIO_Error(status)

        self.cur_state = READ_STATE
        self.cur_sector = sector

    @micropython.native
    def read_data(self, buffer):
        assert self.cur_state == READ_STATE, "read_data while not in READ_STATE"

        if (status := sdio.rx_start(buffer, SDIO_BLOCK_SIZE)) != SDIO_OK:
            raise SDIO_Error(status)

        while (status:=sdio.rx_poll()) == SDIO_BUSY:
            pass

        if status != SDIO_OK:
            raise SDIO_Error(status)

        self.cur_sector += len(buffer)//SDIO_BLOCK_SIZE

    @micropython.native
    def read_trigger(self, sector, buffer):
        self.read_start(sector)
        if (status := sdio.rx_start(buffer, SDIO_BLOCK_SIZE)) != SDIO_OK:
            raise SDIO_Error(status)
        self.cur_sector += len(buffer)//SDIO_BLOCK_SIZE

    @micropython.native
    def read_done(self):
        status= sdio.rx_poll()
        if status == SDIO_BUSY:
            return False

        if status != SDIO_OK:
            raise SDIO_Error(status)

        return True


    @micropython.native
    def read_single_sector(self, sector, buffer):
        address = sector if (self.type == "SDHC") else sector * 512;
        # SET_BLOCKLEN
        self.command_u32(16, SDIO_BLOCK_SIZE, 0)
        # READ_SINGLE_BLOCK
        reply = self.command_u32(CMD17, address, FLAG_STOP_CLK)

        # Prepare for reception
        if (status := sdio.rx_start(buffer, SDIO_BLOCK_SIZE)) != SDIO_OK:
            raise SDIO_Error(status)

        if (reply & 0xFFF80000) and (sector != (self.sector_count-1)):
            sdio.stop()
            raise SDIO_Error(status)

        while (status:=sdio.rx_poll()) == SDIO_BUSY:
            pass

        sdio.stop()

        if status != SDIO_OK:
            raise SDIO_Error(status)

    @micropython.native
    def readblocks(self, block_num, buf):
        block_count = len(buf)//SDIO_BLOCK_SIZE
        if block_count == 1:
            self.read_single_sector(block_num, buf)
            self.last_sector = block_num
            self.last_crc = crc32(buf)
            return

        print(f"multi read {block_count} blocks from {block_num}")
        mv = memoryview(buf)
        for i in range(block_count):
            self.read_single_sector(block_num+i, mv[512*i:512*i+512])

    def ioctl(self, op, arg):
        print(f"ioctl {op} {arg}")
        if op==4:
            return self.sector_count
        if op==5:
            return 512

        return 0

    def get_first_sector(self, path):
        chunks, file_length = extract_chunks(self, path)
        assert len(chunks) == 1, f"Fragmentation detected in {path}"
        return chunks[0][1], file_length

def init_card():
    card = SDCard()
    card.begin()
    print(f"{card.type} card, {card.sector_count} sectors ({512*card.sector_count/1_000_000_000:.1f}GB)")


    sdio.init(MODE_STANDARD)
    data = bytearray(512)

    avail = gc.mem_free()
    for idx in range(10):
        try:
            card.read_single_sector(0, data)
            if sum(data) != 1612:
                print(f'MODE_STANDARD sum fail {idx} {sum(data)=}')
        except SDIO_Error as e:
            print(f'MODE_STANDARD fail {idx} {e}')


    print(f"testing eat {(avail-gc.mem_free())/1024}k")

    nb_sect = 1000
    delta = 0
    data = bytearray(512)
    for i in range(nb_sect):
        sect = random.randint(0, 2_000_000)
        start = time.ticks_us()
        card.read_single_sector(sect, data)
        delta += (time.ticks_us()-start)
    delta /= nb_sect
    print(f"MODE_STANDARD took {delta:.2f}  useconds")

    vfs.mount(card, "/sd", readonly=True)

    return card


def extract_chunks(card, path):
    cur_sector = None
    buf = bytearray(512)
    chunks = []
    offset = 0
    with open(path, "rb") as ifile:
        while True:
            sz = ifile.readinto(buf)

            if sz == 0:
                break

            crc = crc32(buf)
            if sz==512 and crc != card.last_crc:
                raise Exception("crc mismatch")

            read_sect = card.last_sector
            if cur_sector is None or (read_sect != (cur_sector+1)):
                chunks.append((offset, read_sect))

            offset += sz
            cur_sector = read_sect

    return chunks, offset
