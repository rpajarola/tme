/* machine/sun/sun-idprom.c - Sun IDPROM contents: */

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

/* includes: */
#include "sun-idprom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

/* mapping from machine name to Sun code name.  a leading asterisk
   marks the name used for a code name in a banner, after which the
   remaining names for that code name aren't shown: */
const char * const tme_sun_idprom_machines[] = {
  "2/120",		"sun2 Multibus",
  "2/170",		"sun2 Multibus",
  "2/50",		"sun2 VME",
  "2/130",		"sun2 VME",
  "2/160",		"sun2 VME",
  "3/75",		"Carrera",
  "3/140",		"Carrera",
  "3/150",		"Carrera",
  "3/160",		"Carrera",
  "3/180",		"Carrera",
  "3/50",		"Model 25",
  "3/260",		"Sirius",
  "3/280",		"Sirius",
  "3/110",		"Prism",
  "3/60",		"Ferrari",
  "3/E",		"Sun3E",
  "4/260",		"Sunrise",
  "4/280",		"Sunrise",
  "4/110",		"Cobra",
  "4/150",		"Cobra",
  "4/330",		"Stingray",
  "4/310",		"Stingray",
  "4/350",		"Stingray",
  "4/360",		"Stingray",
  "4/370",		"Stingray",
  "4/380",		"Stingray",
  "4/390",		"Stingray",
  "4/470",		"Sunray",
  "4/490",		"Sunray",
  "386i/MB1",		"Roadrunner-MB1",
  "386i/150",		"Roadrunner",
  "386i/250",		"Roadrunner",
  "3/460",		"Pegasus",
  "3/470",		"Pegasus",
  "3/480",		"Pegasus",
  "3/80",		"Hydra",
  "*SPARCStation 1",	"Campus",
  "SS1",		"Campus",
  "4/60",		"Campus",
  "*SPARCstation IPC",	"Phoenix",
  "IPC",		"Phoenix",
  "4/40",		"Phoenix",
  "*SPARCStation 1+",	"Campus B",
  "SS1+",		"Campus B",
  "4/65",		"Campus B",
  "*SPARCstation SLC",	"Off-Campus",
  "SLC",		"Off-Campus",
  "4/20",		"Off-Campus",
  "*SPARCStation 2",	"Calvin",
  "SS2",		"Calvin",
  "4/75",		"Calvin",
  "*SPARCstation ELC",	"Node Warrior",
  "ELC",		"Node Warrior",
  "4/25",		"Node Warrior",
  "*SPARCstation IPX",	"Hobbes",
  "IPX",		"Hobbes",
  "4/50",		"Hobbes",
  "*SPARCengine 1",	"Polaris",
  "4/E",		"Polaris",
  "4/600",		"Galaxy",
  "4/630",		"Galaxy",
  "4/670",		"Galaxy",
  "4/690",		"Galaxy",
  "*SPARCstation 10",	"Campus-2",
  "SS10",		"Campus-2",
  "*SPARCstation 20",	"Kodiak",
  "SS20",		"Kodiak",
  "*SPARCclassic X",	"Hamlet",
  "Classic X",		"Hamlet",
  "4/10",		"Hamlet",
  "*SPARCClassic",	"Sunergy",
  "4/15",		"Sunergy",
  "classic",		"Sunergy",
  "4/30",		"Sunergy",
  "SPARCstation LX",	"Sunergy",
  "LX",			"Sunergy",
  "Voyager",		"Gypsy",
  "SS5",		"Aurora",
  "SS4",		"Perigee",
  NULL
};

/* mapping from Sun code name to IDPROM machine type byte: */
const struct tme_sun_idprom_codename tme_sun_idprom_codenames[] = {
  { "sun2 Multibus",	0x01 },
  { "sun2 VME",		0x02 },
  { "Carrera",		0x11 },
  { "Model 25",		0x12 },
  { "Sirius",		0x13 },
  { "Prism",		0x14 },
  { "Ferrari",		0x17 },
  { "Sun3E",		0x18 },
  { "Sunrise",		0x21 },
  { "Cobra",		0x22 },
  { "Stingray",		0x23 },
  { "Sunray",		0x24 },
  { "Roadrunner",	0x31 },
  { "Roadrunner-MB1",	0x3a },
  { "Pegasus",		0x41 },
  { "Hydra",		0x42 },
  { "Campus",		0x51 },
  { "Phoenix",		0x52 },
  { "Campus B",		0x53 },
  { "Off-Campus",	0x54 },
  { "Calvin",		0x55 },
  { "Node Warrior",	0x56 },
  { "Hobbes",		0x57 },
  { "Polaris",		0x61 },
  { "Galaxy",		0x71 },
  { "Campus-2",		0x72 },
  { "Kodiak",		0x72 },
  { "Sunergy",		0x80 },
  { "Gypsy",		0x80 },
  { "Aurora",		0x80 },
  { "Perigee",		0x80 },
  { NULL,		0 }
};

/* mapping from Sun code name to sometime during the
   shipping/availability period, if known, for those machines that
   shipped with a real manufacture date in the IDPROM: */
static const struct {
  const char *codename;
  unsigned long date;
} codename_shipping[] = {
  { "sun2 Multibus",	457370182 },
  { "Carrera",		587239630 },
  { "Ferrari",		564526348 },
  { NULL,		0 }
};

/* this returns a random number: */
static long
_tme_sun_idprom_random(void)
{
  static int seeded;

  if (!seeded) {
    srandom((unsigned int) time(NULL) ^ (unsigned int) getpid());
    seeded = 1;
  }
  return (random());
}

/* this returns the IDPROM machine type for a code name, or -1: */
int
tme_sun_idprom_machtype(const char *codename)
{
  int i;

  for (i = 0; tme_sun_idprom_codenames[i].codename != NULL; i++) {
    if (strcmp(tme_sun_idprom_codenames[i].codename, codename) == 0) {
      return (tme_sun_idprom_codenames[i].machtype);
    }
  }
  return (-1);
}

/* this returns the code name for a machine name or a code name, or
   NULL: */
const char *
tme_sun_idprom_codename(const char *name)
{
  int i;

  for (i = 0; tme_sun_idprom_codenames[i].codename != NULL; i++) {
    if (strcmp(tme_sun_idprom_codenames[i].codename, name) == 0) {
      return (tme_sun_idprom_codenames[i].codename);
    }
  }
  for (i = 0; tme_sun_idprom_machines[i] != NULL; i += 2) {
    if (strcmp(tme_sun_idprom_machines[i], name) == 0
	|| (tme_sun_idprom_machines[i][0] == '*'
	    && strcmp(tme_sun_idprom_machines[i] + 1, name) == 0)) {
      return (tme_sun_idprom_machines[i + 1]);
    }
  }
  return (NULL);
}

/* this makes an IDPROM for a code name, with the given Ethernet
   address and serial number: */
void
tme_sun_idprom_make(unsigned char *idprom, const char *codename,
		    const unsigned long *ether, unsigned long serial)
{
  unsigned long date;
  unsigned int cksum;
  int i;

  memset(idprom, 0, TME_SUN_IDPROM_BYTES);

  /* the format byte and the machine type: */
  idprom[0] = 1;
  idprom[1] = tme_sun_idprom_machtype(codename);

  /* the Ethernet address: */
  for (i = 0; i < 6; i++) {
    idprom[2 + i] = ether[i];
  }

  /* a date, if possible, smeared within 30 days of the known date: */
  date = 0;
  for (i = 0; codename_shipping[i].codename != NULL; i++) {
    if (strcmp(codename_shipping[i].codename, codename) == 0) {
      date = (codename_shipping[i].date
	      + (((_tme_sun_idprom_random() % 60) - 30) * (24 * 60 * 60)));
    }
  }
  idprom[8] = date >> 24;
  idprom[9] = date >> 16;
  idprom[10] = date >> 8;
  idprom[11] = date;

  /* the serial number: */
  idprom[12] = serial >> 16;
  idprom[13] = serial >> 8;
  idprom[14] = serial;

  /* the checksum covers the first 15 bytes, and the last 16 bytes
     are zero: */
  cksum = 0;
  for (i = 0; i < 15; i++) {
    cksum ^= idprom[i];
  }
  idprom[15] = cksum;
}

/* this makes an IDPROM for a code name, with a random Ethernet
   address in Sun's OUI and a random serial number: */
void
tme_sun_idprom_make_random(unsigned char *idprom, const char *codename)
{
  unsigned long ether[6];
  int i;

  ether[0] = 0x08;
  ether[1] = 0x00;
  ether[2] = 0x20;
  for (i = 3; i < 6; i++) {
    ether[i] = _tme_sun_idprom_random() & 0xff;
  }
  tme_sun_idprom_make(idprom, codename, ether,
		      1 + (_tme_sun_idprom_random() % 100000));
}

/* this creates a file with the given contents, if it doesn't exist
   already.  it returns zero if the file now exists, sets *_created
   if this created it, and otherwise returns an errno value: */
int
tme_sun_file_create(const char *filename, const void *contents,
		    size_t size, int *_created)
{
  int fd, saved_errno;
  ssize_t rc;

  *_created = 0;
  fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0) {
    return (errno == EEXIST ? 0 : errno);
  }
  rc = write(fd, contents, size);
  saved_errno = errno;
  if (close(fd) != 0 && rc == (ssize_t) size) {
    rc = -1;
    saved_errno = errno;
  }
  if (rc != (ssize_t) size) {
    unlink(filename);
    return (rc < 0 ? saved_errno : EIO);
  }
  *_created = 1;
  return (0);
}

/* this creates a file holding an IDPROM, or a larger image with an
   IDPROM at the given offset, if it doesn't exist already.  it
   returns zero if the file now exists, and otherwise returns an
   errno value: */
int
tme_sun_idprom_file_create(const char *filename, const char *codename,
			   size_t size, size_t offset, const char *what)
{
  unsigned char *image, *idprom;
  int created, rc;

  image = calloc(1, size);
  if (image == NULL) {
    return (ENOMEM);
  }
  idprom = image + offset;
  tme_sun_idprom_make_random(idprom, codename);
  rc = tme_sun_file_create(filename, image, size, &created);
  if (rc == 0 && created) {
    fprintf(stderr,
	    "created %s, a new %s for a %s with Ethernet address %x:%x:%x:%x:%x:%x and serial #%lu\n",
	    filename, what, codename,
	    idprom[2], idprom[3], idprom[4], idprom[5], idprom[6], idprom[7],
	    (((unsigned long) idprom[12] << 16)
	     | ((unsigned long) idprom[13] << 8)
	     | idprom[14]));
  }
  free(image);
  return (rc);
}
