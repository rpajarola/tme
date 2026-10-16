/* host/display/sdl/sdl-files.c - SDL2 display file choosers: */

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

/* SDL has no file chooser, so the SDL display runs the desktop's:
   osascript on macOS, and zenity or kdialog elsewhere.  the chooser
   runs as another process, which the display checks on every update,
   so the emulated machine keeps running while it is up.  the chooser
   prints the paths chosen, one per line: */

#include <tme/common.h>

/* includes: */
#include "sdl-display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* the choosers: */
#define TME_SDL_FILES_OSASCRIPT	(1)
#define TME_SDL_FILES_ZENITY	(2)
#define TME_SDL_FILES_KDIALOG	(3)

/* the chooser to run: */
static int _tme_sdl_files_chooser;

/* this returns nonzero if a program is in the PATH: */
static int
_tme_sdl_files_in_path(const char *program)
{
  const char *path, *colon;
  char *file;
  size_t length;
  int found;

  path = getenv("PATH");
  if (path == NULL) {
    return (FALSE);
  }
  found = FALSE;
  for (; !found; path = colon + 1) {
    colon = strchr(path, ':');
    if (colon == NULL) {
      colon = path + strlen(path);
    }
    length = colon - path;
    file = tme_new(char, length + 1 + strlen(program) + 1);
    if (length == 0) {
      strcpy(file, program);
    }
    else {
      memcpy(file, path, length);
      file[length] = '/';
      strcpy(file + length + 1, program);
    }
    found = (access(file, X_OK) == 0);
    tme_free(file);
    if (*colon == '\0') {
      break;
    }
  }
  return (found);
}

/* this appends a string to a string, reallocating it: */
static char *
_tme_sdl_files_append(char *string, const char *more)
{
  size_t length;

  length = (string == NULL ? 0 : strlen(string));
  string = tme_renew(char, string, length + strlen(more) + 1);
  strcpy(string + length, more);
  return (string);
}

/* this appends an AppleScript string literal to a string: */
static char *
_tme_sdl_files_append_applescript(char *string, const char *text)
{
  char quoted[2];

  string = _tme_sdl_files_append(string, "\"");
  quoted[1] = '\0';
  for (; *text != '\0'; text++) {
    if (*text == '"' || *text == '\\') {
      string = _tme_sdl_files_append(string, "\\");
    }
    quoted[0] = *text;
    string = _tme_sdl_files_append(string, quoted);
  }
  return (_tme_sdl_files_append(string, "\""));
}

/* this starts a file chooser, for the display's choose files
   function: */
static void
_tme_sdl_files_choose(struct tme_screen *screen,
		      struct tme_display_files_request *request)
{
  struct tme_sdl_display *display;
  struct tme_sdl_files *files;
  const char *argv[12];
  char *script, *arg;
  const char *start;
  struct stat st;
  int argc, fds[2], fd;
  pid_t pid;

  display = (struct tme_sdl_display *) screen->tme_screen_display;

  /* one chooser at a time: */
  if (display->tme_sdl_display_files != NULL) {
    _tme_display_message(screen, _("Another file chooser is already open"));
    _tme_display_files_chosen(screen, request, NULL);
    return;
  }

  /* only start in a folder that exists: */
  start = request->tme_display_files_request_start;
  if (start != NULL
      && (stat(start, &st) != 0 || !S_ISDIR(st.st_mode))) {
    start = NULL;
  }

  /* make the command: */
  argc = 0;
  script = NULL;
  arg = NULL;
  switch (_tme_sdl_files_chooser) {

  case TME_SDL_FILES_OSASCRIPT:
    script = _tme_sdl_files_append(script, "activate\n");
    if (request->tme_display_files_request_multiple) {
      script = _tme_sdl_files_append(script, "set chosen to choose file with prompt ");
    }
    else {
      script = _tme_sdl_files_append(script, "set chosen to {choose ");
      script = _tme_sdl_files_append(script,
				     (request->tme_display_files_request_folder
				      ? "folder"
				      : "file"));
      script = _tme_sdl_files_append(script, " with prompt ");
    }
    script = _tme_sdl_files_append_applescript(script, request->tme_display_files_request_title);
    if (start != NULL) {
      script = _tme_sdl_files_append(script, " default location (POSIX file ");
      script = _tme_sdl_files_append_applescript(script, start);
      script = _tme_sdl_files_append(script, ")");
    }
    if (request->tme_display_files_request_multiple) {
      script = _tme_sdl_files_append(script, " with multiple selections allowed\n");
    }
    else {
      script = _tme_sdl_files_append(script, "}\n");
    }
    script = _tme_sdl_files_append(script,
				   "set paths to \"\"\n"
				   "repeat with f in chosen\n"
				   "set paths to paths & POSIX path of (contents of f) & linefeed\n"
				   "end repeat\n"
				   "return paths\n");
    argv[argc++] = "osascript";
    argv[argc++] = "-e";
    argv[argc++] = script;
    break;

  case TME_SDL_FILES_ZENITY:
    argv[argc++] = "zenity";
    argv[argc++] = "--file-selection";
    arg = _tme_sdl_files_append(NULL, "--title=");
    arg = _tme_sdl_files_append(arg, request->tme_display_files_request_title);
    argv[argc++] = arg;
    if (request->tme_display_files_request_folder) {
      argv[argc++] = "--directory";
    }
    if (request->tme_display_files_request_multiple) {
      argv[argc++] = "--multiple";
      argv[argc++] = "--separator=\n";
    }
    if (start != NULL) {
      script = _tme_sdl_files_append(NULL, "--filename=");
      script = _tme_sdl_files_append(script, start);
      script = _tme_sdl_files_append(script, "/");
      argv[argc++] = script;
    }
    break;

  case TME_SDL_FILES_KDIALOG:
    argv[argc++] = "kdialog";
    argv[argc++] = "--title";
    argv[argc++] = request->tme_display_files_request_title;
    argv[argc++] = (request->tme_display_files_request_folder
		    ? "--getexistingdirectory"
		    : "--getopenfilename");
    argv[argc++] = (start != NULL ? start : ".");
    if (request->tme_display_files_request_multiple) {
      argv[argc++] = "--multiple";
      argv[argc++] = "--separate-output";
    }
    break;

  default:
    abort();
  }
  argv[argc] = NULL;

  /* start the chooser, with its output to a pipe: */
  pid = -1;
  if (pipe(fds) == 0) {
    pid = fork();
    if (pid == 0) {
      fd = open("/dev/null", O_RDWR);
      dup2(fd, STDIN_FILENO);
      dup2(fd, STDERR_FILENO);
      dup2(fds[1], STDOUT_FILENO);
      close(fds[0]);
      close(fds[1]);
      if (fd > STDERR_FILENO) {
	close(fd);
      }
      execvp(argv[0], (char * const *) argv);
      _exit(127);
    }
    close(fds[1]);
    if (pid < 0) {
      close(fds[0]);
    }
  }
  if (script != NULL) {
    tme_free(script);
  }
  if (arg != NULL) {
    tme_free(arg);
  }
  if (pid < 0) {
    _tme_display_message(screen, _("Could not start a file chooser: %s"), strerror(errno));
    _tme_display_files_chosen(screen, request, NULL);
    return;
  }
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);

  files = tme_new0(struct tme_sdl_files, 1);
  files->tme_sdl_files_pid = pid;
  files->tme_sdl_files_fd = fds[0];
  files->tme_sdl_files_screen = screen;
  files->tme_sdl_files_request = request;
  display->tme_sdl_display_files = files;
}

/* this checks on any running file chooser, and when it's done, passes
   on the files chosen: */
void
_tme_sdl_files_poll(struct tme_sdl_display *display)
{
  struct tme_sdl_files *files;
  char buffer[1024];
  char **paths, *line, *next;
  ssize_t got;
  size_t length;
  unsigned int count;
  int status;

  files = display->tme_sdl_display_files;
  if (files == NULL) {
    return;
  }

  /* read what the chooser has printed, until the end: */
  for (;;) {
    got = read(files->tme_sdl_files_fd, buffer, sizeof(buffer));
    if (got < 0 && errno == EINTR) {
      continue;
    }
    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    if (got <= 0) {
      break;
    }
    files->tme_sdl_files_output
      = tme_renew(char, files->tme_sdl_files_output,
		  files->tme_sdl_files_output_length + got + 1);
    memcpy(files->tme_sdl_files_output + files->tme_sdl_files_output_length, buffer, got);
    files->tme_sdl_files_output_length += got;
    files->tme_sdl_files_output[files->tme_sdl_files_output_length] = '\0';
  }
  close(files->tme_sdl_files_fd);
  while (waitpid(files->tme_sdl_files_pid, &status, 0) < 0
	 && errno == EINTR);
  display->tme_sdl_display_files = NULL;

  /* the paths are the nonempty lines.  a folder may end in a
     slash: */
  count = 0;
  paths = tme_new0(char *, 1);
  for (line = files->tme_sdl_files_output;
       line != NULL && *line != '\0';
       line = next) {
    next = strchr(line, '\n');
    if (next != NULL) {
      *(next++) = '\0';
    }
    length = strlen(line);
    while (length > 1 && line[length - 1] == '/') {
      line[--length] = '\0';
    }
    if (length == 0) {
      continue;
    }
    paths = tme_renew(char *, paths, count + 2);
    paths[count++] = tme_strdup(line);
    paths[count] = NULL;
  }

  /* bring the window back to the front, and pass on the files: */
  SDL_RaiseWindow(((struct tme_sdl_screen *) files->tme_sdl_files_screen)->tme_sdl_screen_window);
  _tme_display_files_chosen(files->tme_sdl_files_screen,
			    files->tme_sdl_files_request,
			    (count > 0 ? paths : NULL));
  tme_free_string_array(paths, -1);
  if (files->tme_sdl_files_output != NULL) {
    tme_free(files->tme_sdl_files_output);
  }
  tme_free(files);
}

/* this sets up file choosers for a display, returning nonzero if there
   is one: */
int
_tme_sdl_files_init(struct tme_sdl_display *display)
{
#ifdef __APPLE__
  _tme_sdl_files_chooser = TME_SDL_FILES_OSASCRIPT;
#else  /* !__APPLE__ */
  if (_tme_sdl_files_in_path("zenity")) {
    _tme_sdl_files_chooser = TME_SDL_FILES_ZENITY;
  }
  else if (_tme_sdl_files_in_path("kdialog")) {
    _tme_sdl_files_chooser = TME_SDL_FILES_KDIALOG;
  }
#endif /* !__APPLE__ */
  if (_tme_sdl_files_chooser == 0) {
    return (FALSE);
  }
  display->display.tme_screen_files_choose = _tme_sdl_files_choose;
  return (TRUE);
}
