/* machine/sun/tme-sun-idprom.c - dumps and makes Sun IDPROM contents.
   this was tme-sun-idprom.pl: */

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

/* usage: tme-sun-idprom MACHINE ETHERNET [ SERIAL ] > IDPROM
          tme-sun-idprom < IDPROM

   with standard input not a terminal, this checks the IDPROM there
   and displays a PROM-like banner for it.  otherwise, this makes a
   new IDPROM for MACHINE, a machine name like 3/160 or SS2, or a Sun
   code name like Carrera: */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include "sun-idprom.h"

/* this appends to a fixed-size string buffer: */
static void
cat(char *buffer, size_t size, const char *s)
{
  size_t len;

  len = strlen(buffer);
  snprintf(buffer + len, size - len, "%s", s);
}

/* this checks an IDPROM on standard input: */
static int
idprom_check(void)
{
  unsigned char idprom[TME_SUN_IDPROM_BYTES];
  ssize_t rc;
  size_t size;
  unsigned int format, machtype, cksum_old, cksum_new;
  unsigned long date, serial;
  const char *codenames[16];
  char machines[16][512];
  const char *codename_machines[64];
  int codenames_count, codename_machines_count;
  int i, j, k;
  const char *machine, *sep;
  time_t date_time;
  struct tm *tm;
  char date_buffer[64];

  /* read in the IDPROM: */
  for (size = 0; size < TME_SUN_IDPROM_BYTES; size += rc) {
    rc = read(0, idprom + size, TME_SUN_IDPROM_BYTES - size);
    if (rc <= 0) {
      break;
    }
  }
  if (size != TME_SUN_IDPROM_BYTES) {
    fprintf(stderr, "fatal: IDPROM has wrong size (%lu), must be exactly 32\n", (unsigned long) size);
    return (1);
  }

  /* unpack the IDPROM: */
  format = idprom[0];
  machtype = idprom[1];
  date = (((unsigned long) idprom[8] << 24)
	  | ((unsigned long) idprom[9] << 16)
	  | ((unsigned long) idprom[10] << 8)
	  | idprom[11]);
  serial = (((unsigned long) idprom[12] << 16)
	    | ((unsigned long) idprom[13] << 8)
	    | idprom[14]);

  /* check the format: */
  if (format != 1) {
    printf("warning: IDPROM has invalid format byte (%u), must be 1\n", format);
  }

  /* check the checksum, which only covers the first 16 bytes: */
  cksum_old = idprom[15];
  cksum_new = 0;
  for (i = 0; i < 15; i++) {
    cksum_new ^= idprom[i];
  }
  if (cksum_new != cksum_old) {
    printf("warning: IDPROM has bad cksum (0x%02x), should be 0x%02x\n", cksum_old, cksum_new);
  }

  /* turn the machine type into a set of code names and machines: */
  codenames_count = 0;
  for (i = 0; tme_sun_idprom_codenames[i].codename != NULL; i++) {
    if (tme_sun_idprom_codenames[i].machtype != machtype) {
      continue;
    }
    codenames[codenames_count] = tme_sun_idprom_codenames[i].codename;
    codename_machines_count = 0;
    for (j = 0; tme_sun_idprom_machines[j] != NULL; j += 2) {
      if (strcmp(tme_sun_idprom_machines[j + 1], codenames[codenames_count]) != 0) {
	continue;
      }
      machine = tme_sun_idprom_machines[j];
      if (machine[0] == '*') {
	codename_machines[codename_machines_count++] = machine + 1;
	break;
      }
      codename_machines[codename_machines_count++] = machine;
    }
    machines[codenames_count][0] = '\0';
    for (k = 0; k < codename_machines_count; k++) {
      if (k > 0) {
	cat(machines[codenames_count], sizeof(machines[0]),
	    (codename_machines_count == 2 ? " or " : ", "));
      }
      if (codename_machines_count > 2 && k == codename_machines_count - 1) {
	cat(machines[codenames_count], sizeof(machines[0]), "or ");
      }
      cat(machines[codenames_count], sizeof(machines[0]), codename_machines[k]);
    }
    codenames_count++;
  }

  /* display a PROM-like banner: */
  if (codenames_count == 0) {
    printf("warning: IDPROM has unknown machine type (0x%02x)\n", machtype);
  }
  else if (codenames_count == 1) {
    machine = machines[0];
    for (k = 0; isdigit((unsigned char) machine[k]); k++);
    if ((k > 0
	 && (machine[k] == '/'
	     || (machine[k] == 'i' && machine[k + 1] == '/')))
	|| strstr(machine, " or ") != NULL) {
      printf("Sun Workstation, Model ");
    }
    printf("%s", machine);
    if (strncmp(codenames[0], "sun2", 4) != 0) {
      printf(" (codename \"%s\")", codenames[0]);
    }
    printf("\n");
  }
  else {
    printf("Sun Workstation, Model ");
    for (i = 0; i < codenames_count; i++) {
      if (strncmp(codenames[i], "sun2", 4) != 0) {
	cat(machines[i], sizeof(machines[0]), " (codename \"");
	cat(machines[i], sizeof(machines[0]), codenames[i]);
	cat(machines[i], sizeof(machines[0]), "\")");
      }
      if (i > 0) {
	printf("%s", (codenames_count == 2 ? " or " : ", or "));
      }
      printf("%s", machines[i]);
    }
    printf("\n");
  }
  printf("Serial #%lu, Ethernet address", serial);
  sep = " ";
  for (i = 0; i < 6; i++) {
    printf("%s%x", sep, idprom[2 + i]);
    sep = ":";
  }
  printf("\n");

  /* the manufacture date, in the format of perl's scalar gmtime: */
  date_time = (time_t) date;
  tm = gmtime(&date_time);
  strftime(date_buffer, sizeof(date_buffer), "%a %b %e %H:%M:%S %Y", tm);
  printf("Manufacture date %s\n", date_buffer);
  return (0);
}

/* this parses leading hexadecimal digits, like perl's hex(): */
static unsigned long
hex(const char *s)
{
  unsigned long value;

  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
  }
  for (value = 0; isxdigit((unsigned char) *s); s++) {
    value = (value * 16) + (isdigit((unsigned char) *s)
			    ? (*s - '0')
			    : (tolower((unsigned char) *s) - 'a' + 10));
  }
  return (value);
}

/* this makes a new IDPROM: */
static int
idprom_make(int argc, char **argv)
{
  const char *argv0, *name, *codename, *ether, *serial_arg;
  char *ether_copy, *part, *parts[64];
  int usage, parts_count, i;
  unsigned long ether_bytes[6], serial;
  unsigned char idprom[TME_SUN_IDPROM_BYTES];

  argv0 = argv[0];
  argc--;
  argv++;
  usage = 0;
  codename = NULL;
  ether = NULL;
  parts_count = 0;
  srandom((unsigned int) time(NULL) ^ (unsigned int) getpid());

  /* the machine or code name: */
  if (argc == 0) {
    fprintf(stderr, "missing machine name\n");
    usage = 1;
  }
  else {
    name = *argv++;
    argc--;
    codename = tme_sun_idprom_codename(name);
    if (codename == NULL) {
      fprintf(stderr, "unknown machine name '%s'\n", name);
      usage = 1;
    }
  }

  /* the Ethernet address, six hexadecimal bytes separated by
     colons: */
  if (argc == 0) {
    fprintf(stderr, "missing Ethernet address\n");
    usage = 1;
  }
  else {
    ether = *argv++;
    argc--;
    ether_copy = strdup(ether);
    for (part = ether_copy;;) {
      if (parts_count < 64) {
	parts[parts_count++] = part;
      }
      part = strchr(part, ':');
      if (part == NULL) {
	break;
      }
      *part++ = '\0';
    }
    /* trailing empty parts are ignored: */
    while (parts_count > 0 && parts[parts_count - 1][0] == '\0') {
      parts_count--;
    }
    if (!(isxdigit((unsigned char) ether[0]) || ether[0] == ':')
	|| parts_count != 6) {
      fprintf(stderr, "bad Ethernet address '%s'\n", ether);
      usage = 1;
    }
    else {
      for (i = 0; i < 6; i++) {
	ether_bytes[i] = hex(parts[i]);
      }
    }
  }

  /* the optional serial number: */
  if (argc > 0) {
    serial_arg = *argv++;
    argc--;
    for (i = 0; isdigit((unsigned char) serial_arg[i]); i++);
    if (i == 0 || serial_arg[i] != '\0') {
      fprintf(stderr, "bad serial number '%s'\n", serial_arg);
      usage = 1;
    }
    serial = strtoul(serial_arg, NULL, 10);
  }
  else {
    serial = 1 + (random() % 100000);
  }

  /* standard output must not be a terminal: */
  if (isatty(1)) {
    fprintf(stderr, "standard output must not be a terminal\n");
    usage = 1;
  }

  /* any other arguments are unexpected: */
  if (argc > 0) {
    fprintf(stderr, "unexpected argument '%s'\n", argv[0]);
    usage = 1;
  }

  if (usage) {
    fprintf(stderr, "usage: %s MACHINE ETHERNET [ SERIAL ] > IDPROM\n", argv0);
    fprintf(stderr, "       %s < IDPROM\n", argv0);
    return (1);
  }

  if (ether_bytes[0] != 0x08
      || ether_bytes[1] != 0x00
      || ether_bytes[2] != 0x20) {
    fprintf(stderr, "warning: Ethernet address %s doesn't begin with Sun's OUI 8:0:20\n", ether);
  }

  /* make the IDPROM: */
  tme_sun_idprom_make(idprom, codename, ether_bytes, serial);

  /* dump out the IDPROM: */
  if (fwrite(idprom, 1, sizeof(idprom), stdout) != sizeof(idprom)
      || fflush(stdout) != 0) {
    perror("stdout");
    return (1);
  }
  return (0);
}

int
main(int argc, char **argv)
{
  /* if our standard input is not a terminal, assume we're checking
     an IDPROM there: */
  if (!isatty(0)) {
    return (idprom_check());
  }
  return (idprom_make(argc, argv));
}
