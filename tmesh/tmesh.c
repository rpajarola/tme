/* $Id: tmesh.c,v 1.4 2009/08/30 17:06:38 fredette Exp $ */

/* tmesh/tmesh.c - the tme shell: */

/*
 * Copyright (c) 2003 Matt Fredette
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
_TME_RCSID("$Id: tmesh.c,v 1.4 2009/08/30 17:06:38 fredette Exp $");

/* includes: */
#include <tme/tme.h>
#include <tme/tmesh.h>
#include <tme/hash.h>
#include "tmesh-ctl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef TMESH_WINDOW
#include "tmesh-window.h"
#endif /* TMESH_WINDOW */
#ifdef HAVE_READLINE
#include <signal.h>
#include <time.h>
#include <readline/readline.h>
#include <readline/history.h>
#endif /* HAVE_READLINE */
/* macros: */

/* the binary log message buffer size: */
#define _TMESH_LOG_MESSAGE_BINARY_BUFFER_SIZE	(5 * TME_LOG_MESSAGE_SIZE_MAX_BINARY)

/* the binary log message handle, size, and errno: */
#define _TMESH_LOG_MESSAGE_BINARY_ERRNO		(0xff)
#if TME_LOG_MESSAGE_SIZE_MAX_BINARY & (TME_LOG_MESSAGE_SIZE_MAX_BINARY - 1)
#error "TME_LOG_MESSAGE_SIZE_MAX_BINARY must be a power of two"
#endif
#define _TMESH_LOG_MESSAGE_BINARY_SIZE \
  ((TME_LOG_MESSAGE_SIZE_MAX_BINARY - 1) * (_TMESH_LOG_MESSAGE_BINARY_ERRNO + 1))
#define _TMESH_LOG_MESSAGE_BINARY_HANDLE \
  (~ (tme_uint32_t) (_TMESH_LOG_MESSAGE_BINARY_SIZE + _TMESH_LOG_MESSAGE_BINARY_ERRNO))

/* types: */

/* an input buffer: */
struct _tmesh_input {
  tme_thread_handle_t _tmesh_input_handle;
  char _tmesh_input_buffer[1024];
  unsigned int _tmesh_input_buffer_head;
  unsigned int _tmesh_input_buffer_tail;
  int _tmesh_input_buffer_eof;
};

/* a binary log message: */
struct _tmesh_log_message_binary {
  tme_uint32_t _tmesh_log_message_binary_handle_size_errno;
  tme_uint32_t _tmesh_log_message_binary_level;
};

/* globals: */
const char *argv0;
static const char *argv0_full;

/* our shell instance: */
static void *_tmesh;

/* our current input: */
static struct tmesh_io *_tmesh_io;

/* our log and its mutex: */
static FILE *_tmesh_log;
static tme_mutex_t _tmesh_log_mutex;

/* the log mode: */
static unsigned int _tmesh_log_mode = TME_LOG_MODE_TEXT;

/* the next log handle number: */
static tme_uint32_t _tmesh_log_handle_next;

/* a format hash: */
static tme_hash_t _tmesh_log_hash_format;

/* the standard input: */
static struct _tmesh_input *_tmesh_input_stdin;

#ifdef HAVE_READLINE

/* the most history lines kept: */
#define _TMESH_READLINE_HISTORY_MAX	(1000)

/* nonzero iff the prompt uses readline: */
static int _tmesh_readline;

/* nonzero iff the readline prompt is up: */
static int _tmesh_readline_up;

/* nonzero iff readline has given us a complete line: */
static int _tmesh_readline_line_done;

/* the prompt, the history file, and the last line added to the
   history: */
static char _tmesh_readline_prompt[64];
static char *_tmesh_readline_history;
static char *_tmesh_readline_line_last;

/* the time of the last Ctrl-C: */
static volatile time_t _tmesh_sigint_last;

/* this is called by readline with a complete line, or NULL at EOF: */
static void
_tmesh_readline_line(char *line)
{
  struct _tmesh_input *input;
  size_t len;

  /* take the prompt down while the command runs, so its output
     doesn't get mixed up with the prompt: */
  rl_callback_handler_remove();
  _tmesh_readline_up = FALSE;
  _tmesh_readline_line_done = TRUE;

  /* at EOF, the input is done: */
  input = _tmesh_input_stdin;
  if (line == NULL) {
    printf("\n");
    input->_tmesh_input_buffer_eof = TRUE;
    return;
  }

  /* add the line to the input: */
  len = strlen(line);
  if (input->_tmesh_input_buffer_head + len + 1
      > sizeof(input->_tmesh_input_buffer)) {
    fprintf(stderr, "%s: command too long\n", argv0);
  }
  else {
    memcpy(input->_tmesh_input_buffer + input->_tmesh_input_buffer_head, line, len);
    input->_tmesh_input_buffer_head += len;
    input->_tmesh_input_buffer[input->_tmesh_input_buffer_head++] = '\n';
  }

  /* add the line to the history, unless it's empty or the same as
     the last one: */
  if (line[0] != '\0'
      && (_tmesh_readline_line_last == NULL
	  || strcmp(line, _tmesh_readline_line_last) != 0)) {
    add_history(line);
    free(_tmesh_readline_line_last);
    _tmesh_readline_line_last = strdup(line);
  }
  free(line);
}

/* this puts the prompt up, if it isn't up: */
static void
_tmesh_readline_prompt_up(void)
{
  if (!_tmesh_readline_up) {
    fflush(stdout);
    rl_callback_handler_install(_tmesh_readline_prompt, _tmesh_readline_line);
    _tmesh_readline_up = TRUE;
  }
}

/* this puts the terminal back and saves the history, at exit: */
static void
_tmesh_readline_finish(void)
{
  if (_tmesh_readline_up) {
    rl_callback_handler_remove();
    _tmesh_readline_up = FALSE;
  }
  if (_tmesh_readline_history != NULL) {
    write_history(_tmesh_readline_history);
  }
}

/* this handles a Ctrl-C.  a second one within a few seconds exits: */
static void
_tmesh_sigint(int sig)
{
  static const char again[] = "\n(press Ctrl-C again to quit)\n";
  time_t now;

  now = time(NULL);
  if (_tmesh_sigint_last != 0
      && now - _tmesh_sigint_last <= 3) {
    if (write(1, "\n", 1) < 0) {
      /* nothing */
    }
    exit(0);
  }
  _tmesh_sigint_last = now;
  if (write(1, again, sizeof(again) - 1) < 0) {
    /* nothing */
  }
  if (_tmesh_readline_up) {
    rl_forced_update_display();
  }
}

/* this starts using readline for the prompt: */
static void
_tmesh_readline_start(void)
{
  const char *home;

  _tmesh_readline = TRUE;
  rl_readline_name = "tmesh";
  snprintf(_tmesh_readline_prompt, sizeof(_tmesh_readline_prompt), "%s> ", argv0);

  /* the history is kept in ~/.tmesh_history: */
  stifle_history(_TMESH_READLINE_HISTORY_MAX);
  home = getenv("HOME");
  if (home != NULL) {
    _tmesh_readline_history = tme_new(char, strlen(home) + sizeof("/.tmesh_history"));
    sprintf(_tmesh_readline_history, "%s/.tmesh_history", home);
    read_history(_tmesh_readline_history);
  }

  atexit(_tmesh_readline_finish);
  signal(SIGINT, _tmesh_sigint);
}

#endif /* HAVE_READLINE */

/* this removes all consumed characters from a buffer, and shifts
   everything else down: */
static void
_tmesh_remove_consumed(struct _tmesh_input *input)
{
  input->_tmesh_input_buffer_head -= input->_tmesh_input_buffer_tail;
  memmove(input->_tmesh_input_buffer,
	  input->_tmesh_input_buffer
	  + input->_tmesh_input_buffer_tail,
	  input->_tmesh_input_buffer_head);
  input->_tmesh_input_buffer_tail = 0;
}

/* our getc function: */
static int
_tmesh_getc(struct tmesh_io *io)
{
  struct _tmesh_input *input;
  int c;

  /* recover our input: */
  input = io->tmesh_io_private;

  /* if the buffer isn't empty yet, return the next character: */
  if (input->_tmesh_input_buffer_tail
      < input->_tmesh_input_buffer_head) {
    c = input->_tmesh_input_buffer[input->_tmesh_input_buffer_tail++];
    return (c);
  }

  /* if we're at EOF, return EOF: */
  if (input->_tmesh_input_buffer_eof) {
    return (TMESH_C_EOF);
  }

  /* otherwise, we must yield: */
  return (TMESH_C_YIELD);
}

/* our close function: */
static void
_tmesh_close(struct tmesh_io *io_old, struct tmesh_io *io_new)
{
  struct _tmesh_input *input;

  /* recover our input: */
  input = io_old->tmesh_io_private;

  /* close the file and free the input: */
  tme_thread_close(input->_tmesh_input_handle);
  tme_free(input);

  /* set the new, emerging input: */
  _tmesh_io = io_new;
  if(io_new) _tmesh_remove_consumed(io_new->tmesh_io_private);
}

/* our open function: */
static int
_tmesh_open(struct tmesh_io *io_new, struct tmesh_io *io_old, char **_output)
{
  struct _tmesh_input *input;
  int saved_errno;

  /* allocate a new input: */
  input = tme_new0(struct _tmesh_input, 1);

  /* try to open the file: */
  input->_tmesh_input_handle =
#ifdef WIN32
    (io_old==NULL) ? (tme_win32_open(io_new->tmesh_io_name, TME_FILE_RO, FILE_ATTRIBUTE_NORMAL, 0)) :
#endif
    (tme_thread_open(io_new->tmesh_io_name, TME_FILE_RO));
  
  /* if the open failed: */
  if (input->_tmesh_input_handle == TME_INVALID_HANDLE) {
    saved_errno = errno;
    tme_free(input);
    return (saved_errno);
  }

  /* set the new input: */
  io_new->tmesh_io_private = input;
  io_new->tmesh_io_getc = _tmesh_getc;
  io_new->tmesh_io_close = _tmesh_close;
  io_new->tmesh_io_open = _tmesh_open;
  _tmesh_io = io_new;

  return (TME_OK);
}

/* our log output function: */
static void
_tmesh_log_output(struct tme_log_handle *handle)
{
  FILE *fp;
  
  /* lock the log mutex: */
  tme_mutex_lock(&_tmesh_log_mutex);

  /* if this is an error, report it to stderr, else it goes to the
     log: */
  fp = (handle->tme_log_handle_errno != TME_OK
	? stderr
	: _tmesh_log);

  /* give it to any control socket client: */
  tmesh_ctl_log((char *) handle->tme_log_handle_private,
		handle->tme_log_handle_level,
		handle->tme_log_handle_errno,
		handle->tme_log_handle_message);

  fprintf(fp,
	  "[%s.%lu]",
	  (char *) handle->tme_log_handle_private,
	  handle->tme_log_handle_level);

  if (handle->tme_log_handle_message != NULL) {
    fprintf(fp, ": %s", handle->tme_log_handle_message);
    tme_free(handle->tme_log_handle_message);
    handle->tme_log_handle_message = NULL;
  }
  
  if (handle->tme_log_handle_errno != TME_OK) {
    fprintf(fp, ": %s", strerror(handle->tme_log_handle_errno));
  }

  fputc('\n', fp);

  /* unlock the log mutex: */
  tme_mutex_unlock(&_tmesh_log_mutex);
}

/* our binary log output function: */
static void
_tmesh_log_output_binary(struct tme_log_handle *handle)
{
  struct _tmesh_log_message_binary *binary_message;
  tme_uint32_t handle_size_errno;
  struct _tmesh_log_message_binary binary_message_buffer;
  tme_uint32_t buffer_size;
  
  /* lock the log mutex: */
  tme_mutex_lock(&_tmesh_log_mutex);

  /* make values for the binary message header: */
  binary_message = (struct _tmesh_log_message_binary *) handle->tme_log_handle_private;
  handle_size_errno = binary_message->_tmesh_log_message_binary_handle_size_errno;
  TME_FIELD_MASK_DEPOSITU(handle_size_errno,
			  _TMESH_LOG_MESSAGE_BINARY_SIZE,
			  (handle->tme_log_handle_message_size
			   - sizeof(*binary_message)));
  TME_FIELD_MASK_DEPOSITU(handle_size_errno,
			  _TMESH_LOG_MESSAGE_BINARY_ERRNO,
			  handle->tme_log_handle_errno);

  /* write the binary message header: */
  binary_message = (struct _tmesh_log_message_binary *) handle->tme_log_handle_message;
  if (_TME_ALIGNOF_INT32_T == 1) {
    binary_message->_tmesh_log_message_binary_handle_size_errno = handle_size_errno;
    binary_message->_tmesh_log_message_binary_level = handle->tme_log_handle_level;
  }
  else {
    binary_message_buffer._tmesh_log_message_binary_handle_size_errno = handle_size_errno;
    binary_message_buffer._tmesh_log_message_binary_level = handle->tme_log_handle_level;
    memcpy(binary_message,
	   &binary_message_buffer,
	   sizeof(binary_message_buffer));
  }

  /* if there isn't enough room in the buffer for a maximum-sized
     message: */
  buffer_size
    = ((((char *) binary_message)
	+ handle->tme_log_handle_message_size)
       - (char *) handle->tme_log_handle_private);
  if (buffer_size
      > (_TMESH_LOG_MESSAGE_BINARY_BUFFER_SIZE
	 - TME_LOG_MESSAGE_SIZE_MAX_BINARY)) {

    /* write out the buffer: */
    fwrite(handle->tme_log_handle_private,
	   1,
	   buffer_size,
	   _tmesh_log);

    /* reset the buffer: */
    handle->tme_log_handle_message = handle->tme_log_handle_private;
  }

  /* otherwise, there is enough room in the buffer for a maximum-sized
     message: */
  else {

    /* advance the buffer: */
    handle->tme_log_handle_message += handle->tme_log_handle_message_size;
  }

  /* reset for the next message: */
  handle->tme_log_handle_message_size = sizeof(*binary_message);

  /* unlock the log mutex: */
  tme_mutex_unlock(&_tmesh_log_mutex);
}

/* our log open function: */
static void 
_tmesh_log_open(struct tmesh_support *support, 
		struct tme_log_handle *handle,
		const char *pathname, 
		const char *module)
{
  struct _tmesh_log_message_binary *binary_message;
  
  /* lock the log mutex: */
  tme_mutex_lock(&_tmesh_log_mutex);

  handle->tme_log_handle_level_max = 0;
  handle->tme_log_handle_mode = _tmesh_log_mode;

  /* if the log is binary: */
  if (handle->tme_log_handle_mode == TME_LOG_MODE_BINARY) {

    /* allocate the binary buffer: */
    handle->tme_log_handle_private = tme_malloc(_TMESH_LOG_MESSAGE_BINARY_BUFFER_SIZE);

    /* allocate the handle number and make the first message
       structure: */
    binary_message = (struct _tmesh_log_message_binary *) handle->tme_log_handle_private;
    memset (binary_message, 0, sizeof(*binary_message));
    TME_FIELD_MASK_DEPOSITU(binary_message->_tmesh_log_message_binary_handle_size_errno,
			    _TMESH_LOG_MESSAGE_BINARY_HANDLE,
			    _tmesh_log_handle_next);
    _tmesh_log_handle_next++;
    handle->tme_log_handle_message = (char *) binary_message;
    handle->tme_log_handle_message_size = sizeof(*binary_message);
    assert (handle->tme_log_handle_message_size < TME_LOG_MESSAGE_SIZE_MAX_BINARY);
    
    /* write a dummy first message for the handle that is only the
       pathname: */
    TME_FIELD_MASK_DEPOSITU(binary_message->_tmesh_log_message_binary_handle_size_errno,
			    _TMESH_LOG_MESSAGE_BINARY_SIZE,
			    strlen(pathname) + 1);
    fwrite(binary_message,
	   sizeof(*binary_message),
	   1,
	   _tmesh_log);
    fwrite(pathname,
	   1,
	   (strlen(pathname) + 1),
	   _tmesh_log);

    /* set the output function: */
    handle->tme_log_handle_output = _tmesh_log_output_binary;
    
    /* set the format hash: */
    handle->tme_log_handle_hash_format = _tmesh_log_hash_format;
  }

  /* otherwise, the log is text: */
  else {

    /* set the output function: */
    handle->tme_log_handle_message = NULL;
    handle->tme_log_handle_output = _tmesh_log_output;
    handle->tme_log_handle_private = tme_strdup(pathname);
  }

  /* unlock the log mutex: */
  tme_mutex_unlock(&_tmesh_log_mutex);
}

/* our log close function: */
static void 
_tmesh_log_close(struct tmesh_support *support, 
		 struct tme_log_handle *handle)
{
  tme_free(handle->tme_log_handle_private);
}

/* the tmesh evaluation loop */
int _tmesh_eval() {
  int rc;
  struct tmesh_io *io;
  struct _tmesh_input *input;
  char *output;
  unsigned int consumed;
  int yield;

  /* this command may have changed the current io, so reload: */
  io = _tmesh_io;
  input = io->tmesh_io_private;

  /* run commands until we have to yield: */
  for (;;) {
    
    /* all characters already read have been consumed: */
    consumed = input->_tmesh_input_buffer_tail;
    
    /* run a command: */
    rc = tmesh_eval(_tmesh, &output, &yield);
    
    /* if we're yielding: */
    if (yield || _tmesh_io == NULL) {
      
      /* if the current io has not changed, mark how many
	 characters were consumed by successful commands: */
      if (io == _tmesh_io) {
	input->_tmesh_input_buffer_tail = consumed;
      }

      break;
    }

    /* this command may have changed the current io, so reload: */
    io = _tmesh_io;
    input = io->tmesh_io_private;

    /* display this command's output: */
    if (rc == TME_OK) {
      if (output != NULL
	  && *output != '\0') {
	printf("%s\n", output);
      }
    }
    else {
      fprintf(stderr, "%s:%lu: ",
	      io->tmesh_io_name,
	      io->tmesh_io_input_line);
      if (output != NULL
	  && *output != '\0') {
	fprintf(stderr, "%s: ", output);
      }
      fprintf(stderr, "%s\n", strerror(rc));
    }
    if (output != NULL) {
      tme_free(output);
    }
  }
}

/* this runs a tmesh command for the control socket, as if it were
   sourced from a file, and returns what it printed: */
static int
_tmesh_ctl_command(const char *line, char **_text)
{
  struct tmesh_io io;
  struct tmesh_io *io_saved;
  struct _tmesh_input *input;
  unsigned int depth;
  unsigned int len;
  int rc;
  int yield;
  int passes;
  char *output;

  /* the command, as an input that is at its end: */
  len = strlen(line);
  if (len + 1 >= sizeof(input->_tmesh_input_buffer)) {
    return (E2BIG);
  }
  input = tme_new0(struct _tmesh_input, 1);
  memcpy(input->_tmesh_input_buffer, line, len);
  input->_tmesh_input_buffer[len] = '\n';
  input->_tmesh_input_buffer_head = len + 1;
  input->_tmesh_input_buffer_eof = TRUE;
  input->_tmesh_input_handle = TME_INVALID_HANDLE;
  io.tmesh_io_name = "*control*";
  io.tmesh_io_private = input;
  io.tmesh_io_input_line = 0;
  io.tmesh_io_getc = _tmesh_getc;
  io.tmesh_io_close = _tmesh_close;
  io.tmesh_io_open = _tmesh_open;

  /* push it, and run commands until it (and anything it sources) has
     been read: */
  io_saved = _tmesh_io;
  depth = tmesh_io_depth(_tmesh);
  tmesh_io_push(_tmesh, &io);
  *_text = NULL;
  for (passes = 0; tmesh_io_depth(_tmesh) > depth && passes < 1000; passes++) {
    rc = tmesh_eval(_tmesh, &output, &yield);
    if (yield) {
      break;
    }
    if (rc == TME_OK) {
      if (output != NULL && *output != '\0') {
	tme_output_append(_text, "%s\n", output);
      }
    }
    else {
      if (output != NULL && *output != '\0') {
	tme_output_append(_text, "%s: ", output);
      }
      tme_output_append(_text, "%s\n", strerror(rc));
    }
    if (output != NULL) {
      tme_free(output);
    }
  }
  _tmesh_io = io_saved;
  return (TME_OK);
}

/* our thread: */
static _tme_thret
_tmesh_th(int *interactive)
{
  int rc;
  struct tmesh_io *io;
  struct _tmesh_input *input;
  
  tme_thread_enter(NULL);

  /* this command may have changed the current io, so reload: */
  io = _tmesh_io;
  input = io->tmesh_io_private;
  
  /* loop while we have a current input buffer: */
  for (;;) {

#ifdef HAVE_READLINE
    /* if readline is reading standard input: */
    if (_tmesh_readline
	&& input == _tmesh_input_stdin) {

      /* put the prompt up, and wait until there's a character to
	 read.  NB that when the wait yields, this thread starts over
	 from the beginning: */
      _tmesh_readline_prompt_up();
      (void) tme_thread_read(input->_tmesh_input_handle,
			     input->_tmesh_input_buffer,
			     0,
			     NULL);

      /* give the character to readline.  continue until it has a
	 whole line: */
      rl_callback_read_char();
      if (!_tmesh_readline_line_done) {
	continue;
      }
      _tmesh_readline_line_done = FALSE;
      rc = 0;
    }
    else
#endif /* HAVE_READLINE */
    
    /* try to read more input: */
    rc = tme_thread_read(input->_tmesh_input_handle,
		       input->_tmesh_input_buffer
		       + input->_tmesh_input_buffer_head,
		       sizeof(input->_tmesh_input_buffer)
		       - input->_tmesh_input_buffer_head,
		       NULL);
    /*
      (tme_read(TME_THREAD_HANDLE(input->_tmesh_input_handle),
		input->_tmesh_input_buffer
		+ input->_tmesh_input_buffer_head,
		sizeof(input->_tmesh_input_buffer)
		- input->_tmesh_input_buffer_head));
    */
    /* if the read failed: */
    if (rc < 0) {
      fprintf(stderr, "%s: %s\n",
	      io->tmesh_io_name,
	      strerror(errno));
      continue;
    }

    /* add characters in our current input buffer, or set EOF: */
    if (rc > 0) {
      input->_tmesh_input_buffer_head += rc;
    }
#ifdef HAVE_READLINE
    else if (_tmesh_readline
	     && input == _tmesh_input_stdin) {
      /* readline has already added the line, or set EOF */
    }
#endif /* HAVE_READLINE */
    else {
      input->_tmesh_input_buffer_eof = TRUE;
    }

    /* evaluate commands as they come in */
    _tmesh_eval();
    
    /* if all input has been read, we're done, as a shell is at EOF.
       to keep running without reading commands, use
       --noninteractive: */
    if (_tmesh_io == NULL) {
      exit(0);
    }
    
    /* this command may have changed the current io, so reload: */
    io = _tmesh_io;
    input = io->tmesh_io_private;

    /* If return to console & running interactive, then put up the next prompt, else exit thread: */
    if(input->_tmesh_input_handle == TME_STD_THREAD_HANDLE(stdin)) {
      if(!*interactive) break;
#ifdef HAVE_READLINE
      if (!_tmesh_readline)
#endif /* HAVE_READLINE */
      {
	printf("%s> ", argv0);
	fflush(stdout);
      }
    }
    
    /* remove all consumed characters: */
    _tmesh_remove_consumed(input);
       
    /* if the current input buffer is full, a command is too long: */
    if (input->_tmesh_input_buffer_head
	== sizeof(input->_tmesh_input_buffer)) {
      fprintf(stderr, "%s: command too long\n", argv0);
      input->_tmesh_input_buffer_head = 0;
    }
  }
  
  tme_thread_exit(NULL);
}

#ifdef TMESH_WINDOW
/* this tells the machine's display where its script is, and how to
   run tmesh again, for its Edit Configuration... menu item: */
static void
_tmesh_set_config(const char *script)
{
  const char *path, *colon;
  char *real, file[4096];
  size_t length;

  real = realpath(script, NULL);
  if (real == NULL) {
    return;
  }
  setenv("TMESH_CONFIG", real, 1);
  free(real);

  /* find tmesh itself: */
  real = NULL;
  if (strchr(argv0_full, '/') != NULL) {
    real = realpath(argv0_full, NULL);
  }
  else {
    for (path = getenv("PATH"); real == NULL && path != NULL && *path != '\0'; path = colon + 1) {
      colon = strchr(path, ':');
      if (colon == NULL) {
	colon = path + strlen(path);
      }
      length = colon - path;
      snprintf(file, sizeof(file), "%.*s/%s", (int) length, path, argv0_full);
      if (length > 0 && access(file, X_OK) == 0) {
	real = realpath(file, NULL);
      }
      if (*colon == '\0') {
	break;
      }
    }
  }
  if (real != NULL) {
    setenv("TMESH_PROGRAM", real, 1);
    free(real);
  }
}
#endif /* TMESH_WINDOW */

static void
do_usage(const char *prog_name, char *msg)
{
  if (msg != NULL)
    fputs(msg, stderr);
  fprintf(stderr, "usage: %s [OPTIONS] <INITIAL-CONFIG> \
                   \nwhere OPTIONS are:			   \
                   \n--log LOGFILE          log to LOGFILE		\
                   \n-c, --noninteractive   read no commands from standard input (<INITIAL-CONFIG> required here)\
                   \n--control unix:PATH|tcp:PORT  listen for requests from programs; send them with\
                   \n                       %s --ctl unix:PATH|tcp:PORT COMMAND [ARGUMENT];\
                   \n                       for AI agents, %s --mcp unix:PATH|tcp:PORT is an MCP server using it\n", prog_name, prog_name, prog_name);
#ifdef TMESH_WINDOW
  fprintf(stderr,
	  "--edit [SCRIPT]        open the machine window (on SCRIPT's machine, or a new one there)\n"
	  "--shell                without <INITIAL-CONFIG>, read commands instead of opening the window\n"
	  "with no <INITIAL-CONFIG>, tmesh opens the machine window, unless commands are piped in\n");
#endif /* TMESH_WINDOW */
  
#ifdef TME_THREADS_POSIX
#define fpe(msg) fprintf(stderr, "\t%s", msg);          /* Shorter */
#ifdef HAVE_PTHREAD_SETAFFINITY_NP
  fpe("--cpus            cpusetmask\n");
#endif // HAVE_PTHREAD_SETAFFINITY_NP
#ifdef HAVE_PTHREAD_SETSCHEDPARAM  
  fpe("-a <policy><prio> Set scheduling policy and priority in\n");
  fpe("                  thread attributes object\n");
  fpe("                  <policy> can be\n");
  fpe("                     f  SCHED_FIFO\n");
  fpe("                     r  SCHED_RR\n");
  fpe("                     o  SCHED_OTHER\n");
  fpe("-A                Use default thread attributes object\n");
  fpe("-i {e|i}          Set inherit scheduler attribute to\n");
  fpe("                  'explicit' or 'inherit'\n");
  fpe("-m <policy><prio> Set scheduling policy and priority on\n");
  fpe("                  main thread before pthread_create() call\n");
#endif // HAVE_PTHREAD_SETSCHEDPARAM  
#endif // TME_THREADS_POSIX
  exit(1);
}

#ifdef TME_THREADS_POSIX
#define handle_error_en(en, msg)					\
  do { errno = en; perror(msg); /* exit(EXIT_FAILURE) ;*/ } while (0)

#ifdef HAVE_PTHREAD_SETSCHEDPARAM  
static int
get_policy(char p, int *policy)
{
  switch (p) {
  case 'f': *policy = SCHED_FIFO;     return 1;
  case 'r': *policy = SCHED_RR;       return 1;
  case 'o': *policy = SCHED_OTHER;    return 1;
  default:  return 0;
  }
}

static void
display_sched_attr(int policy, struct sched_param *param)
{
  printf("    policy=%s, priority=%d\n",
	 (policy == SCHED_FIFO)  ? "SCHED_FIFO" :
	 (policy == SCHED_RR)    ? "SCHED_RR" :
	 (policy == SCHED_OTHER) ? "SCHED_OTHER" :
	 "???",
	 param->sched_priority);
}

static void
display_thread_sched_attr(char *msg)
{
  int policy, s;
  struct sched_param param;

  s = pthread_getschedparam(pthread_self(), &policy, &param);
  if (s != 0)
    handle_error_en(s, "pthread_getschedparam");

  printf("%s\n", msg);
  display_sched_attr(policy, &param);
}
#endif // HAVE_PTHREAD_SETSCHEDPARAM  
#endif // TME_THREADS_POSIX

static void tmesh_finish() {
  printf("Goodbye\n");
}

int
main(int argc, char **argv)
{
  int usage;
  const char *opt;
  int arg_i;
  const char **initial_config_filename;
  const char *log_filename;
  const char *control_spec;
  int interactive;
  struct tmesh_io io;
  struct tmesh_support support;
  struct _tmesh_input *input_stdin;
  char *output;
  int rc;
  tme_threadid_t tmesh_thread;
  int edit, edit_running, shell;
  const char *edit_path;
  char *window_script, *slash;
  struct stat stdin_stat;
#ifdef TME_THREADS_POSIX
  pthread_t thread;
#ifdef HAVE_PTHREAD_SETAFFINITY_NP
  int j, cpus=-1;
  tme_cpuset_t cpuset;
#endif // HAVE_PTHREAD_SETAFFINITY_NP
#ifdef HAVE_PTHREAD_SETSCHEDPARAM
  int inheritsched, use_null_attrib, policy;
  pthread_attr_t attr, *attrp;
  char *attr_sched_str, *main_sched_str, *inheritsched_str;
  struct sched_param param;

  use_null_attrib = 0;
  attr_sched_str = NULL;
  main_sched_str = NULL;
  inheritsched_str = NULL;
#endif // HAVE_PTHREAD_SETSCHEDPARAM
#endif // TME_THREADS_POSIX

  /* check our command line: */
  usage = FALSE;
  initial_config_filename = NULL;
  log_filename = "/dev/null";
  control_spec = NULL;
  interactive = TRUE;
  edit = FALSE;
  edit_running = FALSE;
  edit_path = NULL;
  shell = FALSE;
  window_script = NULL;
  argv0_full = argv[0];
  if ((argv0 = strrchr(argv[0], '/')) == NULL) argv0 = argv[0]; else argv0++;

  /* tmesh --ctl ...: a control socket client: */
  if (argc >= 2 && !strcmp(argv[1], "--ctl")) {
    return (tmesh_ctl_client(argc, argv));
  }

  /* tmesh --mcp ...: an MCP server for AI agents: */
  if (argc >= 2 && !strcmp(argv[1], "--mcp")) {
    return (tmesh_mcp(argc, argv));
  }
  for (arg_i = 1;
       (arg_i < argc
	&& *argv[arg_i] == '-');
       arg_i++) {
    opt = argv[arg_i];
    if (!strcmp(opt, "--log")) {
      if (++arg_i < argc) {
	log_filename = argv[arg_i];
      }
      else {
	usage = TRUE;
	break;
      }
    }
    else if (!strcmp(opt, "--log-mode")) {
      ++arg_i;
      if (arg_i >= argc
	  || strcmp(argv[arg_i], "binary")) {
	usage = TRUE;
	break;
      }
      _tmesh_log_mode = TME_LOG_MODE_BINARY;
      if (_tmesh_log_hash_format == NULL) {
	_tmesh_log_hash_format = tme_hash_new(tme_direct_hash, tme_direct_compare, TME_HASH_DATA_NULL);
      }
    }
#ifdef TME_THREADS_POSIX
#ifdef HAVE_PTHREAD_SETAFFINITY_NP
    else if (!strcmp(opt, "--cpus")) {
      if (++arg_i < argc) {
	cpus=strtol(argv[arg_i], NULL, 0);
      } else {
	usage = TRUE;
	break;
      }
    }
#endif // HAVE_PTHREAD_SETAFFINITY_NP
#ifdef HAVE_PTHREAD_SETSCHEDPARAM  
    else if (!strcmp(opt, "-m")) {
      if (++arg_i < argc) {
	main_sched_str=argv[arg_i];
      } else {
	usage = TRUE;
	break;
      }
    }
    else if (!strcmp(opt, "-a")) {
      if (++arg_i < argc) {
	attr_sched_str=argv[arg_i];
      } else {
	usage = TRUE;
	break;
      }
    }
    else if (!strcmp(opt, "-A")) {
      use_null_attrib = 1;
    }
    else if (!strcmp(opt, "-i")) {
      if (++arg_i < argc) {
	inheritsched_str=argv[arg_i];
      } else {
	usage = TRUE;
	break;
      }
    }
#endif // HAVE_PTHREAD_SETSCHEDPARAM  
#endif // TME_THREADS_POSIX
    else if (!strcmp(opt, "--control")) {
      if (++arg_i < argc) {
	control_spec = argv[arg_i];
      }
      else {
	usage = TRUE;
	break;
      }
    }
    else if (!strcmp(opt, "-c")
	     || !strcmp(opt, "--noninteractive")) {
      interactive = FALSE;
    }
#ifdef TMESH_WINDOW
    else if (!strcmp(opt, "--edit")) {
      edit = TRUE;
      if (arg_i + 1 < argc && *argv[arg_i + 1] != '-') {
	edit_path = argv[++arg_i];
      }
    }
    else if (!strcmp(opt, "--running")) {
      edit_running = TRUE;
    }
    else if (!strcmp(opt, "--shell")) {
      shell = TRUE;
    }
#endif /* TMESH_WINDOW */
    else {
      if (strcmp(opt, "-h")
	  && strcmp(opt, "--help")
	  && strcmp(opt, "-h")) {
	fprintf(stderr, "%s: unknown option %s\n",
		argv0, opt);
      }
      usage = TRUE;
      break;
    }
  }
  
  if (usage) do_usage(argv0, NULL);

#ifdef TMESH_WINDOW
  /* with --edit, or without a script (unless commands are piped in),
     open the machine window.  the machine chosen there is started
     from its folder: */
  if (edit
      || (arg_i == argc
	  && !shell
	  && interactive
	  && !(fstat(STDIN_FILENO, &stdin_stat) == 0
	       && (S_ISFIFO(stdin_stat.st_mode) || S_ISREG(stdin_stat.st_mode))))) {
    if (!tmesh_window_run(edit_path, edit_running, &window_script)) {
      if (edit) {
	exit(1);
      }
      fprintf(stderr, "%s: reading commands instead (see %s --help)\n", argv0, argv0);
    }
    else if (window_script == NULL) {
      exit(0);
    }
    else {

      /* without a terminal (as when tmesh is started from the
	 desktop), the machine runs without reading commands, since
	 tmesh would quit at the end of its input: */
      if (!isatty(STDIN_FILENO)) {
	interactive = FALSE;
      }
      _tmesh_set_config(window_script);
      slash = strrchr(window_script, '/');
      if (slash != NULL) {
	*slash = '\0';
	if (chdir(*window_script != '\0' ? window_script : "/") != 0) {
	  perror(window_script);
	  exit(1);
	}
	window_script = slash + 1;
      }
    }
  }
  else if (arg_i + 1 == argc) {
    _tmesh_set_config(argv[arg_i]);
  }
#endif /* TMESH_WINDOW */

  if (!strcmp(log_filename, "-")) {
    _tmesh_log = stdout;
  }
  else {
    _tmesh_log = fopen(log_filename, "a");
    if (_tmesh_log == NULL) {
      perror(log_filename);
      exit(1);
    }
  }

  /* initialize libtme: */
  tme_init();

  //  tme_thread_enter(NULL);
  
#ifdef TME_THREADS_POSIX
  thread = pthread_self();

#ifdef HAVE_PTHREAD_SETAFFINITY_NP
  /* Set affinity mask to include CPUs */

  tme_cpuset_init(cpuset);
  for (j = 0; j < sizeof(cpus); j++)
    if(cpus & (1<<j))
      tme_cpuset_set(j, cpuset);
  
  rc = pthread_setaffinity_np(thread, tme_cpuset_size(cpuset), tme_cpuset_ref(cpuset));
  if (rc != 0)
    handle_error_en(rc, "pthread_setaffinity_np");
  
  /* Check the actual affinity mask assigned to the thread */
  
  rc = pthread_getaffinity_np(thread, tme_cpuset_size(cpuset), tme_cpuset_ref(cpuset));
  if (rc != 0)
    handle_error_en(rc, "pthread_getaffinity_np");

  printf("Set returned by pthread_getaffinity_np() contained:\n");
  
  for (j = 0; j < sizeof(cpus); j++)
    if (tme_cpuset_isset(j, cpuset))
      printf("    CPU %d\n", j);
#endif // HAVE_PTHREAD_SETAFFINITY_NP

#ifdef HAVE_PTHREAD_SETSCHEDPARAM  
  if (use_null_attrib &&
      (inheritsched_str != NULL || attr_sched_str != NULL)) {
    do_usage(argv0, "Can't specify -A with -i or -a\n");
  }

  if (main_sched_str != NULL) {
    if (!get_policy(main_sched_str[0], &policy))
      do_usage(argv0, "Bad policy for main thread (-m)\n");
    param.sched_priority = strtol(&main_sched_str[1], NULL, 0);
    
    rc = pthread_setschedparam(pthread_self(), policy, &param);
    if (rc != 0)
      handle_error_en(rc, "pthread_setschedparam");
  }
  
  display_thread_sched_attr("Scheduler settings of main thread");
  printf("\n");

  /* Initialize thread attributes object according to options */

  attrp = NULL;

  if (!use_null_attrib) {
    rc = pthread_attr_init(&attr);
    if (rc != 0)
      handle_error_en(rc, "pthread_attr_init");
    attrp = &attr;
  }

  if (inheritsched_str != NULL) {
    if (inheritsched_str[0] == 'e')
      inheritsched = PTHREAD_EXPLICIT_SCHED;
    else if (inheritsched_str[0] == 'i')
      inheritsched = PTHREAD_INHERIT_SCHED;
    else
      do_usage(argv0, "Value for -i must be 'e' or 'i'\n");

    rc = pthread_attr_setinheritsched(&attr, inheritsched);
    if (rc != 0)
      handle_error_en(rc, "pthread_attr_setinheritsched");
  }

  if (attr_sched_str != NULL) {
    if (!get_policy(attr_sched_str[0], &policy))
      do_usage(argv0,
	       "Bad policy for 'attr' (-a)\n");
    param.sched_priority = strtol(&attr_sched_str[1], NULL, 0);

    rc = pthread_attr_setschedpolicy(&attr, policy);
    if (rc != 0)
      handle_error_en(rc, "pthread_attr_setschedpolicy");
    rc = pthread_attr_setschedparam(&attr, &param);
    if (rc != 0)
      handle_error_en(rc, "pthread_attr_setschedparam");
  }

  tme_thread_set_defattr(attrp);
  
  /* If we initialized a thread attributes object, display
     the scheduling attributes that were set in the object */

  if (attrp != NULL) {
    rc = pthread_attr_getschedparam(&attr, &param);
    if (rc != 0)
      handle_error_en(rc, "pthread_attr_getschedparam");
    rc = pthread_attr_getschedpolicy(&attr, &policy);
    if (rc != 0)
      handle_error_en(rc, "pthread_attr_getschedpolicy");

    printf("Scheduler settings in 'attr'\n");
    display_sched_attr(policy, &param);

    rc = pthread_attr_getinheritsched(&attr, &inheritsched);
    printf("    inheritsched is %s\n",
	   (inheritsched == PTHREAD_INHERIT_SCHED)  ? "INHERIT" :
	   (inheritsched == PTHREAD_EXPLICIT_SCHED) ? "EXPLICIT" :
	   "???");
    printf("\n");
  }
#endif // HAVE_PTHREAD_SETSCHEDPARAM  
#endif

  /* initialize libtmesh: */
  (void) tmesh_init();

  /* Register a finalizer: */
  atexit(tmesh_finish);
  
  /* create our support: */
  tme_mutex_init(&_tmesh_log_mutex);
  support.tmesh_support_log_open = _tmesh_log_open;
  support.tmesh_support_log_close = _tmesh_log_close;

  /* create our stdin input buffer */
  input_stdin = tme_new0(struct _tmesh_input, 1);
  input_stdin->_tmesh_input_handle = TME_STD_THREAD_HANDLE(stdin);
  _tmesh_input_stdin = input_stdin;

  input_stdin->_tmesh_input_buffer[sizeof(input_stdin->_tmesh_input_buffer) - 1] = '\0';
  
  /* create our stdin io: */
  io.tmesh_io_name = "*stdin*";
  io.tmesh_io_private = input_stdin;
  io.tmesh_io_input_line = 0;
  io.tmesh_io_getc = _tmesh_getc;
  io.tmesh_io_close = _tmesh_close;
  io.tmesh_io_open = _tmesh_open;
  _tmesh_io = &io;

  /* create our shell: */
  _tmesh = tmesh_new(&support, _tmesh_io);
  
  /* if we're reading commands from a terminal, use readline: */
#ifdef HAVE_READLINE
  if (interactive && isatty(0)) {
    _tmesh_readline_start();
  }
  else
#endif /* HAVE_READLINE */

  /* evaluate pre-threads once (to avoid synchronization problems with init code): */
  if(interactive) printf("%s> ", argv0);
  rc = 0;
  if (window_script != NULL) {
    /* source the script of the machine chosen in the window: */
    rc += snprintf(input_stdin->_tmesh_input_buffer + rc,
		   sizeof(input_stdin->_tmesh_input_buffer) - rc,
		   "source \"%s\"\n", window_script);
  }
  for(;arg_i<argc;arg_i++) {
    /* stuff console with the commands to source the initial config files: */
    rc += snprintf(input_stdin->_tmesh_input_buffer + rc,
		   sizeof(input_stdin->_tmesh_input_buffer) - rc,
		   "source %s\n", argv[arg_i]);
  }
  input_stdin->_tmesh_input_buffer_head += rc;
  printf("%s", input_stdin->_tmesh_input_buffer);
  fflush(stdout);
  
  _tmesh_eval();
    
  /* remove all consumed characters: */
  _tmesh_remove_consumed(input_stdin);

  /* start the control socket: */
  if (control_spec != NULL) {
    tmesh_ctl_start(control_spec, _tmesh_ctl_command);
  }

  /* create our eval loop thread for interactive mode: */
  tme_thread_create(&tmesh_thread, (tme_thread_t) _tmesh_th, &interactive);

  /* run the threads: */
  tme_threads_run();

#if defined(TME_THREADS_POSIX) && defined(HAVE_PTHREAD_SETSCHEDPARAM)
  /* Destroy unneeded thread attributes object */

  if (!use_null_attrib) {
    rc = pthread_attr_destroy(&attr);
    if (rc != 0)
      handle_error_en(rc, "pthread_attr_destroy");
  }
#endif

  if(interactive)
    tme_thread_join(tmesh_thread);

  /* done: */
  exit(0);
}
