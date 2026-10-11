/* tme/media.h - removable media: */

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

#ifndef _TME_MEDIA_H
#define _TME_MEDIA_H

#include <tme/common.h>

/* includes: */
#include <tme/element.h>

/* elements that hold media that can be loaded and unloaded, like
   disks and tapes, register here, so that a user interface can show
   what's loaded and change it: */

/* the media types: */
#define TME_MEDIA_TYPE_DISK	(0)
#define TME_MEDIA_TYPE_TAPE	(1)

/* a media holder: */
struct tme_media {

  /* media holders are kept on a list: */
  struct tme_media *tme_media_next;

  /* the element, and its name, if it has been given one: */
  struct tme_element *tme_media_element;
  char *tme_media_name;

  /* the media type: */
  unsigned int tme_media_type;

  /* this returns a NULL-terminated array of the names of the files
     loaded, which the caller frees with tme_free_string_array(names,
     -1), or NULL if nothing is loaded.  it sets *_read_only iff the
     media is, or was last, loaded read-only: */
  char **(*tme_media_loaded) _TME_P((struct tme_media *, int *_read_only));

  /* this loads media from one or more files, returning an errno
     value and any error output: */
  int (*tme_media_load) _TME_P((struct tme_media *,
				_tme_const char * _tme_const *filenames,
				int read_only,
				char **_output));

  /* this unloads media, returning an errno value and any error
     output: */
  int (*tme_media_unload) _TME_P((struct tme_media *, char **_output));
};

/* prototypes: */
void tme_media_register _TME_P((struct tme_media *));
void tme_media_name _TME_P((struct tme_element *, _tme_const char *));
struct tme_media *tme_media_list _TME_P((void));
void tme_media_changed _TME_P((void));
unsigned long tme_media_generation _TME_P((void));

#endif /* !_TME_MEDIA_H */
