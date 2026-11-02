/* libtme/win32.c - native Windows (mingw-w64): what POSIX has and
   Windows lacks: */

/*
 * Copyright (c) 2026 Rico Pajarola
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Matt Fredette.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <tme/common.h>
#include <errno.h>
#include <string.h>
#ifdef WIN32
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <ole2.h>
#endif /* WIN32 */

#ifdef WIN32

/* realpath(path, NULL), which fails for a file that doesn't exist: */
char *
tme_win32_realpath(const char *path)
{
  char *full;

  full = _fullpath(NULL, path, 0);
  if (full != NULL && _access(full, 0) != 0) {
    free(full);
    full = NULL;
  }
  return (full);
}

/* pread(2) and pwrite(2), without their atomicity: */
ssize_t
tme_win32_pread(int fd, void *buf, size_t count, off_t offset)
{
  if (_lseeki64(fd, offset, SEEK_SET) < 0) {
    return (-1);
  }
  return (_read(fd, buf, count));
}

ssize_t
tme_win32_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
  if (_lseeki64(fd, offset, SEEK_SET) < 0) {
    return (-1);
  }
  return (_write(fd, buf, count));
}

/* fmemopen(3), only for reading: the buffer is copied into a
   temporary file, which is deleted when it is closed ("D"), and
   kept in memory if it fits ("T"): */
FILE *
tme_win32_fmemopen(void *buf, size_t size, const char *mode)
{
  char dir[MAX_PATH], name[MAX_PATH];
  FILE *file;

  (void) mode;
  if (GetTempPathA(sizeof(dir), dir) == 0
      || GetTempFileNameA(dir, "tme", 0, name) == 0) {
    return (NULL);
  }
  file = fopen(name, "w+bTD");
  if (file == NULL) {
    DeleteFileA(name);
    return (NULL);
  }
  if (fwrite(buf, 1, size, file) != size) {
    fclose(file);
    return (NULL);
  }
  rewind(file);
  return (file);
}

/* this appends an argument to a command line, quoted so that the C
   library of the program started splits it out again: */
static void
_tme_win32_quote(char *cmd, size_t size, const char *arg)
{
  size_t len, backslashes;

  len = strlen(cmd);
  if (len > 0 && len + 1 < size) {
    cmd[len++] = ' ';
  }
  if (len + 1 < size) {
    cmd[len++] = '"';
  }
  for (backslashes = 0; *arg != '\0' && len + 4 < size; arg++) {
    if (*arg == '\\') {
      backslashes++;
    }
    else {
      /* backslashes before a quote are doubled, and the quote is
	 escaped: */
      if (*arg == '"') {
	for (; backslashes > 0 && len + 4 < size; backslashes--) {
	  cmd[len++] = '\\';
	}
	cmd[len++] = '\\';
      }
      backslashes = 0;
    }
    cmd[len++] = *arg;
  }

  /* backslashes before the closing quote are doubled: */
  for (; backslashes > 0 && len + 2 < size; backslashes--) {
    cmd[len++] = '\\';
  }
  if (len + 1 < size) {
    cmd[len++] = '"';
  }
  cmd[len] = '\0';
}

/* this starts a program with the arguments given (argv[0] first,
   NULL-terminated), without waiting for it.  with detached, it gets
   no console.  this returns zero, or an errno: */
int
tme_win32_spawn(const char *program, const char *const *argv, int detached)
{
  char cmd[32768];
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;

  cmd[0] = '\0';
  for (; *argv != NULL; argv++) {
    _tme_win32_quote(cmd, sizeof(cmd), *argv);
  }
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  if (!CreateProcessA(program, cmd, NULL, NULL, FALSE,
		      (detached ? DETACHED_PROCESS : 0),
		      NULL, NULL, &si, &pi)) {
    return (GetLastError() == ERROR_FILE_NOT_FOUND ? ENOENT : EINVAL);
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return (0);
}

/* a file chooser.  the dialogs are modal, so each runs in a thread of
   its own, which writes the paths chosen to a pipe, one per line, as
   the choosers on other systems print them, and then closes it: */
struct _tme_win32_chooser {
  char title[256];
  char start[MAX_PATH];
  char save_name[MAX_PATH];
  char paths[65536];
  int folder;
  int multiple;
  int save;
  int fd;
};

/* this starts the folder chooser in the start folder: */
static int CALLBACK
_tme_win32_folder_callback(HWND hwnd, UINT msg, LPARAM lparam, LPARAM data)
{
  (void) lparam;
  if (msg == BFFM_INITIALIZED && ((const char *) data)[0] != '\0') {
    SendMessageA(hwnd, BFFM_SETSELECTIONA, TRUE, data);
  }
  return (0);
}

/* this writes a path to the chooser's pipe, with forward slashes: */
static void
_tme_win32_chooser_put(struct _tme_win32_chooser *chooser, const char *dir, const char *file)
{
  char path[2 * MAX_PATH + 2], *p;

  snprintf(path, sizeof(path), "%s%s%s\n",
	   dir,
	   (file != NULL ? "\\" : ""),
	   (file != NULL ? file : ""));
  for (p = path; *p != '\0'; p++) {
    if (*p == '\\') {
      *p = '/';
    }
  }
  _write(chooser->fd, path, strlen(path));
}

static DWORD WINAPI
_tme_win32_chooser_main(LPVOID _chooser)
{
  struct _tme_win32_chooser *chooser;
  char *paths, *file;
  OPENFILENAMEA ofn;
  BROWSEINFOA bi;
  LPITEMIDLIST pidl;
  BOOL ok;

  chooser = (struct _tme_win32_chooser *) _chooser;
  paths = chooser->paths;
  if (chooser->folder) {

    /* (the new dialog, with Make New Folder, needs OLE): */
    OleInitialize(NULL);
    memset(&bi, 0, sizeof(bi));
    bi.lpszTitle = chooser->title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = _tme_win32_folder_callback;
    bi.lParam = (LPARAM) chooser->start;
    pidl = SHBrowseForFolderA(&bi);
    if (pidl != NULL) {
      if (SHGetPathFromIDListA(pidl, paths)) {
	_tme_win32_chooser_put(chooser, paths, NULL);
      }
      CoTaskMemFree(pidl);
    }
    OleUninitialize();
  }
  else {
    snprintf(paths, sizeof(chooser->paths), "%s", chooser->save_name);
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "All files\0*.*\0";
    ofn.lpstrFile = paths;
    ofn.nMaxFile = sizeof(chooser->paths);
    ofn.lpstrInitialDir = (chooser->start[0] != '\0' ? chooser->start : NULL);
    ofn.lpstrTitle = chooser->title;

    /* (without OFN_NOCHANGEDIR, our current directory would
       change): */
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (chooser->save) {
      ofn.Flags |= OFN_OVERWRITEPROMPT;
      ok = GetSaveFileNameA(&ofn);
    }
    else {
      ofn.Flags |= OFN_FILEMUSTEXIST;
      if (chooser->multiple) {
	ofn.Flags |= OFN_ALLOWMULTISELECT;
      }
      ok = GetOpenFileNameA(&ofn);
    }
    if (ok) {

      /* with more than one file, the buffer has the folder and then
	 each file name, all terminated by NULs, then another NUL: */
      file = paths + strlen(paths) + 1;
      if (chooser->multiple && !chooser->save && *file != '\0') {
	for (; *file != '\0'; file += strlen(file) + 1) {
	  _tme_win32_chooser_put(chooser, paths, file);
	}
      }
      else {
	_tme_win32_chooser_put(chooser, paths, NULL);
      }
    }
  }
  _close(chooser->fd);
  free(chooser);
  return (0);
}

/* this starts a file chooser, returning the file descriptor to read
   the paths chosen from, or -1 with errno set.  save_name, if not
   NULL, chooses a file to save to, with that name to start with: */
int
tme_win32_choose(const char *title, const char *start, int folder, int multiple, const char *save_name)
{
  struct _tme_win32_chooser *chooser;
  HANDLE thread;
  int fds[2];
  char *p;

  if (_pipe(fds, 65536, _O_BINARY) != 0) {
    return (-1);
  }
  chooser = (struct _tme_win32_chooser *) calloc(1, sizeof(*chooser));
  snprintf(chooser->title, sizeof(chooser->title), "%s", title);
  snprintf(chooser->start, sizeof(chooser->start), "%s", (start != NULL ? start : ""));
  for (p = chooser->start; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\\';
    }
  }
  snprintf(chooser->save_name, sizeof(chooser->save_name), "%s", (save_name != NULL ? save_name : ""));
  chooser->folder = folder;
  chooser->multiple = multiple;
  chooser->save = (save_name != NULL);
  chooser->fd = fds[1];
  thread = CreateThread(NULL, 0, _tme_win32_chooser_main, chooser, 0, NULL);
  if (thread == NULL) {
    _close(fds[0]);
    _close(fds[1]);
    free(chooser);
    errno = EAGAIN;
    return (-1);
  }
  CloseHandle(thread);
  return (fds[0]);
}

/* this reads a pipe without blocking: when it is empty, but not
   closed, this fails with EAGAIN, as a non-blocking read does: */
ssize_t
tme_win32_read_nb(int fd, void *buf, size_t len)
{
  DWORD avail;
  HANDLE handle;

  handle = (HANDLE) _get_osfhandle(fd);
  if (handle != INVALID_HANDLE_VALUE
      && GetFileType(handle) == FILE_TYPE_PIPE
      && PeekNamedPipe(handle, NULL, 0, NULL, &avail, NULL)
      && avail == 0) {
    errno = EAGAIN;
    return (-1);
  }
  return (_read(fd, buf, len));
}

/* this opens a file or a folder with the program that the desktop
   uses for it: */
int
tme_win32_open_file(const char *path)
{
  return ((INT_PTR) ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL) > 32 ? 0 : ENOENT);
}

/* standard input, when it's a pipe or a console, is read by a thread
   that copies it into a pipe of our own, which replaces it.  the
   cooperative threads can only wait for input that event_wait() can
   see coming, and PeekNamedPipe() works with our pipes everywhere,
   but not with every pipe we may be given (Wine's, from a Unix
   pipe), nor with consoles: */
static HANDLE _tme_win32_console_in;
static DWORD _tme_win32_console_in_mode;
static HANDLE _tme_win32_console_out;
static DWORD _tme_win32_console_out_mode;

static void
_tme_win32_console_restore(void)
{
  SetConsoleMode(_tme_win32_console_in, _tme_win32_console_in_mode);
  if (_tme_win32_console_out != NULL) {
    SetConsoleMode(_tme_win32_console_out, _tme_win32_console_out_mode);
  }
}

struct _tme_win32_pump {
  HANDLE in;
  int out;
};

static DWORD WINAPI
_tme_win32_pump_main(LPVOID _pump)
{
  struct _tme_win32_pump *pump;
  char buffer[4096];
  DWORD got;

  pump = (struct _tme_win32_pump *) _pump;
  while (ReadFile(pump->in, buffer, sizeof(buffer), &got, NULL)
	 && got > 0
	 && _write(pump->out, buffer, got) == (int) got);
  _close(pump->out);
  CloseHandle(pump->in);
  free(pump);
  return (0);
}

/* this puts standard input behind a pump, if it isn't already, and
   isn't a file.  with raw, a console is put in raw mode first, and
   gives the terminal escape sequences for keys like the arrows: */
void
tme_win32_stdin_pump(int raw)
{
  static int pumped;
  struct _tme_win32_pump *pump;
  HANDLE in, out, thread;
  DWORD mode, type;
  int fds[2];

  if (pumped) {
    return;
  }
  in = (HANDLE) _get_osfhandle(0);
  if (in == INVALID_HANDLE_VALUE) {
    return;
  }
  type = GetFileType(in);
  if (type != FILE_TYPE_PIPE && type != FILE_TYPE_CHAR) {
    return;
  }
  if (type == FILE_TYPE_CHAR && GetConsoleMode(in, &mode)) {
    if (!raw) {
      /* a console in line mode is waited on by event_wait(): */
      return;
    }
    _tme_win32_console_in = in;
    _tme_win32_console_in_mode = mode;
    out = (HANDLE) _get_osfhandle(1);
    if (out != INVALID_HANDLE_VALUE
	&& GetConsoleMode(out, &_tme_win32_console_out_mode)) {
      _tme_win32_console_out = out;
      SetConsoleMode(out, _tme_win32_console_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    SetConsoleMode(in, ((mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT))
			| ENABLE_VIRTUAL_TERMINAL_INPUT));
    atexit(_tme_win32_console_restore);
  }

  pump = (struct _tme_win32_pump *) calloc(1, sizeof(*pump));
  if (!DuplicateHandle(GetCurrentProcess(), in, GetCurrentProcess(), &pump->in,
		       0, FALSE, DUPLICATE_SAME_ACCESS)
      || _pipe(fds, 65536, _O_BINARY) != 0) {
    free(pump);
    return;
  }
  pump->out = fds[1];
  thread = CreateThread(NULL, 0, _tme_win32_pump_main, pump, 0, NULL);
  if (thread == NULL) {
    _close(fds[0]);
    _close(fds[1]);
    CloseHandle(pump->in);
    free(pump);
    return;
  }
  CloseHandle(thread);
  _dup2(fds[0], 0);
  _close(fds[0]);
  pumped = TRUE;
}

/* this starts Winsock: */
int
tme_win32_sockets_init(void)
{
  WSADATA wsa;

  return (WSAStartup(MAKEWORD(2, 2), &wsa));
}

#endif /* WIN32 */
