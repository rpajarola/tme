/* tme/control.h - device controls: */

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

#ifndef _TME_CONTROL_H
#define _TME_CONTROL_H

#include <tme/common.h>

/* includes: */
#include <tme/element.h>

/* elements with controls that a user can work while the machine
   runs, like a machine's reset button and switches, register here, so
   that a user interface can show them: */

/* the control item types: */
#define TME_CONTROL_ITEM_ACTION	(0)
#define TME_CONTROL_ITEM_SWITCH	(1)

/* a control item: */
struct tme_control_item {

  /* the label for the item: */
  _tme_const char *tme_control_item_label;

  /* the item type.  an action is done when the item is chosen, and a
     switch is on or off: */
  unsigned int tme_control_item_type;
};

/* the controls of an element: */
struct tme_controls {

  /* controls are kept on a list: */
  struct tme_controls *tme_controls_next;

  /* the element, and its name, if it has been given one: */
  struct tme_element *tme_controls_element;
  char *tme_controls_name;

  /* the items: */
  _tme_const struct tme_control_item *tme_controls_items;
  unsigned int tme_controls_count;

  /* this returns nonzero iff a switch item is on: */
  int (*tme_controls_get) _TME_P((struct tme_controls *, unsigned int));

  /* this does an action item, or turns a switch item on or off,
     returning an errno value and any error output: */
  int (*tme_controls_set) _TME_P((struct tme_controls *, unsigned int,
				  int, char **_output));
};

/* elements whose contents don't survive losing power, like RAM,
   register a function that loses them.  a machine calls them all when
   it's powered up again, so its firmware sees a cold start: */
typedef void (*tme_power_cycle_t) _TME_P((void *));

/* prototypes: */
void tme_controls_register _TME_P((struct tme_controls *));
void tme_controls_name _TME_P((struct tme_element *, _tme_const char *));
struct tme_controls *tme_controls_list _TME_P((void));
unsigned long tme_controls_generation _TME_P((void));
void tme_power_cycle_register _TME_P((tme_power_cycle_t, void *));
void tme_power_cycle _TME_P((void));

#endif /* !_TME_CONTROL_H */
