/* host/display/display-keysym.c - X keysym names and case: */

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

#include <tme/common.h>

/* includes: */
#include "display.h"
#include <string.h>

/* a keysym name: */
struct _tme_keysym_name {
  tme_keyboard_keyval_t value;
  const char *name;
};

#include "keysym-names.h"

/* this returns the name of a keysym, or NULL if it has none.  like
   GDK, this gives no names to keysyms that directly encode Unicode
   characters: */
const char *
_tme_keysym_name(tme_keyboard_keyval_t keysym)
{
  unsigned int lo, hi, mid;

  if ((keysym & 0xff000000) == 0x01000000) {
    return (NULL);
  }

  /* find the first name with this value, which is the name to use: */
  lo = 0;
  hi = TME_ARRAY_ELS(_tme_keysym_names);
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (_tme_keysym_names[mid].value < keysym) {
      lo = mid + 1;
    }
    else {
      hi = mid;
    }
  }
  if (lo < TME_ARRAY_ELS(_tme_keysym_names)
      && _tme_keysym_names[lo].value == keysym) {
    return (_tme_keysym_names[lo].name);
  }
  return (NULL);
}

/* this returns the keysym with a name, or TME_KEYSYM_VOID if there
   is no keysym with that name: */
static tme_keyboard_keyval_t
_tme_keysym_from_name_exact(const char *name)
{
  unsigned int lo, hi, mid;
  const struct _tme_keysym_name *keysym_name;
  int cmp;

  lo = 0;
  hi = TME_ARRAY_ELS(_tme_keysym_names_by_name);
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    keysym_name = &_tme_keysym_names[_tme_keysym_names_by_name[mid]];
    cmp = strcmp(name, keysym_name->name);
    if (cmp == 0) {
      return (keysym_name->value);
    }
    if (cmp < 0) {
      hi = mid;
    }
    else {
      lo = mid + 1;
    }
  }
  return (TME_KEYSYM_VOID);
}

/* this returns the keysym with a name, or TME_KEYSYM_VOID if there
   is no keysym with that name.  a name from XF86keysym.h can have
   the XF86 prefix or not: */
tme_keyboard_keyval_t
_tme_keysym_from_name(const char *name)
{
  tme_keyboard_keyval_t keysym;

  keysym = _tme_keysym_from_name_exact(name);
  if (keysym == TME_KEYSYM_VOID
      && strncmp(name, "XF86", 4) == 0) {
    keysym = _tme_keysym_from_name_exact(name + 4);
  }
  return (keysym);
}

/* this returns the lowercase and uppercase versions of a keysym.  a
   keysym without case is its own lowercase and uppercase version.
   only Latin-1 keysyms have case here, which is all that the host
   keymap needs: */
void
_tme_keysym_convert_case(tme_keyboard_keyval_t keysym,
			 tme_keyboard_keyval_t *_lower,
			 tme_keyboard_keyval_t *_upper)
{
  tme_keyboard_keyval_t lower, upper;

  lower = keysym;
  upper = keysym;

  /* A-Z, Agrave-Odiaeresis, and Oslash-Thorn: */
  if ((keysym >= 0x41 && keysym <= 0x5a)
      || (keysym >= 0xc0 && keysym <= 0xd6)
      || (keysym >= 0xd8 && keysym <= 0xde)) {
    lower = keysym + 0x20;
  }

  /* a-z, agrave-odiaeresis, and oslash-thorn: */
  else if ((keysym >= 0x61 && keysym <= 0x7a)
	   || (keysym >= 0xe0 && keysym <= 0xf6)
	   || (keysym >= 0xf8 && keysym <= 0xfe)) {
    upper = keysym - 0x20;
  }

  *_lower = lower;
  *_upper = upper;
}
