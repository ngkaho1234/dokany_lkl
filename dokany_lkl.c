/*
  Dokan : user-mode file system library for Windows

  Copyright (C) 2015 - 2016 Adrien J. <liryna.stark@gmail.com> and Maxime C.
  Copyright (C) 2007 - 2011 Hiroki Asakawa <info@dokan-dev.net>

  http://dokan-dev.github.io

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <dokan/dokan.h>
#include <dokan/fileinfo.h>
#include <lkl.h>
#include <lkl_host.h>
#include "utils.h"

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include <malloc.h>
#include <ntstatus.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <winbase.h>

BOOL g_UseStdErr;
BOOL g_DebugMode;

static void DbgPrint(LPCWSTR format, ...) {
  if (g_DebugMode) {
    const WCHAR *outputString;
    WCHAR *buffer = NULL;
    size_t length;
    va_list argp;

    va_start(argp, format);
    length = _vscwprintf(format, argp) + 1;
    buffer = _malloca(length * sizeof(WCHAR));
    if (buffer) {
      vswprintf_s(buffer, length, format, argp);
      outputString = buffer;
    } else {
      outputString = format;
    }
    if (g_UseStdErr)
      fputws(outputString, stderr);
    else
      OutputDebugStringW(outputString);
    if (buffer)
      _freea(buffer);
    va_end(argp);
  }
}

#define lkl_mount_point_final "/"

static char lkl_mount_point[MAX_PATH];
static WCHAR lkl_mount_fstype[MAX_PATH] = L"";
static WCHAR mount_point[MAX_PATH] = L"M:\\";
static lkl_mode_t default_mode = 0755;
static WCHAR disk_path[MAX_PATH];

NTSTATUS ToNtStatus(DWORD dwError) {
  switch (dwError) {
  case ERROR_FILE_NOT_FOUND:
    return STATUS_OBJECT_NAME_NOT_FOUND;
  case ERROR_PATH_NOT_FOUND:
    return STATUS_OBJECT_PATH_NOT_FOUND;
  case ERROR_INVALID_PARAMETER:
    return STATUS_INVALID_PARAMETER;
  case ERROR_ACCESS_DENIED:
    return STATUS_ACCESS_DENIED;
  case ERROR_SHARING_VIOLATION:
    return STATUS_SHARING_VIOLATION;
  case ERROR_INVALID_NAME:
    return STATUS_OBJECT_NAME_NOT_FOUND;
  case ERROR_FILE_EXISTS:
  case ERROR_ALREADY_EXISTS:
    return STATUS_OBJECT_NAME_COLLISION;
  case ERROR_PRIVILEGE_NOT_HELD:
    return STATUS_PRIVILEGE_NOT_HELD;
  case ERROR_NOT_READY:
    return STATUS_DEVICE_NOT_READY;
  default:
    DbgPrint(L"Create got unknown error code %d\n", dwError);
    return STATUS_ACCESS_DENIED;
  }
}

int ntstatus_to_lkl_errno(NTSTATUS Status)
{
  switch (Status) {
  case STATUS_ACCESS_DENIED:
    return (-LKL_EACCES);

  case STATUS_ACCESS_VIOLATION:
    return (-LKL_EFAULT);

  case STATUS_BUFFER_TOO_SMALL:
    return (-LKL_EINVAL);

  case STATUS_INVALID_PARAMETER:
    return (-LKL_EINVAL);

  case STATUS_NOT_IMPLEMENTED:
  case STATUS_NOT_SUPPORTED:
    return (-LKL_EOPNOTSUPP);

  case STATUS_INVALID_ADDRESS:
  case STATUS_INVALID_ADDRESS_COMPONENT:
    return (-LKL_EADDRNOTAVAIL);

  case STATUS_NO_SUCH_DEVICE:
  case STATUS_NO_SUCH_FILE:
  case STATUS_OBJECT_NAME_NOT_FOUND:
  case STATUS_OBJECT_PATH_NOT_FOUND:
  case STATUS_NETWORK_BUSY:
  case STATUS_INVALID_NETWORK_RESPONSE:
  case STATUS_UNEXPECTED_NETWORK_ERROR:
    return (-LKL_ENETDOWN);

  case STATUS_BAD_NETWORK_PATH:
  case STATUS_NETWORK_UNREACHABLE:
  case STATUS_PROTOCOL_UNREACHABLE:
    return (-LKL_ENETUNREACH);

  case STATUS_LOCAL_DISCONNECT:
  case STATUS_TRANSACTION_ABORTED:
  case STATUS_CONNECTION_ABORTED:
    return (-LKL_ECONNABORTED);

  case STATUS_REMOTE_DISCONNECT:
  case STATUS_LINK_FAILED:
  case STATUS_CONNECTION_DISCONNECTED:
  case STATUS_CONNECTION_RESET:
  case STATUS_PORT_UNREACHABLE:
    return (-LKL_ECONNRESET);

  case STATUS_INSUFFICIENT_RESOURCES:
    return (-LKL_ENOMEM);

  case STATUS_PAGEFILE_QUOTA:
  case STATUS_NO_MEMORY:
  case STATUS_CONFLICTING_ADDRESSES:
  case STATUS_QUOTA_EXCEEDED:
  case STATUS_TOO_MANY_PAGING_FILES:
  case STATUS_WORKING_SET_QUOTA:
  case STATUS_COMMITMENT_LIMIT:
  case STATUS_TOO_MANY_ADDRESSES:
  case STATUS_REMOTE_RESOURCES:
    return (-LKL_ENOBUFS);

  case STATUS_INVALID_CONNECTION:
    return (-LKL_ENOTCONN);

  case STATUS_PIPE_DISCONNECTED:
    return (-LKL_ESHUTDOWN);

  case STATUS_TIMEOUT:
  case STATUS_IO_TIMEOUT:
  case STATUS_LINK_TIMEOUT:
    return (-LKL_ETIMEDOUT);

  case STATUS_REMOTE_NOT_LISTENING:
  case STATUS_CONNECTION_REFUSED:
    return (-LKL_ECONNREFUSED);

  case STATUS_HOST_UNREACHABLE:
    return (-LKL_EHOSTUNREACH);

  case STATUS_PENDING:
  case STATUS_DEVICE_NOT_READY:
    return (-LKL_EAGAIN);

  case STATUS_CANCELLED:
  case STATUS_REQUEST_ABORTED:
    return (-LKL_EINTR);

  case STATUS_BUFFER_OVERFLOW:
  case STATUS_INVALID_BUFFER_SIZE:
    return (-LKL_EMSGSIZE);

  case STATUS_ADDRESS_ALREADY_EXISTS:
    return (-LKL_EADDRINUSE);
  }

  if (Status == STATUS_SUCCESS) // TODO: More than one.
    return 0;

  return (-LKL_EINVAL);
}

NTSTATUS lkl_errno_to_ntstatus(int rc)
{
  switch (rc) {

  case 0:
    return STATUS_SUCCESS;

  case -LKL_EPERM:
  case -LKL_EACCES:
    return STATUS_ACCESS_DENIED;

  case -LKL_ENOENT:
    return  STATUS_OBJECT_NAME_NOT_FOUND;

  case -LKL_EFAULT:
    return STATUS_ACCESS_VIOLATION;

  case -LKL_EBADMSG:
  case -LKL_EBADF:
  case -LKL_EINVAL:
  case -LKL_EFBIG:
    return STATUS_INVALID_PARAMETER;

  case -LKL_EBUSY:
    return STATUS_DEVICE_BUSY;

  case -LKL_ENOSYS:
    return STATUS_NOT_IMPLEMENTED;

  case -LKL_ENOSPC:
    return STATUS_DISK_FULL;

  case -LKL_EOPNOTSUPP:
    return STATUS_NOT_SUPPORTED;

  case -LKL_EDEADLK:
    return STATUS_POSSIBLE_DEADLOCK;

  case -LKL_EEXIST:
    return STATUS_OBJECT_NAME_COLLISION;

  case -LKL_EIO:
    return STATUS_UNEXPECTED_IO_ERROR;

  case -LKL_ENOTDIR:
    return STATUS_NOT_A_DIRECTORY;

  case -LKL_EISDIR:
    return STATUS_FILE_IS_A_DIRECTORY;

  case -LKL_ENOTEMPTY:
    return STATUS_DIRECTORY_NOT_EMPTY;

  case -LKL_ENODEV:
    return STATUS_NO_SUCH_DEVICE;

  case -LKL_ENXIO:
    return STATUS_INVALID_ADDRESS;

  case -LKL_EADDRNOTAVAIL:
    return STATUS_INVALID_ADDRESS;

  case -LKL_ENETDOWN:
    return STATUS_UNEXPECTED_NETWORK_ERROR;

  case -LKL_ENETUNREACH:
    return STATUS_NETWORK_UNREACHABLE;

  case -LKL_ECONNABORTED:
    return STATUS_CONNECTION_ABORTED;

  case -LKL_ECONNRESET:
    return STATUS_CONNECTION_RESET;

  case -LKL_ENOMEM:
    return STATUS_INSUFFICIENT_RESOURCES;

  case -LKL_ENOBUFS:
    return STATUS_NO_MEMORY;

  case -LKL_ENOTCONN:
    return STATUS_INVALID_CONNECTION;

  case -LKL_ESHUTDOWN:
    return STATUS_CONNECTION_DISCONNECTED;

  case -LKL_ETIMEDOUT:
    return STATUS_TIMEOUT;

  case -LKL_ECONNREFUSED:
    return STATUS_CONNECTION_REFUSED;

  case -LKL_EHOSTUNREACH:
    return STATUS_HOST_UNREACHABLE;

  case -LKL_EAGAIN:
    return STATUS_DEVICE_NOT_READY;

  case -LKL_EINTR:
    return  STATUS_CANCELLED;

  case -LKL_EMSGSIZE:
    return STATUS_INVALID_BUFFER_SIZE;

  case -LKL_EADDRINUSE:
    return STATUS_ADDRESS_ALREADY_EXISTS;
  }

  return STATUS_UNSUCCESSFUL;
}

#define LklCheckFlag(val, flag)                                             \
  if (val & flag) {                                                            \
  }

static int convert_flags(DWORD flags) {
  BOOL want_read = (flags & GENERIC_READ) != 0;
  BOOL want_write = (flags & GENERIC_WRITE) != 0;
  if (want_read && !want_write)
    return LKL_O_RDONLY;

  if (!want_read && want_write)
    return LKL_O_WRONLY;

  return LKL_O_RDWR;
}

static NTSTATUS DOKAN_CALLBACK
LklCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
              ACCESS_MASK DesiredAccess, ULONG FileAttributes,
              ULONG ShareAccess, ULONG CreateDisposition,
              ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo) {
  int lkl_ret;
  int flags = convert_flags(DesiredAccess) | LKL_O_LARGEFILE;
  NTSTATUS retval = STATUS_SUCCESS;
  char *unix_filename = win_path_to_unix(wchar_to_utf8_buf(FileName, NULL));
  if (!unix_filename) {
    retval = STATUS_INSUFFICIENT_RESOURCES;
    goto out;
  }

  DbgPrint(L"%s: FileName = %s\n", __func__, FileName);
  if (CreateDisposition == FILE_CREATE || CreateDisposition == FILE_OPEN_IF) {
    if ((CreateOptions & FILE_DIRECTORY_FILE) != FILE_DIRECTORY_FILE) {
      flags |= LKL_O_CREAT;
      DokanFileInfo->IsDirectory = FALSE;
    } else {
      retval = lkl_errno_to_ntstatus(lkl_ret =
        lkl_sys_mkdir(unix_filename, default_mode));
      if (retval != STATUS_SUCCESS && lkl_ret != -LKL_EEXIST)
        goto out;

      if (lkl_ret == -LKL_EEXIST && CreateDisposition != FILE_CREATE)
        goto out;

      DokanFileInfo->IsDirectory = TRUE;
      flags = LKL_O_DIRECTORY;
    }
  } else {
    struct lkl_stat lkl_stat;
    lkl_ret = lkl_sys_lstat(unix_filename, &lkl_stat);
    retval = lkl_errno_to_ntstatus(lkl_ret);
    if (retval != STATUS_SUCCESS)
      goto out;

    if (LKL_S_ISDIR(lkl_stat.st_mode)) {
      flags = LKL_O_DIRECTORY;
      DokanFileInfo->IsDirectory = TRUE;
    } else
      DokanFileInfo->IsDirectory = FALSE;

    if ((CreateOptions & FILE_NON_DIRECTORY_FILE) == FILE_NON_DIRECTORY_FILE) {
      if (LKL_S_ISDIR(lkl_stat.st_mode)) {
        retval = STATUS_FILE_IS_A_DIRECTORY;
        goto out;
      }
    }
  }

  lkl_ret = lkl_sys_open(unix_filename, flags, default_mode);
  if (lkl_ret < 0)
    retval = lkl_errno_to_ntstatus(lkl_ret);
  else {
    retval = STATUS_SUCCESS;
    DokanFileInfo->Context = lkl_ret;
  }

out:
  if (unix_filename)
    free_char_buf(unix_filename);

  return retval;
}

static void DOKAN_CALLBACK LklCloseFile(LPCWSTR FileName,
                                        PDOKAN_FILE_INFO DokanFileInfo) {
  DokanFileInfo->Context = 0;
}

static void DOKAN_CALLBACK LklCleanup(LPCWSTR FileName,
                                      PDOKAN_FILE_INFO DokanFileInfo) {
  int lkl_fd = (int)DokanFileInfo->Context;
  DokanFileInfo->Context = 0;
  lkl_sys_close(lkl_fd);

  if (DokanFileInfo->DeleteOnClose) {
    char *unix_filename = win_path_to_unix(wchar_to_utf8_buf(FileName, NULL));
    if (!unix_filename)
      return;

    if (!DokanFileInfo->IsDirectory)
      lkl_sys_unlink(unix_filename);
    else
      lkl_sys_rmdir(unix_filename);

    free_char_buf(unix_filename);
  }
}

static NTSTATUS DOKAN_CALLBACK LklReadFile(LPCWSTR FileName, LPVOID Buffer,
                                           DWORD BufferLength,
                                           LPDWORD ReadLength,
                                           LONGLONG Offset,
                                           PDOKAN_FILE_INFO DokanFileInfo) {
  NTSTATUS retval;
  int lkl_fd, lkl_ret;
  DWORD orig_BufferLength = BufferLength;
  if (DokanFileInfo->IsDirectory)
    return STATUS_INVALID_PARAMETER;

  if (ReadLength)
    *ReadLength = 0;

  lkl_fd = (int)DokanFileInfo->Context;

  do {
    lkl_ret = lkl_sys_pread64(lkl_fd, Buffer, BufferLength, Offset);
    if (lkl_ret <= 0)
      break;

    BufferLength -= lkl_ret;
    Offset += lkl_ret;
    Buffer += lkl_ret;
  } while (BufferLength > 0);

  if (lkl_ret < 0)
    retval = lkl_errno_to_ntstatus(lkl_ret);
  else
    retval = STATUS_SUCCESS;

  if (retval == STATUS_SUCCESS && ReadLength)
    *ReadLength = orig_BufferLength - BufferLength;

  return retval;
}

static NTSTATUS DOKAN_CALLBACK LklWriteFile(LPCWSTR FileName, LPCVOID Buffer,
                                            DWORD NumberOfBytesToWrite,
                                            LPDWORD NumberOfBytesWritten,
                                            LONGLONG Offset,
                                            PDOKAN_FILE_INFO DokanFileInfo) {
  NTSTATUS retval;
  int lkl_fd, lkl_ret;
  DWORD orig_NumberOfBytesToWrite = NumberOfBytesToWrite;
  if (DokanFileInfo->IsDirectory)
    return STATUS_INVALID_PARAMETER;

  if (NumberOfBytesWritten)
    *NumberOfBytesWritten = 0;

  lkl_fd = (int)DokanFileInfo->Context;

  do {
    lkl_ret = lkl_sys_pwrite64(lkl_fd, Buffer, NumberOfBytesToWrite, Offset);
    if (lkl_ret <= 0)
      break;

    NumberOfBytesToWrite -= lkl_ret;
    Offset += lkl_ret;
    Buffer += lkl_ret;
  } while (NumberOfBytesToWrite > 0);

  if (lkl_ret < 0)
    retval = lkl_errno_to_ntstatus(lkl_ret);
  else
    retval = STATUS_SUCCESS;

  if (retval == STATUS_SUCCESS && NumberOfBytesWritten)
    *NumberOfBytesWritten =
      orig_NumberOfBytesToWrite - NumberOfBytesToWrite;

  return retval;
}

static NTSTATUS DOKAN_CALLBACK
LklFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
  int lkl_fd;
  if (DokanFileInfo->IsDirectory)
    return STATUS_SUCCESS;

  lkl_fd = (int)DokanFileInfo->Context;
  return lkl_errno_to_ntstatus(lkl_sys_fsync(lkl_fd));
}

lkl_stat_to_def(file_info, BY_HANDLE_FILE_INFORMATION)

static NTSTATUS DOKAN_CALLBACK LklGetFileInformation(
    LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
    PDOKAN_FILE_INFO DokanFileInfo) {
  struct lkl_stat lkl_stat;
  NTSTATUS retval = STATUS_SUCCESS;
  char *unix_filename = win_path_to_unix(wchar_to_utf8_buf(FileName, NULL));
  if (!unix_filename) {
    retval = STATUS_INSUFFICIENT_RESOURCES;
    goto out;
  }

  ZeroMemory(&lkl_stat, sizeof(struct lkl_stat));
  retval = lkl_errno_to_ntstatus(lkl_sys_lstat(unix_filename, &lkl_stat));
  if (retval != STATUS_SUCCESS)
    goto out;

  HandleFileInformation->nNumberOfLinks = lkl_stat.st_nlink;
  if (LKL_S_ISDIR(lkl_stat.st_mode))
    DokanFileInfo->IsDirectory = TRUE;
  else
    DokanFileInfo->IsDirectory = FALSE;

  lkl_stat_to_file_info(&lkl_stat, unix_filename, HandleFileInformation);
out:
  if (unix_filename)
    free_char_buf(unix_filename);

  return retval;
}

lkl_stat_to_def(find_data, WIN32_FIND_DATAW)

static NTSTATUS DOKAN_CALLBACK
LklFindFiles(LPCWSTR FileName,
             PFillFindData FillFindData, // function pointer
             PDOKAN_FILE_INFO DokanFileInfo) {
  int lkl_ret, name_len;
  struct lkl_dir *dir;
  struct lkl_linux_dirent64 *de;
  NTSTATUS retval = STATUS_SUCCESS;
  char *unix_filename =
      win_path_to_unix(wchar_to_utf8_buf(FileName, &name_len));
  if (!unix_filename) {
    retval = STATUS_INSUFFICIENT_RESOURCES;
    goto out;
  }

  dir = lkl_opendir(unix_filename, &lkl_ret);
  if (!dir) {
    retval = lkl_errno_to_ntstatus(lkl_ret);
    goto out;
  }
  retval = STATUS_SUCCESS;

  while ((de = lkl_readdir(dir))) {
    int nr_char;
    wchar_t *w_filename;
    char *unix_fullpath;
    struct lkl_stat lkl_stat;
    WIN32_FIND_DATAW find_data;
    ZeroMemory(&find_data, sizeof(WIN32_FIND_DATAW));

    unix_fullpath =
        append_unix_path(unix_filename, de->d_name, -1, -1);
    if (!unix_fullpath) {
      retval = STATUS_INSUFFICIENT_RESOURCES;
      break;
    }

    lkl_ret = lkl_sys_lstat(unix_fullpath, &lkl_stat);
    retval = lkl_errno_to_ntstatus(lkl_ret);
    free_char_buf(unix_fullpath);
    unix_fullpath = NULL;
    if (retval != STATUS_SUCCESS)
      break;

    w_filename = utf8_to_wchar_buf(de->d_name, &nr_char);
    if (!w_filename) {
      retval = STATUS_INSUFFICIENT_RESOURCES;
      break;
    }

    lkl_stat_to_find_data(&lkl_stat, unix_filename, &find_data);
    CopyMemory(find_data.cFileName,
               w_filename, nr_char * sizeof(wchar_t));
    free_char_buf(w_filename);

    FillFindData(&find_data, DokanFileInfo);
  }

  lkl_closedir(dir);

out:
  if (unix_filename)
    free_char_buf(unix_filename);

  return retval;
}

static NTSTATUS DOKAN_CALLBACK
LklDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
  NTSTATUS retval = STATUS_SUCCESS;
  char *unix_filename = win_path_to_unix(wchar_to_utf8_buf(FileName, NULL));
  if (!unix_filename) {
    retval = STATUS_INSUFFICIENT_RESOURCES;
    goto out;
  }

  retval = lkl_errno_to_ntstatus(lkl_sys_unlink(unix_filename));
out:
  if (unix_filename)
    free_char_buf(unix_filename);

  return retval;
}

static NTSTATUS DOKAN_CALLBACK
LklDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
  NTSTATUS retval = STATUS_SUCCESS;
  char *unix_filename = win_path_to_unix(wchar_to_utf8_buf(FileName, NULL));
  if (!unix_filename) {
    retval = STATUS_INSUFFICIENT_RESOURCES;
    goto out;
  }

  retval = lkl_errno_to_ntstatus(lkl_sys_rmdir(unix_filename));
out:
  if (unix_filename)
    free_char_buf(unix_filename);

  return retval;
}

static NTSTATUS DOKAN_CALLBACK
LklMoveFile(LPCWSTR FileName, // existing file name
            LPCWSTR NewFileName, BOOL ReplaceIfExisting,
            PDOKAN_FILE_INFO DokanFileInfo) {
  NTSTATUS retval = STATUS_SUCCESS;
  char *unix_filename = win_path_to_unix(wchar_to_utf8_buf(FileName, NULL));
  char *unix_new_filename = win_path_to_unix(wchar_to_utf8_buf(NewFileName, NULL));
  if (!unix_filename || !unix_new_filename) {
    retval = STATUS_INSUFFICIENT_RESOURCES;
    goto out;
  }

  retval = lkl_errno_to_ntstatus(lkl_sys_rename(unix_filename, unix_new_filename));
out:
  if (unix_filename)
    free_char_buf(unix_filename);

  if (unix_new_filename)
    free_char_buf(unix_filename);

  return retval;
}

/* The physical file size is also referred to as the end of the file. */
static NTSTATUS DOKAN_CALLBACK LklSetEndOfFile(
    LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo) {
  int lkl_fd = (int)DokanFileInfo->Context, lkl_ret;
  if (DokanFileInfo->IsDirectory)
    return STATUS_INVALID_PARAMETER;

  lkl_ret = lkl_sys_ftruncate(lkl_fd, ByteOffset);
  return lkl_errno_to_ntstatus(lkl_ret);
}

static NTSTATUS DOKAN_CALLBACK LklSetAllocationSize(
    LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo) {
  int lkl_fd = (int)DokanFileInfo->Context, lkl_ret;
  if (DokanFileInfo->IsDirectory)
    return STATUS_INVALID_PARAMETER;

  lkl_ret = lkl_sys_fallocate(lkl_fd, 0, 0, AllocSize);
  return lkl_errno_to_ntstatus(lkl_ret);
}

static NTSTATUS DOKAN_CALLBACK LklSetFileAttributes(
    LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo) {
  NTSTATUS retval = STATUS_SUCCESS;
  char *unix_filename = win_path_to_unix(wchar_to_utf8_buf(FileName, NULL));
  if (!unix_filename) {
    retval = STATUS_INSUFFICIENT_RESOURCES;
    goto out;
  }

  /*
   * TODO: supports for other attributes.
   */
  if (FileAttributes == FILE_ATTRIBUTE_NORMAL) {
    retval = lkl_errno_to_ntstatus(
        lkl_sys_chmod(unix_filename, default_mode));
  } else {
    if ((FileAttributes & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY) {
      retval = lkl_errno_to_ntstatus(
          lkl_sys_chmod(unix_filename, LKL_S_IRUSR|LKL_S_IRGRP|LKL_S_IROTH));
    }
    if ((FileAttributes & FILE_ATTRIBUTE_TEMPORARY) == FILE_ATTRIBUTE_TEMPORARY)
      retval = STATUS_NOT_IMPLEMENTED;

  }
out:
  if (unix_filename)
    free_char_buf(unix_filename);

  return retval;
}

#define LKL_UTIME_OMIT ((1l << 30) - 2l)

static NTSTATUS DOKAN_CALLBACK
LklSetFileTime(LPCWSTR FileName, CONST FILETIME *CreationTime,
               CONST FILETIME *LastAccessTime, CONST FILETIME *LastWriteTime,
               PDOKAN_FILE_INFO DokanFileInfo) {
  struct lkl_timespec ts[2];
  NTSTATUS retval = STATUS_SUCCESS;
  char *unix_filename = win_path_to_unix(wchar_to_utf8_buf(FileName, NULL));
  if (!unix_filename) {
    retval = STATUS_INSUFFICIENT_RESOURCES;
    goto out;
  }

  ts[0].tv_sec = 0;
  ts[0].tv_nsec = LKL_UTIME_OMIT;
  ts[1].tv_sec = 0;
  ts[1].tv_nsec = LKL_UTIME_OMIT;

  if (LastAccessTime) {
    ts[0].tv_sec = filetime_to_unixtime(LastAccessTime);
    ts[0].tv_nsec = 0;
  }

  if (LastWriteTime) {
    ts[1].tv_sec = filetime_to_unixtime(LastWriteTime);
    ts[1].tv_nsec = 0;
  }

  retval = lkl_errno_to_ntstatus(
        lkl_sys_utimensat(-1, unix_filename, ts, LKL_AT_SYMLINK_NOFOLLOW));
out:
  if (unix_filename)
    free_char_buf(unix_filename);

  return retval;
}

static NTSTATUS DOKAN_CALLBACK
LklGetDiskFreeSpace(PULONGLONG FreeBytesAvailable,
                    PULONGLONG TotalNumberOfBytes,
                    PULONGLONG TotalNumberOfFreeBytes,
                    PDOKAN_FILE_INFO DokanFileInfo)
{
  int lkl_ret;
  NTSTATUS retval;
  LONG block_size;
  struct lkl_statfs lkl_statfs;
  lkl_ret = lkl_sys_statfs(lkl_mount_point_final, &lkl_statfs);
  retval = lkl_errno_to_ntstatus(lkl_ret);
  if (retval != STATUS_SUCCESS)
    return retval;

  block_size = lkl_statfs.f_bsize;

  if (FreeBytesAvailable)
    *FreeBytesAvailable = lkl_statfs.f_bavail * block_size;

  if (TotalNumberOfBytes)
    *TotalNumberOfBytes = lkl_statfs.f_blocks * block_size;

  if (TotalNumberOfFreeBytes)
    *TotalNumberOfFreeBytes = lkl_statfs.f_bfree * block_size;

  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK LklGetVolumeInformation(
    LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
    LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
    LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
    PDOKAN_FILE_INFO DokanFileInfo) {
  UNREFERENCED_PARAMETER(DokanFileInfo);

  wcscpy_s(VolumeNameBuffer, VolumeNameSize, L"Testing");
  *VolumeSerialNumber = 0x19831116;
  *MaximumComponentLength = 256;
  *FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES |
                     FILE_SUPPORTS_REMOTE_STORAGE | FILE_UNICODE_ON_DISK |
                     FILE_PERSISTENT_ACLS;

  // File system name could be anything up to 10 characters.
  // But Windows check few feature availability based on file system name.
  wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, lkl_mount_fstype);

  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK LklMounted(PDOKAN_FILE_INFO DokanFileInfo) {
  UNREFERENCED_PARAMETER(DokanFileInfo);

  DbgPrint(L"Mounted\n");
  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK LklUnmounted(PDOKAN_FILE_INFO DokanFileInfo) {
  UNREFERENCED_PARAMETER(DokanFileInfo);

  DbgPrint(L"Unmounted\n");
  return STATUS_SUCCESS;
}

static HANDLE disk_handle = INVALID_HANDLE_VALUE;
static int disk_id;
static union lkl_disk lkl_disk;

static int start_lkl(void)
{
  long ret;
  char *fstype = wchar_to_utf8_buf(lkl_mount_fstype, NULL);

  ret = lkl_start_kernel(&lkl_host_ops, 64 * 1024 * 1024, "");
  if (ret) {
    fprintf(stderr, "can't start kernel: %s\n", lkl_strerror(ret));
    goto out;
  }

  ret = lkl_mount_dev(disk_id, fstype, 0, NULL,
    lkl_mount_point, sizeof(lkl_mount_point));

  if (ret) {
    fprintf(stderr, "can't mount disk: %s\n", lkl_strerror(ret));
    goto out_halt;
  }

  ret = lkl_sys_chroot(lkl_mount_point);
  if (ret) {
    fprintf(stderr, "can't chdir to %s: %s\n", lkl_mount_point,
      lkl_strerror(ret));
    goto out_umount;
  }

  ret = 0;
  goto out;

out_umount:
  lkl_umount_dev(disk_id, 0, 1000);

out_halt:
  lkl_sys_halt();

out:
  if (fstype)
    free_char_buf(fstype);

  return ret;
}

static void stop_lkl(void)
{
  int ret;

  ret = lkl_sys_chdir("/");
  if (ret)
    fprintf(stderr, "can't chdir to /: %s\n", lkl_strerror(ret));
  ret = lkl_sys_umount("/", 0);
  if (ret)
    fprintf(stderr, "failed to umount disk: %d: %s\n",
      disk_id, lkl_strerror(ret));

  lkl_sys_halt();
}

int __cdecl wmain(ULONG argc, PWCHAR argv[]) {
  int status;
  int ret;
  ULONG command;
  PDOKAN_OPERATIONS dokanOperations =
      (PDOKAN_OPERATIONS)malloc(sizeof(DOKAN_OPERATIONS));
  if (dokanOperations == NULL) {
    return EXIT_FAILURE;
  }
  PDOKAN_OPTIONS dokanOptions = (PDOKAN_OPTIONS)malloc(sizeof(DOKAN_OPTIONS));
  if (dokanOptions == NULL) {
    free(dokanOperations);
    return EXIT_FAILURE;
  }

  if (argc < 3) {
    fprintf(stderr, "mirror.exe\n"
                    "  /r File/Device (ex. /r c:\\test)\n"
                    "  /f Filesystem (ex. btrfs)\n"
                    "  /l DriveLetter (ex. /l m)\n"
                    "  /t ThreadCount (ex. /t 5)\n"
                    "  /d (enable debug output)\n"
                    "  /s (use stderr for output)\n"
                    "  /n (use network drive)\n"
                    "  /m (use removable drive)\n"
                    "  /w (write-protect drive)\n"
                    "  /o (use mount manager)\n"
                    "  /c (mount for current session only)\n"
                    "  /i (Timeout in Milliseconds ex. /i 30000)\n");
    free(dokanOperations);
    free(dokanOptions);
    return EXIT_FAILURE;
  }

  g_DebugMode = FALSE;
  g_UseStdErr = FALSE;

  ZeroMemory(dokanOptions, sizeof(DOKAN_OPTIONS));
  dokanOptions->Version = DOKAN_VERSION;
  dokanOptions->ThreadCount = 0; // use default

  for (command = 1; command < argc; command++) {
    switch (towlower(argv[command][1])) {
    case L'r':
      command++;
      wcscpy_s(disk_path, sizeof(disk_path) / sizeof(WCHAR), argv[command]);
      break;
    case L'f':
      command++;
      wcscpy_s(lkl_mount_fstype, sizeof(lkl_mount_fstype) / sizeof(WCHAR), argv[command]);
      break;
    case L'l':
      command++;
      wcscpy_s(mount_point, sizeof(mount_point) / sizeof(WCHAR), argv[command]);
      dokanOptions->MountPoint = mount_point;
      break;
    case L't':
      command++;
      dokanOptions->ThreadCount = (USHORT)_wtoi(argv[command]);
      break;
    case L'd':
      g_DebugMode = TRUE;
      break;
    case L's':
      g_UseStdErr = TRUE;
      break;
    case L'n':
      dokanOptions->Options |= DOKAN_OPTION_NETWORK;
      break;
    case L'm':
      dokanOptions->Options |= DOKAN_OPTION_REMOVABLE;
      break;
    case L'w':
      dokanOptions->Options |= DOKAN_OPTION_WRITE_PROTECT;
      break;
    case L'o':
      dokanOptions->Options |= DOKAN_OPTION_MOUNT_MANAGER;
      break;
    case L'c':
      dokanOptions->Options |= DOKAN_OPTION_CURRENT_SESSION;
      break;
    case L'i':
      command++;
      dokanOptions->Timeout = (ULONG)_wtol(argv[command]);
      break;
    default:
      fwprintf(stderr, L"unknown command: %s\n", argv[command]);
      free(dokanOperations);
      free(dokanOptions);
      return EXIT_FAILURE;
    }
  }


  disk_handle = CreateFile(disk_path, GENERIC_READ|GENERIC_WRITE,
                   FILE_SHARE_READ, NULL, OPEN_EXISTING,
                   FILE_ATTRIBUTE_NORMAL, NULL);
  if (disk_handle == INVALID_HANDLE_VALUE) {
    fwprintf(stderr, L"Can't add disk: %s\n", L"Failed to open the file/device.");
    free(dokanOperations);
    free(dokanOptions);
    return -1;
  }
  lkl_disk.fd = disk_handle;
  disk_id = lkl_disk_add(lkl_disk);
  if (disk_id < 0) {
    fprintf(stderr, "Can't add disk: %s\n", lkl_strerror(ret));
    CloseHandle(disk_handle);
    free(dokanOperations);
    free(dokanOptions);
    return -1;
  }

  if (dokanOptions->Options & DOKAN_OPTION_NETWORK &&
      dokanOptions->Options & DOKAN_OPTION_MOUNT_MANAGER) {
    fwprintf(stderr, L"Mount manager cannot be used on network drive.\n");
    free(dokanOperations);
    free(dokanOptions);
    return -1;
  }

  if (!(dokanOptions->Options & DOKAN_OPTION_MOUNT_MANAGER) &&
      wcscmp(mount_point, L"") == 0) {
    fwprintf(stderr, L"Mount Point required.\n");
    free(dokanOperations);
    free(dokanOptions);
    return -1;
  }

  if ((dokanOptions->Options & DOKAN_OPTION_MOUNT_MANAGER) &&
      (dokanOptions->Options & DOKAN_OPTION_CURRENT_SESSION)) {
    fwprintf(stderr,
             L"Mount Manager always mount the drive for all user sessions.\n");
    free(dokanOperations);
    free(dokanOptions);
    return -1;
  }

  if (g_DebugMode) {
    dokanOptions->Options |= DOKAN_OPTION_DEBUG;
  }
  if (g_UseStdErr) {
    dokanOptions->Options |= DOKAN_OPTION_STDERR;
  }

  dokanOptions->Options |= DOKAN_OPTION_ALT_STREAM;

  ZeroMemory(dokanOperations, sizeof(DOKAN_OPERATIONS));
  dokanOperations->ZwCreateFile = LklCreateFile;
  dokanOperations->Cleanup = LklCleanup;
  dokanOperations->CloseFile = LklCloseFile;
  dokanOperations->ReadFile = LklReadFile;
  dokanOperations->WriteFile = LklWriteFile;
  dokanOperations->FlushFileBuffers = LklFlushFileBuffers;
  dokanOperations->GetFileInformation = LklGetFileInformation;
  dokanOperations->FindFiles = LklFindFiles;
  dokanOperations->FindFilesWithPattern = NULL;
  dokanOperations->SetFileAttributes = LklSetFileAttributes;
  dokanOperations->SetFileTime = LklSetFileTime;
  dokanOperations->DeleteFile = LklDeleteFile;
  dokanOperations->DeleteDirectory = LklDeleteDirectory;
  dokanOperations->MoveFile = LklMoveFile;
  dokanOperations->SetEndOfFile = LklSetEndOfFile;
  dokanOperations->SetAllocationSize = LklSetAllocationSize;
  dokanOperations->LockFile = NULL;
  dokanOperations->UnlockFile = NULL;
  dokanOperations->GetFileSecurity = NULL;
  dokanOperations->SetFileSecurity = NULL;
  dokanOperations->GetDiskFreeSpace = LklGetDiskFreeSpace;
  dokanOperations->GetVolumeInformation = LklGetVolumeInformation;
  dokanOperations->Unmounted = LklUnmounted;
  dokanOperations->FindStreams = NULL;
  dokanOperations->Mounted = LklMounted;

  start_lkl();

  status = DokanMain(dokanOptions, dokanOperations);
  switch (status) {
  case DOKAN_SUCCESS:
    fprintf(stderr, "Success\n");
    break;
  case DOKAN_ERROR:
    fprintf(stderr, "Error\n");
    break;
  case DOKAN_DRIVE_LETTER_ERROR:
    fprintf(stderr, "Bad Drive letter\n");
    break;
  case DOKAN_DRIVER_INSTALL_ERROR:
    fprintf(stderr, "Can't install driver\n");
    break;
  case DOKAN_START_ERROR:
    fprintf(stderr, "Driver something wrong\n");
    break;
  case DOKAN_MOUNT_ERROR:
    fprintf(stderr, "Can't assign a drive letter\n");
    break;
  case DOKAN_MOUNT_POINT_ERROR:
    fprintf(stderr, "Mount point error\n");
    break;
  case DOKAN_VERSION_ERROR:
    fprintf(stderr, "Version error\n");
    break;
  default:
    fprintf(stderr, "Unknown error: %d\n", status);
    break;
  }

  stop_lkl();

out:
  if (disk_handle != INVALID_HANDLE_VALUE)
    CloseHandle(disk_handle);

  free(dokanOptions);
  free(dokanOperations);
  return EXIT_SUCCESS;
}
