/* libtme/media.c - removable media: */

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
#include <tme/media.h>

/* the media holders, in the order registered: */
static struct tme_media *_tme_media_list;

/* this counts changes to the media holders and what they hold: */
static unsigned long _tme_media_generation;

/* this registers a media holder: */
void
tme_media_register(struct tme_media *media)
{
  struct tme_media **_prev;

  for (_prev = &_tme_media_list;
       *_prev != NULL;
       _prev = &(*_prev)->tme_media_next);
  media->tme_media_next = NULL;
  *_prev = media;
  tme_media_changed();
}

/* this names the media holder of an element, if it has one: */
void
tme_media_name(struct tme_element *element, const char *name)
{
  struct tme_media *media;

  for (media = _tme_media_list;
       media != NULL;
       media = media->tme_media_next) {
    if (media->tme_media_element == element) {
      if (media->tme_media_name != NULL) {
	tme_free(media->tme_media_name);
      }
      media->tme_media_name = tme_strdup(name);
      tme_media_changed();
    }
  }
}

/* this returns the media holders: */
struct tme_media *
tme_media_list(void)
{
  return (_tme_media_list);
}

/* this notes a change to a media holder or what it holds: */
void
tme_media_changed(void)
{
  _tme_media_generation++;
}

/* this returns the count of changes to the media holders and what
   they hold, so a user interface can tell when to update: */
unsigned long
tme_media_generation(void)
{
  return (_tme_media_generation);
}
