/* tmesh/tmesh-mcp.c - the tmesh MCP server: */

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

/* tmesh --mcp unix:PATH|tcp:PORT lets an AI agent (such as Claude
   Code) use a running tmesh's control socket as MCP tools.

   it speaks MCP (JSON-RPC 2.0, one message per line) on its standard
   input and output, and turns each tool call into control socket
   requests, on a connection of its own; so tmesh can be started,
   stopped and restarted independently, and tmesh --ctl keeps working
   between the calls.  on top of the control socket's commands, tools
   which run the machine can wait for it to stop, and screendump
   returns the screen as an image.  see the "MCP Server" section of
   the manual: */

#include <tme/common.h>

/* includes: */
#include "tmesh-ctl.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#ifndef WIN32
#include <unistd.h>
#endif /* !WIN32 */

/* the program name, from tmesh.c: */
extern const char *argv0;

#ifndef WIN32

/* macros: */

/* the newest MCP version this server knows; an older one which the
   client asks for is used instead: */
#define TMESH_MCP_PROTOCOL_VERSION	"2025-06-18"

/* how long to wait for tmesh to answer a request: */
#define TMESH_MCP_RESPONSE_TIMEOUT_SEC	(60)

/* the longest a tool waits for the machine to stop: */
#define TMESH_MCP_MAX_WAIT_SEC		(3600)

/* what a tool does, besides sending its command: */
#define TMESH_MCP_PLAIN		(0)
#define TMESH_MCP_RUN		(1)	/* waits for the machine to stop */
#define TMESH_MCP_WAIT		(2)	/* only waits */
#define TMESH_MCP_STATUS	(3)	/* hello and status */
#define TMESH_MCP_SCREENDUMP	(4)	/* returns an image */

#define TMESH_MCP_WAIT_PARAM(def)					\
  "\"wait\": {\"type\": \"number\", \"description\": \"Seconds to wait "	\
  "for the machine to stop (default " def "). If it is still running "	\
  "then, the result says so, and wait can keep waiting.\"}"

#define TMESH_MCP_CPU_PARAM						\
  "\"cpu\": {\"type\": \"integer\", \"description\": \"The CPU, from "	\
  "status (default 0).\"}"

#define TMESH_MCP_CONSOLE_PARAM						\
  "\"console\": {\"type\": \"integer\", \"description\": \"A console, "	\
  "from status (default 0).\"}"

#define TMESH_MCP_ENCODING_PARAM					\
  "\"encoding\": {\"enum\": [\"text\", \"hex\"], \"description\": "	\
  "\"text (the default), or hex for the bytes exactly.\"}"

#define TMESH_MCP_SPACE_PARAM						\
  "\"space\": {\"type\": \"integer\", \"description\": \"An m68k "	\
  "function code, or a SPARC ASI (default: the CPU's data space, "	\
  "virtual).\"}"

#define TMESH_MCP_ADDR							\
  "An address: hex, or a register of the CPU, optionally plus or "	\
  "minus a number, such as \\\"pc\\\", \\\"a7+8\\\" or \\\"o6-64\\\"."

/* the tools: */
static const struct _tmesh_mcp_tool {
  const char *name;
  const char *cmd;		/* the control socket command */
  int kind;
  int wait;			/* TMESH_MCP_RUN, TMESH_MCP_WAIT: the default wait */
  const char *description;
  const char *properties;	/* the input schema's properties */
  const char *required;		/* and its required list */
} _tmesh_mcp_tools[] = {
  { "status", NULL, TMESH_MCP_STATUS, 0,
    "tmesh's version, its CPUs and consoles, whether the machine is "
    "running or stopped, how it last stopped, and each CPU's pc. Start "
    "with this.",
    "", "" },

  { "pause", "pause", TMESH_MCP_RUN, 10,
    "Pauses the machine (its CPUs; devices and timers keep running), "
    "and says where it stopped.",
    TMESH_MCP_WAIT_PARAM("10"), "" },

  { "continue", "continue", TMESH_MCP_RUN, 0,
    "Continues the stopped machine. With wait, waits for it to stop "
    "again, as at a breakpoint or watch, and says how it stopped.",
    TMESH_MCP_WAIT_PARAM("0: return right away"), "" },

  { "step", "step", TMESH_MCP_RUN, 30,
    "Runs n instructions of a CPU (the machine must be stopped), and "
    "says where it stopped.",
    TMESH_MCP_CPU_PARAM ", \"n\": {\"type\": \"integer\", \"description\": "
    "\"How many instructions (default 1).\"}, " TMESH_MCP_WAIT_PARAM("30"),
    "" },

  { "run_until", "run_until", TMESH_MCP_RUN, 60,
    "Runs the machine until the first of the given conditions happens "
    "(console output, a pc, a number of instructions, or a time), and "
    "says which one. Also ends when the machine stops for another "
    "reason, such as a breakpoint.",
    "\"console\": {\"type\": \"object\", \"description\": \"Stop when a "
    "console prints one of the patterns.\", \"properties\": {"
    "\"pattern\": {\"anyOf\": [{\"type\": \"string\"}, {\"type\": "
    "\"array\", \"items\": {\"type\": \"string\"}}], \"description\": "
    "\"Plain text (not a regular expression), or a list of them.\"}, "
    TMESH_MCP_CONSOLE_PARAM ", \"since\": {\"type\": \"integer\", "
    "\"description\": \"A console offset to search from (default: what "
    "is printed from now on).\"}, \"input\": {\"type\": \"string\", "
    "\"description\": \"Typed into the console once the search has "
    "started. It is typed as it is; end it with a carriage return "
    "character (in JSON, \\\"boot\\\\r\\\") for Return, not a backslash "
    "and an r.\"}}, \"required\": [\"pattern\"]}, "
    "\"pc\": {\"type\": \"string\", \"description\": \"Stop when cpu "
    "reaches this address. " TMESH_MCP_ADDR "\"}, " TMESH_MCP_CPU_PARAM ", "
    "\"insns\": {\"type\": \"integer\", \"description\": \"Stop after "
    "about this many more instructions. Like the console patterns and "
    "timeout_ms, this is checked every few milliseconds, so the machine "
    "stops some 100000 instructions later; step runs an exact number.\"}, "
    "\"timeout_ms\": {\"type\": \"integer\", \"description\": \"Stop "
    "after this many milliseconds.\"}, "
    TMESH_MCP_WAIT_PARAM("60, or longer than timeout_ms"), "" },

  { "wait", NULL, TMESH_MCP_WAIT, 60,
    "Waits for the running machine to stop, and says how it stopped. If "
    "it is already stopped, says how it last stopped.",
    TMESH_MCP_WAIT_PARAM("60"), "" },

  { "break_add", "break_add", TMESH_MCP_PLAIN, 0,
    "Adds a breakpoint for a CPU. Returns its id.",
    TMESH_MCP_CPU_PARAM ", \"pc\": {\"type\": \"string\", \"description\": "
    "\"" TMESH_MCP_ADDR "\"}",
    "\"pc\"" },

  { "break_del", "break_del", TMESH_MCP_PLAIN, 0,
    "Removes a breakpoint.",
    "\"bp\": {\"type\": \"integer\", \"description\": \"The breakpoint's "
    "id.\"}", "\"bp\"" },

  { "break_list", "break_list", TMESH_MCP_PLAIN, 0,
    "Lists the breakpoints.", "", "" },

  { "console_read", "console_read", TMESH_MCP_PLAIN, 0,
    "Reads what the guest printed on a serial console (a "
    "tme/host/posix/console element). tmesh keeps the last 1 MB, counted "
    "by an offset which only grows; pass the returned offset as since "
    "next time to get only what is new.",
    TMESH_MCP_CONSOLE_PARAM ", \"since\": {\"type\": \"integer\", "
    "\"description\": \"The offset to read from (default 0).\"}, \"max\": "
    "{\"type\": \"integer\", \"description\": \"At most this many bytes "
    "(default 65536).\"}, " TMESH_MCP_ENCODING_PARAM, "" },

  { "console_write", "console_write", TMESH_MCP_PLAIN, 0,
    "Types into a serial console, as if from its terminal. Text is typed "
    "as it is: a backslash is a backslash. For Return, set enter; other "
    "control characters must be the characters themselves (in JSON, "
    "\"\\u0003\" for Control-C), or use encoding hex (\"03\").",
    TMESH_MCP_CONSOLE_PARAM ", \"data\": {\"type\": \"string\", "
    "\"description\": \"What to type.\"}, \"enter\": {\"type\": "
    "\"boolean\", \"description\": \"Press Return (a carriage return, "
    "0x0d) after data.\"}, " TMESH_MCP_ENCODING_PARAM, "" },

  { "keyboard", "keyboard", TMESH_MCP_PLAIN, 0,
    "Types on the keyboard of a display (such as a Sun keyboard), as if "
    "from the host's keyboard: the characters of text, or keys. Text is "
    "typed as it is: a backslash is a backslash. For Return, set enter; "
    "Tab, BackSpace and Escape are the characters themselves (in JSON, "
    "\"\\t\", \"\\b\", \"\\u001b\"), and other control characters are "
    "Control and a letter.",
    "\"text\": {\"type\": \"string\", \"description\": \"What to type; "
    "characters are typed with Shift if they need it on a US "
    "keyboard.\"}, \"keys\": {\"type\": \"array\", \"items\": {\"type\": "
    "\"string\"}, \"description\": \"Instead of text: keys named as X11 "
    "keysyms (\\\"Return\\\", \\\"F1\\\", \\\"a\\\", \\\"space\\\", "
    "\\\"Control_L\\\"), each pressed and released; keys joined by + are "
    "held down together, such as \\\"Shift_L+a\\\", or \\\"L1+a\\\" for a "
    "Sun keyboard's Stop-A.\"}, \"enter\": {\"type\": \"boolean\", "
    "\"description\": \"Press Return after text or keys.\"}", "" },

  { "regs", "regs", TMESH_MCP_PLAIN, 0,
    "Reads a CPU's registers, as hex strings.",
    TMESH_MCP_CPU_PARAM ", \"names\": {\"type\": \"array\", \"items\": "
    "{\"type\": \"string\"}, \"description\": \"Which registers (default: "
    "all).\"}", "" },

  { "reg_set", "reg_set", TMESH_MCP_PLAIN, 0,
    "Sets a register (the machine must be stopped).",
    TMESH_MCP_CPU_PARAM ", \"name\": {\"type\": \"string\"}, \"value\": "
    "{\"type\": \"string\", \"description\": \"A value, in hex.\"}",
    "\"name\", \"value\"" },

  { "mem_read", "mem_read", TMESH_MCP_PLAIN, 0,
    "Reads a CPU's memory, as hex. Doesn't cause guest exceptions; only "
    "memory is read, never device registers.",
    "\"addr\": {\"type\": \"string\", \"description\": \"" TMESH_MCP_ADDR
    "\"}, \"len\": {\"type\": \"integer\", \"description\": \"Bytes "
    "(default 64).\"}, " TMESH_MCP_SPACE_PARAM ", " TMESH_MCP_CPU_PARAM,
    "\"addr\"" },

  { "mem_write", "mem_write", TMESH_MCP_PLAIN, 0,
    "Writes a CPU's memory (the machine must be stopped).",
    "\"addr\": {\"type\": \"string\", \"description\": \"" TMESH_MCP_ADDR
    "\"}, \"data\": {\"type\": \"string\", \"description\": \"The bytes, "
    "in hex.\"}, " TMESH_MCP_SPACE_PARAM ", " TMESH_MCP_CPU_PARAM,
    "\"addr\", \"data\"" },

  { "watch_add", "watch_add", TMESH_MCP_PLAIN, 0,
    "Watches CPU exceptions, or the guest's accesses to a device's "
    "registers. What a watch sees goes into the trace (trace_read); with "
    "action stop, it also stops the machine.",
    "\"kind\": {\"enum\": [\"exception\", \"device\"]}, \"names\": "
    "{\"anyOf\": [{\"type\": \"string\"}, {\"type\": \"array\", \"items\": "
    "{\"type\": \"string\"}}], \"description\": \"Exception names, such "
    "as bus_error or data_access_exception, with * and ? wildcards "
    "(default: any).\"}, \"device\": {\"type\": \"string\", "
    "\"description\": \"A device name, from devices.\"}, \"offset\": "
    "{\"type\": \"string\", \"description\": \"The start of a range of "
    "the device's registers (default 0).\"}, \"len\": {\"type\": "
    "\"string\", \"description\": \"The range's length (default: all of "
    "the device).\"}, \"access\": {\"enum\": [\"r\", \"w\", \"rw\"]}, "
    "\"action\": {\"enum\": [\"stop\", \"trace\"], \"description\": "
    "\"stop (the default) or trace only.\"}",
    "\"kind\"" },

  { "watch_del", "watch_del", TMESH_MCP_PLAIN, 0,
    "Removes a watch.",
    "\"watch\": {\"type\": \"integer\", \"description\": \"The watch's "
    "id.\"}", "\"watch\"" },

  { "watch_list", "watch_list", TMESH_MCP_PLAIN, 0,
    "Lists the watches.", "", "" },

  { "trace_read", "trace_read", TMESH_MCP_PLAIN, 0,
    "Reads what watches saw (the last 32768 events); pass the returned "
    "next as since to get only what is new.",
    "\"since\": {\"type\": \"integer\", \"description\": \"An event "
    "number (default 0).\"}, \"max\": {\"type\": \"integer\", "
    "\"description\": \"At most this many events (default 1000).\"}",
    "" },

  { "devices", "devices", TMESH_MCP_PLAIN, 0,
    "Lists the machine's devices, with their buses and addresses.",
    "", "" },

  { "screendump", "screendump", TMESH_MCP_SCREENDUMP, 0,
    "Shows what a screen shows, as an image (the machine needs a "
    "display).",
    "\"screen\": {\"type\": \"integer\", \"description\": \"A framebuffer "
    "connected to a display (default 0).\"}, \"file\": {\"type\": "
    "\"string\", \"description\": \"Also keep the image as this PNG "
    "file.\"}", "" },

  { "raw", "raw", TMESH_MCP_PLAIN, 0,
    "Runs a tmesh command, such as \"ls\", \"command mainbus0 power "
    "up\" or \"log\", and returns what it printed.",
    "\"line\": {\"type\": \"string\"}", "\"line\"" },

  { "quit", "quit", TMESH_MCP_PLAIN, 0,
    "Exits tmesh.", "", "" },

  { NULL, NULL, TMESH_MCP_PLAIN, 0, NULL, NULL, NULL }
};

/* the control socket commands which have no tool of the same name
   (status is in the status tool, with hello): */
static const char * const _tmesh_mcp_hidden[] = { "hello", "status", "subscribe", NULL };

static const char _tmesh_mcp_instructions[] =
  "These tools drive a tmesh (The Machine Emulator) through its control "
  "socket. tmesh must be running with --control (the same socket as "
  "given to --mcp), e.g. tmesh --control unix:/tmp/tme.sock -c "
  "tme.cfg. Start with status.\n"
  "Addresses and register values are hex strings; an address can also "
  "be a register, optionally plus or minus a number (\"pc\", \"a7+8\"). "
  "step, reg_set and mem_write need the machine stopped: pause it "
  "first. pause, step and run_until wait for the machine to stop and "
  "say how it stopped; so does continue, with wait. Serial consoles are "
  "read with console_read and typed into with console_write; "
  "screendump shows the screen, and keyboard types on the display's "
  "keyboard. raw runs a tmesh command. Every result has ninstrs, about "
  "how many instructions the first CPU has run.";

/* the socket: */
static const char *_tmesh_mcp_spec;

/* the number of the next temporary screendump: */
static int _tmesh_mcp_tmp_count;

/* a connection to tmesh, for one tool call: */
struct _tmesh_mcp_conn {
  int fd;
  char *buf;
  size_t len;
  int next_id;
  char error[300];		/* why the last request failed */
};

/* this sends a message on standard output, and frees it: */
static void
_tmesh_mcp_send(cJSON *msg)
{
  char *s;

  s = cJSON_PrintUnformatted(msg);
  fputs(s, stdout);
  fputc('\n', stdout);
  fflush(stdout);
  free(s);
  cJSON_Delete(msg);
}

static void
_tmesh_mcp_send_result(cJSON *id, cJSON *result)
{
  cJSON *msg;

  msg = cJSON_CreateObject();
  cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
  cJSON_AddItemToObject(msg, "id", cJSON_Duplicate(id, TRUE));
  cJSON_AddItemToObject(msg, "result", result);
  _tmesh_mcp_send(msg);
}

static void
_tmesh_mcp_send_error(cJSON *id, int code, const char *message)
{
  cJSON *msg, *e;

  msg = cJSON_CreateObject();
  cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
  cJSON_AddItemToObject(msg, "id", (id != NULL ? cJSON_Duplicate(id, TRUE) : cJSON_CreateNull()));
  e = cJSON_AddObjectToObject(msg, "error");
  cJSON_AddNumberToObject(e, "code", code);
  cJSON_AddStringToObject(e, "message", message);
  _tmesh_mcp_send(msg);
}

/* this returns a tool's result, with text, and whether it is an
   error: */
static cJSON *
_tmesh_mcp_text_result(const char *text, int is_error)
{
  cJSON *r, *content, *t;

  r = cJSON_CreateObject();
  content = cJSON_AddArrayToObject(r, "content");
  t = cJSON_CreateObject();
  cJSON_AddStringToObject(t, "type", "text");
  cJSON_AddStringToObject(t, "text", text);
  cJSON_AddItemToArray(content, t);
  if (is_error) {
    cJSON_AddBoolToObject(r, "isError", TRUE);
  }
  return (r);
}

/* this returns a tool's result with a JSON object as its text, and
   frees the object: */
static cJSON *
_tmesh_mcp_json_result(cJSON *o)
{
  char *s;
  cJSON *r;

  s = cJSON_PrintUnformatted(o);
  r = _tmesh_mcp_text_result(s, FALSE);
  free(s);
  cJSON_Delete(o);
  return (r);
}

static int
_tmesh_mcp_conn_open(struct _tmesh_mcp_conn *conn)
{
  conn->buf = NULL;
  conn->len = 0;
  conn->next_id = 1;
  conn->error[0] = '\0';
  conn->fd = tmesh_ctl_client_connect(_tmesh_mcp_spec);
  if (conn->fd < 0) {
    snprintf(conn->error, sizeof(conn->error),
	     "can't connect to %s; is tmesh running with --control %s?",
	     _tmesh_mcp_spec, _tmesh_mcp_spec);
  }
  return (conn->fd >= 0);
}

static void
_tmesh_mcp_conn_close(struct _tmesh_mcp_conn *conn)
{
  if (conn->fd >= 0) {
    close(conn->fd);
  }
  conn->fd = -1;
  free(conn->buf);
  conn->buf = NULL;
}

static void
_tmesh_mcp_deadline(struct timeval *tv, double seconds)
{
  gettimeofday(tv, NULL);
  tv->tv_sec += (time_t) seconds;
  tv->tv_usec += (long) ((seconds - (time_t) seconds) * 1e6);
  if (tv->tv_usec >= 1000000) {
    tv->tv_sec++;
    tv->tv_usec -= 1000000;
  }
}

/* this sends a request (and frees it), and returns the response's
   result, or NULL with the connection's error set.  events before the
   response are skipped: */
static cJSON *
_tmesh_mcp_request(struct _tmesh_mcp_conn *conn, cJSON *req, double *_ninstrs)
{
  struct timeval deadline;
  int timed_out, sent;
  char *s, *line;
  size_t len;
  cJSON *msg, *ok, *ninstrs, *result, *e, *code, *message;

  cJSON_AddNumberToObject(req, "id", conn->next_id++);
  s = cJSON_PrintUnformatted(req);
  cJSON_Delete(req);
  len = strlen(s);
  s = realloc(s, len + 2);
  s[len] = '\n';
  s[len + 1] = '\0';
  sent = tmesh_ctl_send_all(conn->fd, s, len + 1);
  free(s);
  if (!sent) {
    snprintf(conn->error, sizeof(conn->error), "can't send to %s", _tmesh_mcp_spec);
    return (NULL);
  }

  timed_out = FALSE;
  _tmesh_mcp_deadline(&deadline, TMESH_MCP_RESPONSE_TIMEOUT_SEC);
  while ((line = tmesh_ctl_client_readline(conn->fd, &conn->buf, &conn->len, &deadline, &timed_out)) != NULL) {
    msg = cJSON_Parse(line);
    free(line);
    ok = cJSON_GetObjectItemCaseSensitive(msg, "ok");
    if (ok == NULL) {
      cJSON_Delete(msg);
      continue;
    }
    ninstrs = cJSON_GetObjectItemCaseSensitive(msg, "ninstrs");
    if (_ninstrs != NULL && cJSON_IsNumber(ninstrs)) {
      *_ninstrs = ninstrs->valuedouble;
    }
    if (cJSON_IsTrue(ok)) {
      result = cJSON_DetachItemFromObjectCaseSensitive(msg, "result");
      cJSON_Delete(msg);
      return (result != NULL ? result : cJSON_CreateObject());
    }
    e = cJSON_GetObjectItemCaseSensitive(msg, "error");
    code = cJSON_GetObjectItemCaseSensitive(e, "code");
    message = cJSON_GetObjectItemCaseSensitive(e, "message");
    snprintf(conn->error, sizeof(conn->error), "%s: %s",
	     (cJSON_IsString(code) ? code->valuestring : "error"),
	     (cJSON_IsString(message) ? message->valuestring : ""));
    cJSON_Delete(msg);
    return (NULL);
  }

  if (timed_out) {
    snprintf(conn->error, sizeof(conn->error), "no answer from %s in %d seconds",
	     _tmesh_mcp_spec, TMESH_MCP_RESPONSE_TIMEOUT_SEC);
  }
  else {
    snprintf(conn->error, sizeof(conn->error), "%s closed the connection", _tmesh_mcp_spec);
  }
  return (NULL);
}

static cJSON *
_tmesh_mcp_cmd(struct _tmesh_mcp_conn *conn, const char *cmd, double *_ninstrs)
{
  cJSON *req;

  req = cJSON_CreateObject();
  cJSON_AddStringToObject(req, "cmd", cmd);
  return (_tmesh_mcp_request(conn, req, _ninstrs));
}

/* this waits up to seconds for a stopped or exited event, and adds
   what happened to out: stopped (the event), exited, or running, if
   the machine didn't stop in time: */
static void
_tmesh_mcp_wait_stop(struct _tmesh_mcp_conn *conn, cJSON *out, double seconds)
{
  struct timeval deadline;
  int timed_out;
  char *line;
  cJSON *msg, *ev;

  timed_out = FALSE;
  _tmesh_mcp_deadline(&deadline, seconds);
  while ((line = tmesh_ctl_client_readline(conn->fd, &conn->buf, &conn->len, &deadline, &timed_out)) != NULL) {
    msg = cJSON_Parse(line);
    free(line);
    ev = cJSON_GetObjectItemCaseSensitive(msg, "event");
    if (cJSON_IsString(ev) && !strcmp(ev->valuestring, "exited")) {
      cJSON_AddBoolToObject(out, "exited", TRUE);
      cJSON_Delete(msg);
      return;
    }
    if (cJSON_IsString(ev) && !strcmp(ev->valuestring, "stopped")) {
      cJSON_DeleteItemFromObjectCaseSensitive(msg, "event");
      cJSON_AddItemToObject(out, "stopped", msg);
      return;
    }
    cJSON_Delete(msg);
  }

  if (timed_out) {
    cJSON_AddBoolToObject(out, "running", TRUE);
    cJSON_AddStringToObject(out, "note", "still running; wait waits for it to stop, pause stops it");
  }
  else {
    cJSON_AddBoolToObject(out, "exited", TRUE);
  }
}

/* this returns the wait parameter, which tools take as seconds: */
static double
_tmesh_mcp_get_wait(cJSON *args, const struct _tmesh_mcp_tool *tool)
{
  cJSON *wait, *ms;
  double seconds;

  wait = cJSON_GetObjectItemCaseSensitive(args, "wait");
  seconds = tool->wait;
  if (cJSON_IsNumber(wait)) {
    seconds = wait->valuedouble;
  }
  else if (tool->cmd != NULL && !strcmp(tool->cmd, "run_until")) {
    /* long enough for its timeout: */
    ms = cJSON_GetObjectItemCaseSensitive(args, "timeout_ms");
    if (cJSON_IsNumber(ms) && ms->valuedouble / 1000 + 10 > seconds) {
      seconds = ms->valuedouble / 1000 + 10;
    }
  }
  if (seconds < 0) {
    seconds = 0;
  }
  if (seconds > TMESH_MCP_MAX_WAIT_SEC) {
    seconds = TMESH_MCP_MAX_WAIT_SEC;
  }
  return (seconds);
}

/* this returns data in base64, in a malloced string: */
static char *
_tmesh_mcp_base64(const unsigned char *data, size_t len)
{
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char *out, *p;
  size_t i;
  tme_uint32_t v;

  out = p = malloc((len + 2) / 3 * 4 + 1);
  for (i = 0; i < len; i += 3) {
    v = (tme_uint32_t) data[i] << 16;
    if (i + 1 < len) {
      v |= (tme_uint32_t) data[i + 1] << 8;
    }
    if (i + 2 < len) {
      v |= data[i + 2];
    }
    *p++ = b64[(v >> 18) & 63];
    *p++ = b64[(v >> 12) & 63];
    *p++ = (i + 1 < len ? b64[(v >> 6) & 63] : '=');
    *p++ = (i + 2 < len ? b64[v & 63] : '=');
  }
  *p = '\0';
  return (out);
}

/* this reads a whole file into a malloced buffer: */
static unsigned char *
_tmesh_mcp_read_file(const char *path, size_t *_len)
{
  FILE *f;
  unsigned char *data;
  long len;

  f = fopen(path, "rb");
  if (f == NULL) {
    return (NULL);
  }
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len < 0) {
    fclose(f);
    return (NULL);
  }
  data = malloc(len + 1);
  *_len = fread(data, 1, len, f);
  fclose(f);
  return (data);
}

/* this merges the status command's result into hello's: */
static void
_tmesh_mcp_merge_status(cJSON *out, cJSON *status)
{
  cJSON *item, *cpus, *cpu, *field;
  int cpu_i;

  cJSON_DeleteItemFromObjectCaseSensitive(out, "state");
  cJSON_DeleteItemFromObjectCaseSensitive(out, "proto");
  while ((item = status->child) != NULL) {
    cJSON_DetachItemViaPointer(status, item);
    cpus = cJSON_GetObjectItemCaseSensitive(out, item->string);
    if (!strcmp(item->string, "cpus") && cJSON_IsArray(cpus)) {

      /* each CPU's pc and whether it is stopped: */
      cpu_i = 0;
      cJSON_ArrayForEach(cpu, item) {
	while ((field = cpu->child) != NULL) {
	  cJSON_DetachItemViaPointer(cpu, field);
	  if (strcmp(field->string, "id") && cJSON_GetArrayItem(cpus, cpu_i) != NULL) {
	    cJSON_AddItemToObject(cJSON_GetArrayItem(cpus, cpu_i), field->string, field);
	  }
	  else {
	    cJSON_Delete(field);
	  }
	}
	cpu_i++;
      }
      cJSON_Delete(item);
    }
    else {
      cJSON_DeleteItemFromObjectCaseSensitive(out, item->string);
      cJSON_AddItemToObject(out, item->string, item);
    }
  }
  cJSON_Delete(status);
}

/* this adds a Return after console_write's data, or keyboard's text
   or keys, for their enter parameter: */
static void
_tmesh_mcp_enter(cJSON *req, const char *cmd)
{
  cJSON *enter, *data, *encoding, *keys;
  const char *key, *d;
  int hex;
  char *s;

  enter = cJSON_GetObjectItemCaseSensitive(req, "enter");
  if (enter == NULL) {
    return;
  }
  if (!cJSON_IsTrue(enter)) {
    cJSON_DeleteItemFromObjectCaseSensitive(req, "enter");
    return;
  }
  cJSON_DeleteItemFromObjectCaseSensitive(req, "enter");

  keys = cJSON_GetObjectItemCaseSensitive(req, "keys");
  if (!strcmp(cmd, "keyboard") && cJSON_IsArray(keys)) {
    cJSON_AddItemToArray(keys, cJSON_CreateString("Return"));
    return;
  }

  key = (!strcmp(cmd, "keyboard") ? "text" : "data");
  data = cJSON_GetObjectItemCaseSensitive(req, key);
  encoding = cJSON_GetObjectItemCaseSensitive(req, "encoding");
  hex = (cJSON_IsString(encoding) && !strcmp(encoding->valuestring, "hex"));
  d = (cJSON_IsString(data) ? data->valuestring : "");
  s = malloc(strlen(d) + 3);
  sprintf(s, "%s%s", d, (hex ? "0d" : "\r"));
  cJSON_DeleteItemFromObjectCaseSensitive(req, key);
  cJSON_AddStringToObject(req, key, s);
  free(s);
}

/* this runs a tool, and returns its result: */
static cJSON *
_tmesh_mcp_call(const struct _tmesh_mcp_tool *tool, const char *cmd, cJSON *args)
{
  struct _tmesh_mcp_conn conn;
  cJSON *req, *out, *result, *state, *stop, *already, *stopped, *ninstrs_item, *file, *r, *img;
  double ninstrs, seconds;
  char tmp[4096];
  const char *dir;
  unsigned char *png;
  char *data;
  size_t len;
  int own_file;

  if (!_tmesh_mcp_conn_open(&conn)) {
    return (_tmesh_mcp_text_result(conn.error, TRUE));
  }
  ninstrs = 0;

  /* the request: the tool's arguments, without the MCP server's own: */
  req = (cJSON_IsObject(args) ? cJSON_Duplicate(args, TRUE) : cJSON_CreateObject());
  cJSON_DeleteItemFromObjectCaseSensitive(req, "id");
  cJSON_DeleteItemFromObjectCaseSensitive(req, "cmd");
  if (tool != NULL && (tool->kind == TMESH_MCP_RUN || tool->kind == TMESH_MCP_WAIT)) {
    cJSON_DeleteItemFromObjectCaseSensitive(req, "wait");
  }
  if (cmd != NULL && (!strcmp(cmd, "console_write") || !strcmp(cmd, "keyboard"))) {
    _tmesh_mcp_enter(req, cmd);
  }
  own_file = FALSE;
  if (tool != NULL
      && tool->kind == TMESH_MCP_SCREENDUMP
      && cJSON_GetObjectItemCaseSensitive(req, "file") == NULL) {
    dir = getenv("TMPDIR");
    if (dir == NULL || dir[0] == '\0') {
      dir = "/tmp";
    }
    snprintf(tmp, sizeof(tmp), "%s/tmesh-mcp-%ld-%d.png", dir, (long) getpid(), _tmesh_mcp_tmp_count++);
    cJSON_AddStringToObject(req, "file", tmp);
    own_file = TRUE;
  }

  if (tool != NULL && tool->kind == TMESH_MCP_STATUS) {

    /* hello, and status: */
    cJSON_Delete(req);
    out = _tmesh_mcp_cmd(&conn, "hello", NULL);
    result = (out != NULL ? _tmesh_mcp_cmd(&conn, "status", &ninstrs) : NULL);
    if (result != NULL) {
      _tmesh_mcp_merge_status(out, result);
    }
    else {
      cJSON_Delete(out);
      out = NULL;
    }
  }
  else if (tool != NULL && tool->kind == TMESH_MCP_WAIT) {
    cJSON_Delete(req);
    out = _tmesh_mcp_cmd(&conn, "status", &ninstrs);
    state = cJSON_GetObjectItemCaseSensitive(out, "state");
    if (cJSON_IsString(state) && !strcmp(state->valuestring, "running")) {
      cJSON_Delete(out);
      out = cJSON_CreateObject();
      _tmesh_mcp_wait_stop(&conn, out, _tmesh_mcp_get_wait(args, tool));
    }
    else if (out != NULL) {
      stop = cJSON_DetachItemFromObjectCaseSensitive(out, "stop");
      cJSON_Delete(out);
      out = cJSON_CreateObject();
      cJSON_AddBoolToObject(out, "already_stopped", TRUE);
      if (stop != NULL) {
	cJSON_AddItemToObject(out, "stopped", stop);
      }
    }
  }
  else {
    cJSON_AddStringToObject(req, "cmd", cmd);
    out = _tmesh_mcp_request(&conn, req, &ninstrs);
    if (out != NULL && tool != NULL && tool->kind == TMESH_MCP_RUN) {
      already = cJSON_GetObjectItemCaseSensitive(out, "already_stopped");
      seconds = _tmesh_mcp_get_wait(args, tool);
      if (seconds > 0 && !cJSON_IsTrue(already)) {
	_tmesh_mcp_wait_stop(&conn, out, seconds);
      }
    }
  }

  if (out == NULL) {
    r = _tmesh_mcp_text_result(conn.error, TRUE);
    _tmesh_mcp_conn_close(&conn);
    return (r);
  }
  _tmesh_mcp_conn_close(&conn);

  /* the ninstrs of a stop is the later one: */
  stopped = cJSON_GetObjectItemCaseSensitive(out, "stopped");
  ninstrs_item = cJSON_GetObjectItemCaseSensitive(stopped, "ninstrs");
  if (cJSON_IsNumber(ninstrs_item) && ninstrs_item->valuedouble > ninstrs) {
    ninstrs = ninstrs_item->valuedouble;
  }
  if (cJSON_GetObjectItemCaseSensitive(out, "ninstrs") == NULL) {
    cJSON_AddNumberToObject(out, "ninstrs", ninstrs);
  }
  if (tool == NULL || tool->kind != TMESH_MCP_SCREENDUMP) {
    return (_tmesh_mcp_json_result(out));
  }

  /* the screen, as an image: */
  file = cJSON_GetObjectItemCaseSensitive(out, "file");
  png = (cJSON_IsString(file) ? _tmesh_mcp_read_file(file->valuestring, &len) : NULL);
  if (png == NULL) {
    snprintf(tmp, sizeof(tmp), "can't read the screendump %s",
	     (cJSON_IsString(file) ? file->valuestring : ""));
    cJSON_Delete(out);
    return (_tmesh_mcp_text_result(tmp, TRUE));
  }
  if (own_file) {
    remove(file->valuestring);
    cJSON_DeleteItemFromObjectCaseSensitive(out, "file");
  }
  r = _tmesh_mcp_json_result(out);
  img = cJSON_CreateObject();
  data = _tmesh_mcp_base64(png, len);
  free(png);
  cJSON_AddStringToObject(img, "type", "image");
  cJSON_AddStringToObject(img, "data", data);
  cJSON_AddStringToObject(img, "mimeType", "image/png");
  free(data);
  cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(r, "content"), img);
  return (r);
}

static const struct _tmesh_mcp_tool *
_tmesh_mcp_find_tool(const char *name)
{
  int tool_i;

  for (tool_i = 0; _tmesh_mcp_tools[tool_i].name != NULL; tool_i++) {
    if (!strcmp(_tmesh_mcp_tools[tool_i].name, name)) {
      return (&_tmesh_mcp_tools[tool_i]);
    }
  }
  return (NULL);
}

/* this returns nonzero if a control socket command is a tool of its
   own, or hidden: */
static int
_tmesh_mcp_covered(const char *cmd)
{
  int i;

  for (i = 0; _tmesh_mcp_tools[i].name != NULL; i++) {
    if (_tmesh_mcp_tools[i].cmd != NULL && !strcmp(_tmesh_mcp_tools[i].cmd, cmd)) {
      return (TRUE);
    }
  }
  for (i = 0; _tmesh_mcp_hidden[i] != NULL; i++) {
    if (!strcmp(_tmesh_mcp_hidden[i], cmd)) {
      return (TRUE);
    }
  }
  return (FALSE);
}

static cJSON *
_tmesh_mcp_tool_list(void)
{
  cJSON *list, *o, *schema, *props;
  const char *name, *params, *p;
  char *s, desc[300];
  size_t len, wlen;
  int i;

  list = cJSON_CreateArray();
  for (i = 0; _tmesh_mcp_tools[i].name != NULL; i++) {
    len = strlen(_tmesh_mcp_tools[i].properties) + strlen(_tmesh_mcp_tools[i].required) + 100;
    s = malloc(len);
    snprintf(s, len,
	     "{\"type\": \"object\", \"properties\": {%s}, \"required\": [%s], \"additionalProperties\": false}",
	     _tmesh_mcp_tools[i].properties, _tmesh_mcp_tools[i].required);
    schema = cJSON_Parse(s);
    free(s);
    if (schema == NULL) {
      fprintf(stderr, "%s --mcp: bad schema for %s\n", argv0, _tmesh_mcp_tools[i].name);
      schema = cJSON_Parse("{\"type\": \"object\"}");
    }
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", _tmesh_mcp_tools[i].name);
    cJSON_AddStringToObject(o, "description", _tmesh_mcp_tools[i].description);
    cJSON_AddItemToObject(o, "inputSchema", schema);
    cJSON_AddItemToArray(list, o);
  }

  /* the commands which have no tool of their own yet: */
  for (i = 0; tmesh_ctl_command(i, &name, &params); i++) {
    if (_tmesh_mcp_covered(name)) {
      continue;
    }
    o = cJSON_CreateObject();
    snprintf(desc, sizeof(desc), "The control socket's %s command (see the tme manual).", name);
    cJSON_AddStringToObject(o, "name", name);
    cJSON_AddStringToObject(o, "description", desc);
    schema = cJSON_AddObjectToObject(o, "inputSchema");
    cJSON_AddStringToObject(schema, "type", "object");
    props = cJSON_AddObjectToObject(schema, "properties");
    for (p = params; *p != '\0'; p += wlen) {
      for (; *p == ' '; p++);
      wlen = strcspn(p, " ");
      if (wlen > 0) {
	snprintf(desc, sizeof(desc), "%.*s", (int) wlen, p);
	cJSON_AddObjectToObject(props, desc);
      }
    }
    cJSON_AddBoolToObject(schema, "additionalProperties", FALSE);
    cJSON_AddItemToArray(list, o);
  }
  return (list);
}

static void
_tmesh_mcp_handle(const char *line)
{
  cJSON *msg, *id, *method, *params, *r, *version, *caps, *info, *name, *args;
  const struct _tmesh_mcp_tool *tool;
  const char *m, *v, *cmd_name, *cmd_params;
  int known, i;

  msg = cJSON_Parse(line);
  if (!cJSON_IsObject(msg)) {
    _tmesh_mcp_send_error(NULL, -32700, "not a JSON object");
    cJSON_Delete(msg);
    return;
  }
  id = cJSON_GetObjectItemCaseSensitive(msg, "id");
  method = cJSON_GetObjectItemCaseSensitive(msg, "method");
  params = cJSON_GetObjectItemCaseSensitive(msg, "params");

  /* notifications (such as notifications/initialized), and responses,
     need no answer: */
  if (id == NULL || !cJSON_IsString(method)) {
    cJSON_Delete(msg);
    return;
  }

  m = method->valuestring;
  if (!strcmp(m, "initialize")) {
    version = cJSON_GetObjectItemCaseSensitive(params, "protocolVersion");
    v = TMESH_MCP_PROTOCOL_VERSION;
    if (cJSON_IsString(version)
	&& (!strcmp(version->valuestring, "2024-11-05")
	    || !strcmp(version->valuestring, "2025-03-26"))) {
      v = version->valuestring;
    }
    r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "protocolVersion", v);
    caps = cJSON_AddObjectToObject(r, "capabilities");
    cJSON_AddObjectToObject(caps, "tools");
    info = cJSON_AddObjectToObject(r, "serverInfo");
    cJSON_AddStringToObject(info, "name", "tmesh");
    cJSON_AddStringToObject(info, "version", PACKAGE_VERSION);
    cJSON_AddStringToObject(r, "instructions", _tmesh_mcp_instructions);
    _tmesh_mcp_send_result(id, r);
  }
  else if (!strcmp(m, "ping")) {
    _tmesh_mcp_send_result(id, cJSON_CreateObject());
  }
  else if (!strcmp(m, "tools/list")) {
    r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "tools", _tmesh_mcp_tool_list());
    _tmesh_mcp_send_result(id, r);
  }
  else if (!strcmp(m, "tools/call")) {
    name = cJSON_GetObjectItemCaseSensitive(params, "name");
    args = cJSON_GetObjectItemCaseSensitive(params, "arguments");
    tool = (cJSON_IsString(name) ? _tmesh_mcp_find_tool(name->valuestring) : NULL);
    known = (tool != NULL);

    /* a command which has no tool of its own: */
    for (i = 0; !known && cJSON_IsString(name) && tmesh_ctl_command(i, &cmd_name, &cmd_params); i++) {
      known = (!strcmp(cmd_name, name->valuestring) && !_tmesh_mcp_covered(cmd_name));
    }
    if (!known) {
      _tmesh_mcp_send_error(id, -32602, "unknown tool");
    }
    else {
      _tmesh_mcp_send_result(id, _tmesh_mcp_call(tool, (tool != NULL ? tool->cmd : name->valuestring), args));
    }
  }
  else {
    _tmesh_mcp_send_error(id, -32601, "unknown method");
  }
  cJSON_Delete(msg);
}

/* this reads one line from standard input, malloced, without the
   newline, returning NULL at the end: */
static char *
_tmesh_mcp_read_line(void)
{
  size_t len, size;
  char *buf;
  int c;

  len = 0;
  size = 4096;
  buf = malloc(size);
  while ((c = getchar()) != EOF && c != '\n') {
    if (len + 1 >= size) {
      size *= 2;
      buf = realloc(buf, size);
    }
    buf[len++] = c;
  }
  if (c == EOF && len == 0) {
    free(buf);
    return (NULL);
  }
  if (len > 0 && buf[len - 1] == '\r') {
    len--;
  }
  buf[len] = '\0';
  return (buf);
}

/* this is tmesh --mcp unix:PATH|tcp:PORT: */
int
tmesh_mcp(int argc, char **argv)
{
  char *line;

  if (argc != 3
      || (strncmp(argv[2], "unix:", 5)
	  && strncmp(argv[2], "tcp:", 4))) {
    fprintf(stderr,
	    "usage: %s --mcp unix:PATH|tcp:PORT\n"
	    "An MCP server for AI agents, which uses the control socket of a tmesh\n"
	    "started with --control unix:PATH|tcp:PORT.  See the tme manual.\n",
	    argv0);
    return (2);
  }
  _tmesh_mcp_spec = argv[2];
  while ((line = _tmesh_mcp_read_line()) != NULL) {
    if (line[0] != '\0') {
      _tmesh_mcp_handle(line);
    }
    free(line);
  }
  return (0);
}

#else  /* WIN32 */

int
tmesh_mcp(int argc, char **argv)
{
  fprintf(stderr, "%s: --mcp isn't supported on Windows\n", argv0);
  return (2);
}

#endif /* WIN32 */
