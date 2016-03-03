#ifndef _UTILS_H
#define _UTILS_H

#include <time.h>
#include <Windows.h>

wchar_t *utf8_to_wchar_buf(const char *src, int *nr_char);
char *wchar_to_utf8_buf(const wchar_t *src, int *size);
void free_char_buf(void *buf);
void *win_path_to_unix(char *path);
void *unix_path_to_win(char *path);
char *append_unix_path(const char *path, const char *name, int path_len,
                       int name_len);

FILETIME unix_time_to_filetime(time_t t);
time_t filetime_to_unixtime(const FILETIME *ft);

#define lkl_stat_to_def(func_name, s) \
static void \
lkl_stat_to_##func_name(const struct lkl_stat *lkl_stat, const char *name, \
		  s *find_data) \
{ \
	if (lkl_stat == NULL) \
		return; \
 \
	if (LKL_S_ISDIR(lkl_stat->st_mode)) \
		find_data->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY; \
	else \
		find_data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL; \
 \
	find_data->nFileSizeLow = (DWORD) lkl_stat->st_size; \
	find_data->nFileSizeHigh = lkl_stat->st_size>>32; \
 \
	if (lkl_stat->lkl_st_ctime!=0) \
		find_data->ftCreationTime=unix_time_to_filetime(lkl_stat->lkl_st_ctime); \
	if (lkl_stat->lkl_st_atime!=0) \
		find_data->ftLastAccessTime=unix_time_to_filetime(lkl_stat->lkl_st_atime); \
	if (lkl_stat->lkl_st_mtime!=0) \
		find_data->ftLastWriteTime=unix_time_to_filetime(lkl_stat->lkl_st_mtime); \
 \
	if (!(lkl_stat->st_mode & 0222)) /* TODO: File owner checking. */ \
		find_data->dwFileAttributes|=FILE_ATTRIBUTE_READONLY; \
 \
	/* TODO: Hidden file checking. */ \
}

#endif /* _UTILS_H */
