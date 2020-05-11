#ifndef FF_DEFINED
#define FF_DEFINED 86606

#include "ffconf.h"

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || defined(__cplusplus)
#define FF_INTDEF 2
#include <stdint.h>
typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t QWORD;
typedef WORD WCHAR;
#else
#define FF_INTDEF 1
typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef WORD WCHAR;
#endif

#ifndef _INC_TCHAR
#define _INC_TCHAR

typedef char TCHAR;
#define _T(x) x
#define _TEXT(x) x

#endif
typedef DWORD FSIZE_t;
typedef DWORD LBA_t;

typedef struct
{
	BYTE fs_type;
	BYTE pdrv;
	BYTE n_fats;
	BYTE wflag;
	BYTE fsi_flag;
	WORD id;
	WORD n_rootdir;
	WORD csize;
#if !FF_FS_READONLY
	DWORD last_clst;
	DWORD free_clst;
#endif
	DWORD n_fatent;
	DWORD fsize;
	LBA_t volbase;
	LBA_t fatbase;
	LBA_t dirbase;
	LBA_t database;
	LBA_t winsect;
	BYTE win[FF_MAX_SS];
} FATFS;

typedef struct
{
	FATFS *fs;
	WORD id;
	BYTE attr;
	BYTE stat;
	DWORD sclust;
	FSIZE_t objsize;
} FFOBJID;

typedef struct
{
	FFOBJID obj;
	BYTE flag;
	BYTE err;
	FSIZE_t fptr;
	DWORD clust;
	LBA_t sect;
#if !FF_FS_READONLY
	LBA_t dir_sect;
	BYTE *dir_ptr;
#endif
} FIL;

typedef struct
{
	FFOBJID obj;
	DWORD dptr;
	DWORD clust;
	LBA_t sect;
	BYTE *dir;
	BYTE fn[12];
} DIR;

typedef struct
{
	FSIZE_t fsize;
	WORD fdate;
	WORD ftime;
	BYTE fattrib;
	TCHAR fname[12 + 1];
} FILINFO;

typedef struct
{
	BYTE fmt;
	BYTE n_fat;
	UINT align;
	UINT n_root;
	DWORD au_size;
} MKFS_PARM;

typedef enum
{
	FR_OK = 0,
	FR_DISK_ERR,
	FR_INT_ERR,
	FR_NOT_READY,
	FR_NO_FILE,
	FR_NO_PATH,
	FR_INVALID_NAME,
	FR_DENIED,
	FR_EXIST,
	FR_INVALID_OBJECT,
	FR_WRITE_PROTECTED,
	FR_INVALID_DRIVE,
	FR_NOT_ENABLED,
	FR_NO_FILESYSTEM,
	FR_MKFS_ABORTED,
	FR_TIMEOUT,
	FR_LOCKED,
	FR_NOT_ENOUGH_CORE,
	FR_TOO_MANY_OPEN_FILES,
	FR_INVALID_PARAMETER
} FRESULT;

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode);
FRESULT f_close(FIL *fp);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
FRESULT f_sync(FIL *fp);
FRESULT f_unlink(const TCHAR *path);
FRESULT f_stat(const TCHAR *path, FILINFO *fno);
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt);

#define f_size(fp) ((fp)->obj.objsize)

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_NEW 0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS 0x10
#define FA_OPEN_APPEND 0x30

#define CREATE_LINKMAP ((FSIZE_t)0 - 1)

#define FM_FAT 0x01
#define FM_FAT32 0x02
#define FM_EXFAT 0x04
#define FM_ANY 0x07
#define FM_SFD 0x08

#define FS_FAT12 1
#define FS_FAT16 2
#define FS_FAT32 3
#define FS_EXFAT 4

#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_DIR 0x10
#define AM_ARC 0x20

#endif