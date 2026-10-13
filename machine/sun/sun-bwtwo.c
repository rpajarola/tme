/* $Id: sun-bwtwo.c,v 1.6 2009/11/08 17:03:58 fredette Exp $ */

/* machine/sun/sun-bwtwo.c - Sun bwtwo emulation: */

/*
 * Copyright (c) 2003, 2004, 2006 Matt Fredette
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

#include <tme/common.h>
_TME_RCSID("$Id: sun-bwtwo.c,v 1.6 2009/11/08 17:03:58 fredette Exp $");

/* includes: */
#include <tme/machine/sun.h>
#include <tme/generic/bus-device.h>
#include <tme/generic/fb.h>
#include "sun-fb.h"
#include <string.h>

/* macros: */

/* bwtwo types: */
#define TME_SUNBW2_TYPE_NULL		(0)
#define TME_SUNBW2_TYPE_MULTIBUS	(1)
#define TME_SUNBW2_TYPE_OLD_ONBOARD	(2)
#define TME_SUNBW2_TYPE_ONBOARD		(3)
#define TME_SUNBW2_TYPE_P4		(4)
#define TME_SUNBW2_TYPE_SBUS		(5)

/* register offsets and sizes: */
#define TME_SUNBW2_REG_CSR_MULTIBUS	(0x81800)
#define TME_SUNBW2_REG_CSR_OLD_ONBOARD	(0x20000)
#define TME_SUNBW2_SIZ_CSR		(0x00002)
#define TME_SUNBW2_SIZ_CSR_PAGE		(0x00800)

/* the bits in the Multibus and old-onboard Control/Status register: */
#define TME_SUNBW2_CSR_ENABLE_VIDEO	(0x8000)	/* enable video */
#define TME_SUNBW2_CSR_ENABLE_COPY	(0x4000)	/* enable copy mode */
#define TME_SUNBW2_CSR_ENABLE_INT	(0x2000)	/* interrupt enable */
#define TME_SUNBW2_CSR_INT_ACTIVE	(0x1000)	/* interrupt is active */
#define TME_SUNBW2_CSR_JUMPER_B		(0x0800)	/* jumper B */
#define TME_SUNBW2_CSR_JUMPER_A		(0x0400)	/* jumper A */
#define TME_SUNBW2_CSR_JUMPER_COLOR	(0x0200)	/* jumper color */
#define TME_SUNBW2_CSR_JUMPER_HIRES	(0x0100)	/* jumper hires */
#define TME_SUNBW2_CSR_COPYBASE_MASK	(0x007E)	/* copybase mask */

#if 0
#define TME_SUNBW2_DEBUG
#endif

/* structures: */

/* the card: */
struct tme_sunbw2 {

  /* our generic Sun framebuffer: */
  struct tme_sunfb tme_sunbw2_sunfb;
#define tme_sunbw2_mutex tme_sunbw2_sunfb.tme_sunfb_mutex
#define tme_sunbw2_bus_subregion_memory tme_sunbw2_sunfb.tme_sunfb_bus_subregion_memory
#define tme_sunbw2_bus_subregion_regs tme_sunbw2_sunfb.tme_sunfb_bus_subregion_regs
#define tme_sunbw2_csr_address tme_sunbw2_bus_subregion_regs.tme_bus_subregion_address_first

  /* the type of the bwtwo: */
  tme_uint32_t tme_sunbw2_type;

  /* our csr: */
  tme_uint16_t tme_sunbw2_csr;

  /* in copy mode, the contents of the main memory region when it
     was last checked, and the CSR they are for: */
  tme_uint8_t *tme_sunbw2_copy_shadow;
  tme_uint16_t tme_sunbw2_copy_csr;
};

#ifdef TME_SUNBW2_DEBUG
#define TME_SUNBW2_LO_WIDTH	(1152)
#define TME_SUNBW2_LO_HEIGHT	(900)
static int
_tme_sunbw2_update_debug(struct tme_fb_connection *conn_fb)
{
  struct tme_sunbw2 *sunbw2;
  static int y = -1;
  static int x;
  unsigned long pixel;
  unsigned int pixel_byte;
  tme_uint8_t pixel_bit;
  int box, box_y, box_x;

  sunbw2 = conn_fb->tme_fb_connection.tme_connection_element->tme_element_private;

  for (box = 0; box < 2; box++) {
    if (y < 0) {
      y = 16;
      x = 0;
      continue;
    }
    for (box_y = 0; box_y < 2; box_y++) {
      for (box_x = 0; box_x < 2; box_x++) {
	pixel = (((y + box_y)
		  * TME_SUNBW2_LO_WIDTH)
		 + x
		 + box_x);
	pixel_byte = (pixel / 8);
	pixel_bit = (0x80 >> (pixel % 8));
	sunbw2->tme_sunbw2_fb_memory[pixel_byte] ^= pixel_bit;
      }
    }
    if (box == 0) {
      x += 2;
      if (x == TME_SUNBW2_LO_WIDTH) {
	x = 0;
	y += 2;
	if (y == TME_SUNBW2_LO_HEIGHT) {
	  y = 0;
	}
      }
    }
  }
    
  return (TME_OK);
}
#undef TME_SUNBW2_LO_WIDTH
#undef TME_SUNBW2_LO_HEIGHT
#endif /* TME_SUNBW2_DEBUG */

/* the sunbw2 CSR bus cycle handler: */
static int
_tme_sunbw2_bus_cycle_csr(void *_sunbw2, struct tme_bus_cycle *cycle_init)
{
  struct tme_sunbw2 *sunbw2;
  tme_uint16_t csr_old, csr_new;
  tme_bus_addr32_t undecoded;

  /* recover our data structure: */
  sunbw2 = (struct tme_sunbw2 *) _sunbw2;

  /* lock the mutex: */
  tme_mutex_lock(&sunbw2->tme_sunbw2_mutex);

  /* get the old CSR value: */
  csr_old = tme_betoh_u16(sunbw2->tme_sunbw2_csr);

  /* the entire 2KB (one page's) worth of addresses at
     tme_sunbw2_csr_address are all decoded (or, rather, not decoded)
     as the CSR: */
  undecoded
    = (cycle_init->tme_bus_cycle_address
       & (TME_SUNBW2_SIZ_CSR_PAGE - sizeof(sunbw2->tme_sunbw2_csr)));
  cycle_init->tme_bus_cycle_address
    -= undecoded;

  /* run the cycle: */
  assert (cycle_init->tme_bus_cycle_address
	  >= sunbw2->tme_sunbw2_csr_address);
  tme_bus_cycle_xfer_memory(cycle_init, 
			    (((tme_uint8_t *) &sunbw2->tme_sunbw2_csr)
			     - sunbw2->tme_sunbw2_csr_address),
			    (sunbw2->tme_sunbw2_csr_address
			     + sizeof(sunbw2->tme_sunbw2_csr)
			     - 1));
  cycle_init->tme_bus_cycle_address
    += undecoded;

  /* get the new CSR value: */
  csr_new = tme_betoh_u16(sunbw2->tme_sunbw2_csr);

  /* put back the unchanging bits: */
  csr_new
    = ((csr_new
	& ~(TME_SUNBW2_CSR_INT_ACTIVE
	    | TME_SUNBW2_CSR_JUMPER_B
	    | TME_SUNBW2_CSR_JUMPER_A
	    | TME_SUNBW2_CSR_JUMPER_COLOR
	    | TME_SUNBW2_CSR_JUMPER_HIRES))
       | (csr_old
	  & (TME_SUNBW2_CSR_INT_ACTIVE
	     | TME_SUNBW2_CSR_JUMPER_B
	     | TME_SUNBW2_CSR_JUMPER_A
	     | TME_SUNBW2_CSR_JUMPER_COLOR
	     | TME_SUNBW2_CSR_JUMPER_HIRES)));

  /* clearing the interrupt enable clears any active interrupt: */
  if (!(csr_new & TME_SUNBW2_CSR_ENABLE_INT)) {
    csr_new &= ~TME_SUNBW2_CSR_INT_ACTIVE;
  }

  /* set the new CSR value: */
  sunbw2->tme_sunbw2_csr = tme_htobe_u16(csr_new);

  /* if the interrupt signal should change, wake the callout thread: */
  if (!(csr_new & TME_SUNBW2_CSR_INT_ACTIVE)
      != !sunbw2->tme_sunbw2_sunfb.tme_sunfb_int_asserted) {
    tme_cond_notify(&sunbw2->tme_sunbw2_sunfb.tme_sunfb_callout_cond, FALSE);
  }

  /* unlock the mutex: */
  tme_mutex_unlock(&sunbw2->tme_sunbw2_mutex);

  /* no faults: */
  return (TME_OK);
}

/* this gets the state of the CSR interrupt: */
static int
_tme_sunbw2_int_get(struct tme_sunfb *sunfb)
{
  struct tme_sunbw2 *sunbw2;

  /* recover our data structure: */
  sunbw2 = (struct tme_sunbw2 *) sunfb;

  return ((tme_betoh_u16(sunbw2->tme_sunbw2_csr) & TME_SUNBW2_CSR_INT_ACTIVE) != 0);
}

/* in copy mode, writes to a 128KB region of main memory, selected by
   the copybase bits in the CSR, are also written to the framebuffer
   memory, so that software can draw in (faster) main memory.  we
   emulate this before each display update by copying the bytes of
   that region that have changed since the last update into the
   framebuffer memory, so writes made directly to the framebuffer
   memory stay until the same bytes are written in main memory.  this
   must be called with the mutex locked: */
static void
_tme_sunbw2_copy(struct tme_sunbw2 *sunbw2, tme_uint16_t csr)
{
  struct tme_sunfb *sunfb;
  struct tme_bus_connection *conn_bus;
  struct tme_bus_tlb tlb;
  struct tme_token token;
  tme_bus_addr32_t copybase;
  tme_uint32_t size, offset, count, byte_i;
  const tme_uint8_t *memory;
  tme_uint8_t *shadow;
  int start;

  sunfb = &sunbw2->tme_sunbw2_sunfb;

  /* get the region of main memory, and the size of the displayed
     framebuffer memory: */
  copybase = ((tme_bus_addr32_t) (csr & TME_SUNBW2_CSR_COPYBASE_MASK)) << 16;
  size = (sunfb->tme_sunfb_memory_address_last_displayed
	  - sunbw2->tme_sunbw2_bus_subregion_memory.tme_bus_subregion_address_first) + 1;

  /* get our bus connection: */
  conn_bus = tme_memory_atomic_pointer_read(struct tme_bus_connection *,
					    sunfb->tme_sunfb_device.tme_bus_device_connection,
					    &sunfb->tme_sunfb_device.tme_bus_device_connection_rwlock);

  /* if copy mode has just been enabled, or the region has changed,
     only take a snapshot of the region now, since nothing has been
     written to it yet: */
  if (sunbw2->tme_sunbw2_copy_shadow == NULL) {
    sunbw2->tme_sunbw2_copy_shadow = tme_new(tme_uint8_t, size);
    sunbw2->tme_sunbw2_copy_csr = 0;
  }
  shadow = sunbw2->tme_sunbw2_copy_shadow;
  start = (((sunbw2->tme_sunbw2_copy_csr ^ csr)
	    & (TME_SUNBW2_CSR_ENABLE_COPY | TME_SUNBW2_CSR_COPYBASE_MASK))
	   != 0);
  sunbw2->tme_sunbw2_copy_csr = csr;

  /* copy the changed bytes in the region, as far as it can be read
     quickly: */
  tme_token_init(&token);
  for (offset = 0; offset < size; offset += count) {
    tlb.tme_bus_tlb_token = &token;
    if ((*conn_bus->tme_bus_tlb_fill)(conn_bus, &tlb, copybase + offset, TME_BUS_CYCLE_READ) != TME_OK
	|| tlb.tme_bus_tlb_emulator_off_read == TME_EMULATOR_OFF_UNDEF
	|| (copybase + offset) < tlb.tme_bus_tlb_addr_first
	|| (copybase + offset) > tlb.tme_bus_tlb_addr_last) {
      break;
    }
    count = TME_MIN(size - offset, (tlb.tme_bus_tlb_addr_last - (copybase + offset)) + 1);
    memory = (const tme_uint8_t *) (tlb.tme_bus_tlb_emulator_off_read + copybase + offset);
    if (start) {
      memcpy(shadow + offset, memory, count);
      continue;
    }
    for (byte_i = 0; byte_i < count; byte_i++) {
      if (memory[byte_i] != shadow[offset + byte_i]) {
	shadow[offset + byte_i] = memory[byte_i];
	sunfb->tme_sunfb_memory[offset + byte_i] = memory[byte_i];
      }
    }
  }
}

/* this is called before the display is updated, which we treat as a
   vertical retrace: */
static int
_tme_sunbw2_memory_update(struct tme_fb_connection *conn_fb)
{
  struct tme_sunbw2 *sunbw2;
  tme_uint16_t csr;

  /* recover our data structure: */
  sunbw2 = conn_fb->tme_fb_connection.tme_connection_element->tme_element_private;

  /* lock the mutex: */
  tme_mutex_lock(&sunbw2->tme_sunbw2_mutex);

  /* in copy mode, copy main memory into the framebuffer memory: */
  csr = tme_betoh_u16(sunbw2->tme_sunbw2_csr);
  if (csr & TME_SUNBW2_CSR_ENABLE_COPY) {
    _tme_sunbw2_copy(sunbw2, csr);
  }
  else {
    sunbw2->tme_sunbw2_copy_csr = csr;
  }

  /* if the interrupt is enabled, it becomes active, and stays active
     until it's disabled: */
  if ((csr & TME_SUNBW2_CSR_ENABLE_INT)
      && !(csr & TME_SUNBW2_CSR_INT_ACTIVE)) {
    csr |= TME_SUNBW2_CSR_INT_ACTIVE;
    sunbw2->tme_sunbw2_csr = tme_htobe_u16(csr);
    tme_cond_notify(&sunbw2->tme_sunbw2_sunfb.tme_sunfb_callout_cond, FALSE);
  }

  /* like other bwtwos, we don't track which parts of the framebuffer
     memory have been written, so the whole display must be checked: */
  conn_fb->tme_fb_connection_offset_updated_first = 0;
  conn_fb->tme_fb_connection_offset_updated_last = 0 - (tme_uint32_t) 1;

  /* unlock the mutex: */
  tme_mutex_unlock(&sunbw2->tme_sunbw2_mutex);

  return (TME_OK);
}

/* this sets the bwtwo type: */
static const char *
_tme_sunbw2_type_set(struct tme_sunfb *sunfb, const char *bw2_type_string)
{
  struct tme_sunbw2 *sunbw2;
  tme_uint32_t bw2_type;

  /* recover our data structure: */
  sunbw2 = (struct tme_sunbw2 *) sunfb;

  /* see if this is a good type: */
  bw2_type = TME_SUNBW2_TYPE_NULL;
  if (TME_ARG_IS(bw2_type_string, "multibus")) {
    bw2_type = TME_SUNBW2_TYPE_MULTIBUS;
  }
  else if (TME_ARG_IS(bw2_type_string, "old-onboard")) {
    bw2_type = TME_SUNBW2_TYPE_OLD_ONBOARD;
  }
  else if (TME_ARG_IS(bw2_type_string, "onboard")) {
    bw2_type = TME_SUNBW2_TYPE_ONBOARD;
  }
  else if (TME_ARG_IS(bw2_type_string, "P4")) {
    bw2_type = TME_SUNBW2_TYPE_P4;
  }
  else if (TME_ARG_IS(bw2_type_string, "sbus")) {
    bw2_type = TME_SUNBW2_TYPE_SBUS;
  }

  /* set the new type: */
  sunbw2->tme_sunbw2_type = bw2_type;

  /* assume the (relative) bus address of the first byte of displayed
     framebuffer memory is zero: */
  sunbw2->tme_sunbw2_bus_subregion_memory.tme_bus_subregion_address_first = 0;

  /* assume that we can use the unspecified interrupt: */
  sunfb->tme_sunfb_bus_signal_int = TME_BUS_SIGNAL_INT_UNSPEC;

  /* dispatch on the new type: */
  switch (bw2_type) {

    /* if this was a bad type, return a string of types: */
  default: assert(FALSE);
  case TME_SUNBW2_TYPE_NULL:
    return ("multibus | old-onboard | onboard | P4 | sbus");

  case TME_SUNBW2_TYPE_MULTIBUS:
  case TME_SUNBW2_TYPE_OLD_ONBOARD:

    /* set the addresses of the CSR bus subregion: */
    sunbw2->tme_sunbw2_csr_address
      = (bw2_type == TME_SUNBW2_TYPE_MULTIBUS
	 ? TME_SUNBW2_REG_CSR_MULTIBUS
	 : TME_SUNBW2_REG_CSR_OLD_ONBOARD);
    sunbw2->tme_sunbw2_bus_subregion_regs.tme_bus_subregion_address_last
      = (sunbw2->tme_sunbw2_csr_address
	 + TME_SUNBW2_SIZ_CSR_PAGE
	 - 1);

    /* set the CSR bus cycle handler, and the CSR interrupt: */
    sunfb->tme_sunfb_bus_handler_regs = _tme_sunbw2_bus_cycle_csr;
    sunfb->tme_sunfb_int_get = _tme_sunbw2_int_get;
    sunfb->tme_sunfb_memory_update = _tme_sunbw2_memory_update;

    /* the original Multibus bwtwo and onboard bwtwo only support
       1152x900 and 1024x1024: */
    sunfb->tme_sunfb_size
      = (TME_SUNFB_SIZE_1152_900
	 | TME_SUNFB_SIZE_1024_1024);
    break;

  case TME_SUNBW2_TYPE_ONBOARD:

    /* the onboard bwtwo doesn't have any CSR: */
    sunfb->tme_sunfb_bus_handler_regs = NULL;
    
    /* the sizes supported by a CSR-less bwtwo appear to depend on the
       actual model; we assume the user knows what he is doing: */
    sunfb->tme_sunfb_size = (TME_SUNFB_SIZE_NULL - 1);
    break;

  case TME_SUNBW2_TYPE_P4:

    /* set our initial P4 register: */
    sunfb->tme_sunfb_p4 = tme_htobe_u32(TME_SUNFB_P4_ID_BWTWO);

    /* set the P4 register bus cycle handler: */
    sunfb->tme_sunfb_bus_handler_regs = tme_sunfb_bus_cycle_p4;

    /* we support the default P4 framebuffer sizes: */
    sunfb->tme_sunfb_size = 0;

    /* the framebuffer memory begins at a fixed offset after the P4 register: */
    sunbw2->tme_sunbw2_bus_subregion_memory.tme_bus_subregion_address_first
      = TME_SUNFB_P4_OFFSET_BITMAP;

    break;

  case TME_SUNBW2_TYPE_SBUS:

    /* set the S4 register bus cycle handler: */
    sunfb->tme_sunfb_bus_handler_regs = tme_sunfb_bus_cycle_s4;

    /* we support the default S4 framebuffer sizes: */
    sunfb->tme_sunfb_size = 0;

    /* we use the default S4 memory address: */
    sunbw2->tme_sunbw2_bus_subregion_memory.tme_bus_subregion_address_first = 0;

    /* the SBus bwtwo uses priority seven interrupts: */
    sunfb->tme_sunfb_bus_signal_int = TME_BUS_SIGNAL_INT(7);

    break;
  }

  /* success: */
  return (NULL);
}

/* the new sun bwtwo function: */
int
tme_sun_bwtwo(struct tme_element *element, const char * const *args, char **_output)
{
  struct tme_sunbw2 *sunbw2;
  int rc;
  tme_uint16_t v;
  
  /* start the sunbw2 structure: */
  sunbw2 = tme_new0(struct tme_sunbw2, 1);
  sunbw2->tme_sunbw2_sunfb.tme_sunfb_element = element;

  /* initialize the sunfb structure: */
  sunbw2->tme_sunbw2_sunfb.tme_sunfb_class = TME_FB_XLAT_CLASS_MONOCHROME;
  sunbw2->tme_sunbw2_sunfb.tme_sunfb_depth = 1;
  sunbw2->tme_sunbw2_sunfb.tme_sunfb_type_set = _tme_sunbw2_type_set;

  /* if the generic initialization fails: */
  rc = tme_sunfb_new(&sunbw2->tme_sunbw2_sunfb, args, _output);
  if (rc) {

    /* free the sunfb structure and return the error: */
    tme_free(sunbw2);
    return (rc);
  }

  /* dispatch on the bwtwo type: */
  switch (sunbw2->tme_sunbw2_type) {
  default: break;
  case TME_SUNBW2_TYPE_MULTIBUS:
  case TME_SUNBW2_TYPE_OLD_ONBOARD:

    v = TME_SUNBW2_CSR_ENABLE_VIDEO
		      | (sunbw2->tme_sunbw2_sunfb.tme_sunfb_size == TME_SUNFB_SIZE_1024_1024
			 ? TME_SUNBW2_CSR_JUMPER_HIRES
			 : 0);
    /* set our initial CSR: */
    sunbw2->tme_sunbw2_csr
      = tme_htobe_u16(v);

    break;
  }

  return (TME_OK);
}

