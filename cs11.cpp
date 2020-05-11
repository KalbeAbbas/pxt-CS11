#include "ff.h"
#include "diskio.h"
#include "inttypes.h"
#include "pxt.h"
#include "pxtcore.h"

namespace cs11
{

//%
void _mkdir(String s)
{
}

//%
void _remove(String s)
{
    FATFS FatFs;
    f_mount(&FatFs, "", 0);
    f_unlink((const char *)s->getUTF8Data());
}

//%
bool _file(String s, String v, uint8_t x)
{
    FATFS FatFs;
    f_mount(&FatFs, "", 0);
    FIL Fil;
    UINT bw;
    FRESULT fr;
    fr = f_open(&Fil, (const char *)s->getUTF8Data(), x);
    if (fr == FR_OK)
    {
        f_write(&Fil, (const char *)v->getUTF8Data(), v->getUTF8Size(), &bw);
        fr = f_close(&Fil);
        if (fr == FR_OK && bw == v->getUTF8Size())
        {
            return true;
        }
        return false;
    }
    return false;
}

//%
uint32_t _size(String s)
{
    FATFS FatFs;
    f_mount(&FatFs, "", 0);
    uint32_t lSize = 0;
    FIL Fil;
    FRESULT fr;
    fr = f_open(&Fil, (const char *)s->getUTF8Data(), FA_READ | FA_WRITE);
    lSize = f_size(&Fil);
    f_close(&Fil);
    return lSize;
}

//%
bool _exists(String s)
{
    FATFS FatFs;
    f_mount(&FatFs, "", 0);
    FRESULT fr;
    FILINFO fno;
    fr = f_stat((const char *)s->getUTF8Data(), &fno);
    if (fr == FR_OK)
        return true;
    return false;
}

//%
String _read(String s)
{
}
} // namespace cs11