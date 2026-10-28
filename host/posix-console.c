/* host/posix-console.c - a serial console for tmesh's control socket: */

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

/* this is the other end of a serial line, like a terminal, for a
   program rather than a person: what the guest writes is kept for
   tmesh's control socket (see tme/debug.h), which also types into
   it.  with "echo", what the guest writes also goes to tmesh's
   standard output: */

#include <tme/common.h>

/* includes: */
#include <tme/generic/serial.h>
#include <tme/threads.h>
#include <tme/debug.h>
#include <stdio.h>

/* macros: */
#define TME_POSIX_CONSOLE_BUFFER_SIZE	(4096)

/* structures: */
struct tme_posix_console {

  /* our mutex: */
  tme_mutex_t tme_posix_console_mutex;

  /* backpointer to our element: */
  struct tme_element *tme_posix_console_element;

  /* our connection: */
  struct tme_serial_connection *tme_posix_console_connection;

  /* this is nonzero iff callouts are running: */
  int tme_posix_console_callouts_running;

  /* our current control inputs: */
  unsigned int tme_posix_console_ctrl_callin;

  /* our current control outputs, and the last ones we called out: */
  unsigned int tme_posix_console_ctrl_callout;
  unsigned int tme_posix_console_ctrl_callout_last;

  /* what has been typed, for the guest to read: */
  struct tme_serial_buffer tme_posix_console_buffer_in;

  /* nonzero if output also goes to standard output: */
  int tme_posix_console_echo;

  /* the console, for the debugger: */
  struct tme_debug_console tme_posix_console_debug;
};

/* the console callout function.  it must be called with the mutex
   locked: */
static void
_tme_posix_console_callout(struct tme_posix_console *console)
{
  struct tme_serial_connection *conn_serial;
  unsigned int ctrl;
  tme_uint8_t buffer[1024];
  tme_serial_data_flags_t data_flags;
  int rc;
  int again;

  /* if this function is already running in another thread, return
     now.  the other thread will do our work: */
  if (console->tme_posix_console_callouts_running) {
    return;
  }

  /* callouts are now running: */
  console->tme_posix_console_callouts_running = TRUE;

  /* loop running callouts until there is nothing to do: */
  for (again = TRUE; again;) {
    again = FALSE;

    /* if we're not connected, there is nothing to do: */
    conn_serial = console->tme_posix_console_connection;
    if (conn_serial == NULL) {
      break;
    }

    /* if we need to notify our connection of new ctrl bits: */
    ctrl = console->tme_posix_console_ctrl_callout;
    if (ctrl != console->tme_posix_console_ctrl_callout_last) {
      tme_mutex_unlock(&console->tme_posix_console_mutex);
      rc = (*conn_serial->tme_serial_connection_ctrl)(conn_serial, ctrl);
      tme_mutex_lock(&console->tme_posix_console_mutex);
      if (rc == TME_OK) {
	console->tme_posix_console_ctrl_callout_last = ctrl;
	again = TRUE;
      }
    }

    /* if our connection is readable, read what the guest wrote: */
    if (console->tme_posix_console_ctrl_callin & TME_SERIAL_CTRL_OK_READ) {
      tme_mutex_unlock(&console->tme_posix_console_mutex);
      rc = (*conn_serial->tme_serial_connection_read)
	(conn_serial, buffer, sizeof(buffer), &data_flags);
      tme_mutex_lock(&console->tme_posix_console_mutex);

      /* keep what we got, and echo it: */
      if (rc > 0) {
	tme_debug_console_output(&console->tme_posix_console_debug, buffer, rc);
	if (console->tme_posix_console_echo) {
	  fwrite(buffer, 1, rc, stdout);
	  fflush(stdout);
	}
	again = TRUE;
      }

      /* otherwise, the read failed, and the convention dictates
	 that we forget this connection was readable: */
      else {
	console->tme_posix_console_ctrl_callin &= ~TME_SERIAL_CTRL_OK_READ;
      }
    }
  }

  /* there are no more callouts to make: */
  console->tme_posix_console_callouts_running = FALSE;
}

/* the debugger types into the console: */
static unsigned int
_tme_posix_console_input(struct tme_debug_console *debug, const tme_uint8_t *data, unsigned int count)
{
  struct tme_posix_console *console;
  unsigned int space;

  /* recover our data structure: */
  console = debug->tme_debug_console_private;

  /* lock our mutex: */
  tme_mutex_lock(&console->tme_posix_console_mutex);

  /* take what fits, and tell the guest it may read: */
  space = tme_serial_buffer_space_free(&console->tme_posix_console_buffer_in);
  count = TME_MIN(count, space);
  if (count > 0) {
    (void) tme_serial_buffer_copyin(&console->tme_posix_console_buffer_in,
				    data, count,
				    TME_SERIAL_DATA_NORMAL,
				    TME_SERIAL_COPY_NORMAL);
    console->tme_posix_console_ctrl_callout |= TME_SERIAL_CTRL_OK_READ;
    _tme_posix_console_callout(console);
  }

  /* unlock our mutex: */
  tme_mutex_unlock(&console->tme_posix_console_mutex);

  return (count);
}

/* the serial configuration callin function: */
static int
_tme_posix_console_config(struct tme_serial_connection *conn_serial, struct tme_serial_config *config)
{
  /* any baud rate, character size and parity will do: */
  return (TME_OK);
}

/* the serial control callin function: */
static int
_tme_posix_console_ctrl(struct tme_serial_connection *conn_serial, unsigned int control)
{
  struct tme_posix_console *console;

  /* recover our data structure: */
  console = conn_serial->tme_serial_connection.tme_connection_element->tme_element_private;

  /* lock our mutex: */
  tme_mutex_lock(&console->tme_posix_console_mutex);

  /* remember these controls.  if the OK_READ is set, call out a read: */
  console->tme_posix_console_ctrl_callin = control;
  if (control & TME_SERIAL_CTRL_OK_READ) {
    _tme_posix_console_callout(console);
  }

  /* unlock our mutex: */
  tme_mutex_unlock(&console->tme_posix_console_mutex);

  return (TME_OK);
}

/* the serial read callin function: */
static int
_tme_posix_console_read(struct tme_serial_connection *conn_serial,
			tme_uint8_t *data, unsigned int count,
			tme_serial_data_flags_t *_data_flags)
{
  struct tme_posix_console *console;
  unsigned int rc;

  /* recover our data structure: */
  console = conn_serial->tme_serial_connection.tme_connection_element->tme_element_private;

  /* lock the mutex: */
  tme_mutex_lock(&console->tme_posix_console_mutex);

  /* copy data out of the input buffer: */
  rc = tme_serial_buffer_copyout(&console->tme_posix_console_buffer_in,
				 data, count,
				 _data_flags,
				 TME_SERIAL_COPY_NORMAL);

  /* if the input buffer is now empty, our connection shouldn't read
     again until more has been typed.  we don't call this transition
     out, because the convention dictates that our connection forget
     that we're readable: */
  if (rc < count) {
    console->tme_posix_console_ctrl_callout &= ~TME_SERIAL_CTRL_OK_READ;
    console->tme_posix_console_ctrl_callout_last &= ~TME_SERIAL_CTRL_OK_READ;
  }

  /* unlock the mutex: */
  tme_mutex_unlock(&console->tme_posix_console_mutex);

  return (rc);
}

/* this scores a connection: */
static int
_tme_posix_console_connection_score(struct tme_connection *conn, unsigned int *_score)
{
  /* serial connections are always good: */
  *_score = 1;
  return (TME_OK);
}

/* this makes a new connection: */
static int
_tme_posix_console_connection_make(struct tme_connection *conn, unsigned int state)
{
  struct tme_posix_console *console;

  /* recover our console: */
  console = conn->tme_connection_element->tme_element_private;

  /* we only have to do work when the connection has gone full,
     namely taking the other side of the connection: */
  if (state == TME_CONNECTION_FULL) {
    tme_mutex_lock(&console->tme_posix_console_mutex);
    console->tme_posix_console_connection =
      (struct tme_serial_connection *) conn->tme_connection_other;
    _tme_posix_console_callout(console);
    tme_mutex_unlock(&console->tme_posix_console_mutex);
  }

  return (TME_OK);
}

/* this breaks a connection: */
static int
_tme_posix_console_connection_break(struct tme_connection *conn, unsigned int state)
{
  abort();
}

/* this makes a new connection side for a console: */
static int
_tme_posix_console_connections_new(struct tme_element *element,
				   const char * const *args,
				   struct tme_connection **_conns,
				   char **_output)
{
  struct tme_posix_console *console;
  struct tme_serial_connection *conn_serial;
  struct tme_connection *conn;

  /* recover our console: */
  console = (struct tme_posix_console *) element->tme_element_private;

  /* if this console is already connected, we can do nothing: */
  if (console->tme_posix_console_connection != NULL) {
    return (EISCONN);
  }

  /* create our side of a serial connection: */
  conn_serial = tme_new0(struct tme_serial_connection, 1);
  conn = &conn_serial->tme_serial_connection;

  /* fill in the generic connection: */
  conn->tme_connection_next = *_conns;
  conn->tme_connection_type = TME_CONNECTION_SERIAL;
  conn->tme_connection_score = _tme_posix_console_connection_score;
  conn->tme_connection_make = _tme_posix_console_connection_make;
  conn->tme_connection_break = _tme_posix_console_connection_break;

  /* fill in the serial connection: */
  conn_serial->tme_serial_connection_config = _tme_posix_console_config;
  conn_serial->tme_serial_connection_ctrl = _tme_posix_console_ctrl;
  conn_serial->tme_serial_connection_read = _tme_posix_console_read;

  /* return the connection side possibility: */
  *_conns = conn;
  return (TME_OK);
}

/* the new console function: */
TME_ELEMENT_SUB_NEW_DECL(tme_host_posix,console) {
  struct tme_posix_console *console;
  int echo;
  int arg_i;

  /* check our arguments: */
  echo = FALSE;
  for (arg_i = 1; args[arg_i] != NULL; arg_i++) {
    if (TME_ARG_IS(args[arg_i], "echo")) {
      echo = TRUE;
    }
    else {
      tme_output_append_error(_output,
			      "%s %s, %s %s [ echo ]",
			      args[arg_i],
			      _("unexpected"),
			      _("usage:"),
			      args[0]);
      return (EINVAL);
    }
  }

  /* start the console structure: */
  console = tme_new0(struct tme_posix_console, 1);
  console->tme_posix_console_element = element;
  console->tme_posix_console_echo = echo;

  /* a terminal is there: */
  console->tme_posix_console_ctrl_callout
    = (TME_SERIAL_CTRL_DCD | TME_SERIAL_CTRL_CTS);
  console->tme_posix_console_ctrl_callout_last = 0;
  tme_serial_buffer_init(&console->tme_posix_console_buffer_in,
			 TME_POSIX_CONSOLE_BUFFER_SIZE);
  tme_mutex_init(&console->tme_posix_console_mutex);

  /* register the console for the debugger: */
  console->tme_posix_console_debug.tme_debug_console_element = element;
  console->tme_posix_console_debug.tme_debug_console_private = console;
  console->tme_posix_console_debug.tme_debug_console_input = _tme_posix_console_input;
  tme_debug_console_register(&console->tme_posix_console_debug);

  /* fill the element: */
  element->tme_element_private = console;
  element->tme_element_connections_new = _tme_posix_console_connections_new;

  return (TME_OK);
}
