#include "ff.h"
#include "diskio.h"

#include "pxt.h"

void DO_INIT()
{
  auto led = LOOKUP_PIN(MISO);
  led->setDigitalValue(0);
}

bool DO()
{
  auto led = LOOKUP_PIN(MISO);
  return led->getDigitalValue();
}

void DI_INIT()
{
  auto led = LOOKUP_PIN(MOSI);
  led->setDigitalValue(0);
}

void DI_H()
{
  auto led = LOOKUP_PIN(MOSI);
  led->setDigitalValue(1);
}
void DI_L()
{
  auto led = LOOKUP_PIN(MOSI);
  led->setDigitalValue(0);
}

void CK_INIT()
{
  auto led = LOOKUP_PIN(SCK);
  led->setDigitalValue(0);
}
void CK_H()
{
  auto led = LOOKUP_PIN(SCK);
  led->setDigitalValue(1);
}
void CK_L()
{
  auto led = LOOKUP_PIN(SCK);
  led->setDigitalValue(0);
}

void CS_INIT()
{
  auto led = getPin(PA09);
  led->setDigitalValue(0);
}
void CS_H()
{
  auto led = getPin(PA09);
  led->setDigitalValue(1);
}
void CS_L()
{
  auto led = getPin(PA09);
  led->setDigitalValue(0);
}

void dly_us(UINT n)
{
  sleep_us(n);
}

#define CMD0 (0)
#define CMD1 (1)
#define ACMD41 (0x80 + 41)
#define CMD8 (8)
#define CMD9 (9)
#define CMD12 (12)
#define CMD16 (16)
#define CMD17 (17)
#define CMD18 (18)
#define ACMD23 (0x80 + 23)
#define CMD24 (24)
#define CMD25 (25)
#define CMD55 (55)
#define CMD58 (58)

static DSTATUS Stat = STA_NOINIT;

static BYTE CardType;

static void xmit_mmc(
    const BYTE *buff,
    UINT bc)
{
  BYTE d;

  do
  {
    d = *buff++;
    if (d & 0x80)
      DI_H();
    else
      DI_L();
    CK_H();
    CK_L();
    if (d & 0x40)
      DI_H();
    else
      DI_L();
    CK_H();
    CK_L();
    if (d & 0x20)
      DI_H();
    else
      DI_L();
    CK_H();
    CK_L();
    if (d & 0x10)
      DI_H();
    else
      DI_L();
    CK_H();
    CK_L();
    if (d & 0x08)
      DI_H();
    else
      DI_L();
    CK_H();
    CK_L();
    if (d & 0x04)
      DI_H();
    else
      DI_L();
    CK_H();
    CK_L();
    if (d & 0x02)
      DI_H();
    else
      DI_L();
    CK_H();
    CK_L();
    if (d & 0x01)
      DI_H();
    else
      DI_L();
    CK_H();
    CK_L();
  } while (--bc);
}

static void rcvr_mmc(
    BYTE *buff,
    UINT bc)
{
  BYTE r;

  DI_H();

  do
  {
    r = 0;
    if (DO())
      r++;
    CK_H();
    CK_L();
    r <<= 1;
    if (DO())
      r++;
    CK_H();
    CK_L();
    r <<= 1;
    if (DO())
      r++;
    CK_H();
    CK_L();
    r <<= 1;
    if (DO())
      r++;
    CK_H();
    CK_L();
    r <<= 1;
    if (DO())
      r++;
    CK_H();
    CK_L();
    r <<= 1;
    if (DO())
      r++;
    CK_H();
    CK_L();
    r <<= 1;
    if (DO())
      r++;
    CK_H();
    CK_L();
    r <<= 1;
    if (DO())
      r++;
    CK_H();
    CK_L();
    *buff++ = r;
  } while (--bc);
}

static int wait_ready(void)
{
  BYTE d;
  UINT tmr;

  for (tmr = 5000; tmr; tmr--)
  {
    rcvr_mmc(&d, 1);
    if (d == 0xFF)
      break;
    dly_us(100);
  }

  return tmr ? 1 : 0;
}

void deselect(void)
{
  BYTE d;

  CS_H();
  rcvr_mmc(&d, 1);
}

static int select(void)
{
    auto er = getPin(PA16);
  er->setDigitalValue(1);
  BYTE d;

  CS_L();
  rcvr_mmc(&d, 1);
  if (wait_ready())
    return 1;

  deselect();
  return 0;
}

static int rcvr_datablock(
    BYTE *buff,
    UINT btr)
{
  BYTE d[2];
  UINT tmr;

  for (tmr = 1000; tmr; tmr--)
  {
    rcvr_mmc(d, 1);
    if (d[0] != 0xFF)
      break;
    dly_us(100);
  }
  if (d[0] != 0xFE)
    return 0;

  rcvr_mmc(buff, btr);
  rcvr_mmc(d, 2);

  return 1;
}

static int xmit_datablock(
    const BYTE *buff,
    BYTE token)
{
  BYTE d[2];

  if (!wait_ready())
    return 0;

  d[0] = token;
  xmit_mmc(d, 1);
  if (token != 0xFD)
  {
    xmit_mmc(buff, 512);
    rcvr_mmc(d, 2);
    rcvr_mmc(d, 1);
    if ((d[0] & 0x1F) != 0x05)
      return 0;
  }

  return 1;
}

static BYTE send_cmd(
    BYTE cmd,
    DWORD arg)
{
  BYTE n, d, buf[6];

  if (cmd & 0x80)
  {
    cmd &= 0x7F;
    n = send_cmd(CMD55, 0);
    if (n > 1)
      return n;
  }

  if (cmd != CMD12)
  {
    deselect();
    if (!select())
      return 0xFF;
  }

  buf[0] = 0x40 | cmd;
  buf[1] = (BYTE)(arg >> 24);
  buf[2] = (BYTE)(arg >> 16);
  buf[3] = (BYTE)(arg >> 8);
  buf[4] = (BYTE)arg;
  n = 0x01;
  if (cmd == CMD0)
    n = 0x95;
  if (cmd == CMD8)
    n = 0x87;
  buf[5] = n;
  xmit_mmc(buf, 6);

  if (cmd == CMD12)
    rcvr_mmc(&d, 1);
  n = 10;
  do
    rcvr_mmc(&d, 1);
  while ((d & 0x80) && --n);

  return d;
}

DSTATUS disk_status(
    BYTE drv)
{
  if (drv)
    return STA_NOINIT;

  return Stat;
}

DSTATUS disk_initialize(
    BYTE drv)
{
  BYTE n, ty, cmd, buf[4];
  UINT tmr;
  DSTATUS s;

  if (drv)
    return RES_NOTRDY;

  dly_us(10000);
  CS_INIT();
  CS_H();
  CK_INIT();
  CK_L();
  DI_INIT();
  DO_INIT();

  for (n = 10; n; n--)
    rcvr_mmc(buf, 1);

  ty = 0;
  if (send_cmd(CMD0, 0) == 1)
  {
    if (send_cmd(CMD8, 0x1AA) == 1)
    {
      rcvr_mmc(buf, 4);
      if (buf[2] == 0x01 && buf[3] == 0xAA)
      {
        for (tmr = 1000; tmr; tmr--)
        {
          if (send_cmd(ACMD41, 1UL << 30) == 0)
            break;
          dly_us(1000);
        }
        if (tmr && send_cmd(CMD58, 0) == 0)
        {
          rcvr_mmc(buf, 4);
          ty = (buf[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;
        }
      }
    }
    else
    {
      if (send_cmd(ACMD41, 0) <= 1)
      {
        ty = CT_SD1;
        cmd = ACMD41;
      }
      else
      {
        ty = CT_MMC;
        cmd = CMD1;
      }
      for (tmr = 1000; tmr; tmr--)
      {
        if (send_cmd(cmd, 0) == 0)
          break;
        dly_us(1000);
      }
      if (!tmr || send_cmd(CMD16, 512) != 0)
        ty = 0;
    }
  }
  CardType = ty;
  s = ty ? 0 : STA_NOINIT;
  Stat = s;

  deselect();

  return s;
}

DRESULT disk_read(
    BYTE drv,
    BYTE *buff,
    LBA_t sector,
    UINT count)
{
  BYTE cmd;
  DWORD sect = (DWORD)sector;

  if (disk_status(drv) & STA_NOINIT)
    return RES_NOTRDY;
  if (!(CardType & CT_BLOCK))
    sect *= 512;

  cmd = count > 1 ? CMD18 : CMD17;
  if (send_cmd(cmd, sect) == 0)
  {
    do
    {
      if (!rcvr_datablock(buff, 512))
        break;
      buff += 512;
    } while (--count);
    if (cmd == CMD18)
      send_cmd(CMD12, 0);
  }
  deselect();

  return count ? RES_ERROR : RES_OK;
}

DRESULT disk_write(
    BYTE drv,
    const BYTE *buff,
    LBA_t sector,
    UINT count)
{
  DWORD sect = (DWORD)sector;

  if (disk_status(drv) & STA_NOINIT)
    return RES_NOTRDY;
  if (!(CardType & CT_BLOCK))
    sect *= 512;

  if (count == 1)
  {
    if ((send_cmd(CMD24, sect) == 0) && xmit_datablock(buff, 0xFE))
      count = 0;
  }
  else
  {
    if (CardType & CT_SDC)
      send_cmd(ACMD23, count);
    if (send_cmd(CMD25, sect) == 0)
    {
      do
      {
        if (!xmit_datablock(buff, 0xFC))
          break;
        buff += 512;
      } while (--count);
      if (!xmit_datablock(0, 0xFD))
        count = 1;
    }
  }
  deselect();

  return count ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(
    BYTE drv,
    BYTE ctrl,
    void *buff)
{
  DRESULT res;
  BYTE n, csd[16];
  DWORD cs;

  if (disk_status(drv) & STA_NOINIT)
    return RES_NOTRDY;

  res = RES_ERROR;
  switch (ctrl)
  {
  case CTRL_SYNC:
    if (select())
      res = RES_OK;
    break;

  case GET_SECTOR_COUNT:
    if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16))
    {
      if ((csd[0] >> 6) == 1)
      {
        cs = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
        *(LBA_t *)buff = cs << 10;
      }
      else
      {
        n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
        cs = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
        *(LBA_t *)buff = cs << (n - 9);
      }
      res = RES_OK;
    }
    break;

  case GET_BLOCK_SIZE:
    *(DWORD *)buff = 128;
    res = RES_OK;
    break;

  default:
    res = RES_PARERR;
  }

  deselect();

  return res;
}
