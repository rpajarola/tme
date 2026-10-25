/* tmesh/tmesh-config.c - machine configurations, for the machine window: */

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

/* a machine's configuration is its tmesh script, which this reads
   into lines and words the way tmesh does (see tmesh-input.y): words
   are letters, digits, and the characters . / - _, or anything in
   quotation marks; # starts a comment; and every other character is
   a word of its own.  a line is changed by changing its words, which
   writes the line again; every other line stays as it was.

   the window's own state is kept in tme's folder for such things:

	macOS:   ~/Library/Application Support/tme/
	others:  $XDG_CONFIG_HOME/tme/ (~/.config/tme/)

   "machines" there lists the scripts in the window's list, one per
   line, most recently used first.  "settings" has a line "last-dir
   FOLDER" for the folder last used in a file chooser, and a line
   "rom-dir FOLDER" for each folder that ROM images are looked for
   in.  new machines are saved in ~/TME by default: */

#include <tme/common.h>

/* includes: */
#include "tmesh-config.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* the most machines in the list: */
#define TMESH_CONFIG_RECENT_MAX	(30)

/* the keyboard macros that a new machine gets (from
   machine/sun/my-sun-macros.txt): */
static const char _tmesh_config_macros[] = "\
## keyboard macros, for keys found on Sun keyboards that probably\n\
## aren't on yours.  edit this file to match your keyboard.\n\
\n\
# these macros generate L1 through L10:\n\
Control_R F1 = L1\n\
Control_R F2 = L2\n\
Control_R F3 = L3\n\
Control_R F4 = L4\n\
Control_R F5 = L5\n\
Control_R F6 = L6\n\
Control_R F7 = L7\n\
Control_R F8 = L8\n\
Control_R F9 = L9\n\
Control_R F10 = L10\n\
\n\
# F1 generates L1, which is Stop, so that F1-A is Stop-A, to\n\
# interrupt the boot or get to the PROM monitor.  Mac keyboards have\n\
# no right Control for the macro above.  on a Mac, turn on \"Use F1,\n\
# F2, etc. keys as standard function keys\" in the keyboard settings\n\
# and use plain F1-A; fn-F1-A doesn't work, because macOS takes fn-A\n\
# for itself:\n\
F1 = L1\n\
";

/* the framebuffers of a SPARCstation 2: */
static const struct tmesh_config_fb _tmesh_config_fbs_sun4c[] = {
  { "cgthree (8-bit color)", "cgthree0",
    "cgthree0 at sbus0 slot 1 offset 0x0 ipl 7: tme/bus/sbus/cgthree\n"
    "cgthreerom0 at sbus0 slot 1 offset 0x0: tme/host/posix/memory rom \"SUNW,501-1415.bin\"\n" },
  { "bwtwo (monochrome)", "bwtwo0",
    "bwtwo0 at sbus0 slot 2 offset 0x0 ipl 7: tme/bus/sbus/bwtwo\n"
    "bwtworom0 at sbus0 slot 2 offset 0x0: tme/host/posix/memory rom \"SUNW,501-1561.bin\"\n" },
  { NULL, NULL, NULL }
};

/* the kinds of machines, with a new machine's script for each: */
static const struct tmesh_config_kind _tmesh_config_kinds[] = {

  { "Sun-2/120 (Multibus)", "tme/machine/sun2", "multibus",
    300, "tme-scsi-1", 0, 4, 6, FALSE, NULL,
    "## name: Sun-2/120\n"
    "## a Sun-2/120, with a 68010, Multibus SCSI and Ethernet boards, a\n"
    "## monochrome screen, a disk, and a tape drive.  see\n"
    "## machine/sun2/SUN2-MULTIBUS in the tme sources for more.\n"
    "\n"
    "## the board, its memory and its PROM:\n"
    "mainbus0: tme/machine/sun2 multibus idprom.bin\n"
    "cpu0 at mainbus0: tme/ic/m68010\n"
    "obio0 at mainbus0 obio: tme/generic/bus size 8MB\n"
    "obmem0 at mainbus0 obmem: tme/generic/bus size 16MB\n"
    "ram0 at obmem0 addr 0x0: tme/host/posix/memory ram 4MB\n"
    "rom0 at obmem0 addr 0xef0000: tme/host/posix/memory rom sun2-multi-rev-R.bin\n"
    "rom0 at obmem0 addr 0xef8000\n"
    "clock0 at obio0 addr 0x2800: tme/machine/sun2/clock\n"
    "tod0 at obio0 addr 0x3800: tme/machine/sun2/tod\n"
    "zs0 at obio0 addr 0x2000 ipl 6: tme/machine/sun2/zs\n"
    "mbio0 at mainbus0 mbio: tme/generic/bus size 8MB\n"
    "mbmem0: tme/generic/bus size 8MB\n"
    "mainbus0 mbmem at mbmem0 addr 0x00000\n"
    "\n"
    "## the SCSI board:\n"
    "sc0 at mbmem0 addr 0x80000 ipl 2: tme/bus/multibus/sun-sc\n"
    "scsibus0 at sc0: tme/scsi/bus\n"
    "\n"
    "## the Ethernet board:\n"
    "mie0: tme/bus/multibus/sun-mie\n"
    "mie0 csr at mbmem0 addr 0x88000 ipl 3\n"
    "mie0 memory 0xa0000 at mbmem0 addr 0xa0000\n"
    "ie0 at mie0: tme/ic/i82586\n"
    "\n"
    "## the screen, keyboard and mouse:\n"
    "bwtwo0 at obmem0 addr 0x700000 ipl 4: tme/machine/sun2/bwtwo type multibus\n"
    "zs1 at obmem0 addr 0x780000 ipl 6: tme/machine/sun2/zs\n"
    "kbd0 at zs1 channel A: tme/serial/keyboard type sun-type-2 macros sun-macros.txt rate 20\n"
    "ms0 at zs1 channel B: tme/serial/mouse type mousesystems-5\n"
    "display0: tme/host/sdl/display\n"
    "display0 at bwtwo0\n"
    "display0 at kbd0\n"
    "display0 at ms0\n"
    "\n"
    "## the disk and tape drives:\n"
    "sd0 at scsibus0: tme/scsi/disk id 0 type tme-scsi-1\n"
    "disk0 at sd0: tme/host/posix/disk file disk0.img\n"
    "st0 at scsibus0: tme/scsi/tape id 4 type emulex-mt02 vendor EMULEX product \"MT-02 QIC\"\n"
    "tape0 at st0: tme/host/posix/tape\n"
    "\n"
    "## power up the machine:\n"
    "command mainbus0 power up\n",
    "Runs SunOS 2.0 and 3.x, and NetBSD/sun2.  It needs the Sun-2 "
    "Multibus PROM image (sun2-multi-rev-R.bin), which you choose "
    "under Files.  To install SunOS, load the installation tapes from "
    "the Media menu, and boot from the tape with \"b st()\"; label a "
    "new disk first with tme-sunlabel (SunOS 2.0 needs one of 512MB or "
    "less)." },

  { "Sun-3/160 (Carrera)", "tme/machine/sun3", NULL,
    600, "acb4000", 0, 4, 6, FALSE, NULL,
    "## name: Sun-3/160\n"
    "## a Sun-3/160 (\"Carrera\"), with a 68020 and 68881, a VME SCSI\n"
    "## board, the on-board Ethernet, a monochrome screen, a disk, and a\n"
    "## tape drive.  see machine/sun3/SUN3-CARRERA in the tme sources\n"
    "## for more.\n"
    "\n"
    "## the board, its memory and its PROM:\n"
    "mainbus0: tme/machine/sun3 idprom.bin\n"
    "cpu0 at mainbus0: tme/ic/m68020 fpu-type m68881 fpu-compliance unknown fpu-incomplete line-f\n"
    "obio0 at mainbus0 obio: tme/generic/bus size 16MB\n"
    "obmem0 at mainbus0 obmem: tme/generic/bus size 4GB\n"
    "ram0 at obmem0 addr 0x0: tme/host/posix/memory ram 16MB\n"
    "rom0 at obmem0 addr 0x0fef0000: tme/host/posix/memory rom sun3-carrera-rev-3.0.bin\n"
    "zs0 at obio0 addr 0x020000 ipl 6: tme/machine/sun3/zs\n"
    "zs1 at obio0 addr 0x000000 ipl 6: tme/machine/sun3/zs\n"
    "eeprom0 at obio0 addr 0x040000: tme/machine/sun3/eeprom eeprom.bin\n"
    "clock0 at obio0 addr 0x060000: tme/machine/sun3/clock\n"
    "mainbus0 memerr at obio0 addr 0x080000 ipl 7\n"
    "mainbus0 intreg at obio0 addr 0x0A0000\n"
    "\n"
    "## the on-board Ethernet:\n"
    "obie0: tme/machine/sun3/obie\n"
    "obie0 csr at obio0 addr 0x0c0000 ipl 3\n"
    "obie0 memory at mainbus0\n"
    "ie0 at obie0: tme/ic/i82586\n"
    "\n"
    "## the screen, keyboard and mouse:\n"
    "kbd0 at zs1 channel A: tme/serial/keyboard type sun-type-3 macros sun-macros.txt rate 20\n"
    "ms0 at zs1 channel B: tme/serial/mouse type mousesystems-5\n"
    "bwtwo0 at obmem0 addr 0xff000000: tme/machine/sun3/bwtwo type old-onboard\n"
    "display0: tme/host/sdl/display\n"
    "display0 at bwtwo0\n"
    "display0 at kbd0\n"
    "display0 at ms0\n"
    "\n"
    "## the VME bus, and the SCSI board:\n"
    "vme0: tme/generic/bus size 4GB\n"
    "mainbus0 vme at vme0 addr 0x00000\n"
    "sc0 at vme0 addr 0xff200000 ipl 2: tme/bus/multibus/sun-sc vme\n"
    "scsibus0 at sc0: tme/scsi/bus\n"
    "\n"
    "## the disk and tape drives:\n"
    "sd0 at scsibus0: tme/scsi/disk id 0 type acb4000\n"
    "disk0 at sd0: tme/host/posix/disk file disk0.img\n"
    "st0 at scsibus0: tme/scsi/tape id 4 type emulex-mt02 vendor EMULEX product \"MT-02 QIC\"\n"
    "tape0 at st0: tme/host/posix/tape\n"
    "\n"
    "## power up the machine:\n"
    "command mainbus0 power up\n",
    "Runs SunOS 3.x and 4.0, and NetBSD/sun3.  It needs the Sun-3 "
    "Carrera PROM image (sun3-carrera-rev-3.0.bin), which you choose "
    "under Files.  The disk is an Adaptec ACB-4000, which SunOS "
    "formats with its diag program; to install SunOS, load the "
    "installation tapes from the Media menu, and boot from the tape "
    "with \"b st()\"." },

  { "SPARCstation 2 (Sun 4/75)", "tme/machine/sun4", NULL,
    1024, "tme-scsi-1", 3, 4, 6, TRUE, _tmesh_config_fbs_sun4c,
    "## name: SPARCstation 2\n"
    "## a SPARCstation 2 (Sun 4/75), with a color screen, a disk, and a\n"
    "## CD-ROM drive.  see machine/sun4/SUN4-75 in the tme sources for\n"
    "## more.\n"
    "\n"
    "## the board, its memory and its PROM:\n"
    "board0: tme/machine/sun4 name Calvin\n"
    "cpu0 at board0: tme/ic/cy7c601 fpu-type tms390-c602A fpu-compliance partial fpu-incomplete trap\n"
    "sbus0: tme/generic/bus size 4GB slot-addr 0xf8000000 slot-size 32MB slot 0 slot 1 slot 2 slot 3\n"
    "sbus0 controller at board0 sbus\n"
    "alias mainbus0 sbus0\n"
    "ram0 at mainbus0 addr 0x0: tme/host/posix/memory ram 16MB\n"
    "ram1 at mainbus0 addr 0x08000000: tme/host/posix/memory ram 32MB\n"
    "ram2 at mainbus0 addr 0x0a000000: tme/host/posix/memory ram 32MB\n"
    "rom0 at mainbus0 addr 0xf6000000: tme/host/posix/memory rom sun4-75-rev-2.9.bin\n"
    "zs0 at mainbus0 addr 0xf1000000 ipl 12: tme/machine/sun4/zs\n"
    "zs1 at mainbus0 addr 0xf0000000 ipl 12: tme/machine/sun4/zs\n"
    "nvram0 at mainbus0 addr 0xf2000000: tme/machine/sun4/nvram nvram.bin\n"
    "clock0 at mainbus0 addr 0xf20007f8: tme/machine/sun4/clock type tme/ic/mk48t02\n"
    "board0 timer at mainbus0 addr 0xf3000000\n"
    "board0 memerr at mainbus0 addr 0xf4000000\n"
    "board0 intreg at mainbus0 addr 0xf5000000\n"
    "fdc0 at mainbus0 addr 0xf7200000 ipl 11: tme/machine/sun4/fdc type tme/ic/i82072\n"
    "audioamd0 at mainbus0 addr 0xf7201000 ipl 13: tme/ic/am7930\n"
    "board0 auxreg at mainbus0 addr 0xf7400003\n"
    "\n"
    "## the keyboard and mouse, and ttya:\n"
    "kbd0 at zs1 channel A: tme/serial/keyboard type sun-type-4-us macros sun-macros.txt rate 20\n"
    "ms0 at zs1 channel B: tme/serial/mouse type mousesystems-5\n"
    "sink1 at zs0 channel A: tme/serial/sink\n"
    "\n"
    "## the SCSI and Ethernet on the board:\n"
    "esp0 at sbus0 slot 0 offset 0x800000 ipl 3: tme/ic/lsi64854 revision \"1+\" channel scsi\n"
    "esp0 dma at sbus0 slot 0 offset 0x400000\n"
    "ncr0 at esp0 master: tme/ic/ncr53c9x variant esp100a\n"
    "scsibus0 at ncr0: tme/scsi/bus\n"
    "le0 at sbus0 slot 0 offset 0xc00000 ipl 5 dma-offset 0xff000000: tme/ic/am7990\n"
    "\n"
    "## the screen:\n"
    "cgthree0 at sbus0 slot 1 offset 0x0 ipl 7: tme/bus/sbus/cgthree\n"
    "cgthreerom0 at sbus0 slot 1 offset 0x0: tme/host/posix/memory rom \"SUNW,501-1415.bin\"\n"
    "display0: tme/host/sdl/display\n"
    "display0 at cgthree0\n"
    "display0 at kbd0\n"
    "display0 at ms0\n"
    "\n"
    "## the disk and CD-ROM drives:\n"
    "sd0 at scsibus0: tme/scsi/disk id 3 type tme-scsi-1\n"
    "disk0 at sd0: tme/host/posix/disk file disk0.img\n"
    "cd0 at scsibus0: tme/scsi/cdrom id 6 type tme-scsi-1\n"
    "\n"
    "## power up the machine:\n"
    "command cpu0 idle-type netbsd32-type-1\n"
    "command board0 power up\n",
    "Runs SunOS 4.1, Solaris 2 up to 2.5, NetBSD/sparc and OpenBSD/sparc, "
    "and Sprite.  It needs the SPARCstation 2 PROM image "
    "(sun4-75-rev-2.9.bin), and the framebuffer's FCode ROM image "
    "(SUNW,501-1415.bin for the cgthree, or SUNW,501-1561.bin for the "
    "bwtwo), which you choose under Files.  To install, choose an "
    "installation CD-ROM image under Drives, and at the ok prompt, "
    "boot from it with \"boot cdrom\" (or \"b sd(0,6,2)\" for SunOS)." },
};

/* the SCSI drive types: */
static const char * const _tmesh_config_disk_types[] = { "tme-scsi-1", "acb4000" };
static const char * const _tmesh_config_cdrom_types[] = { "tme-scsi-1" };
static const char * const _tmesh_config_tape_types[] = { "tme-scsi-1", "emulex-mt02" };

/* the drives' elements, and their images' elements: */
static const char * const _tmesh_config_drive_modules[] = {
  "tme/scsi/disk", "tme/scsi/cdrom", "tme/scsi/tape"
};
static const char * const _tmesh_config_drive_prefixes[] = { "sd", "cd", "st" };
static const char * const _tmesh_config_host_prefixes[] = { "disk", "cdrom", "tape" };

/* the elements that connect an Ethernet interface to the host: */
static const char * const _tmesh_config_net_modules[] = {
  NULL, "tme/host/udp/eth", "tme/host/bsd/bpf", "tme/host/tun/tap"
};
static const char * const _tmesh_config_net_prefixes[] = { NULL, "link", "bpf", "tap" };

/* this returns nonzero if a character is part of a word: */
static int
_tmesh_config_word_char(int c)
{
  return (c < 0x80
	  && (isalnum(c)
	      || c == '.'
	      || c == '/'
	      || c == '-'
	      || c == '_'));
}

/* this adds a string to a list of strings: */
void
tmesh_config_list_add(int *_n, char ***_list, const char *string)
{
  *_list = tme_renew(char *, *_list, *_n + 2);
  (*_list)[(*_n)++] = tme_strdup(string);
  (*_list)[*_n] = NULL;
}

/* this frees a list of strings: */
void
tmesh_config_list_free(int n, char **list)
{
  int i;

  for (i = 0; i < n; i++) {
    tme_free(list[i]);
  }
  if (list != NULL) {
    tme_free(list);
  }
}

/* this splits a line into words: */
static void
_tmesh_config_split(struct tmesh_config_line *line)
{
  const unsigned char *p;
  char *word;
  size_t length;
  int in_quotes, have_word;

  line->tmesh_config_line_argc = 0;
  line->tmesh_config_line_argv = tme_new(char *, 1);
  line->tmesh_config_line_argv[0] = NULL;
  word = tme_new(char, strlen(line->tmesh_config_line_text) + 1);
  length = 0;
  in_quotes = FALSE;
  have_word = FALSE;
  for (p = (const unsigned char *) line->tmesh_config_line_text;; p++) {

    /* quotation marks start and end a quoted part of a word: */
    if (*p == '"') {
      in_quotes = !in_quotes;
      have_word = TRUE;
      continue;
    }

    /* collect the characters of a word: */
    if (*p != '\0'
	&& (in_quotes || _tmesh_config_word_char(*p))) {
      word[length++] = *p;
      have_word = TRUE;
      continue;
    }

    /* anything else ends any word: */
    if (have_word) {
      word[length] = '\0';
      tmesh_config_list_add(&line->tmesh_config_line_argc,
			    &line->tmesh_config_line_argv,
			    word);
      length = 0;
      have_word = FALSE;
    }

    /* the end of the line, or a comment, ends the words: */
    if (*p == '\0' || *p == '#') {
      break;
    }

    /* any other character but a space is a word of its own: */
    if (!isspace(*p)) {
      word[0] = *p;
      word[1] = '\0';
      tmesh_config_list_add(&line->tmesh_config_line_argc,
			    &line->tmesh_config_line_argv,
			    word);
    }
  }
  tme_free(word);
}

/* this sets a line's text, and splits it into words: */
static void
_tmesh_config_line_set(struct tmesh_config_line *line, const char *text)
{
  if (line->tmesh_config_line_text != NULL) {
    tme_free(line->tmesh_config_line_text);
    tmesh_config_list_free(line->tmesh_config_line_argc,
			   line->tmesh_config_line_argv);
  }
  line->tmesh_config_line_text = tme_strdup(text);
  _tmesh_config_split(line);
}

/* this writes a line again from its words, keeping any comment: */
static void
_tmesh_config_line_join(struct tmesh_config_line *line)
{
  const char *comment, *p, *word;
  char *text;
  size_t size, length;
  int in_quotes, arg_i, quote;

  /* find any comment: */
  comment = NULL;
  in_quotes = FALSE;
  for (p = line->tmesh_config_line_text; *p != '\0'; p++) {
    if (*p == '"') {
      in_quotes = !in_quotes;
    }
    else if (*p == '#' && !in_quotes) {
      comment = p;
      break;
    }
  }

  /* join the words: */
  size = 2 + (comment != NULL ? strlen(comment) : 0);
  for (arg_i = 0; arg_i < line->tmesh_config_line_argc; arg_i++) {
    size += strlen(line->tmesh_config_line_argv[arg_i]) + 3;
  }
  text = tme_new(char, size);
  length = 0;
  for (arg_i = 0; arg_i < line->tmesh_config_line_argc; arg_i++) {
    word = line->tmesh_config_line_argv[arg_i];

    /* a colon follows the word before it; other words follow a space: */
    if (arg_i > 0 && strcmp(word, ":") != 0) {
      text[length++] = ' ';
    }

    /* a word that isn't all word characters is quoted, except for a
       colon: */
    quote = (*word == '\0');
    for (p = word; *p != '\0'; p++) {
      if (!_tmesh_config_word_char((unsigned char) *p)) {
	quote = TRUE;
      }
    }
    if (strcmp(word, ":") == 0) {
      quote = FALSE;
    }
    if (quote) {
      text[length++] = '"';
    }
    strcpy(text + length, word);
    length += strlen(word);
    if (quote) {
      text[length++] = '"';
    }
  }
  if (comment != NULL) {
    if (length > 0) {
      text[length++] = ' ';
    }
    strcpy(text + length, comment);
    length += strlen(comment);
  }
  text[length] = '\0';
  tme_free(line->tmesh_config_line_text);
  line->tmesh_config_line_text = text;
}

/* this parses a script.  the path is where it is, or NULL: */
struct tmesh_config *
tmesh_config_parse(const char *text, const char *path)
{
  struct tmesh_config *config;
  const char *end;
  char *line, *slash;
  size_t length;

  config = tme_new0(struct tmesh_config, 1);
  if (path != NULL) {
    config->tmesh_config_path = tme_strdup(path);
    config->tmesh_config_dir = tme_strdup(path);
    slash = strrchr(config->tmesh_config_dir, '/');
    if (slash == NULL) {
      strcpy(config->tmesh_config_dir, ".");
    }
    else {
      slash[slash == config->tmesh_config_dir] = '\0';
    }
  }
  while (*text != '\0') {
    end = strchr(text, '\n');
    length = (end != NULL ? (size_t) (end - text) : strlen(text));
    line = tme_new(char, length + 1);
    memcpy(line, text, length);
    line[length] = '\0';
    if (length > 0 && line[length - 1] == '\r') {
      line[length - 1] = '\0';
    }
    tmesh_config_insert(config, config->tmesh_config_n_lines, line);
    tme_free(line);
    text += length + (end != NULL);
  }
  return (config);
}

/* this reads a script: */
struct tmesh_config *
tmesh_config_load(const char *path, char **_err)
{
  struct tmesh_config *config;
  FILE *file;
  char *text, *abs_path, message[1200];
  long size;

  file = fopen(path, "r");
  if (file == NULL) {
    snprintf(message, sizeof(message), "%s: %s", path, strerror(errno));
    *_err = tme_strdup(message);
    return (NULL);
  }
  fseek(file, 0, SEEK_END);
  size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size < 0 || size > 4 * 1024 * 1024) {
    fclose(file);
    snprintf(message, sizeof(message), "%s: %s", path, "not a tmesh script");
    *_err = tme_strdup(message);
    return (NULL);
  }
  text = tme_new(char, size + 1);
  size = fread(text, 1, size, file);
  text[size] = '\0';
  fclose(file);
  if (strlen(text) != (size_t) size) {
    tme_free(text);
    snprintf(message, sizeof(message), "%s: %s", path, "not a tmesh script");
    *_err = tme_strdup(message);
    return (NULL);
  }

  abs_path = realpath(path, NULL);
  config = tmesh_config_parse(text, (abs_path != NULL ? abs_path : path));
  if (abs_path != NULL) {
    free(abs_path);
  }
  tme_free(text);
  return (config);
}

/* this returns a script's text: */
char *
tmesh_config_text(const struct tmesh_config *config)
{
  char *text;
  size_t size;
  int line_i;

  size = 1;
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    size += strlen(config->tmesh_config_lines[line_i].tmesh_config_line_text) + 1;
  }
  text = tme_new(char, size);
  text[0] = '\0';
  size = 0;
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    strcpy(text + size, config->tmesh_config_lines[line_i].tmesh_config_line_text);
    size += strlen(text + size);
    text[size++] = '\n';
    text[size] = '\0';
  }
  return (text);
}

/* this saves a script, as the file given.  afterwards, that is where
   the script is: */
int
tmesh_config_save(struct tmesh_config *config, const char *path, char **_err)
{
  struct tmesh_config *saved;
  char *text, *temp, message[1200];
  FILE *file;
  int ok;

  text = tmesh_config_text(config);
  temp = tme_new(char, strlen(path) + 8);
  sprintf(temp, "%s.new", path);
  file = fopen(temp, "w");
  ok = (file != NULL);
  if (ok) {
    ok = (fputs(text, file) >= 0);
    ok = (fclose(file) == 0) && ok;
  }
  if (ok) {
    ok = (rename(temp, path) == 0);
  }
  tme_free(text);
  if (!ok) {
    snprintf(message, sizeof(message), "Could not save %s: %s", path, strerror(errno));
    *_err = tme_strdup(message);
    unlink(temp);
    tme_free(temp);
    return (FALSE);
  }
  tme_free(temp);

  /* the script is now there.  (relative file names don't change,
     since they are relative to the script's folder.)  */
  saved = tmesh_config_load(path, _err);
  if (saved == NULL) {
    return (FALSE);
  }
  if (config->tmesh_config_path != NULL) {
    tme_free(config->tmesh_config_path);
    tme_free(config->tmesh_config_dir);
  }
  config->tmesh_config_path = saved->tmesh_config_path;
  config->tmesh_config_dir = saved->tmesh_config_dir;
  saved->tmesh_config_path = NULL;
  saved->tmesh_config_dir = NULL;
  tmesh_config_free(saved);
  return (TRUE);
}

/* this copies a script: */
struct tmesh_config *
tmesh_config_copy(const struct tmesh_config *config)
{
  struct tmesh_config *copy;
  char *text;

  text = tmesh_config_text(config);
  copy = tmesh_config_parse(text, config->tmesh_config_path);
  tme_free(text);
  return (copy);
}

/* this frees a script: */
void
tmesh_config_free(struct tmesh_config *config)
{
  int line_i;

  if (config == NULL) {
    return;
  }
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    tme_free(config->tmesh_config_lines[line_i].tmesh_config_line_text);
    tmesh_config_list_free(config->tmesh_config_lines[line_i].tmesh_config_line_argc,
			   config->tmesh_config_lines[line_i].tmesh_config_line_argv);
  }
  if (config->tmesh_config_lines != NULL) {
    tme_free(config->tmesh_config_lines);
  }
  if (config->tmesh_config_path != NULL) {
    tme_free(config->tmesh_config_path);
    tme_free(config->tmesh_config_dir);
  }
  tme_free(config);
}

/* this returns nonzero if a script differs from its file: */
int
tmesh_config_changed(const struct tmesh_config *config)
{
  struct tmesh_config *saved;
  char *err, *text, *saved_text;
  int changed;

  if (config->tmesh_config_path == NULL) {
    return (TRUE);
  }
  saved = tmesh_config_load(config->tmesh_config_path, &err);
  if (saved == NULL) {
    tme_free(err);
    return (TRUE);
  }
  text = tmesh_config_text(config);
  saved_text = tmesh_config_text(saved);
  changed = (strcmp(text, saved_text) != 0);
  tme_free(text);
  tme_free(saved_text);
  tmesh_config_free(saved);
  return (changed);
}

/* this returns a word of a line, or NULL: */
const char *
tmesh_config_word(const struct tmesh_config *config, int line_i, int arg_i)
{
  const struct tmesh_config_line *line;

  if (line_i < 0 || line_i >= config->tmesh_config_n_lines) {
    return (NULL);
  }
  line = &config->tmesh_config_lines[line_i];
  if (arg_i < 0 || arg_i >= line->tmesh_config_line_argc) {
    return (NULL);
  }
  return (line->tmesh_config_line_argv[arg_i]);
}

/* this sets a word of a line, or with a NULL word, removes it.  a
   word one past the last one is added: */
void
tmesh_config_set_word(struct tmesh_config *config, int line_i, int arg_i, const char *word)
{
  struct tmesh_config_line *line;

  line = &config->tmesh_config_lines[line_i];
  if (arg_i < line->tmesh_config_line_argc) {

    /* (the new word may be part of the old one.)  */
    if (word != NULL) {
      word = tme_strdup(word);
      tme_free(line->tmesh_config_line_argv[arg_i]);
      line->tmesh_config_line_argv[arg_i] = (char *) word;
    }
    else {
      tme_free(line->tmesh_config_line_argv[arg_i]);
      memmove(&line->tmesh_config_line_argv[arg_i],
	      &line->tmesh_config_line_argv[arg_i + 1],
	      sizeof(char *) * (line->tmesh_config_line_argc - arg_i));
      line->tmesh_config_line_argc--;
    }
  }
  else if (word != NULL) {
    tmesh_config_list_add(&line->tmesh_config_line_argc,
			  &line->tmesh_config_line_argv,
			  word);
  }
  _tmesh_config_line_join(line);
}

/* this inserts a line before the line given: */
void
tmesh_config_insert(struct tmesh_config *config, int line_i, const char *text)
{
  config->tmesh_config_lines
    = tme_renew(struct tmesh_config_line,
		config->tmesh_config_lines,
		config->tmesh_config_n_lines + 1);
  memmove(&config->tmesh_config_lines[line_i + 1],
	  &config->tmesh_config_lines[line_i],
	  sizeof(struct tmesh_config_line) * (config->tmesh_config_n_lines - line_i));
  config->tmesh_config_n_lines++;
  memset(&config->tmesh_config_lines[line_i], 0, sizeof(struct tmesh_config_line));
  _tmesh_config_line_set(&config->tmesh_config_lines[line_i], text);
}

/* this deletes a line: */
void
tmesh_config_delete(struct tmesh_config *config, int line_i)
{
  tme_free(config->tmesh_config_lines[line_i].tmesh_config_line_text);
  tmesh_config_list_free(config->tmesh_config_lines[line_i].tmesh_config_line_argc,
			 config->tmesh_config_lines[line_i].tmesh_config_line_argv);
  config->tmesh_config_n_lines--;
  memmove(&config->tmesh_config_lines[line_i],
	  &config->tmesh_config_lines[line_i + 1],
	  sizeof(struct tmesh_config_line) * (config->tmesh_config_n_lines - line_i));
}

/* this returns the index of a line's colon, or -1: */
static int
_tmesh_config_colon(const struct tmesh_config *config, int line_i)
{
  const struct tmesh_config_line *line;
  int arg_i;

  line = &config->tmesh_config_lines[line_i];
  for (arg_i = 0; arg_i < line->tmesh_config_line_argc; arg_i++) {
    if (strcmp(line->tmesh_config_line_argv[arg_i], ":") == 0) {
      return (arg_i);
    }
  }
  return (-1);
}

/* this returns the element that a line makes, or NULL: */
static const char *
_tmesh_config_element(const struct tmesh_config *config, int line_i)
{
  if (_tmesh_config_colon(config, line_i) < 1) {
    return (NULL);
  }
  return (tmesh_config_word(config, line_i, 0));
}

/* this returns the module of the element that a line makes, or
   NULL: */
const char *
tmesh_config_module(const struct tmesh_config *config, int line_i)
{
  int colon;

  colon = _tmesh_config_colon(config, line_i);
  if (colon < 1) {
    return (NULL);
  }
  return (tmesh_config_word(config, line_i, colon + 1));
}

/* this returns the element that a line connects its element to, or
   NULL: */
static const char *
_tmesh_config_parent(const struct tmesh_config *config, int line_i)
{
  const char *word;
  int arg_i;

  for (arg_i = 1; arg_i <= 2; arg_i++) {
    word = tmesh_config_word(config, line_i, arg_i);
    if (word != NULL && strcmp(word, "at") == 0) {
      return (tmesh_config_word(config, line_i, arg_i + 1));
    }
  }
  return (NULL);
}

/* this returns the line that makes an element, or -1: */
int
tmesh_config_find(const struct tmesh_config *config, const char *element)
{
  const char *name;
  int line_i;

  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    name = _tmesh_config_element(config, line_i);
    if (name != NULL && strcmp(name, element) == 0) {
      return (line_i);
    }
  }
  return (-1);
}

/* this returns the line that makes an element with a module, the
   first after the line given, or -1: */
static int
_tmesh_config_find_module(const struct tmesh_config *config, int line_i, const char *module)
{
  const char *word;

  for (line_i++; line_i < config->tmesh_config_n_lines; line_i++) {
    word = tmesh_config_module(config, line_i);
    if (word != NULL && strcmp(word, module) == 0) {
      return (line_i);
    }
  }
  return (-1);
}

/* this returns the index of a word among a line's module arguments,
   or -1: */
static int
_tmesh_config_arg_index(const struct tmesh_config *config, int line_i, const char *word)
{
  const struct tmesh_config_line *line;
  int arg_i, colon;

  colon = _tmesh_config_colon(config, line_i);
  if (colon < 0) {
    return (-1);
  }
  line = &config->tmesh_config_lines[line_i];
  for (arg_i = colon + 2; arg_i < line->tmesh_config_line_argc; arg_i++) {
    if (strcmp(line->tmesh_config_line_argv[arg_i], word) == 0) {
      return (arg_i);
    }
  }
  return (-1);
}

/* this returns the value of a module argument, the word after the one
   given, or NULL: */
const char *
tmesh_config_arg(const struct tmesh_config *config, int line_i, const char *word)
{
  int arg_i;

  arg_i = _tmesh_config_arg_index(config, line_i, word);
  if (arg_i < 0) {
    return (NULL);
  }
  return (tmesh_config_word(config, line_i, arg_i + 1));
}

/* this sets the value of a module argument, adding it at the end if
   it isn't there: */
static void
_tmesh_config_set_arg(struct tmesh_config *config, int line_i, const char *word, const char *value)
{
  int arg_i;

  arg_i = _tmesh_config_arg_index(config, line_i, word);
  if (arg_i < 0) {
    arg_i = config->tmesh_config_lines[line_i].tmesh_config_line_argc;
    tmesh_config_set_word(config, line_i, arg_i, word);
  }
  tmesh_config_set_word(config, line_i, arg_i + 1, value);
}

/* this returns the kinds of machines: */
const struct tmesh_config_kind *
tmesh_config_kinds(int *_count)
{
  *_count = TME_ARRAY_ELS(_tmesh_config_kinds);
  return (_tmesh_config_kinds);
}

/* this returns the line that makes the machine's board, or -1: */
static int
_tmesh_config_board(const struct tmesh_config *config)
{
  const char *module;
  int line_i;

  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    module = tmesh_config_module(config, line_i);
    if (module != NULL
	&& strncmp(module, "tme/machine/", 12) == 0
	&& strchr(module + 12, '/') == NULL) {
      return (line_i);
    }
  }
  return (-1);
}

/* this returns the kind of machine that a script makes, or NULL: */
const struct tmesh_config_kind *
tmesh_config_kind(const struct tmesh_config *config)
{
  const struct tmesh_config_kind *kind;
  const char *module, *arg;
  unsigned int kind_i;
  int line_i;

  line_i = _tmesh_config_board(config);
  if (line_i < 0) {
    return (NULL);
  }
  module = tmesh_config_module(config, line_i);
  arg = tmesh_config_word(config, line_i, _tmesh_config_colon(config, line_i) + 2);
  for (kind_i = 0; kind_i < TME_ARRAY_ELS(_tmesh_config_kinds); kind_i++) {
    kind = &_tmesh_config_kinds[kind_i];
    if (strcmp(kind->tmesh_config_kind_module, module) == 0
	&& (kind->tmesh_config_kind_arg == NULL
	    || (arg != NULL && strcmp(kind->tmesh_config_kind_arg, arg) == 0))) {
      return (kind);
    }
  }
  return (NULL);
}

/* this returns the line that names the machine ("## name: NAME"),
   or -1: */
static int
_tmesh_config_name_line(const struct tmesh_config *config, const char **_name)
{
  const char *text;
  int line_i;

  for (line_i = 0; line_i < config->tmesh_config_n_lines && line_i < 40; line_i++) {
    text = config->tmesh_config_lines[line_i].tmesh_config_line_text;
    if (*text != '#') {
      continue;
    }
    for (; *text == '#' || *text == ' ' || *text == '\t'; text++);
    if (strncasecmp(text, "name:", 5) == 0) {
      for (text += 5; *text == ' ' || *text == '\t'; text++);
      *_name = text;
      return (line_i);
    }
  }
  return (-1);
}

/* this returns the machine's name: the one in the script, or the
   name of its folder: */
const char *
tmesh_config_name(const struct tmesh_config *config)
{
  const char *name, *slash;

  if (_tmesh_config_name_line(config, &name) >= 0 && *name != '\0') {
    return (name);
  }
  if (config->tmesh_config_dir != NULL) {
    slash = strrchr(config->tmesh_config_dir, '/');
    return (slash != NULL && slash[1] != '\0' ? slash + 1 : config->tmesh_config_dir);
  }
  return ("");
}

/* this sets the machine's name: */
void
tmesh_config_set_name(struct tmesh_config *config, const char *name)
{
  char *text;
  const char *old;
  int line_i;

  text = tme_new(char, strlen(name) + 16);
  sprintf(text, "## name: %s", name);
  line_i = _tmesh_config_name_line(config, &old);
  if (line_i >= 0) {
    _tmesh_config_line_set(&config->tmesh_config_lines[line_i], text);
  }
  else {
    tmesh_config_insert(config, 0, text);
  }
  tme_free(text);
}

/* this returns the lines that make RAM: */
int
tmesh_config_ram(const struct tmesh_config *config, int **_lines)
{
  const char *module;
  int line_i, count;

  *_lines = tme_new(int, config->tmesh_config_n_lines + 1);
  count = 0;
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    module = tmesh_config_module(config, line_i);
    if (module != NULL
	&& strcmp(module, "tme/host/posix/memory") == 0
	&& _tmesh_config_arg_index(config, line_i, "ram") >= 0) {
      (*_lines)[count++] = line_i;
    }
  }
  return (count);
}

/* this returns the size of some RAM, in MB, or -1: */
int
tmesh_config_ram_mb(const struct tmesh_config *config, int line_i)
{
  const char *size;
  char *end;
  long value;

  size = tmesh_config_arg(config, line_i, "ram");
  if (size == NULL) {
    return (-1);
  }
  value = strtol(size, &end, 0);
  if (strcasecmp(end, "MB") == 0 || strcasecmp(end, "M") == 0) {
    return (value);
  }
  if (strcasecmp(end, "GB") == 0 || strcasecmp(end, "G") == 0) {
    return (value * 1024);
  }
  if (strcasecmp(end, "KB") == 0 || strcasecmp(end, "K") == 0) {
    return (value / 1024);
  }
  if (*end == '\0') {
    return (value / (1024 * 1024));
  }
  return (-1);
}

/* this sets the size of some RAM, in MB: */
void
tmesh_config_set_ram_mb(struct tmesh_config *config, int line_i, int mb)
{
  char size[32];

  snprintf(size, sizeof(size), "%dMB", mb);
  _tmesh_config_set_arg(config, line_i, "ram", size);
}

/* this adds a file to the list of files: */
static void
_tmesh_config_file_add(struct tmesh_config_file **_files, int *_count,
		       int line_i, int arg_i, int kind, const char *label)
{
  struct tmesh_config_file *file;

  *_files = tme_renew(struct tmesh_config_file, *_files, *_count + 1);
  file = &(*_files)[(*_count)++];
  file->tmesh_config_file_line = line_i;
  file->tmesh_config_file_arg = arg_i;
  file->tmesh_config_file_kind = kind;
  snprintf(file->tmesh_config_file_label, sizeof(file->tmesh_config_file_label), "%s", label);
}

/* this returns the files that the script names, other than the
   drives' images: */
int
tmesh_config_files(const struct tmesh_config *config, struct tmesh_config_file **_files)
{
  const char *module, *element, *board_module;
  char label[64];
  int line_i, arg_i, colon, count, board;

  *_files = NULL;
  count = 0;
  board = _tmesh_config_board(config);
  board_module = (board >= 0 ? tmesh_config_module(config, board) : NULL);
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    module = tmesh_config_module(config, line_i);
    if (module == NULL) {
      continue;
    }
    element = _tmesh_config_element(config, line_i);
    colon = _tmesh_config_colon(config, line_i);

    /* the board's IDPROM: */
    if (line_i == board) {
      if (strcmp(board_module, "tme/machine/sun2") == 0) {
	arg_i = colon + 3;
      }
      else if (strcmp(board_module, "tme/machine/sun3") == 0) {
	arg_i = colon + 2;
      }
      else {
	arg_i = _tmesh_config_arg_index(config, line_i, "idprom");
	arg_i = (arg_i < 0 ? -1 : arg_i + 1);
      }
      if (tmesh_config_word(config, line_i, arg_i) != NULL) {
	_tmesh_config_file_add(_files, &count, line_i, arg_i,
			       TMESH_CONFIG_FILE_STATE, "IDPROM");
      }
    }

    /* ROM images and memory kept in files: */
    else if (strcmp(module, "tme/host/posix/memory") == 0) {
      arg_i = _tmesh_config_arg_index(config, line_i, "rom");
      if (arg_i >= 0 && tmesh_config_word(config, line_i, arg_i + 1) != NULL) {
	if (strcmp(element, "rom0") == 0) {
	  snprintf(label, sizeof(label), "PROM");
	}
	else if (strlen(element) > 4
		 && strcmp(element + strlen(element) - 4, "rom0") == 0) {
	  snprintf(label, sizeof(label), "%.*s FCode ROM",
		   (int) strlen(element) - 4, element);
	}
	else {
	  snprintf(label, sizeof(label), "ROM (%s)", element);
	}
	_tmesh_config_file_add(_files, &count, line_i, arg_i + 1,
			       TMESH_CONFIG_FILE_ROM, label);
      }
      arg_i = _tmesh_config_arg_index(config, line_i, "persistent");
      if (arg_i >= 0 && tmesh_config_word(config, line_i, arg_i + 1) != NULL) {
	snprintf(label, sizeof(label), "%s",
		 (strncmp(element, "eeprom", 6) == 0 ? "EEPROM" : element));
	_tmesh_config_file_add(_files, &count, line_i, arg_i + 1,
			       TMESH_CONFIG_FILE_STATE, label);
      }
    }

    /* the NVRAM of a SPARCstation: */
    else if (strcmp(module, "tme/machine/sun4/nvram") == 0
	     && tmesh_config_word(config, line_i, colon + 2) != NULL) {
      _tmesh_config_file_add(_files, &count, line_i, colon + 2,
			     TMESH_CONFIG_FILE_STATE, "NVRAM");
    }

    /* the EEPROM of a Sun-3: */
    else if (strcmp(module, "tme/machine/sun3/eeprom") == 0
	     && tmesh_config_word(config, line_i, colon + 2) != NULL) {
      _tmesh_config_file_add(_files, &count, line_i, colon + 2,
			     TMESH_CONFIG_FILE_STATE, "EEPROM");
    }

    /* the keyboard's macros and map: */
    else if (strcmp(module, "tme/serial/keyboard") == 0) {
      arg_i = _tmesh_config_arg_index(config, line_i, "macros");
      if (arg_i >= 0 && tmesh_config_word(config, line_i, arg_i + 1) != NULL) {
	_tmesh_config_file_add(_files, &count, line_i, arg_i + 1,
			       TMESH_CONFIG_FILE_MACROS, "Keyboard macros");
      }
      arg_i = _tmesh_config_arg_index(config, line_i, "map");
      if (arg_i >= 0 && tmesh_config_word(config, line_i, arg_i + 1) != NULL) {
	_tmesh_config_file_add(_files, &count, line_i, arg_i + 1,
			       TMESH_CONFIG_FILE_OTHER, "Keyboard map");
      }
    }
  }
  return (count);
}

/* this returns a file's path: as it is, if absolute, or else in the
   script's folder: */
char *
tmesh_config_resolve(const struct tmesh_config *config, const char *file)
{
  char *path;

  if (file[0] == '/' || config->tmesh_config_dir == NULL) {
    return (tme_strdup(file));
  }
  path = tme_new(char, strlen(config->tmesh_config_dir) + strlen(file) + 2);
  sprintf(path, "%s/%s", config->tmesh_config_dir, file);
  return (path);
}

/* this returns a file's name for the script: relative to the
   script's folder, if it's in it, or else absolute: */
char *
tmesh_config_shorten(const struct tmesh_config *config, const char *path)
{
  size_t length;

  if (config->tmesh_config_dir != NULL && path[0] == '/') {
    length = strlen(config->tmesh_config_dir);
    if (strncmp(path, config->tmesh_config_dir, length) == 0
	&& path[length] == '/'
	&& path[length + 1] != '\0') {
      return (tme_strdup(path + length + 1));
    }
  }
  return (tme_strdup(path));
}

/* this returns the kind of drive that a line makes, or -1: */
static int
_tmesh_config_drive_kind(const struct tmesh_config *config, int line_i)
{
  const char *module;
  unsigned int kind;

  module = tmesh_config_module(config, line_i);
  if (module == NULL) {
    return (-1);
  }
  for (kind = 0; kind < TME_ARRAY_ELS(_tmesh_config_drive_modules); kind++) {
    if (strcmp(module, _tmesh_config_drive_modules[kind]) == 0) {
      return (kind);
    }
  }
  return (-1);
}

/* this returns the SCSI drives: */
int
tmesh_config_drives(const struct tmesh_config *config, struct tmesh_config_drive **_drives)
{
  struct tmesh_config_drive *drive;
  const char *element, *module, *parent, *word;
  int line_i, host_i, kind, count;

  *_drives = NULL;
  count = 0;
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    kind = _tmesh_config_drive_kind(config, line_i);
    if (kind < 0) {
      continue;
    }
    *_drives = tme_renew(struct tmesh_config_drive, *_drives, count + 1);
    drive = &(*_drives)[count++];
    memset(drive, 0, sizeof(*drive));
    drive->tmesh_config_drive_line = line_i;
    drive->tmesh_config_drive_host_line = -1;
    drive->tmesh_config_drive_load_line = -1;
    drive->tmesh_config_drive_kind = kind;
    word = tmesh_config_arg(config, line_i, "id");
    drive->tmesh_config_drive_id = (word != NULL ? atoi(word) : -1);
    word = tmesh_config_arg(config, line_i, "type");
    drive->tmesh_config_drive_type = (word != NULL ? word : "");
    drive->tmesh_config_drive_file = "";

    /* find the drive's image: */
    element = _tmesh_config_element(config, line_i);
    for (host_i = 0; host_i < config->tmesh_config_n_lines; host_i++) {
      module = tmesh_config_module(config, host_i);
      parent = _tmesh_config_parent(config, host_i);
      if (module == NULL
	  || parent == NULL
	  || strcmp(parent, element) != 0
	  || strncmp(module, "tme/host/posix/", 15) != 0) {
	continue;
      }
      drive->tmesh_config_drive_host_line = host_i;
      word = tmesh_config_arg(config, host_i, "file");
      if (word != NULL) {
	drive->tmesh_config_drive_file = word;
      }
      drive->tmesh_config_drive_read_only
	= (_tmesh_config_arg_index(config, host_i, "read-only") >= 0);
      break;
    }

    /* find a command that loads a tape: */
    if (drive->tmesh_config_drive_host_line >= 0) {
      element = _tmesh_config_element(config, drive->tmesh_config_drive_host_line);
      for (host_i = 0; host_i < config->tmesh_config_n_lines; host_i++) {
	if (tmesh_config_word(config, host_i, 0) != NULL
	    && strcmp(tmesh_config_word(config, host_i, 0), "command") == 0
	    && tmesh_config_word(config, host_i, 1) != NULL
	    && strcmp(tmesh_config_word(config, host_i, 1), element) == 0
	    && tmesh_config_word(config, host_i, 2) != NULL
	    && strcmp(tmesh_config_word(config, host_i, 2), "load") == 0) {
	  drive->tmesh_config_drive_load_line = host_i;
	  word = tmesh_config_word(config, host_i, 3);
	  if (word != NULL && strcmp(word, "read-only") == 0) {
	    word = tmesh_config_word(config, host_i, 4);
	  }
	  if (word != NULL) {
	    drive->tmesh_config_drive_file = word;
	  }
	  break;
	}
      }
    }
  }
  return (count);
}

/* this sets a drive's SCSI ID: */
void
tmesh_config_drive_set_id(struct tmesh_config *config,
			  const struct tmesh_config_drive *drive, int id)
{
  char value[16];

  snprintf(value, sizeof(value), "%d", id);
  _tmesh_config_set_arg(config, drive->tmesh_config_drive_line, "id", value);
}

/* this sets a drive's type: */
void
tmesh_config_drive_set_type(struct tmesh_config *config,
			    const struct tmesh_config_drive *drive, const char *type)
{
  _tmesh_config_set_arg(config, drive->tmesh_config_drive_line, "type", type);
}

/* this returns an element name with a prefix that no element has: */
static void
_tmesh_config_new_element(const struct tmesh_config *config, const char *prefix,
			  char *name, size_t size)
{
  int unit;

  for (unit = 0;; unit++) {
    snprintf(name, size, "%s%d", prefix, unit);
    if (tmesh_config_find(config, name) < 0) {
      return;
    }
  }
}

/* this sets a drive's image (or with "", leaves the drive empty): */
void
tmesh_config_drive_set_file(struct tmesh_config *config,
			    const struct tmesh_config_drive *drive, const char *file)
{
  const char *element, *host;
  char name[32], *text;
  int kind, line_i;

  kind = drive->tmesh_config_drive_kind;
  element = _tmesh_config_element(config, drive->tmesh_config_drive_line);
  text = tme_new(char, strlen(file) + 128);

  /* a tape is loaded by a command after the tape's element: */
  if (kind == TMESH_CONFIG_DRIVE_TAPE) {
    if (drive->tmesh_config_drive_load_line >= 0) {
      tmesh_config_delete(config, drive->tmesh_config_drive_load_line);
    }
    if (drive->tmesh_config_drive_host_line >= 0) {
      host = _tmesh_config_element(config, drive->tmesh_config_drive_host_line
				   - (drive->tmesh_config_drive_load_line >= 0
				      && (drive->tmesh_config_drive_load_line
					  < drive->tmesh_config_drive_host_line)));
      snprintf(name, sizeof(name), "%s", host);
      line_i = tmesh_config_find(config, name);
    }
    else {
      _tmesh_config_new_element(config, "tape", name, sizeof(name));
      sprintf(text, "%s at %s: tme/host/posix/tape", name, element);
      line_i = tmesh_config_find(config, element) + 1;
      tmesh_config_insert(config, line_i, text);
    }
    if (*file != '\0') {
      sprintf(text, "command %s load", name);
      tmesh_config_insert(config, line_i + 1, text);
      tmesh_config_set_word(config, line_i + 1, 3, file);
    }
  }

  /* a disk or CD-ROM's image is its element's file: */
  else if (*file == '\0') {
    if (drive->tmesh_config_drive_host_line >= 0) {
      tmesh_config_delete(config, drive->tmesh_config_drive_host_line);
    }
  }
  else if (drive->tmesh_config_drive_host_line >= 0) {
    _tmesh_config_set_arg(config, drive->tmesh_config_drive_host_line, "file", file);
  }
  else {
    _tmesh_config_new_element(config, _tmesh_config_host_prefixes[kind], name, sizeof(name));
    sprintf(text, "%s at %s: tme/host/posix/disk file", name, element);
    line_i = drive->tmesh_config_drive_line + 1;
    tmesh_config_insert(config, line_i, text);
    tmesh_config_set_word(config, line_i,
			  config->tmesh_config_lines[line_i].tmesh_config_line_argc,
			  file);
    if (kind == TMESH_CONFIG_DRIVE_CDROM) {
      tmesh_config_set_word(config, line_i,
			    config->tmesh_config_lines[line_i].tmesh_config_line_argc,
			    "read-only");
    }
  }
  tme_free(text);
}

/* this adds a drive, with the first free SCSI ID from the kind's
   usual one, and returns its line, or -1 if there is no SCSI bus or
   no free ID: */
int
tmesh_config_drive_add(struct tmesh_config *config, int kind)
{
  const struct tmesh_config_kind *machine;
  struct tmesh_config_drive *drives;
  const char *bus;
  char name[32], host[32], text[200];
  int count, used, id, drive_i, line_i, bus_i, first;

  /* the drive goes on the first SCSI bus, after its last drive: */
  bus_i = _tmesh_config_find_module(config, -1, "tme/scsi/bus");
  if (bus_i < 0) {
    return (-1);
  }
  bus = _tmesh_config_element(config, bus_i);
  count = tmesh_config_drives(config, &drives);
  used = 1 << 7;
  line_i = bus_i;
  for (drive_i = 0; drive_i < count; drive_i++) {
    if (drives[drive_i].tmesh_config_drive_id >= 0) {
      used |= 1 << drives[drive_i].tmesh_config_drive_id;
    }
    line_i = drives[drive_i].tmesh_config_drive_line;
    if (drives[drive_i].tmesh_config_drive_host_line > line_i) {
      line_i = drives[drive_i].tmesh_config_drive_host_line;
    }
    if (drives[drive_i].tmesh_config_drive_load_line > line_i) {
      line_i = drives[drive_i].tmesh_config_drive_load_line;
    }
  }
  if (drives != NULL) {
    tme_free(drives);
  }

  /* the first free ID from the usual one: */
  machine = tmesh_config_kind(config);
  first = (kind == TMESH_CONFIG_DRIVE_DISK ? 0
	   : kind == TMESH_CONFIG_DRIVE_TAPE ? 4
	   : 6);
  if (machine != NULL) {
    first = (kind == TMESH_CONFIG_DRIVE_DISK ? machine->tmesh_config_kind_disk_id
	     : kind == TMESH_CONFIG_DRIVE_TAPE ? machine->tmesh_config_kind_tape_id
	     : machine->tmesh_config_kind_cdrom_id);
  }
  for (id = first; id < first + 8 && (used & (1 << (id % 8))); id++);
  if (id == first + 8) {
    return (-1);
  }
  id %= 8;

  /* add the drive, and for a disk, its image: */
  _tmesh_config_new_element(config, _tmesh_config_drive_prefixes[kind], name, sizeof(name));
  snprintf(text, sizeof(text), "%s at %s: %s id %d type %s",
	   name, bus, _tmesh_config_drive_modules[kind], id,
	   (kind == TMESH_CONFIG_DRIVE_DISK && machine != NULL
	    ? machine->tmesh_config_kind_disk_type
	    : kind == TMESH_CONFIG_DRIVE_TAPE && machine != NULL && !machine->tmesh_config_kind_sparc
	    ? "emulex-mt02"
	    : "tme-scsi-1"));
  tmesh_config_insert(config, ++line_i, text);
  if (kind == TMESH_CONFIG_DRIVE_DISK) {
    _tmesh_config_new_element(config, "disk", host, sizeof(host));
    snprintf(text, sizeof(text), "%s at %s: tme/host/posix/disk file %s.img", host, name, host);
    tmesh_config_insert(config, line_i + 1, text);
  }
  else if (kind == TMESH_CONFIG_DRIVE_TAPE) {
    _tmesh_config_new_element(config, "tape", host, sizeof(host));
    snprintf(text, sizeof(text), "%s at %s: tme/host/posix/tape", host, name);
    tmesh_config_insert(config, line_i + 1, text);
  }
  return (line_i);
}

/* this removes a drive: */
void
tmesh_config_drive_remove(struct tmesh_config *config, const struct tmesh_config_drive *drive)
{
  int lines[3], i, j, t;

  lines[0] = drive->tmesh_config_drive_line;
  lines[1] = drive->tmesh_config_drive_host_line;
  lines[2] = drive->tmesh_config_drive_load_line;
  for (i = 0; i < 3; i++) {
    for (j = i + 1; j < 3; j++) {
      if (lines[j] > lines[i]) {
	t = lines[i];
	lines[i] = lines[j];
	lines[j] = t;
      }
    }
  }
  for (i = 0; i < 3; i++) {
    if (lines[i] >= 0) {
      tmesh_config_delete(config, lines[i]);
    }
  }
}

/* this returns the types of a kind of drive: */
const char * const *
tmesh_config_drive_types(int kind, int *_count)
{
  if (kind == TMESH_CONFIG_DRIVE_DISK) {
    *_count = TME_ARRAY_ELS(_tmesh_config_disk_types);
    return (_tmesh_config_disk_types);
  }
  if (kind == TMESH_CONFIG_DRIVE_TAPE) {
    *_count = TME_ARRAY_ELS(_tmesh_config_tape_types);
    return (_tmesh_config_tape_types);
  }
  *_count = TME_ARRAY_ELS(_tmesh_config_cdrom_types);
  return (_tmesh_config_cdrom_types);
}

/* this returns the line that makes the display, or -1: */
static int
_tmesh_config_display_line(const struct tmesh_config *config)
{
  const char *module;
  int line_i;

  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    module = tmesh_config_module(config, line_i);
    if (module != NULL
	&& strncmp(module, "tme/host/", 9) == 0
	&& strlen(module) > 8
	&& strcmp(module + strlen(module) - 8, "/display") == 0) {
      return (line_i);
    }
  }
  return (-1);
}

/* this returns the display's kind ("sdl" or "rfb", or "gtk" in an
   older script), or NULL
   if there is no display: */
const char *
tmesh_config_display(const struct tmesh_config *config)
{
  static char kind[16];
  const char *module;
  int line_i;

  line_i = _tmesh_config_display_line(config);
  if (line_i < 0) {
    return (NULL);
  }
  module = tmesh_config_module(config, line_i);
  snprintf(kind, sizeof(kind), "%.*s", (int) (strlen(module) - 17), module + 9);
  return (kind);
}

/* this sets the display's kind: */
void
tmesh_config_set_display(struct tmesh_config *config, const char *kind)
{
  char module[64];
  int line_i;

  line_i = _tmesh_config_display_line(config);
  if (line_i < 0) {
    return;
  }
  snprintf(module, sizeof(module), "tme/host/%s/display", kind);
  tmesh_config_set_word(config, line_i, _tmesh_config_colon(config, line_i) + 1, module);
}

/* this returns which of its kind's framebuffers the machine has, or
   -1: */
int
tmesh_config_fb(const struct tmesh_config *config)
{
  const struct tmesh_config_kind *kind;
  const struct tmesh_config_fb *fbs;
  int fb_i;

  kind = tmesh_config_kind(config);
  if (kind == NULL || kind->tmesh_config_kind_fbs == NULL) {
    return (-1);
  }
  fbs = kind->tmesh_config_kind_fbs;
  for (fb_i = 0; fbs[fb_i].tmesh_config_fb_label != NULL; fb_i++) {
    if (tmesh_config_find(config, fbs[fb_i].tmesh_config_fb_element) >= 0) {
      return (fb_i);
    }
  }
  return (-1);
}

/* this changes the machine's framebuffer to another of its kind's: */
void
tmesh_config_set_fb(struct tmesh_config *config, int new_fb)
{
  const struct tmesh_config_kind *kind;
  const struct tmesh_config_fb *old, *fb;
  struct tmesh_config *lines;
  const char *element, *parent;
  int old_fb, line_i, at, new_i;

  old_fb = tmesh_config_fb(config);
  kind = tmesh_config_kind(config);
  if (old_fb < 0 || old_fb == new_fb) {
    return;
  }
  old = &kind->tmesh_config_kind_fbs[old_fb];
  fb = &kind->tmesh_config_kind_fbs[new_fb];

  /* remove the old framebuffer's lines (the ones that make it, and
     the ones that make elements at the same place, like its ROM),
     where the new one's go: */
  at = tmesh_config_find(config, old->tmesh_config_fb_element);
  lines = tmesh_config_parse(old->tmesh_config_fb_lines, NULL);
  for (line_i = 0; line_i < lines->tmesh_config_n_lines; line_i++) {
    element = _tmesh_config_element(lines, line_i);
    new_i = (element != NULL ? tmesh_config_find(config, element) : -1);
    if (new_i >= 0) {
      if (new_i < at) {
	at--;
      }
      tmesh_config_delete(config, new_i);
    }
  }
  tmesh_config_free(lines);
  lines = tmesh_config_parse(fb->tmesh_config_fb_lines, NULL);
  for (line_i = 0; line_i < lines->tmesh_config_n_lines; line_i++) {
    tmesh_config_insert(config, at + line_i,
			lines->tmesh_config_lines[line_i].tmesh_config_line_text);
  }
  tmesh_config_free(lines);

  /* connect the display to the new framebuffer: */
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    parent = _tmesh_config_parent(config, line_i);
    if (_tmesh_config_colon(config, line_i) < 0
	&& parent != NULL
	&& strcmp(parent, old->tmesh_config_fb_element) == 0) {
      tmesh_config_set_word(config, line_i, 2, fb->tmesh_config_fb_element);
    }
  }
}

/* this finds the Ethernet interface: */
void
tmesh_config_net(const struct tmesh_config *config, struct tmesh_config_net *net)
{
  const char *module, *parent;
  unsigned int type;
  int line_i;

  memset(net, 0, sizeof(*net));
  net->tmesh_config_net_line = -1;
  net->tmesh_config_net_host_line = -1;
  net->tmesh_config_net_type = TMESH_CONFIG_NET_NONE;
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    module = tmesh_config_module(config, line_i);
    if (module != NULL
	&& (strcmp(module, "tme/ic/am7990") == 0
	    || strcmp(module, "tme/ic/i82586") == 0
	    || strcmp(module, "tme/bus/multibus/3c400") == 0)) {
      net->tmesh_config_net_line = line_i;
      net->tmesh_config_net_element = _tmesh_config_element(config, line_i);
      break;
    }
  }
  if (net->tmesh_config_net_line < 0) {
    return;
  }
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    module = tmesh_config_module(config, line_i);
    parent = _tmesh_config_parent(config, line_i);
    if (module == NULL
	|| parent == NULL
	|| strcmp(parent, net->tmesh_config_net_element) != 0) {
      continue;
    }
    net->tmesh_config_net_host_line = line_i;
    net->tmesh_config_net_type = TMESH_CONFIG_NET_OTHER;
    for (type = 1; type < TME_ARRAY_ELS(_tmesh_config_net_modules); type++) {
      if (strcmp(module, _tmesh_config_net_modules[type]) == 0) {
	net->tmesh_config_net_type = type;
      }
    }
    break;
  }
}

/* this connects the Ethernet interface another way: */
void
tmesh_config_set_net(struct tmesh_config *config, int type)
{
  struct tmesh_config_net net;
  char name[32], text[200];

  tmesh_config_net(config, &net);
  if (net.tmesh_config_net_line < 0 || net.tmesh_config_net_type == type) {
    return;
  }
  if (net.tmesh_config_net_host_line >= 0) {
    tmesh_config_delete(config, net.tmesh_config_net_host_line);
  }
  if (type == TMESH_CONFIG_NET_NONE || type == TMESH_CONFIG_NET_OTHER) {
    return;
  }
  _tmesh_config_new_element(config, _tmesh_config_net_prefixes[type], name, sizeof(name));
  snprintf(text, sizeof(text), "%s at %s: %s%s",
	   name, net.tmesh_config_net_element,
	   _tmesh_config_net_modules[type],
	   (type == TMESH_CONFIG_NET_UDP
	    ? " listen 127.0.0.1 4711 peer 127.0.0.1 4712"
	    : type == TMESH_CONFIG_NET_TAP
	    ? " inet 10.0.77.1 netmask 255.255.255.0 bcast 10.0.77.255"
	    : ""));
  tmesh_config_insert(config, net.tmesh_config_net_line + 1, text);
}

/* this returns a word of the Ethernet connection's arguments: the
   n'th after the word given (from one), or NULL: */
const char *
tmesh_config_net_arg(const struct tmesh_config *config, const char *word, int n)
{
  struct tmesh_config_net net;
  int arg_i;

  tmesh_config_net(config, &net);
  if (net.tmesh_config_net_host_line < 0) {
    return (NULL);
  }
  arg_i = _tmesh_config_arg_index(config, net.tmesh_config_net_host_line, word);
  if (arg_i < 0) {
    return (NULL);
  }
  return (tmesh_config_word(config, net.tmesh_config_net_host_line, arg_i + n));
}

/* this sets a word of the Ethernet connection's arguments (the n'th
   after the word given, from one), or with "" for n == 1, removes the
   word and its value: */
void
tmesh_config_set_net_arg(struct tmesh_config *config, const char *word, int n, const char *value)
{
  struct tmesh_config_net net;
  int line_i, arg_i, k;

  tmesh_config_net(config, &net);
  line_i = net.tmesh_config_net_host_line;
  if (line_i < 0) {
    return;
  }
  arg_i = _tmesh_config_arg_index(config, line_i, word);
  if (n == 1 && *value == '\0') {
    if (arg_i >= 0) {
      tmesh_config_set_word(config, line_i, arg_i + 1, NULL);
      tmesh_config_set_word(config, line_i, arg_i, NULL);
    }
    return;
  }
  if (arg_i < 0) {
    arg_i = config->tmesh_config_lines[line_i].tmesh_config_line_argc;
    tmesh_config_set_word(config, line_i, arg_i, word);
  }
  for (k = 1; k < n; k++) {
    if (tmesh_config_word(config, line_i, arg_i + k) == NULL) {
      tmesh_config_set_word(config, line_i, arg_i + k, "0");
    }
  }
  tmesh_config_set_word(config, line_i, arg_i + n, value);
}

/* this returns the line that sets the SPARC idle type, or -1: */
static int
_tmesh_config_idle_line(const struct tmesh_config *config)
{
  const char *word;
  int line_i;

  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    word = tmesh_config_word(config, line_i, 0);
    if (word != NULL
	&& strcmp(word, "command") == 0
	&& tmesh_config_word(config, line_i, 2) != NULL
	&& strcmp(tmesh_config_word(config, line_i, 2), "idle-type") == 0) {
      return (line_i);
    }
  }
  return (-1);
}

/* this returns the SPARC idle type, or "none": */
const char *
tmesh_config_idle_type(const struct tmesh_config *config)
{
  const char *type;

  type = tmesh_config_word(config, _tmesh_config_idle_line(config), 3);
  return (type != NULL ? type : "none");
}

/* this sets the SPARC idle type: */
void
tmesh_config_set_idle_type(struct tmesh_config *config, const char *type)
{
  const char *module;
  char text[100];
  int line_i, cpu_i;

  line_i = _tmesh_config_idle_line(config);
  if (strcmp(type, "none") == 0) {
    if (line_i >= 0) {
      tmesh_config_delete(config, line_i);
    }
    return;
  }
  if (line_i >= 0) {
    tmesh_config_set_word(config, line_i, 3, type);
    return;
  }

  /* the command goes before the one that powers up the machine, or at
     the end: */
  for (cpu_i = 0; cpu_i < config->tmesh_config_n_lines; cpu_i++) {
    module = tmesh_config_module(config, cpu_i);
    if (module != NULL && strncmp(module, "tme/ic/", 7) == 0
	&& tmesh_config_word(config, cpu_i, 0) != NULL
	&& strncmp(tmesh_config_word(config, cpu_i, 0), "cpu", 3) == 0) {
      break;
    }
  }
  if (cpu_i == config->tmesh_config_n_lines) {
    return;
  }
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    if (tmesh_config_word(config, line_i, 0) != NULL
	&& strcmp(tmesh_config_word(config, line_i, 0), "command") == 0
	&& tmesh_config_word(config, line_i, 2) != NULL
	&& strcmp(tmesh_config_word(config, line_i, 2), "power") == 0) {
      break;
    }
  }
  snprintf(text, sizeof(text), "command %s idle-type %s",
	   tmesh_config_word(config, cpu_i, 0), type);
  tmesh_config_insert(config, line_i, text);
}

/* this returns a short description of a machine: */
char *
tmesh_config_summary(const struct tmesh_config *config)
{
  const struct tmesh_config_kind *kind;
  struct tmesh_config_drive *drives;
  struct tmesh_config_net net;
  const char *display;
  char text[2000], *p;
  int *ram, n_ram, mb, i, count;

  p = text;
  *p = '\0';
  kind = tmesh_config_kind(config);
  p += sprintf(p, "%s", (kind != NULL ? kind->tmesh_config_kind_label : "A tmesh script"));
  n_ram = tmesh_config_ram(config, &ram);
  mb = 0;
  for (i = 0; i < n_ram; i++) {
    mb += tmesh_config_ram_mb(config, ram[i]);
  }
  tme_free(ram);
  if (mb > 0) {
    p += sprintf(p, ", %dMB of memory", mb);
  }
  p += sprintf(p, ".\n");

  count = tmesh_config_drives(config, &drives);
  for (i = 0; i < count; i++) {
    p += snprintf(p, 200, "%s, SCSI ID %d: %.120s\n",
		  (drives[i].tmesh_config_drive_kind == TMESH_CONFIG_DRIVE_DISK
		   ? "Disk"
		   : drives[i].tmesh_config_drive_kind == TMESH_CONFIG_DRIVE_CDROM
		   ? "CD-ROM"
		   : "Tape"),
		  drives[i].tmesh_config_drive_id,
		  (*drives[i].tmesh_config_drive_file != '\0'
		   ? drives[i].tmesh_config_drive_file
		   : "(empty)"));
  }
  if (drives != NULL) {
    tme_free(drives);
  }

  display = tmesh_config_display(config);
  p += sprintf(p, "%s\n",
	       (display == NULL ? "No display"
		: strcmp(display, "sdl") == 0 ? "SDL display"
		: strcmp(display, "gtk") == 0 ? "GTK display (no longer built)"
		: "VNC display"));
  tmesh_config_net(config, &net);
  if (net.tmesh_config_net_line >= 0) {
    p += sprintf(p, "Ethernet: %s\n",
		 (net.tmesh_config_net_type == TMESH_CONFIG_NET_NONE ? "not connected"
		  : net.tmesh_config_net_type == TMESH_CONFIG_NET_UDP ? "over UDP"
		  : net.tmesh_config_net_type == TMESH_CONFIG_NET_BPF ? "a host interface"
		  : net.tmesh_config_net_type == TMESH_CONFIG_NET_TAP ? "a tap interface"
		  : "connected"));
  }
  return (tme_strdup(text));
}

/* this returns nonzero if a file exists: */
static int
_tmesh_config_exists(const char *path)
{
  struct stat st;

  return (stat(path, &st) == 0);
}

/* this returns what's wrong with a machine for starting it, or NULL.
   disk images that don't exist are created when it's saved, and the
   machine creates its state files: */
char *
tmesh_config_check(const struct tmesh_config *config)
{
  struct tmesh_config_file *files;
  struct tmesh_config_drive *drives;
  const char *file;
  char text[1200], *path;
  int count, i, problem;

  if (_tmesh_config_board(config) < 0) {
    return (tme_strdup("the script makes no machine"));
  }
  problem = FALSE;
  count = tmesh_config_files(config, &files);
  for (i = 0; i < count && !problem; i++) {
    if (files[i].tmesh_config_file_kind != TMESH_CONFIG_FILE_ROM) {
      continue;
    }
    file = tmesh_config_word(config, files[i].tmesh_config_file_line, files[i].tmesh_config_file_arg);
    path = tmesh_config_resolve(config, file);
    if ((file[0] != '/' && config->tmesh_config_dir == NULL)
	|| !_tmesh_config_exists(path)) {

      /* a ROM image in a ROM folder is copied into the machine's
	 folder when it's saved: */
      tme_free(path);
      path = (strchr(file, '/') == NULL ? tmesh_config_find_rom(file) : NULL);
      if (path == NULL) {
	snprintf(text, sizeof(text), "the %s image %s is missing (choose it under Files)",
		 files[i].tmesh_config_file_label, file);
	problem = TRUE;
      }
    }
    if (path != NULL) {
      tme_free(path);
    }
  }
  if (files != NULL) {
    tme_free(files);
  }
  count = tmesh_config_drives(config, &drives);
  for (i = 0; i < count && !problem; i++) {
    file = drives[i].tmesh_config_drive_file;
    if (*file == '\0' || drives[i].tmesh_config_drive_kind == TMESH_CONFIG_DRIVE_DISK) {
      continue;
    }
    path = tmesh_config_resolve(config, file);
    if (!_tmesh_config_exists(path)) {
      snprintf(text, sizeof(text), "%s is missing (choose it under Drives)", file);
      problem = TRUE;
    }
    tme_free(path);
  }
  if (drives != NULL) {
    tme_free(drives);
  }
  file = tmesh_config_display(config);
  if (!problem && file != NULL && strcmp(file, "gtk") == 0) {
    snprintf(text, sizeof(text), "the GTK display is no longer built (choose another under Display)");
    problem = TRUE;
  }
  return (problem ? tme_strdup(text) : NULL);
}

/* this makes a folder, and its parents.  it returns nonzero if the
   folder is there: */
int
tmesh_config_make_dirs(const char *path)
{
  struct stat st;
  char *copy, *p;

  copy = tme_strdup(path);
  for (p = copy + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      mkdir(copy, 0755);
      *p = '/';
    }
  }
  mkdir(copy, 0755);
  tme_free(copy);
  return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/* this creates an empty disk image, as a sparse file: */
int
tmesh_config_create_disk(const char *path, long long bytes, char **_err)
{
  char message[1200];
  int fd;

  fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0 || ftruncate(fd, (off_t) bytes) != 0) {
    snprintf(message, sizeof(message), "Could not create %s: %s", path, strerror(errno));
    *_err = tme_strdup(message);
    if (fd >= 0) {
      close(fd);
      unlink(path);
    }
    return (FALSE);
  }
  close(fd);
  return (TRUE);
}

/* this copies a file: */
int
tmesh_config_copy_file(const char *from, const char *to, char **_err)
{
  char buffer[65536], message[1200];
  FILE *in, *out;
  size_t got;
  int ok;

  in = fopen(from, "rb");
  if (in == NULL) {
    snprintf(message, sizeof(message), "Could not read %s: %s", from, strerror(errno));
    *_err = tme_strdup(message);
    return (FALSE);
  }
  out = fopen(to, "wb");
  if (out == NULL) {
    snprintf(message, sizeof(message), "Could not create %s: %s", to, strerror(errno));
    *_err = tme_strdup(message);
    fclose(in);
    return (FALSE);
  }
  ok = TRUE;
  while (ok && (got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
    ok = (fwrite(buffer, 1, got, out) == got);
  }
  ok = !ferror(in) && ok;
  ok = (fclose(out) == 0) && ok;
  fclose(in);
  if (!ok) {
    snprintf(message, sizeof(message), "Could not copy %s to %s: %s", from, to, strerror(errno));
    *_err = tme_strdup(message);
    unlink(to);
  }
  return (ok);
}

/* this writes the default keyboard macros: */
int
tmesh_config_write_macros(const char *path, char **_err)
{
  char message[1200];
  FILE *file;
  int ok;

  file = fopen(path, "w");
  ok = (file != NULL);
  if (ok) {
    ok = (fputs(_tmesh_config_macros, file) >= 0);
    ok = (fclose(file) == 0) && ok;
  }
  if (!ok) {
    snprintf(message, sizeof(message), "Could not write %s: %s", path, strerror(errno));
    *_err = tme_strdup(message);
  }
  return (ok);
}

/* this returns the home folder: */
static const char *
_tmesh_config_home(void)
{
  const char *home;

  home = getenv("HOME");
  return (home != NULL && *home != '\0' ? home : ".");
}

/* this returns the folder of the window's state, making it if
   needed: */
static const char *
_tmesh_config_state_dir(void)
{
  static char *dir;
  const char *xdg;
  char path[4096];

  if (dir != NULL) {
    return (dir);
  }
#ifdef __APPLE__
  xdg = NULL;
  snprintf(path, sizeof(path), "%s/Library/Application Support/tme", _tmesh_config_home());
#else  /* !__APPLE__ */
  xdg = getenv("XDG_CONFIG_HOME");
  if (xdg != NULL && xdg[0] == '/') {
    snprintf(path, sizeof(path), "%s/tme", xdg);
  }
  else {
    snprintf(path, sizeof(path), "%s/.config/tme", _tmesh_config_home());
  }
#endif /* !__APPLE__ */
  (void) xdg;
  tmesh_config_make_dirs(path);
  dir = tme_strdup(path);
  return (dir);
}

/* this returns where new machines go by default: */
const char *
tmesh_config_machines_dir(void)
{
  static char *dir;

  if (dir == NULL) {
    dir = tme_new(char, strlen(_tmesh_config_home()) + 8);
    sprintf(dir, "%s/TME", _tmesh_config_home());
  }
  return (dir);
}

/* this reads one of the state files, as lines: */
static int
_tmesh_config_read_lines(const char *name, char ***_lines)
{
  char path[4200], line[4200];
  FILE *file;
  size_t length;
  int count;

  *_lines = NULL;
  count = 0;
  snprintf(path, sizeof(path), "%s/%s", _tmesh_config_state_dir(), name);
  file = fopen(path, "r");
  if (file == NULL) {
    return (0);
  }
  while (fgets(line, sizeof(line), file) != NULL) {
    length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
      line[--length] = '\0';
    }
    if (length > 0) {
      tmesh_config_list_add(&count, _lines, line);
    }
  }
  fclose(file);
  return (count);
}

/* this writes one of the state files, from lines: */
static void
_tmesh_config_write_lines(const char *name, int count, char **lines)
{
  char path[4200];
  FILE *file;
  int i;

  snprintf(path, sizeof(path), "%s/%s", _tmesh_config_state_dir(), name);
  file = fopen(path, "w");
  if (file == NULL) {
    return;
  }
  for (i = 0; i < count; i++) {
    fprintf(file, "%s\n", lines[i]);
  }
  fclose(file);
}

/* this returns the scripts in the window's list, leaving out ones
   that are gone: */
int
tmesh_config_recent(char ***_list)
{
  char **lines;
  int n_lines, count, i;

  n_lines = _tmesh_config_read_lines("machines", &lines);
  *_list = NULL;
  count = 0;
  for (i = 0; i < n_lines; i++) {
    if (_tmesh_config_exists(lines[i])) {
      tmesh_config_list_add(&count, _list, lines[i]);
    }
  }
  tmesh_config_list_free(n_lines, lines);
  return (count);
}

/* this puts a script first in the window's list, or with first FALSE,
   removes it: */
static void
_tmesh_config_recent_update(const char *path, int first)
{
  char **list, **new_list;
  int count, new_count, i;

  count = tmesh_config_recent(&list);
  new_list = NULL;
  new_count = 0;
  if (first) {
    tmesh_config_list_add(&new_count, &new_list, path);
  }
  for (i = 0; i < count; i++) {
    if (strcmp(list[i], path) != 0 && new_count < TMESH_CONFIG_RECENT_MAX) {
      tmesh_config_list_add(&new_count, &new_list, list[i]);
    }
  }
  _tmesh_config_write_lines("machines", new_count, new_list);
  tmesh_config_list_free(count, list);
  tmesh_config_list_free(new_count, new_list);
}

void
tmesh_config_add_recent(const char *path)
{
  _tmesh_config_recent_update(path, TRUE);
}

void
tmesh_config_remove_recent(const char *path)
{
  _tmesh_config_recent_update(path, FALSE);
}

/* this returns a setting, the first one with the key given, or NULL: */
static const char *
_tmesh_config_setting(const char *key)
{
  static char *value;
  char **lines;
  size_t length;
  int count, i;

  if (value != NULL) {
    tme_free(value);
    value = NULL;
  }
  count = _tmesh_config_read_lines("settings", &lines);
  length = strlen(key);
  for (i = 0; i < count && value == NULL; i++) {
    if (strncmp(lines[i], key, length) == 0 && lines[i][length] == ' ') {
      value = tme_strdup(lines[i] + length + 1);
    }
  }
  tmesh_config_list_free(count, lines);
  return (value);
}

/* this replaces all of the settings with a key: */
static void
_tmesh_config_set_settings(const char *key, int n_values, char **values)
{
  char **lines, **new_lines, *line;
  size_t length;
  int count, new_count, i;

  count = _tmesh_config_read_lines("settings", &lines);
  new_lines = NULL;
  new_count = 0;
  length = strlen(key);
  for (i = 0; i < count; i++) {
    if (!(strncmp(lines[i], key, length) == 0 && lines[i][length] == ' ')) {
      tmesh_config_list_add(&new_count, &new_lines, lines[i]);
    }
  }
  for (i = 0; i < n_values; i++) {
    if (values[i] == NULL || *values[i] == '\0' || strchr(values[i], '\n') != NULL) {
      continue;
    }
    line = tme_new(char, length + strlen(values[i]) + 2);
    sprintf(line, "%s %s", key, values[i]);
    tmesh_config_list_add(&new_count, &new_lines, line);
    tme_free(line);
  }
  _tmesh_config_write_lines("settings", new_count, new_lines);
  tmesh_config_list_free(count, lines);
  tmesh_config_list_free(new_count, new_lines);
}

/* the folder last used in a file chooser, or NULL: */
const char *
tmesh_config_last_dir(void)
{
  static char *dir;
  const char *value;

  value = _tmesh_config_setting("last-dir");
  if (dir != NULL) {
    tme_free(dir);
  }
  dir = (value != NULL ? tme_strdup(value) : NULL);
  return (dir);
}

void
tmesh_config_set_last_dir(const char *dir)
{
  char *values[1];

  values[0] = (char *) dir;
  _tmesh_config_set_settings("last-dir", 1, values);
}

/* the folders that ROM images are looked for in: */
int
tmesh_config_rom_dirs(char ***_list)
{
  char **lines;
  int count, n_dirs, i;

  count = _tmesh_config_read_lines("settings", &lines);
  *_list = NULL;
  n_dirs = 0;
  for (i = 0; i < count; i++) {
    if (strncmp(lines[i], "rom-dir ", 8) == 0) {
      tmesh_config_list_add(&n_dirs, _list, lines[i] + 8);
    }
  }
  tmesh_config_list_free(count, lines);
  return (n_dirs);
}

void
tmesh_config_set_rom_dirs(int count, char **list)
{
  _tmesh_config_set_settings("rom-dir", count, list);
}

/* this looks for a ROM image in the ROM folders, returning where it
   is, or NULL: */
char *
tmesh_config_find_rom(const char *name)
{
  char **dirs, *path;
  const char *slash;
  int count, i;

  slash = strrchr(name, '/');
  if (slash != NULL) {
    name = slash + 1;
  }
  if (*name == '\0') {
    return (NULL);
  }
  count = tmesh_config_rom_dirs(&dirs);
  path = NULL;
  for (i = 0; i < count && path == NULL; i++) {
    path = tme_new(char, strlen(dirs[i]) + strlen(name) + 2);
    sprintf(path, "%s/%s", dirs[i], name);
    if (!_tmesh_config_exists(path)) {
      tme_free(path);
      path = NULL;
    }
  }
  tmesh_config_list_free(count, dirs);
  return (path);
}
