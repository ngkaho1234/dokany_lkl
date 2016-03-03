#include <time.h>
#include <Windows.h>

wchar_t *utf8_to_wchar_buf(const char *src, int *nr_char)
{
  int len;
  wchar_t *ret;
  if (src == NULL)
    return NULL;

  if (nr_char)
    *nr_char = 0;

  len = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
  if (!len)
    return NULL;

  ret = malloc((len + 1) * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, src, -1, ret,
                      len + 1);
  if (nr_char)
    *nr_char = len;

  return ret;
}

char *wchar_to_utf8_buf(const wchar_t *src, int *size)
{
  int len;
  char *ret;
  if (src == NULL)
    return NULL;

  if (size)
    *size = 0;

  len = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0,
                            NULL, NULL);
  if (!len)
    return NULL;

  ret = malloc(len + 1);
  WideCharToMultiByte(CP_UTF8, 0, src, -1, ret, len + 1,
                      NULL, NULL);
  if (size)
    *size = len;

  return ret;
}

/*
 * If path_len or name_len is equal to -1, we will determine the
 * length of the string ourselves.
 * */
char *append_unix_path(const char *path, const char *name, int path_len,
                       int name_len)
{
  char *ret;
  int alloc_size;

  /* Empty path or name is not allowed. */
  if (!path_len || !name_len)
    return NULL;

  if (path_len < 0)
    path_len = strlen(path);

  if (name_len < 0)
    name_len = strlen(name);

  /* We will append the slash later. */
  if (path[path_len - 1] != '/')
    ++path_len;

  alloc_size = path_len + name_len + 1;
  ret = malloc(alloc_size);
  if (!ret)
    return NULL;

  memset(ret, 0, alloc_size);
  memcpy(ret, path, path_len);
  ret[path_len - 1] = '/';
  memcpy(ret + path_len, name, name_len);
  return ret;
}

void free_char_buf(void *buf)
{
  free(buf);
}

void *win_path_to_unix(char *path)
{
  // Replace slashes
  int i, len = strlen(path);
  for (i = 0;i < len;i++) {
    char ch = path[i];
    if (ch == '\\')
      path[i] = '/';
  }

  return path;
}

void *unix_path_to_win(char *path)
{
  // Replace slashes
  int i, len = strlen(path);
  for (i = 0;i < len;i++) {
    char ch = path[i];
    if (ch == '/')
      path[i] = '\\';
  }

  return path;
}

FILETIME unix_time_to_filetime(time_t t)
{
  // Note that LONGLONG is a 64-bit value
  LONGLONG ll;

  ll = Int32x32To64(t, 10000000) + 116444736000000000LL;
  FILETIME res;
  res.dwLowDateTime = (DWORD)ll;
  res.dwHighDateTime = (DWORD)(ll >> 32);
  return res;
}

BOOL is_filetime_set(const FILETIME *ft)
{
  if (ft == 0 || (ft->dwHighDateTime == 0 && ft->dwLowDateTime == 0))
    return FALSE;

  return TRUE;
}

time_t filetime_to_unixtime(const FILETIME *ft)
{
  if (!is_filetime_set(ft))
    return 0;

  ULONGLONG ll = ((ULONGLONG)(ft->dwHighDateTime) << 32) + ft->dwLowDateTime;
  return (time_t)((ll - 116444736000000000LL) / 10000000LL);
}
