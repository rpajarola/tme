/* tmesh/tmesh-config.h - machine configurations, for the machine window: */

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

#ifndef _TMESH_CONFIG_H
#define _TMESH_CONFIG_H

/* a machine's configuration is the tmesh script that makes it (its
   tme.cfg), kept as lines.  the machine window edits the lines that
   it knows - the memory, the ROM and other files, the SCSI drives,
   the display and the Ethernet connection - and keeps every other
   line, and every comment, as it is: */

/* a line of a script: */
struct tmesh_config_line {

  /* the line, without its newline: */
  char *tmesh_config_line_text;

  /* its words, without any comment.  a colon is a word of its own: */
  int tmesh_config_line_argc;
  char **tmesh_config_line_argv;
};

/* a script: */
struct tmesh_config {

  /* the script, as an absolute path, and its folder, or NULL if it
     hasn't been saved yet: */
  char *tmesh_config_path;
  char *tmesh_config_dir;

  /* the lines: */
  int tmesh_config_n_lines;
  struct tmesh_config_line *tmesh_config_lines;
};

/* the kinds of machines that the window knows: */
struct tmesh_config_kind {

  /* the kind's name, and the element that makes the machine, with
     its first argument if that tells kinds apart: */
  const char *tmesh_config_kind_label;
  const char *tmesh_config_kind_module;
  const char *tmesh_config_kind_arg;

  /* the size of a new disk image, in MB, and the type of a new disk: */
  int tmesh_config_kind_disk_mb;
  const char *tmesh_config_kind_disk_type;

  /* the SCSI IDs of a first disk, tape and CD-ROM: */
  int tmesh_config_kind_disk_id;
  int tmesh_config_kind_tape_id;
  int tmesh_config_kind_cdrom_id;

  /* nonzero if the CPU is a SPARC (which has idle types): */
  int tmesh_config_kind_sparc;

  /* the framebuffers that can be chosen, or NULL: */
  const struct tmesh_config_fb *tmesh_config_kind_fbs;

  /* a new machine's script, and what it needs and runs: */
  const char *tmesh_config_kind_script;
  const char *tmesh_config_kind_description;
};

/* a framebuffer that can be chosen, as the lines that make it (with
   any FCode ROM), which the display is connected to: */
struct tmesh_config_fb {
  const char *tmesh_config_fb_label;
  const char *tmesh_config_fb_element;
  const char *tmesh_config_fb_lines;
};

/* the kinds of files that a script names: */
#define TMESH_CONFIG_FILE_ROM		(0)	/* a ROM image, which the user supplies */
#define TMESH_CONFIG_FILE_STATE		(1)	/* made by the machine if missing */
#define TMESH_CONFIG_FILE_MACROS	(2)	/* keyboard macros, written if missing */
#define TMESH_CONFIG_FILE_OTHER		(3)

/* a file that a script names, other than a drive's: */
struct tmesh_config_file {
  int tmesh_config_file_line;
  int tmesh_config_file_arg;
  int tmesh_config_file_kind;
  char tmesh_config_file_label[64];
};

/* the kinds of SCSI drives: */
#define TMESH_CONFIG_DRIVE_DISK		(0)
#define TMESH_CONFIG_DRIVE_CDROM	(1)
#define TMESH_CONFIG_DRIVE_TAPE		(2)

/* a SCSI drive: */
struct tmesh_config_drive {

  /* the drive's line, its image's line (or -1), and for a tape, the
     line that loads a tape at the start (or -1): */
  int tmesh_config_drive_line;
  int tmesh_config_drive_host_line;
  int tmesh_config_drive_load_line;

  /* the kind of drive, its SCSI ID and type, and its image (or ""): */
  int tmesh_config_drive_kind;
  int tmesh_config_drive_id;
  const char *tmesh_config_drive_type;
  const char *tmesh_config_drive_file;
  int tmesh_config_drive_read_only;
};

/* how an Ethernet interface is connected: */
#define TMESH_CONFIG_NET_NONE		(0)
#define TMESH_CONFIG_NET_UDP		(1)
#define TMESH_CONFIG_NET_BPF		(2)
#define TMESH_CONFIG_NET_TAP		(3)
#define TMESH_CONFIG_NET_OTHER		(4)

/* the Ethernet interface: */
struct tmesh_config_net {

  /* the interface's line and element, or -1 and NULL if there is
     none, and its connection's line, or -1: */
  int tmesh_config_net_line;
  const char *tmesh_config_net_element;
  int tmesh_config_net_host_line;
  int tmesh_config_net_type;
};

/* scripts: */
struct tmesh_config *tmesh_config_parse _TME_P((const char *, const char *));
struct tmesh_config *tmesh_config_load _TME_P((const char *, char **));
int tmesh_config_save _TME_P((struct tmesh_config *, const char *, char **));
char *tmesh_config_text _TME_P((const struct tmesh_config *));
struct tmesh_config *tmesh_config_copy _TME_P((const struct tmesh_config *));
void tmesh_config_free _TME_P((struct tmesh_config *));
int tmesh_config_changed _TME_P((const struct tmesh_config *));

/* lines and words: */
const char *tmesh_config_word _TME_P((const struct tmesh_config *, int, int));
void tmesh_config_set_word _TME_P((struct tmesh_config *, int, int, const char *));
void tmesh_config_insert _TME_P((struct tmesh_config *, int, const char *));
void tmesh_config_delete _TME_P((struct tmesh_config *, int));
int tmesh_config_find _TME_P((const struct tmesh_config *, const char *));
const char *tmesh_config_module _TME_P((const struct tmesh_config *, int));
const char *tmesh_config_arg _TME_P((const struct tmesh_config *, int, const char *));

/* what a script makes: */
const struct tmesh_config_kind *tmesh_config_kinds _TME_P((int *));
const struct tmesh_config_kind *tmesh_config_kind _TME_P((const struct tmesh_config *));
const char *tmesh_config_name _TME_P((const struct tmesh_config *));
void tmesh_config_set_name _TME_P((struct tmesh_config *, const char *));
char *tmesh_config_summary _TME_P((const struct tmesh_config *));

/* memory: */
int tmesh_config_ram _TME_P((const struct tmesh_config *, int **));
int tmesh_config_ram_mb _TME_P((const struct tmesh_config *, int));
void tmesh_config_set_ram_mb _TME_P((struct tmesh_config *, int, int));

/* files: */
int tmesh_config_files _TME_P((const struct tmesh_config *, struct tmesh_config_file **));
char *tmesh_config_resolve _TME_P((const struct tmesh_config *, const char *));
char *tmesh_config_shorten _TME_P((const struct tmesh_config *, const char *));

/* drives: */
int tmesh_config_drives _TME_P((const struct tmesh_config *, struct tmesh_config_drive **));
void tmesh_config_drive_set_id _TME_P((struct tmesh_config *, const struct tmesh_config_drive *, int));
void tmesh_config_drive_set_type _TME_P((struct tmesh_config *, const struct tmesh_config_drive *, const char *));
void tmesh_config_drive_set_file _TME_P((struct tmesh_config *, const struct tmesh_config_drive *, const char *));
int tmesh_config_drive_add _TME_P((struct tmesh_config *, int));
void tmesh_config_drive_remove _TME_P((struct tmesh_config *, const struct tmesh_config_drive *));
const char * const *tmesh_config_drive_types _TME_P((int, int *));

/* the display: */
const char *tmesh_config_display _TME_P((const struct tmesh_config *));
void tmesh_config_set_display _TME_P((struct tmesh_config *, const char *));
int tmesh_config_fb _TME_P((const struct tmesh_config *));
void tmesh_config_set_fb _TME_P((struct tmesh_config *, int));

/* the Ethernet interface: */
void tmesh_config_net _TME_P((const struct tmesh_config *, struct tmesh_config_net *));
void tmesh_config_set_net _TME_P((struct tmesh_config *, int));
const char *tmesh_config_net_arg _TME_P((const struct tmesh_config *, const char *, int));
void tmesh_config_set_net_arg _TME_P((struct tmesh_config *, const char *, int, const char *));

/* the SPARC idle type: */
const char *tmesh_config_idle_type _TME_P((const struct tmesh_config *));
void tmesh_config_set_idle_type _TME_P((struct tmesh_config *, const char *));

/* what's wrong, if anything, for starting the machine: */
char *tmesh_config_check _TME_P((const struct tmesh_config *));

/* files and folders: */
int tmesh_config_make_dirs _TME_P((const char *));
int tmesh_config_create_disk _TME_P((const char *, long long, char **));
int tmesh_config_copy_file _TME_P((const char *, const char *, char **));
int tmesh_config_write_macros _TME_P((const char *, char **));

/* the window's state, kept between runs: */
const char *tmesh_config_machines_dir _TME_P((void));
int tmesh_config_recent _TME_P((char ***));
void tmesh_config_add_recent _TME_P((const char *));
void tmesh_config_remove_recent _TME_P((const char *));
const char *tmesh_config_last_dir _TME_P((void));
void tmesh_config_set_last_dir _TME_P((const char *));
int tmesh_config_rom_dirs _TME_P((char ***));
void tmesh_config_set_rom_dirs _TME_P((int, char **));
char *tmesh_config_find_rom _TME_P((const char *));

/* lists of strings: */
void tmesh_config_list_add _TME_P((int *, char ***, const char *));
void tmesh_config_list_free _TME_P((int, char **));

#endif /* !_TMESH_CONFIG_H */
