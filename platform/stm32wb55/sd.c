/* SD card in SPI mode: the block device underneath FatFs (diskio.h). */
#include "board.h"
#include "ff.h"
#include "diskio.h"

#define CMD0   0 /* GO_IDLE_STATE */
#define CMD8   8 /* SEND_IF_COND */
#define CMD9   9 /* SEND_CSD */
#define CMD12  12 /* STOP_TRANSMISSION */
#define CMD16  16 /* SET_BLOCKLEN */
#define CMD17  17 /* READ_SINGLE_BLOCK */
#define CMD24  24 /* WRITE_BLOCK */
#define CMD55  55 /* APP_CMD */
#define CMD58  58 /* READ_OCR */
#define ACMD41 (0x80 | 41) /* SD_SEND_OP_COND */

static bool ready;
static bool block_addressed; /* SDHC/SDXC: commands take block numbers */
static SpiSpeed speed = SPI_DIV256;

static uint8_t xfer(uint8_t b) {
    return spi_xfer(SPI2, b);
}

static bool wait_not_busy(uint32_t timeout_ms) {
    uint32_t start = millis();
    do {
        if(xfer(0xFF) == 0xFF) return true;
    } while(millis() - start < timeout_ms);
    return false;
}

static void deselect(void) {
    pin_write(PIN_SD_CS, true);
    xfer(0xFF); /* the card releases the data line one clock after CS goes high */
}

static bool select(void) {
    spi_init(SPI2, speed); /* the display shares this bus at a different speed */
    pin_write(PIN_SD_CS, false);
    xfer(0xFF);
    if(wait_not_busy(500)) return true;
    deselect();
    return false;
}

/* Send a command and return its R1 response (0xFF: no answer). CS stays low. */
static uint8_t command(uint8_t cmd, uint32_t arg) {
    if(cmd & 0x80) {
        uint8_t r = command(CMD55, 0);
        if(r > 1) return r;
        cmd &= 0x7F;
    }
    deselect();
    if(!select() && cmd != CMD0) return 0xFF;
    xfer((uint8_t)(0x40 | cmd));
    xfer((uint8_t)(arg >> 24));
    xfer((uint8_t)(arg >> 16));
    xfer((uint8_t)(arg >> 8));
    xfer((uint8_t)arg);
    /* CRC is only checked for these two commands in SPI mode */
    xfer(cmd == CMD0 ? 0x95 : cmd == CMD8 ? 0x87 : 0x01);
    uint8_t r = 0xFF;
    for(int i = 0; i < 10 && (r & 0x80); i++) r = xfer(0xFF);
    return r;
}

static bool read_block(uint8_t* buf, size_t len) {
    uint32_t start = millis();
    uint8_t token;
    do {
        token = xfer(0xFF);
    } while(token == 0xFF && millis() - start < 300);
    if(token != 0xFE) return false;
    for(size_t i = 0; i < len; i++) buf[i] = xfer(0xFF);
    xfer(0xFF); /* CRC, not checked */
    xfer(0xFF);
    return true;
}

static bool card_init(void) {
    pin_write(PIN_SD_CS, true);
    pin_init(PIN_SD_CS, PIN_OUTPUT, PULL_NONE, false, 0);
    speed = SPI_DIV256; /* 250 kHz while the card identifies itself */
    spi_init(SPI2, speed);
    for(int i = 0; i < 10; i++) xfer(0xFF); /* 80 clocks with CS high */

    bool ok = false;
    block_addressed = false;
    uint8_t ocr[4];
    if(command(CMD0, 0) == 1) {
        if(command(CMD8, 0x1AA) == 1) {
            /* SD version 2: check the voltage echo, then wait for it to leave idle */
            for(int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
            if(ocr[2] == 0x01 && ocr[3] == 0xAA) {
                uint32_t start = millis();
                while(millis() - start < 1500 && command(ACMD41, 1u << 30)) {
                }
                if(command(ACMD41, 1u << 30) == 0 && command(CMD58, 0) == 0) {
                    for(int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
                    block_addressed = ocr[0] & 0x40;
                    ok = true;
                }
            }
        } else {
            /* SD version 1 */
            uint32_t start = millis();
            while(millis() - start < 1500 && command(ACMD41, 0)) {
            }
            ok = command(ACMD41, 0) == 0;
        }
        if(ok && !block_addressed) ok = command(CMD16, 512) == 0;
    }
    deselect();
    if(ok) speed = SPI_DIV4; /* 16 MHz */
    return ok;
}

/* ------------------------------------------------------------ FatFs diskio */

DSTATUS disk_status(BYTE drv) {
    return (drv == 0 && ready) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE drv) {
    if(drv != 0) return STA_NOINIT;
    ready = card_init();
    return ready ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE drv, BYTE* buf, LBA_t sector, UINT count) {
    if(drv != 0 || !ready) return RES_NOTRDY;
    for(; count; count--, sector++, buf += 512) {
        uint32_t addr = block_addressed ? (uint32_t)sector : (uint32_t)sector * 512u;
        bool ok = command(CMD17, addr) == 0 && read_block(buf, 512);
        deselect();
        if(!ok) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE* buf, LBA_t sector, UINT count) {
    if(drv != 0 || !ready) return RES_NOTRDY;
    for(; count; count--, sector++, buf += 512) {
        uint32_t addr = block_addressed ? (uint32_t)sector : (uint32_t)sector * 512u;
        bool ok = command(CMD24, addr) == 0;
        if(ok) {
            xfer(0xFF);
            xfer(0xFE); /* data token */
            for(int i = 0; i < 512; i++) xfer(buf[i]);
            xfer(0xFF); /* dummy CRC */
            xfer(0xFF);
            ok = (xfer(0xFF) & 0x1F) == 0x05 && wait_not_busy(1000);
        }
        deselect();
        if(!ok) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE cmd, void* buf) {
    if(drv != 0 || !ready) return RES_NOTRDY;
    switch(cmd) {
    case CTRL_SYNC: {
        bool ok = select();
        deselect();
        return ok ? RES_OK : RES_ERROR;
    }
    case GET_SECTOR_SIZE:
        *(WORD*)buf = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD*)buf = 1;
        return RES_OK;
    case GET_SECTOR_COUNT: {
        uint8_t csd[16];
        bool ok = command(CMD9, 0) == 0 && read_block(csd, 16);
        deselect();
        if(!ok) return RES_ERROR;
        if((csd[0] >> 6) == 1) { /* CSD version 2 */
            uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
            *(LBA_t*)buf = (c_size + 1) << 10;
        } else {
            uint32_t n = (csd[5] & 15u) + ((csd[10] & 128u) >> 7) + ((csd[9] & 3u) << 1) + 2;
            uint32_t c_size = (csd[8] >> 6) + ((uint32_t)csd[7] << 2) + ((uint32_t)(csd[6] & 3) << 10) + 1;
            *(LBA_t*)buf = c_size << (n - 9);
        }
        return RES_OK;
    }
    default:
        return RES_PARERR;
    }
}

void sd_forget(void) {
    ready = false;
}
