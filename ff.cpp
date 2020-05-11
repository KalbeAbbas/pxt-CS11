#include "ff.h"
#include "diskio.h"

#define MAX_DIR 0x200000
#define MAX_DIR_EX 0x10000000
#define MAX_FAT12 0xFF5
#define MAX_FAT16 0xFFF5
#define MAX_FAT32 0x0FFFFFF5

#define IsLower(c) ((c) >= 'a' && (c) <= 'z')
#define IsDigit(c) ((c) >= '0' && (c) <= '9')

#define FA_SEEKEND 0x20
#define FA_MODIFIED 0x40

#define AM_VOL 0x08
#define AM_LFN 0x0F
#define AM_MASK 0x3F

#define NSFLAG 11

#define NS_LAST 0x04

#define NS_DOT 0x20

#define NS_NONAME 0x80

#define BS_JmpBoot 0

#define BPB_BytsPerSec 11
#define BPB_SecPerClus 13
#define BPB_RsvdSecCnt 14
#define BPB_NumFATs 16
#define BPB_RootEntCnt 17
#define BPB_TotSec16 19

#define BPB_FATSz16 22

#define BPB_TotSec32 32

#define BS_FilSysType 54

#define BS_55AA 510

#define BPB_FATSz32 36

#define BPB_FSVer32 42
#define BPB_RootClus32 44
#define BPB_FSInfo32 48

#define BS_FilSysType32 82

#define DIR_Name 0
#define DIR_Attr 11

#define DIR_CrtTime 14
#define DIR_LstAccDate 18
#define DIR_FstClusHI 20
#define DIR_ModTime 22
#define DIR_FstClusLO 26
#define DIR_FileSize 28

#define SZDIRE 32
#define DDEM 0xE5
#define RDDEM 0x05

#define FSI_LeadSig 0
#define FSI_StrucSig 484
#define FSI_Free_Count 488
#define FSI_Nxt_Free 492

#define MBR_Table 446
#define SZ_PTE 16

#define PTE_StLba 8

#define ABORT(fs, res)         \
	{                          \
		fp->err = (BYTE)(res); \
		LEAVE_FF(fs, res);     \
	}

#define LEAVE_FF(fs, res) return res

#define LD2PD(vol) (BYTE)(vol)
#define LD2PT(vol) 0

#define SS(fs) ((UINT)FF_MAX_SS)

#define GET_FATTIME() ((DWORD)(FF_NORTC_YEAR - 1980) << 25 | (DWORD)FF_NORTC_MON << 21 | (DWORD)FF_NORTC_MDAY << 16)

#define TBL_DC932                                                  \
	{                                                              \
		0x81, 0x9F, 0xE0, 0xFC, 0x40, 0x7E, 0x80, 0xFC, 0x00, 0x00 \
	}

#define MERGE_2STR(a, b) a##b
#define MKCVTBL(hd, cp) MERGE_2STR(hd, cp)

static FATFS *FatFs[FF_VOLUMES];
static WORD Fsid;

#define DEF_NAMBUF
#define INIT_NAMBUF(fs)
#define FREE_NAMBUF()

static const BYTE DbcTbl[] = MKCVTBL(TBL_DC, FF_CODE_PAGE);

static WORD ld_word(const BYTE *ptr)
{
	WORD rv;

	rv = ptr[1];
	rv = rv << 8 | ptr[0];
	return rv;
}

static DWORD ld_dword(const BYTE *ptr)
{
	DWORD rv;

	rv = ptr[3];
	rv = rv << 8 | ptr[2];
	rv = rv << 8 | ptr[1];
	rv = rv << 8 | ptr[0];
	return rv;
}

#if !FF_FS_READONLY
static void st_word(BYTE *ptr, WORD val)
{
	*ptr++ = (BYTE)val;
	val >>= 8;
	*ptr++ = (BYTE)val;
}

static void st_dword(BYTE *ptr, DWORD val)
{
	*ptr++ = (BYTE)val;
	val >>= 8;
	*ptr++ = (BYTE)val;
	val >>= 8;
	*ptr++ = (BYTE)val;
	val >>= 8;
	*ptr++ = (BYTE)val;
}

#endif

static void mem_cpy(void *dst, const void *src, UINT cnt)
{
	BYTE *d = (BYTE *)dst;
	const BYTE *s = (const BYTE *)src;

	if (cnt != 0)
	{
		do
		{
			*d++ = *s++;
		} while (--cnt);
	}
}

static void mem_set(void *dst, int val, UINT cnt)
{
	BYTE *d = (BYTE *)dst;

	do
	{
		*d++ = (BYTE)val;
	} while (--cnt);
}

static int mem_cmp(const void *dst, const void *src, UINT cnt)
{
	const BYTE *d = (const BYTE *)dst, *s = (const BYTE *)src;
	int r = 0;

	do
	{
		r = *d++ - *s++;
	} while (--cnt && r == 0);

	return r;
}

static int chk_chr(const char *str, int chr)
{
	while (*str && *str != chr)
		str++;
	return *str;
}

static int dbc_1st(BYTE c)
{
	if (c >= DbcTbl[0])
	{
		if (c <= DbcTbl[1])
			return 1;
		if (c >= DbcTbl[2] && c <= DbcTbl[3])
			return 1;
	}
	return 0;
}

static int dbc_2nd(BYTE c)
{
	if (c >= DbcTbl[4])
	{
		if (c <= DbcTbl[5])
			return 1;
		if (c >= DbcTbl[6] && c <= DbcTbl[7])
			return 1;
		if (c >= DbcTbl[8] && c <= DbcTbl[9])
			return 1;
	}
	return 0;
}

#if !FF_FS_READONLY
static FRESULT sync_window(
	FATFS *fs)
{
	FRESULT res = FR_OK;

	if (fs->wflag)
	{
		if (disk_write(fs->pdrv, fs->win, fs->winsect, 1) == RES_OK)
		{
			fs->wflag = 0;
			if (fs->winsect - fs->fatbase < fs->fsize)
			{
				if (fs->n_fats == 2)
					disk_write(fs->pdrv, fs->win, fs->winsect + fs->fsize, 1);
			}
		}
		else
		{
			res = FR_DISK_ERR;
		}
	}
	return res;
}
#endif

static FRESULT move_window(
	FATFS *fs,
	LBA_t sect)
{
	FRESULT res = FR_OK;

	if (sect != fs->winsect)
	{
#if !FF_FS_READONLY
		res = sync_window(fs);
#endif
		if (res == FR_OK)
		{
			if (disk_read(fs->pdrv, fs->win, sect, 1) != RES_OK)
			{
				sect = (LBA_t)0 - 1;
				res = FR_DISK_ERR;
			}
			fs->winsect = sect;
		}
	}
	return res;
}

#if !FF_FS_READONLY

static FRESULT sync_fs(
	FATFS *fs)
{
	FRESULT res;

	res = sync_window(fs);
	if (res == FR_OK)
	{
		if (fs->fs_type == FS_FAT32 && fs->fsi_flag == 1)
		{

			mem_set(fs->win, 0, sizeof fs->win);
			st_word(fs->win + BS_55AA, 0xAA55);
			st_dword(fs->win + FSI_LeadSig, 0x41615252);
			st_dword(fs->win + FSI_StrucSig, 0x61417272);
			st_dword(fs->win + FSI_Free_Count, fs->free_clst);
			st_dword(fs->win + FSI_Nxt_Free, fs->last_clst);

			fs->winsect = fs->volbase + 1;
			disk_write(fs->pdrv, fs->win, fs->winsect, 1);
			fs->fsi_flag = 0;
		}

		if (disk_ioctl(fs->pdrv, CTRL_SYNC, 0) != RES_OK)
			res = FR_DISK_ERR;
	}

	return res;
}

#endif

static LBA_t clst2sect(
	FATFS *fs,
	DWORD clst)
{
	clst -= 2;
	if (clst >= fs->n_fatent - 2)
		return 0;
	return fs->database + (LBA_t)fs->csize * clst;
}

static DWORD get_fat(
	FFOBJID *obj,
	DWORD clst)
{
	UINT wc, bc;
	DWORD val;
	FATFS *fs = obj->fs;

	if (clst < 2 || clst >= fs->n_fatent)
	{
		val = 1;
	}
	else
	{
		val = 0xFFFFFFFF;

		switch (fs->fs_type)
		{
		case FS_FAT32:
			if (move_window(fs, fs->fatbase + (clst / (SS(fs) / 4))) != FR_OK)
				break;
			val = ld_dword(fs->win + clst * 4 % SS(fs)) & 0x0FFFFFFF;
			break;
		default:
			val = 1;
		}
	}

	return val;
}

#if !FF_FS_READONLY

static FRESULT put_fat(
	FATFS *fs,
	DWORD clst,
	DWORD val)
{
	UINT bc;
	BYTE *p;
	FRESULT res = FR_INT_ERR;

	if (clst >= 2 && clst < fs->n_fatent)
	{
		switch (fs->fs_type)
		{
		case FS_FAT32:
			res = move_window(fs, fs->fatbase + (clst / (SS(fs) / 4)));
			if (res != FR_OK)
				break;
			if (!FF_FS_EXFAT || fs->fs_type != FS_EXFAT)
			{
				val = (val & 0x0FFFFFFF) | (ld_dword(fs->win + clst * 4 % SS(fs)) & 0xF0000000);
			}
			st_dword(fs->win + clst * 4 % SS(fs), val);
			fs->wflag = 1;
			break;
		}
	}
	return res;
}

#endif

#if !FF_FS_READONLY

static FRESULT remove_chain(
	FFOBJID *obj,
	DWORD clst,
	DWORD pclst)
{
	FRESULT res = FR_OK;
	DWORD nxt;
	FATFS *fs = obj->fs;

	if (clst < 2 || clst >= fs->n_fatent)
		return FR_INT_ERR;

	if (pclst != 0 && (!FF_FS_EXFAT || fs->fs_type != FS_EXFAT || obj->stat != 2))
	{
		res = put_fat(fs, pclst, 0xFFFFFFFF);
		if (res != FR_OK)
			return res;
	}

	do
	{
		nxt = get_fat(obj, clst);
		if (nxt == 0)
			break;
		if (nxt == 1)
			return FR_INT_ERR;
		if (nxt == 0xFFFFFFFF)
			return FR_DISK_ERR;
		if (!FF_FS_EXFAT || fs->fs_type != FS_EXFAT)
		{
			res = put_fat(fs, clst, 0);
			if (res != FR_OK)
				return res;
		}
		if (fs->free_clst < fs->n_fatent - 2)
		{
			fs->free_clst++;
			fs->fsi_flag |= 1;
		}
		clst = nxt;
	} while (clst < fs->n_fatent);
	return FR_OK;
}

static DWORD create_chain(
	FFOBJID *obj,
	DWORD clst)
{
	DWORD cs, ncl, scl;
	FRESULT res;
	FATFS *fs = obj->fs;

	if (clst == 0)
	{
		scl = fs->last_clst;
		if (scl == 0 || scl >= fs->n_fatent)
			scl = 1;
	}
	else
	{
		cs = get_fat(obj, clst);
		if (cs < 2)
			return 1;
		if (cs == 0xFFFFFFFF)
			return cs;
		if (cs < fs->n_fatent)
			return cs;
		scl = clst;
	}
	if (fs->free_clst == 0)
		return 0;
	{
		ncl = 0;
		if (scl == clst)
		{
			ncl = scl + 1;
			if (ncl >= fs->n_fatent)
				ncl = 2;
			cs = get_fat(obj, ncl);
			if (cs == 1 || cs == 0xFFFFFFFF)
				return cs;
			if (cs != 0)
			{
				cs = fs->last_clst;
				if (cs >= 2 && cs < fs->n_fatent)
					scl = cs;
				ncl = 0;
			}
		}
		if (ncl == 0)
		{
			ncl = scl;
			for (;;)
			{
				ncl++;
				if (ncl >= fs->n_fatent)
				{
					ncl = 2;
					if (ncl > scl)
						return 0;
				}
				cs = get_fat(obj, ncl);
				if (cs == 0)
					break;
				if (cs == 1 || cs == 0xFFFFFFFF)
					return cs;
				if (ncl == scl)
					return 0;
			}
		}
		res = put_fat(fs, ncl, 0xFFFFFFFF);
		if (res == FR_OK && clst != 0)
		{
			res = put_fat(fs, clst, ncl);
		}
	}

	if (res == FR_OK)
	{
		fs->last_clst = ncl;
		if (fs->free_clst <= fs->n_fatent - 2)
			fs->free_clst--;
		fs->fsi_flag |= 1;
	}
	else
	{
		ncl = (res == FR_DISK_ERR) ? 0xFFFFFFFF : 1;
	}

	return ncl;
}

#endif

#if !FF_FS_READONLY
static FRESULT dir_clear(
	FATFS *fs,
	DWORD clst)
{
	LBA_t sect;
	UINT n, szb;
	BYTE *ibuf;

	if (sync_window(fs) != FR_OK)
		return FR_DISK_ERR;
	sect = clst2sect(fs, clst);
	fs->winsect = sect;
	mem_set(fs->win, 0, sizeof fs->win);
	{
		ibuf = fs->win;
		szb = 1;
		for (n = 0; n < fs->csize && disk_write(fs->pdrv, ibuf, sect + n, szb) == RES_OK; n += szb)
			;
	}
	return (n == fs->csize) ? FR_OK : FR_DISK_ERR;
}
#endif

static FRESULT dir_sdi(
	DIR *dp,
	DWORD ofs)
{
	DWORD csz, clst;
	FATFS *fs = dp->obj.fs;

	if (ofs >= (DWORD)((FF_FS_EXFAT && fs->fs_type == FS_EXFAT) ? MAX_DIR_EX : MAX_DIR) || ofs % SZDIRE)
	{
		return FR_INT_ERR;
	}
	dp->dptr = ofs;
	clst = dp->obj.sclust;
	if (clst == 0 && fs->fs_type >= FS_FAT32)
	{
		clst = (DWORD)fs->dirbase;
		if (FF_FS_EXFAT)
			dp->obj.stat = 0;
	}

	if (clst == 0)
	{
		if (ofs / SZDIRE >= fs->n_rootdir)
			return FR_INT_ERR;
		dp->sect = fs->dirbase;
	}
	else
	{
		csz = (DWORD)fs->csize * SS(fs);
		while (ofs >= csz)
		{
			clst = get_fat(&dp->obj, clst);
			if (clst == 0xFFFFFFFF)
				return FR_DISK_ERR;
			if (clst < 2 || clst >= fs->n_fatent)
				return FR_INT_ERR;
			ofs -= csz;
		}
		dp->sect = clst2sect(fs, clst);
	}
	dp->clust = clst;
	if (dp->sect == 0)
		return FR_INT_ERR;
	dp->sect += ofs / SS(fs);
	dp->dir = fs->win + (ofs % SS(fs));

	return FR_OK;
}

static FRESULT dir_next(
	DIR *dp,
	int stretch)
{
	DWORD ofs, clst;
	FATFS *fs = dp->obj.fs;

	ofs = dp->dptr + SZDIRE;
	if (ofs >= (DWORD)((FF_FS_EXFAT && fs->fs_type == FS_EXFAT) ? MAX_DIR_EX : MAX_DIR))
		dp->sect = 0;
	if (dp->sect == 0)
		return FR_NO_FILE;

	if (ofs % SS(fs) == 0)
	{
		dp->sect++;

		if (dp->clust == 0)
		{
			if (ofs / SZDIRE >= fs->n_rootdir)
			{
				dp->sect = 0;
				return FR_NO_FILE;
			}
		}
		else
		{
			if ((ofs / SS(fs) & (fs->csize - 1)) == 0)
			{
				clst = get_fat(&dp->obj, dp->clust);
				if (clst <= 1)
					return FR_INT_ERR;
				if (clst == 0xFFFFFFFF)
					return FR_DISK_ERR;
				if (clst >= fs->n_fatent)
				{
					if (!stretch)
					{
						dp->sect = 0;
						return FR_NO_FILE;
					}
					clst = create_chain(&dp->obj, dp->clust);
					if (clst == 0)
						return FR_DENIED;
					if (clst == 1)
						return FR_INT_ERR;
					if (clst == 0xFFFFFFFF)
						return FR_DISK_ERR;
					if (dir_clear(fs, clst) != FR_OK)
						return FR_DISK_ERR;
					if (FF_FS_EXFAT)
						dp->obj.stat |= 4;
				}
				dp->clust = clst;
				dp->sect = clst2sect(fs, clst);
			}
		}
	}
	dp->dptr = ofs;
	dp->dir = fs->win + ofs % SS(fs);

	return FR_OK;
}

#if !FF_FS_READONLY

static FRESULT dir_alloc(
	DIR *dp,
	UINT nent)
{
	FRESULT res;
	UINT n;
	FATFS *fs = dp->obj.fs;

	res = dir_sdi(dp, 0);
	if (res == FR_OK)
	{
		n = 0;
		do
		{
			res = move_window(fs, dp->sect);
			if (res != FR_OK)
				break;
			if (dp->dir[DIR_Name] == DDEM || dp->dir[DIR_Name] == 0)
			{
				if (++n == nent)
					break;
			}
			else
			{
				n = 0;
			}
			res = dir_next(dp, 1);
		} while (res == FR_OK);
	}

	if (res == FR_NO_FILE)
		res = FR_DENIED;
	return res;
}

#endif

static DWORD ld_clust(
	FATFS *fs,
	const BYTE *dir)
{
	DWORD cl;

	cl = ld_word(dir + DIR_FstClusLO);
	if (fs->fs_type == FS_FAT32)
	{
		cl |= (DWORD)ld_word(dir + DIR_FstClusHI) << 16;
	}

	return cl;
}

#if !FF_FS_READONLY
static void st_clust(
	FATFS *fs,
	BYTE *dir,
	DWORD cl)
{
	st_word(dir + DIR_FstClusLO, (WORD)cl);
	if (fs->fs_type == FS_FAT32)
	{
		st_word(dir + DIR_FstClusHI, (WORD)(cl >> 16));
	}
}
#endif

#if FF_FS_MINIMIZE <= 1 || FF_FS_RPATH >= 2 || FF_USE_LABEL || FF_FS_EXFAT

#define DIR_READ_FILE(dp) dir_read(dp, 0)
#define DIR_READ_LABEL(dp) dir_read(dp, 1)

static FRESULT dir_read(
	DIR *dp,
	int vol)
{
	FRESULT res = FR_NO_FILE;
	FATFS *fs = dp->obj.fs;
	BYTE attr, b;

	while (dp->sect)
	{
		res = move_window(fs, dp->sect);
		if (res != FR_OK)
			break;
		b = dp->dir[DIR_Name];
		if (b == 0)
		{
			res = FR_NO_FILE;
			break;
		}
		{
			dp->obj.attr = attr = dp->dir[DIR_Attr] & AM_MASK;
			if (b != DDEM && b != '.' && attr != AM_LFN && (int)((attr & ~AM_ARC) == AM_VOL) == vol)
			{
				break;
			}
		}
		res = dir_next(dp, 0);
		if (res != FR_OK)
			break;
	}

	if (res != FR_OK)
		dp->sect = 0;
	return res;
}

#endif

static FRESULT dir_find(
	DIR *dp)
{
	FRESULT res;
	FATFS *fs = dp->obj.fs;
	BYTE c;

	res = dir_sdi(dp, 0);
	if (res != FR_OK)
		return res;
	do
	{
		res = move_window(fs, dp->sect);
		if (res != FR_OK)
			break;
		c = dp->dir[DIR_Name];
		if (c == 0)
		{
			res = FR_NO_FILE;
			break;
		}
		dp->obj.attr = dp->dir[DIR_Attr] & AM_MASK;
		if (!(dp->dir[DIR_Attr] & AM_VOL) && !mem_cmp(dp->dir, dp->fn, 11))
			break;
		res = dir_next(dp, 0);
	} while (res == FR_OK);

	return res;
}

#if !FF_FS_READONLY

static FRESULT dir_register(
	DIR *dp)
{
	FRESULT res;
	FATFS *fs = dp->obj.fs;

	res = dir_alloc(dp, 1);

	if (res == FR_OK)
	{
		res = move_window(fs, dp->sect);
		if (res == FR_OK)
		{
			mem_set(dp->dir, 0, SZDIRE);
			mem_cpy(dp->dir + DIR_Name, dp->fn, 11);
			fs->wflag = 1;
		}
	}

	return res;
}

#endif

#if !FF_FS_READONLY && FF_FS_MINIMIZE == 0

static FRESULT dir_remove(
	DIR *dp)
{
	FRESULT res;
	FATFS *fs = dp->obj.fs;

	res = move_window(fs, dp->sect);
	if (res == FR_OK)
	{
		dp->dir[DIR_Name] = DDEM;
		fs->wflag = 1;
	}

	return res;
}

#endif

#if FF_FS_MINIMIZE <= 1 || FF_FS_RPATH >= 2

static void get_fileinfo(
	DIR *dp,
	FILINFO *fno)
{
	UINT si, di;
	TCHAR c;

	fno->fname[0] = 0;
	if (dp->sect == 0)
		return;

	si = di = 0;
	while (si < 11)
	{
		c = (TCHAR)dp->dir[si++];
		if (c == ' ')
			continue;
		if (c == RDDEM)
			c = DDEM;
		if (si == 9)
			fno->fname[di++] = '.';
		fno->fname[di++] = c;
	}
	fno->fname[di] = 0;

	fno->fattrib = dp->dir[DIR_Attr];
	fno->fsize = ld_dword(dp->dir + DIR_FileSize);
	fno->ftime = ld_word(dp->dir + DIR_ModTime + 0);
	fno->fdate = ld_word(dp->dir + DIR_ModTime + 2);
}

#endif

static FRESULT create_name(
	DIR *dp,
	const TCHAR **path)
{
	BYTE c, d, *sfn;
	UINT ni, si, i;
	const char *p;

	p = *path;
	sfn = dp->fn;
	mem_set(sfn, ' ', 11);
	si = i = 0;
	ni = 8;
	for (;;)
	{
		c = (BYTE)p[si++];
		if (c <= ' ')
			break;
		if (c == '/' || c == '\\')
		{
			while (p[si] == '/' || p[si] == '\\')
				si++;
			break;
		}
		if (c == '.' || i >= ni)
		{
			if (ni == 11 || c != '.')
				return FR_INVALID_NAME;
			i = 8;
			ni = 11;
			continue;
		}
		if (dbc_1st(c))
		{
			d = (BYTE)p[si++];
			if (!dbc_2nd(d) || i >= ni - 1)
				return FR_INVALID_NAME;
			sfn[i++] = c;
			sfn[i++] = d;
		}
		else
		{
			if (chk_chr("\"*+,:;<=>\?[]|\x7F", c))
				return FR_INVALID_NAME;
			if (IsLower(c))
				c -= 0x20;
			sfn[i++] = c;
		}
	}
	*path = p + si;
	if (i == 0)
		return FR_INVALID_NAME;

	if (sfn[0] == DDEM)
		sfn[0] = RDDEM;
	sfn[NSFLAG] = (c <= ' ') ? NS_LAST : 0;

	return FR_OK;
}

static FRESULT follow_path(
	DIR *dp,
	const TCHAR *path)
{
	FRESULT res;
	BYTE ns;
	FATFS *fs = dp->obj.fs;
	{
		while (*path == '/' || *path == '\\')
			path++;
		dp->obj.sclust = 0;
	}

	if ((UINT)*path < ' ')
	{
		dp->fn[NSFLAG] = NS_NONAME;
		res = dir_sdi(dp, 0);
	}
	else
	{
		for (;;)
		{
			res = create_name(dp, &path);
			if (res != FR_OK)
				break;
			res = dir_find(dp);
			ns = dp->fn[NSFLAG];
			if (res != FR_OK)
			{
				if (res == FR_NO_FILE)
				{
					if (FF_FS_RPATH && (ns & NS_DOT))
					{
						if (!(ns & NS_LAST))
							continue;
						dp->fn[NSFLAG] = NS_NONAME;
						res = FR_OK;
					}
					else
					{
						if (!(ns & NS_LAST))
							res = FR_NO_PATH;
					}
				}
				break;
			}
			if (ns & NS_LAST)
				break;

			if (!(dp->obj.attr & AM_DIR))
			{
				res = FR_NO_PATH;
				break;
			}
			{
				dp->obj.sclust = ld_clust(fs, fs->win + dp->dptr % SS(fs));
			}
		}
	}

	return res;
}

static int get_ldnumber(
	const TCHAR **path)
{
	const TCHAR *tp, *tt;
	TCHAR tc;
	int i, vol = -1;

	tt = tp = *path;
	if (!tp)
		return vol;
	do
		tc = *tt++;
	while ((UINT)tc >= (FF_USE_LFN ? ' ' : '!') && tc != ':');

	if (tc == ':')
	{
		i = FF_VOLUMES;
		if (IsDigit(*tp) && tp + 2 == tt)
		{
			i = (int)*tp - '0';
		}
		if (i < FF_VOLUMES)
		{
			vol = i;
			*path = tt;
		}
		return vol;
	}
	vol = 0;
	return vol;
}

static UINT check_fs(
	FATFS *fs,
	LBA_t sect)
{
	fs->wflag = 0;
	fs->winsect = (LBA_t)0 - 1;
	if (move_window(fs, sect) != FR_OK)
		return 4;

	if (ld_word(fs->win + BS_55AA) != 0xAA55)
		return 3;

	if (FF_FS_EXFAT && !mem_cmp(fs->win + BS_JmpBoot, "\xEB\x76\x90"
													  "EXFAT   ",
								11))
		return 1;

	if (fs->win[BS_JmpBoot] == 0xE9 || fs->win[BS_JmpBoot] == 0xEB || fs->win[BS_JmpBoot] == 0xE8)
	{
		if (!mem_cmp(fs->win + BS_FilSysType, "FAT", 3))
			return 0;
		if (!mem_cmp(fs->win + BS_FilSysType32, "FAT32", 5))
			return 0;
	}
	return 2;
}

static UINT find_volume(
	FATFS *fs,
	UINT part)
{
	UINT fmt, i;
	DWORD mbr_pt[4];

	fmt = check_fs(fs, 0);
	if (fmt != 2 && (fmt >= 3 || part == 0))
		return fmt;

	if (FF_MULTI_PARTITION && part > 4)
		return 3;
	for (i = 0; i < 4; i++)
	{
		mbr_pt[i] = ld_dword(fs->win + MBR_Table + i * SZ_PTE + PTE_StLba);
	}
	i = part ? part - 1 : 0;
	do
	{
		fmt = mbr_pt[i] ? check_fs(fs, mbr_pt[i]) : 3;
	} while (part == 0 && fmt >= 2 && ++i < 4);
	return fmt;
}

static FRESULT mount_volume(
	const TCHAR **path,
	FATFS **rfs,
	BYTE mode)
{
	int vol;
	DSTATUS stat;
	LBA_t bsect;
	DWORD tsect, sysect, fasize, nclst, szbfat;
	WORD nrsv;
	FATFS *fs;
	UINT fmt;

	*rfs = 0;
	vol = get_ldnumber(path);
	if (vol < 0)
		return FR_INVALID_DRIVE;

	fs = FatFs[vol];
	if (!fs)
		return FR_NOT_ENABLED;
	*rfs = fs;

	mode &= (BYTE)~FA_READ;
	if (fs->fs_type != 0)
	{
		stat = disk_status(fs->pdrv);
		if (!(stat & STA_NOINIT))
		{
			if (!FF_FS_READONLY && mode && (stat & STA_PROTECT))
			{
				return FR_WRITE_PROTECTED;
			}
			return FR_OK;
		}
	}

	fs->fs_type = 0;
	fs->pdrv = LD2PD(vol);
	stat = disk_initialize(fs->pdrv);
	if (stat & STA_NOINIT)
	{
		return FR_NOT_READY;
	}
	if (!FF_FS_READONLY && mode && (stat & STA_PROTECT))
	{
		return FR_WRITE_PROTECTED;
	}

	fmt = find_volume(fs, LD2PT(vol));
	if (fmt == 4)
		return FR_DISK_ERR;
	if (fmt >= 2)
		return FR_NO_FILESYSTEM;
	bsect = fs->winsect;
	{
		if (ld_word(fs->win + BPB_BytsPerSec) != SS(fs))
			return FR_NO_FILESYSTEM;

		fasize = ld_word(fs->win + BPB_FATSz16);
		if (fasize == 0)
			fasize = ld_dword(fs->win + BPB_FATSz32);
		fs->fsize = fasize;

		fs->n_fats = fs->win[BPB_NumFATs];
		if (fs->n_fats != 1 && fs->n_fats != 2)
			return FR_NO_FILESYSTEM;
		fasize *= fs->n_fats;

		fs->csize = fs->win[BPB_SecPerClus];
		if (fs->csize == 0 || (fs->csize & (fs->csize - 1)))
			return FR_NO_FILESYSTEM;

		fs->n_rootdir = ld_word(fs->win + BPB_RootEntCnt);
		if (fs->n_rootdir % (SS(fs) / SZDIRE))
			return FR_NO_FILESYSTEM;

		tsect = ld_word(fs->win + BPB_TotSec16);
		if (tsect == 0)
			tsect = ld_dword(fs->win + BPB_TotSec32);

		nrsv = ld_word(fs->win + BPB_RsvdSecCnt);
		if (nrsv == 0)
			return FR_NO_FILESYSTEM;

		sysect = nrsv + fasize + fs->n_rootdir / (SS(fs) / SZDIRE);
		if (tsect < sysect)
			return FR_NO_FILESYSTEM;
		nclst = (tsect - sysect) / fs->csize;
		if (nclst == 0)
			return FR_NO_FILESYSTEM;
		fmt = 0;
		if (nclst <= MAX_FAT32)
			fmt = FS_FAT32;
		if (nclst <= MAX_FAT16)
			fmt = FS_FAT16;
		if (nclst <= MAX_FAT12)
			fmt = FS_FAT12;
		if (fmt == 0)
			return FR_NO_FILESYSTEM;

		fs->n_fatent = nclst + 2;
		fs->volbase = bsect;
		fs->fatbase = bsect + nrsv;
		fs->database = bsect + sysect;
		if (fmt == FS_FAT32)
		{
			if (ld_word(fs->win + BPB_FSVer32) != 0)
				return FR_NO_FILESYSTEM;
			if (fs->n_rootdir != 0)
				return FR_NO_FILESYSTEM;
			fs->dirbase = ld_dword(fs->win + BPB_RootClus32);
			szbfat = fs->n_fatent * 4;
		}
		else
		{
			if (fs->n_rootdir == 0)
				return FR_NO_FILESYSTEM;
			fs->dirbase = fs->fatbase + fasize;
			szbfat = (fmt == FS_FAT16) ? fs->n_fatent * 2 : fs->n_fatent * 3 / 2 + (fs->n_fatent & 1);
		}
		if (fs->fsize < (szbfat + (SS(fs) - 1)) / SS(fs))
			return FR_NO_FILESYSTEM;

#if !FF_FS_READONLY

		fs->last_clst = fs->free_clst = 0xFFFFFFFF;
		fs->fsi_flag = 0x80;
#if (FF_FS_NOFSINFO & 3) != 3
		if (fmt == FS_FAT32 && ld_word(fs->win + BPB_FSInfo32) == 1 && move_window(fs, bsect + 1) == FR_OK)
		{
			fs->fsi_flag = 0;
			if (ld_word(fs->win + BS_55AA) == 0xAA55 && ld_dword(fs->win + FSI_LeadSig) == 0x41615252 && ld_dword(fs->win + FSI_StrucSig) == 0x61417272)
			{
#if (FF_FS_NOFSINFO & 1) == 0
				fs->free_clst = ld_dword(fs->win + FSI_Free_Count);
#endif
#if (FF_FS_NOFSINFO & 2) == 0
				fs->last_clst = ld_dword(fs->win + FSI_Nxt_Free);
#endif
			}
		}
#endif
#endif
	}

	fs->fs_type = (BYTE)fmt;
	fs->id = ++Fsid;
	return FR_OK;
}

static FRESULT validate(
	FFOBJID *obj,
	FATFS **rfs)
{
	FRESULT res = FR_INVALID_OBJECT;

	if (obj && obj->fs && obj->fs->fs_type && obj->id == obj->fs->id)
	{
		if (!(disk_status(obj->fs->pdrv) & STA_NOINIT))
		{
			res = FR_OK;
		}
	}
	*rfs = (res == FR_OK) ? obj->fs : 0;
	return res;
}

FRESULT f_mount(
	FATFS *fs,
	const TCHAR *path,
	BYTE opt)
{
	FATFS *cfs;
	int vol;
	FRESULT res;
	const TCHAR *rp = path;

	vol = get_ldnumber(&rp);
	if (vol < 0)
		return FR_INVALID_DRIVE;
	cfs = FatFs[vol];

	if (cfs)
	{
		cfs->fs_type = 0;
	}

	if (fs)
	{
		fs->fs_type = 0;
	}
	FatFs[vol] = fs;

	if (opt == 0)
		return FR_OK;

	res = mount_volume(&path, &fs, 0);
	LEAVE_FF(fs, res);
}

FRESULT f_open(
	FIL *fp,
	const TCHAR *path,
	BYTE mode)
{
	FRESULT res;
	DIR dj;
	FATFS *fs;
#if !FF_FS_READONLY
	DWORD cl, bcs, clst;
	LBA_t sc;
	FSIZE_t ofs;
#endif
	DEF_NAMBUF

	if (!fp)
		return FR_INVALID_OBJECT;

	mode &= FF_FS_READONLY ? FA_READ : FA_READ | FA_WRITE | FA_CREATE_ALWAYS | FA_CREATE_NEW | FA_OPEN_ALWAYS | FA_OPEN_APPEND;
	res = mount_volume(&path, &fs, mode);
	if (res == FR_OK)
	{
		dj.obj.fs = fs;
		INIT_NAMBUF(fs);
		res = follow_path(&dj, path);
		if (res == FR_OK)
		{
			if (dj.fn[NSFLAG] & NS_NONAME)
			{
				res = FR_INVALID_NAME;
			}
		}

		if (mode & (FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW))
		{
			if (res != FR_OK)
			{
				if (res == FR_NO_FILE)
				{
					res = dir_register(&dj);
				}
				mode |= FA_CREATE_ALWAYS;
			}
			else
			{
				if (dj.obj.attr & (AM_RDO | AM_DIR))
				{
					res = FR_DENIED;
				}
				else
				{
					if (mode & FA_CREATE_NEW)
						res = FR_EXIST;
				}
			}
			if (res == FR_OK && (mode & FA_CREATE_ALWAYS))
			{
				{

					cl = ld_clust(fs, dj.dir);
					st_dword(dj.dir + DIR_CrtTime, GET_FATTIME());
					dj.dir[DIR_Attr] = AM_ARC;
					st_clust(fs, dj.dir, 0);
					st_dword(dj.dir + DIR_FileSize, 0);
					fs->wflag = 1;
					if (cl != 0)
					{
						sc = fs->winsect;
						res = remove_chain(&dj.obj, cl, 0);
						if (res == FR_OK)
						{
							res = move_window(fs, sc);
							fs->last_clst = cl - 1;
						}
					}
				}
			}
		}
		else
		{
			if (res == FR_OK)
			{
				if (dj.obj.attr & AM_DIR)
				{
					res = FR_NO_FILE;
				}
				else
				{
					if ((mode & FA_WRITE) && (dj.obj.attr & AM_RDO))
					{
						res = FR_DENIED;
					}
				}
			}
		}
		if (res == FR_OK)
		{
			if (mode & FA_CREATE_ALWAYS)
				mode |= FA_MODIFIED;
			fp->dir_sect = fs->winsect;
			fp->dir_ptr = dj.dir;
		}

		if (res == FR_OK)
		{
			{
				fp->obj.sclust = ld_clust(fs, dj.dir);
				fp->obj.objsize = ld_dword(dj.dir + DIR_FileSize);
			}
			fp->obj.fs = fs;
			fp->obj.id = fs->id;
			fp->flag = mode;
			fp->err = 0;
			fp->sect = 0;
			fp->fptr = 0;
#if !FF_FS_READONLY
			if ((mode & FA_SEEKEND) && fp->obj.objsize > 0)
			{
				fp->fptr = fp->obj.objsize;
				bcs = (DWORD)fs->csize * SS(fs);
				clst = fp->obj.sclust;
				for (ofs = fp->obj.objsize; res == FR_OK && ofs > bcs; ofs -= bcs)
				{
					clst = get_fat(&fp->obj, clst);
					if (clst <= 1)
						res = FR_INT_ERR;
					if (clst == 0xFFFFFFFF)
						res = FR_DISK_ERR;
				}
				fp->clust = clst;
				if (res == FR_OK && ofs % SS(fs))
				{
					sc = clst2sect(fs, clst);
					if (sc == 0)
					{
						res = FR_INT_ERR;
					}
					else
					{
						fp->sect = sc + (DWORD)(ofs / SS(fs));
					}
				}
			}
#endif
		}

		FREE_NAMBUF();
	}

	if (res != FR_OK)
		fp->obj.fs = 0;

	LEAVE_FF(fs, res);
}

FRESULT f_read(
	FIL *fp,
	void *buff,
	UINT btr,
	UINT *br)
{
	FRESULT res;
	FATFS *fs;
	DWORD clst;
	LBA_t sect;
	FSIZE_t remain;
	UINT rcnt, cc, csect;
	BYTE *rbuff = (BYTE *)buff;

	*br = 0;
	res = validate(&fp->obj, &fs);
	if (res != FR_OK || (res = (FRESULT)fp->err) != FR_OK)
		LEAVE_FF(fs, res);
	if (!(fp->flag & FA_READ))
		LEAVE_FF(fs, FR_DENIED);
	remain = fp->obj.objsize - fp->fptr;
	if (btr > remain)
		btr = (UINT)remain;

	for (; btr;
		 btr -= rcnt, *br += rcnt, rbuff += rcnt, fp->fptr += rcnt)
	{
		if (fp->fptr % SS(fs) == 0)
		{
			csect = (UINT)(fp->fptr / SS(fs) & (fs->csize - 1));
			if (csect == 0)
			{
				if (fp->fptr == 0)
				{
					clst = fp->obj.sclust;
				}
				else
				{
					{
						clst = get_fat(&fp->obj, fp->clust);
					}
				}
				if (clst < 2)
					ABORT(fs, FR_INT_ERR);
				if (clst == 0xFFFFFFFF)
					ABORT(fs, FR_DISK_ERR);
				fp->clust = clst;
			}
			sect = clst2sect(fs, fp->clust);
			if (sect == 0)
				ABORT(fs, FR_INT_ERR);
			sect += csect;
			cc = btr / SS(fs);
			if (cc > 0)
			{
				if (csect + cc > fs->csize)
				{
					cc = fs->csize - csect;
				}
				if (disk_read(fs->pdrv, rbuff, sect, cc) != RES_OK)
					ABORT(fs, FR_DISK_ERR);
#if !FF_FS_READONLY && FF_FS_MINIMIZE <= 2
				if (fs->wflag && fs->winsect - sect < cc)
				{
					mem_cpy(rbuff + ((fs->winsect - sect) * SS(fs)), fs->win, SS(fs));
				}
#endif
				rcnt = SS(fs) * cc;
				continue;
			}
			fp->sect = sect;
		}
		rcnt = SS(fs) - (UINT)fp->fptr % SS(fs);
		if (rcnt > btr)
			rcnt = btr;
		if (move_window(fs, fp->sect) != FR_OK)
			ABORT(fs, FR_DISK_ERR);
		mem_cpy(rbuff, fs->win + fp->fptr % SS(fs), rcnt);
	}

	LEAVE_FF(fs, FR_OK);
}

#if !FF_FS_READONLY

FRESULT f_write(
	FIL *fp,
	const void *buff,
	UINT btw,
	UINT *bw)
{
	FRESULT res;
	FATFS *fs;
	DWORD clst;
	LBA_t sect;
	UINT wcnt, cc, csect;
	const BYTE *wbuff = (const BYTE *)buff;

	*bw = 0;
	res = validate(&fp->obj, &fs);
	if (res != FR_OK || (res = (FRESULT)fp->err) != FR_OK)
		LEAVE_FF(fs, res);
	if (!(fp->flag & FA_WRITE))
		LEAVE_FF(fs, FR_DENIED);

	if ((!FF_FS_EXFAT || fs->fs_type != FS_EXFAT) && (DWORD)(fp->fptr + btw) < (DWORD)fp->fptr)
	{
		btw = (UINT)(0xFFFFFFFF - (DWORD)fp->fptr);
	}

	for (; btw;
		 btw -= wcnt, *bw += wcnt, wbuff += wcnt, fp->fptr += wcnt, fp->obj.objsize = (fp->fptr > fp->obj.objsize) ? fp->fptr : fp->obj.objsize)
	{
		if (fp->fptr % SS(fs) == 0)
		{
			csect = (UINT)(fp->fptr / SS(fs)) & (fs->csize - 1);
			if (csect == 0)
			{
				if (fp->fptr == 0)
				{
					clst = fp->obj.sclust;
					if (clst == 0)
					{
						clst = create_chain(&fp->obj, 0);
					}
				}
				else
				{
					{
						clst = create_chain(&fp->obj, fp->clust);
					}
				}
				if (clst == 0)
					break;
				if (clst == 1)
					ABORT(fs, FR_INT_ERR);
				if (clst == 0xFFFFFFFF)
					ABORT(fs, FR_DISK_ERR);
				fp->clust = clst;
				if (fp->obj.sclust == 0)
					fp->obj.sclust = clst;
			}
			if (fs->winsect == fp->sect && sync_window(fs) != FR_OK)
				ABORT(fs, FR_DISK_ERR);
			sect = clst2sect(fs, fp->clust);
			if (sect == 0)
				ABORT(fs, FR_INT_ERR);
			sect += csect;
			cc = btw / SS(fs);
			if (cc > 0)
			{
				if (csect + cc > fs->csize)
				{
					cc = fs->csize - csect;
				}
				if (disk_write(fs->pdrv, wbuff, sect, cc) != RES_OK)
					ABORT(fs, FR_DISK_ERR);
#if FF_FS_MINIMIZE <= 2
				if (fs->winsect - sect < cc)
				{
					mem_cpy(fs->win, wbuff + ((fs->winsect - sect) * SS(fs)), SS(fs));
					fs->wflag = 0;
				}
#endif
				wcnt = SS(fs) * cc;
				continue;
			}
			if (fp->fptr >= fp->obj.objsize)
			{
				if (sync_window(fs) != FR_OK)
					ABORT(fs, FR_DISK_ERR);
				fs->winsect = sect;
			}
			fp->sect = sect;
		}
		wcnt = SS(fs) - (UINT)fp->fptr % SS(fs);
		if (wcnt > btw)
			wcnt = btw;
		if (move_window(fs, fp->sect) != FR_OK)
			ABORT(fs, FR_DISK_ERR);
		mem_cpy(fs->win + fp->fptr % SS(fs), wbuff, wcnt);
		fs->wflag = 1;
	}

	fp->flag |= FA_MODIFIED;

	LEAVE_FF(fs, FR_OK);
}

FRESULT f_sync(
	FIL *fp)
{
	FRESULT res;
	FATFS *fs;
	DWORD tm;
	BYTE *dir;

	res = validate(&fp->obj, &fs);
	if (res == FR_OK)
	{
		if (fp->flag & FA_MODIFIED)
		{

			tm = GET_FATTIME();
			{
				res = move_window(fs, fp->dir_sect);
				if (res == FR_OK)
				{
					dir = fp->dir_ptr;
					dir[DIR_Attr] |= AM_ARC;
					st_clust(fp->obj.fs, dir, fp->obj.sclust);
					st_dword(dir + DIR_FileSize, (DWORD)fp->obj.objsize);
					st_dword(dir + DIR_ModTime, tm);
					st_word(dir + DIR_LstAccDate, 0);
					fs->wflag = 1;
					res = sync_fs(fs);
					fp->flag &= (BYTE)~FA_MODIFIED;
				}
			}
		}
	}

	LEAVE_FF(fs, res);
}

#endif

FRESULT f_close(
	FIL *fp)
{
	FRESULT res;
	FATFS *fs;

#if !FF_FS_READONLY
	res = f_sync(fp);
	if (res == FR_OK)
#endif
	{
		res = validate(&fp->obj, &fs);
		if (res == FR_OK)
		{
			fp->obj.fs = 0;
		}
	}
	return res;
}

FRESULT f_stat(
	const TCHAR *path,
	FILINFO *fno)
{
	FRESULT res;
	DIR dj;
	DEF_NAMBUF

	res = mount_volume(&path, &dj.obj.fs, 0);
	if (res == FR_OK)
	{
		INIT_NAMBUF(dj.obj.fs);
		res = follow_path(&dj, path);
		if (res == FR_OK)
		{
			if (dj.fn[NSFLAG] & NS_NONAME)
			{
				res = FR_INVALID_NAME;
			}
			else
			{
				if (fno)
					get_fileinfo(&dj, fno);
			}
		}
		FREE_NAMBUF();
	}

	LEAVE_FF(dj.obj.fs, res);
}

FRESULT f_unlink(
	const TCHAR *path)
{
	FRESULT res;
	DIR dj, sdj;
	DWORD dclst = 0;
	FATFS *fs;
	DEF_NAMBUF

	res = mount_volume(&path, &fs, FA_WRITE);
	if (res == FR_OK)
	{
		dj.obj.fs = fs;
		INIT_NAMBUF(fs);
		res = follow_path(&dj, path);
		if (FF_FS_RPATH && res == FR_OK && (dj.fn[NSFLAG] & NS_DOT))
		{
			res = FR_INVALID_NAME;
		}
		if (res == FR_OK)
		{
			if (dj.fn[NSFLAG] & NS_NONAME)
			{
				res = FR_INVALID_NAME;
			}
			else
			{
				if (dj.obj.attr & AM_RDO)
				{
					res = FR_DENIED;
				}
			}
			if (res == FR_OK)
			{
				{
					dclst = ld_clust(fs, dj.dir);
				}
				if (dj.obj.attr & AM_DIR)
				{
					{
						sdj.obj.fs = fs;
						sdj.obj.sclust = dclst;
						res = dir_sdi(&sdj, 0);
						if (res == FR_OK)
						{
							res = DIR_READ_FILE(&sdj);
							if (res == FR_OK)
								res = FR_DENIED;
							if (res == FR_NO_FILE)
								res = FR_OK;
						}
					}
				}
			}
			if (res == FR_OK)
			{
				res = dir_remove(&dj);
				if (res == FR_OK && dclst != 0)
				{
					res = remove_chain(&dj.obj, dclst, 0);
				}
				if (res == FR_OK)
					res = sync_fs(fs);
			}
		}
		FREE_NAMBUF();
	}

	LEAVE_FF(fs, res);
}