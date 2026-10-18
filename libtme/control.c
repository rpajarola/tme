/* libtme/control.c - device controls: */

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
#include <tme/control.h>

/* the controls, in the order registered: */
static struct tme_controls *_tme_controls_list;

/* this counts changes to the controls: */
static unsigned long _tme_controls_generation;

/* a function that loses what doesn't survive losing power: */
struct _tme_power_cycle {
  struct _tme_power_cycle *_tme_power_cycle_next;
  tme_power_cycle_t _tme_power_cycle_func;
  void *_tme_power_cycle_private;
};

/* the functions that lose what doesn't survive losing power: */
static struct _tme_power_cycle *_tme_power_cycles;

/* this registers the controls of an element: */
void
tme_controls_register(struct tme_controls *controls)
{
  struct tme_controls **_prev;

  for (_prev = &_tme_controls_list;
       *_prev != NULL;
       _prev = &(*_prev)->tme_controls_next);
  controls->tme_controls_next = NULL;
  *_prev = controls;
  _tme_controls_generation++;
}

/* this names the controls of an element, if it has any: */
void
tme_controls_name(struct tme_element *element, const char *name)
{
  struct tme_controls *controls;

  for (controls = _tme_controls_list;
       controls != NULL;
       controls = controls->tme_controls_next) {
    if (controls->tme_controls_element == element) {
      if (controls->tme_controls_name != NULL) {
	tme_free(controls->tme_controls_name);
      }
      controls->tme_controls_name = tme_strdup(name);
      _tme_controls_generation++;
    }
  }
}

/* this returns the controls: */
struct tme_controls *
tme_controls_list(void)
{
  return (_tme_controls_list);
}

/* this returns the count of changes to the controls, so a user
   interface can tell when to update.  the state of switches isn't
   counted, since it can change without the controls knowing: */
unsigned long
tme_controls_generation(void)
{
  return (_tme_controls_generation);
}

/* this registers a function that loses what doesn't survive losing
   power: */
void
tme_power_cycle_register(tme_power_cycle_t func, void *private)
{
  struct _tme_power_cycle *power_cycle;

  power_cycle = tme_new(struct _tme_power_cycle, 1);
  power_cycle->_tme_power_cycle_func = func;
  power_cycle->_tme_power_cycle_private = private;
  power_cycle->_tme_power_cycle_next = _tme_power_cycles;
  _tme_power_cycles = power_cycle;
}

/* this loses everything that doesn't survive losing power: */
void
tme_power_cycle(void)
{
  struct _tme_power_cycle *power_cycle;

  for (power_cycle = _tme_power_cycles;
       power_cycle != NULL;
       power_cycle = power_cycle->_tme_power_cycle_next) {
    (*power_cycle->_tme_power_cycle_func)(power_cycle->_tme_power_cycle_private);
  }
}
