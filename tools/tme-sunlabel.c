/* tools/tme-sunlabel.c - plans and writes Sun disk labels: */

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

/* usage: tme-sunlabel [ OPTIONS ] [ IMAGE ]

   this plans a Sun disk label for a disk image: a geometry, and a
   partition table with each partition a whole number of cylinders.
   it prints the plan as the answers to give the SunOS 3 diag program
   (choose "Other" for the drive type), and with -w, it also writes
   the label into the image itself, creating or growing the image as
   needed, so that diag isn't needed at all.

   SunOS 3 can only use the first 1GB of a SCSI disk, because its
   driver uses 6-byte READ and WRITE commands, which hold a 21-bit
   block number.  a label for a bigger disk makes blocks past 1GB
   wrap around onto the start of the disk, so by default the plan is
   limited to 1GB; -4 lifts the limit, for SunOS 4.

   SunOS 2.0 can only use the first 512MB of a SCSI disk.  some of
   its I/O, like paging programs in from their files, only uses a
   20-bit block number, so blocks past 512MB are read from 512MB
   lower down.  a plan for a bigger disk gets a warning.

   IMAGE is only needed to write the label, or to plan a label for
   the size of an existing image: */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* the size of a block, and of a Sun disk label: */
#define BLOCK_SIZE		(512)

/* the most blocks a 6-byte READ or WRITE can address: */
#define BLOCKS_MAX_SUNOS3	(1UL << 21)

/* the Sun disk label magic number: */
#define DKL_MAGIC		(0xdabe)

/* the offsets of the label fields: */
#define DKL_OFF_RPM		(420)
#define DKL_OFF_PCYL		(422)
#define DKL_OFF_APC		(424)
#define DKL_OFF_GAP1		(426)
#define DKL_OFF_GAP2		(428)
#define DKL_OFF_INTRLV		(430)
#define DKL_OFF_NCYL		(432)
#define DKL_OFF_ACYL		(434)
#define DKL_OFF_NHEAD		(436)
#define DKL_OFF_NSECT		(438)
#define DKL_OFF_BHEAD		(440)
#define DKL_OFF_PPART		(442)
#define DKL_OFF_MAP		(444)
#define DKL_OFF_MAGIC		(508)
#define DKL_OFF_CKSUM		(510)

static const char *prog;

/* the plan: */
static unsigned long ncyl, acyl, nhead, nsect;
static unsigned long part_cyl[8], part_blocks[8];
static const char *ident = "TME";

static void
usage(void)
{
  fprintf(stderr, "usage: %s [ OPTIONS ] [ IMAGE ]\n", prog);
  fprintf(stderr, "where OPTIONS are:\n");
  fprintf(stderr, "  -s SIZE          the disk size, like 1G or 500M (default: the size of IMAGE)\n");
  fprintf(stderr, "  -g C/A/H/S       the geometry: data cylinders, alternate cylinders, heads,\n");
  fprintf(stderr, "                   and sectors per track (default: chosen for the size)\n");
  fprintf(stderr, "  -p LAYOUT        the partitions, like a=64M,b=64M,g=rest (the default,\n");
  fprintf(stderr, "                   or a=16M,b=24M,g=rest for disks under 512MB);\n");
  fprintf(stderr, "                   each in turn starts where the previous one ends, and c\n");
  fprintf(stderr, "                   is always the whole disk.  sizes are rounded up to whole\n");
  fprintf(stderr, "                   cylinders, and a size without a unit is in blocks\n");
  fprintf(stderr, "  -n NAME          the drive name in the label (default: TME)\n");
  fprintf(stderr, "  -4               allow disks over 1GB, for SunOS 4\n");
  fprintf(stderr, "  -w               write the label into IMAGE, creating or growing it\n");
  fprintf(stderr, "  -f -w            write the label, replacing an existing one\n");
  exit(1);
}

/* this parses a size, returning it in blocks, or zero if it's bad: */
static unsigned long long
parse_size(const char *s)
{
  unsigned long long value, unit;
  char *end;

  value = strtoull(s, &end, 10);
  if (end == s) {
    return (0);
  }
  switch (toupper((unsigned char) *end)) {
  case '\0': return (value);
  case 'K': unit = 1024ULL; break;
  case 'M': unit = 1024ULL * 1024; break;
  case 'G': unit = 1024ULL * 1024 * 1024; break;
  case 'T': unit = 1024ULL * 1024 * 1024 * 1024; break;
  default: return (0);
  }
  end++;
  if (toupper((unsigned char) *end) == 'B') {
    end++;
  }
  if (*end != '\0') {
    return (0);
  }
  return ((value * unit) / BLOCK_SIZE);
}

/* this prints a size in blocks in a friendly way: */
static const char *
size_string(unsigned long long blocks)
{
  static char buffer[4][32];
  static int buffer_i;
  char *s;

  s = buffer[buffer_i++ % 4];
  if (blocks >= 2 * 1024 * 1024) {
    snprintf(s, sizeof(buffer[0]), "%.2f GB", blocks / (2.0 * 1024 * 1024));
  }
  else {
    snprintf(s, sizeof(buffer[0]), "%.1f MB", blocks / 2048.0);
  }
  return (s);
}

/* this puts a 16-bit big-endian value: */
static void
put16(unsigned char *p, unsigned long value)
{
  p[0] = (value >> 8) & 0xff;
  p[1] = value & 0xff;
}

/* this puts a 32-bit big-endian value: */
static void
put32(unsigned char *p, unsigned long value)
{
  p[0] = (value >> 24) & 0xff;
  p[1] = (value >> 16) & 0xff;
  p[2] = (value >> 8) & 0xff;
  p[3] = value & 0xff;
}

/* this makes the label.  the values of the fields that SunOS 3's
   diag program doesn't ask about are the ones it writes: */
static void
label_make(unsigned char *label, int sunos4)
{
  unsigned int cksum;
  int i;

  memset(label, 0, BLOCK_SIZE);
  snprintf((char *) label, 128, "%s cyl %lu alt %lu hd %lu sec %lu",
	   ident, ncyl, acyl, nhead, nsect);
  if (sunos4) {
    put16(label + DKL_OFF_RPM, 3600);
    put16(label + DKL_OFF_PCYL, ncyl + acyl);
    put16(label + DKL_OFF_INTRLV, 1);
  }
  else {
    put16(label + DKL_OFF_GAP1, 0xffff);
    put16(label + DKL_OFF_GAP2, 0xffff);
    put16(label + DKL_OFF_BHEAD, 2);
  }
  put16(label + DKL_OFF_NCYL, ncyl);
  put16(label + DKL_OFF_ACYL, acyl);
  put16(label + DKL_OFF_NHEAD, nhead);
  put16(label + DKL_OFF_NSECT, nsect);
  for (i = 0; i < 8; i++) {
    put32(label + DKL_OFF_MAP + (i * 8), part_cyl[i]);
    put32(label + DKL_OFF_MAP + (i * 8) + 4, part_blocks[i]);
  }
  put16(label + DKL_OFF_MAGIC, DKL_MAGIC);

  /* the checksum makes all of the 16-bit words XOR to zero: */
  cksum = 0;
  for (i = 0; i < DKL_OFF_CKSUM; i += 2) {
    cksum ^= (label[i] << 8) | label[i + 1];
  }
  put16(label + DKL_OFF_CKSUM, cksum);
}

/* this writes the label into the image: */
static int
label_write(const char *image, int force, int sunos4)
{
  unsigned char label[BLOCK_SIZE], old[BLOCK_SIZE];
  unsigned long long size;
  struct stat statbuf;
  int fd;

  fd = open(image, O_RDWR | O_CREAT, 0644);
  if (fd < 0 || fstat(fd, &statbuf) < 0) {
    fprintf(stderr, "%s: %s: %s\n", prog, image, strerror(errno));
    return (1);
  }

  /* don't replace an existing label unless asked to: */
  if (!force
      && pread(fd, old, sizeof(old), 0) == sizeof(old)
      && ((old[DKL_OFF_MAGIC] << 8) | old[DKL_OFF_MAGIC + 1]) == DKL_MAGIC) {
    fprintf(stderr, "%s: %s already has a label (\"%.60s\"); use -f -w to replace it\n",
	    prog, image, old);
    close(fd);
    return (1);
  }

  /* grow the image to the size of the disk: */
  size = (unsigned long long) (ncyl + acyl) * nhead * nsect * BLOCK_SIZE;
  if ((unsigned long long) statbuf.st_size < size
      && ftruncate(fd, size) < 0) {
    fprintf(stderr, "%s: %s: %s\n", prog, image, strerror(errno));
    close(fd);
    return (1);
  }

  label_make(label, sunos4);
  if (pwrite(fd, label, sizeof(label), 0) != sizeof(label)
      || close(fd) < 0) {
    fprintf(stderr, "%s: %s: %s\n", prog, image, strerror(errno));
    return (1);
  }
  printf("\nwrote the label to %s (%s)\n", image, size_string(size / BLOCK_SIZE));
  return (0);
}

int
main(int argc, char **argv)
{
  const char *image, *layout, *geometry;
  unsigned long long size_blocks, part_size;
  unsigned long cyl_blocks, cyl_next, cyls, blocks_max;
  char *layout_copy, *item, *next;
  struct stat statbuf;
  int sunos4, write_label, force, part, arg_i, i, rest_part;
  int order[8], order_count, rest_order;

  prog = strrchr(argv[0], '/');
  prog = (prog == NULL ? argv[0] : prog + 1);
  size_blocks = 0;
  geometry = NULL;
  layout = NULL;
  sunos4 = write_label = force = 0;
  for (arg_i = 1; arg_i < argc && argv[arg_i][0] == '-'; arg_i++) {
    if (strcmp(argv[arg_i], "-s") == 0 && arg_i + 1 < argc) {
      size_blocks = parse_size(argv[++arg_i]);
      if (size_blocks == 0) {
	fprintf(stderr, "%s: bad size %s\n", prog, argv[arg_i]);
	usage();
      }
    }
    else if (strcmp(argv[arg_i], "-g") == 0 && arg_i + 1 < argc) {
      geometry = argv[++arg_i];
    }
    else if (strcmp(argv[arg_i], "-p") == 0 && arg_i + 1 < argc) {
      layout = argv[++arg_i];
    }
    else if (strcmp(argv[arg_i], "-n") == 0 && arg_i + 1 < argc) {
      ident = argv[++arg_i];
    }
    else if (strcmp(argv[arg_i], "-4") == 0) {
      sunos4 = 1;
    }
    else if (strcmp(argv[arg_i], "-w") == 0) {
      write_label = 1;
    }
    else if (strcmp(argv[arg_i], "-f") == 0) {
      force = 1;
    }
    else {
      usage();
    }
  }
  /* -f only means something when writing: */
  if (force && !write_label) {
    fprintf(stderr, "%s: -f only replaces a label when writing one, with -f -w\n", prog);
    usage();
  }

  /* the image is needed to write the label, or to get the size: */
  image = (arg_i < argc ? argv[arg_i++] : NULL);
  if (arg_i != argc
      || (image == NULL
	  && (write_label
	      || (size_blocks == 0 && geometry == NULL)))) {
    usage();
  }
  blocks_max = (sunos4 ? 0xffffffffUL : BLOCKS_MAX_SUNOS3);

  /* the geometry, given or chosen: */
  if (geometry != NULL) {
    if (sscanf(geometry, "%lu/%lu/%lu/%lu", &ncyl, &acyl, &nhead, &nsect) != 4
	|| ncyl == 0 || nhead == 0 || nsect == 0) {
      fprintf(stderr, "%s: bad geometry %s\n", prog, geometry);
      usage();
    }
  }
  else {

    /* the size, given, or the size of the image: */
    if (size_blocks == 0) {
      if (stat(image, &statbuf) < 0) {
	fprintf(stderr, "%s: %s: %s (give the size with -s)\n", prog, image, strerror(errno));
	return (1);
      }
      size_blocks = statbuf.st_size / BLOCK_SIZE;
    }

    /* 16 heads of 120 sectors, or for a small disk, 8 heads of 60
       sectors, with two alternate cylinders.  SunOS 3's standalone
       copy program, which writes the miniroot during an install,
       refuses a disk with 64 or 128 sectors per track: */
    if (size_blocks >= 128 * 2048) {
      nhead = 16;
      nsect = 120;
    }
    else {
      nhead = 8;
      nsect = 60;
    }
    acyl = 2;
    cyls = size_blocks / (nhead * nsect);
    if (cyls > 65535) {
      cyls = 65535;
    }
    if (cyls <= acyl + 1) {
      fprintf(stderr, "%s: %s is too small for a disk\n", prog, size_string(size_blocks));
      return (1);
    }
    ncyl = cyls - acyl;
  }
  if (geometry != NULL && !sunos4 && (nsect % 64) == 0) {
    fprintf(stderr,
	    "%s: warning: SunOS 3's standalone copy program refuses disks with 64 or 128\n"
	    "    sectors per track, so the miniroot can't be installed on this disk\n",
	    prog);
  }
  if (ncyl + acyl > 65535 || nhead > 65535 || nsect > 65535) {
    fprintf(stderr, "%s: the geometry doesn't fit in a Sun label\n", prog);
    return (1);
  }
  cyl_blocks = nhead * nsect;

  /* the SunOS 3 limit: */
  if ((unsigned long long) (ncyl + acyl) * cyl_blocks > blocks_max) {
    fprintf(stderr,
	    "%s: a %s disk is too big for SunOS 3, which can only use 1GB of a SCSI disk;\n"
	    "    use -s 1G, or -4 if this disk is for SunOS 4\n",
	    prog, size_string((unsigned long long) (ncyl + acyl) * cyl_blocks));
    return (1);
  }

  /* the SunOS 2.0 limit: */
  if (!sunos4 && (unsigned long long) ncyl * cyl_blocks > 512 * 2048) {
    fprintf(stderr,
	    "%s: warning: a %s disk is too big for SunOS 2.0, which can only use 512MB\n"
	    "    of a SCSI disk; use -s 512M if this disk is for SunOS 2.0\n",
	    prog, size_string((unsigned long long) ncyl * cyl_blocks));
  }

  /* the default partitions: */
  if (layout == NULL) {
    layout = (ncyl * cyl_blocks >= 512 * 2048
	      ? "a=64M,b=64M,g=rest"
	      : "a=16M,b=24M,g=rest");
  }

  /* the partitions, each starting where the previous one ends: */
  memset(part_cyl, 0, sizeof(part_cyl));
  memset(part_blocks, 0, sizeof(part_blocks));
  cyl_next = 0;
  rest_part = -1;
  rest_order = -1;
  order_count = 0;
  layout_copy = strdup(layout);
  for (item = layout_copy; item != NULL && *item != '\0'; item = next) {
    next = strchr(item, ',');
    if (next != NULL) {
      *next++ = '\0';
    }
    if (item[0] < 'a' || item[0] > 'h' || item[1] != '=' || item[0] == 'c') {
      fprintf(stderr, "%s: bad partition %s (c is always the whole disk)\n", prog, item);
      usage();
    }
    part = item[0] - 'a';
    for (i = 0; i < order_count; i++) {
      if (order[i] == part) {
	fprintf(stderr, "%s: partition %c is given twice\n", prog, item[0]);
	return (1);
      }
    }
    order[order_count++] = part;
    if (strcmp(item + 2, "rest") == 0) {
      if (rest_part >= 0) {
	fprintf(stderr, "%s: only one partition can be the rest of the disk\n", prog);
	return (1);
      }
      rest_part = part;
      rest_order = order_count - 1;
      cyls = 0;
    }
    else {
      part_size = parse_size(item + 2);
      if (part_size == 0) {
	fprintf(stderr, "%s: bad partition size %s\n", prog, item);
	usage();
      }
      cyls = (part_size + cyl_blocks - 1) / cyl_blocks;
    }
    part_cyl[part] = cyl_next;
    part_blocks[part] = cyls * cyl_blocks;
    cyl_next += cyls;
  }
  if (cyl_next > ncyl) {
    fprintf(stderr, "%s: the partitions need %lu cylinders, but the disk only has %lu\n",
	    prog, cyl_next, ncyl);
    return (1);
  }

  /* the rest of the disk goes to that partition, and any partitions
     after it move up to the end: */
  if (rest_part >= 0) {
    unsigned long rest = ncyl - cyl_next;
    if (rest == 0) {
      fprintf(stderr, "%s: there is nothing left of the disk for partition %c\n",
	      prog, 'a' + rest_part);
      return (1);
    }
    part_blocks[rest_part] = rest * cyl_blocks;
    for (i = rest_order + 1; i < order_count; i++) {
      part_cyl[order[i]] += rest;
    }
  }
  free(layout_copy);

  /* c is the whole disk: */
  part_cyl[2] = 0;
  part_blocks[2] = ncyl * cyl_blocks;

  /* print the plan: */
  printf("disk: %s cyl %lu alt %lu hd %lu sec %lu, %s (%lu blocks of data, %s with alternates)\n",
	 ident, ncyl, acyl, nhead, nsect,
	 size_string((unsigned long long) ncyl * cyl_blocks),
	 ncyl * cyl_blocks,
	 size_string((unsigned long long) (ncyl + acyl) * cyl_blocks));
  printf("\nanswers for the SunOS 3 diag program (drive type: Other):\n");
  printf("  data cylinders: %lu\n", ncyl);
  printf("  alt cylinders: %lu\n", acyl);
  printf("  buffered seek: 2\n");
  printf("  cyl precomp: 0\n");
  printf("  heads: %lu\n", nhead);
  printf("  ident: %s\n", ident);
  printf("  sectors: %lu\n", nsect);
  printf("  interleave: 0\n");
  printf("\npartition table (\"partition\", then \"label\" in diag):\n");
  for (i = 0; i < 8; i++) {
    if (part_blocks[i] != 0) {
      printf("  %c: start cyl: %lu blocks: %lu (%s)\n",
	     'a' + i, part_cyl[i], part_blocks[i], size_string(part_blocks[i]));
    }
  }

  if (write_label) {
    return (label_write(image, force, sunos4));
  }
  return (0);
}
