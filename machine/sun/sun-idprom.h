/* machine/sun/sun-idprom.h - Sun IDPROM contents: */

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

/* this is plain C, shared by the tme-sun-idprom tool and the
   emulator, which uses it to create missing IDPROM, EEPROM and NVRAM
   files: */

#ifndef _MACHINE_SUN_SUN_IDPROM_H
#define _MACHINE_SUN_SUN_IDPROM_H

#include <stddef.h>

/* the size of an IDPROM: */
#define TME_SUN_IDPROM_BYTES		(32)

/* the size of a Sun-3 EEPROM: */
#define TME_SUN3_EEPROM_BYTES		(2048)

/* the size of a sun4c NVRAM, not counting the clock registers at the
   end, and the offset of the IDPROM at the end of it: */
#define TME_SUN4C_NVRAM_BYTES		(2040)
#define TME_SUN4C_NVRAM_OFF_IDPROM	(TME_SUN4C_NVRAM_BYTES - TME_SUN_IDPROM_BYTES)

/* pairs of machine names and their Sun code names, ending with NULL.
   a leading asterisk marks the name shown for a code name in a
   banner: */
extern const char * const tme_sun_idprom_machines[];

/* pairs of Sun code names and their IDPROM machine type bytes: */
struct tme_sun_idprom_codename {
  const char *codename;
  unsigned int machtype;
};
extern const struct tme_sun_idprom_codename tme_sun_idprom_codenames[];

/* this returns the IDPROM machine type for a code name, or -1: */
int tme_sun_idprom_machtype(const char *codename);

/* this returns the code name for a machine name or a code name, or
   NULL: */
const char *tme_sun_idprom_codename(const char *name);

/* this makes an IDPROM for a code name, with the given Ethernet
   address and serial number: */
void tme_sun_idprom_make(unsigned char *idprom, const char *codename,
			 const unsigned long *ether, unsigned long serial);

/* this makes an IDPROM for a code name, with a random Ethernet
   address in Sun's OUI and a random serial number: */
void tme_sun_idprom_make_random(unsigned char *idprom, const char *codename);

/* the default Sun-3 EEPROM contents: */
extern const unsigned char tme_sun3_eeprom_default[TME_SUN3_EEPROM_BYTES];

/* this creates a file with the given contents, if it doesn't exist
   already.  it returns zero if the file now exists, sets *_created
   if this created it, and otherwise returns an errno value: */
int tme_sun_file_create(const char *filename, const void *contents,
			size_t size, int *_created);

/* this creates a file holding an IDPROM, or a larger image of SIZE
   bytes with an IDPROM at OFFSET, if it doesn't exist already, and
   says so on standard error, calling it WHAT.  it returns zero if the
   file now exists, and otherwise returns an errno value: */
int tme_sun_idprom_file_create(const char *filename, const char *codename,
			       size_t size, size_t offset, const char *what);

#endif /* !_MACHINE_SUN_SUN_IDPROM_H */
