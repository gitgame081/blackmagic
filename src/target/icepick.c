/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file implements support for the TI ICEPick controller that sits in front
 * of TAPs in the scan chain on some TI devices.
 *
 * References:
 * SPRUH35 - Using the ICEPick TAP (type-C)
 *   https://www.ti.com/lit/ug/spruh35/spruh35.pdf
 */

#include "general.h"
#include "buffer_utils.h"
#include "jtag_scan.h"
#include "jtagtap.h"
#include "icepick.h"

#define IR_ROUTER      0x02U
#define IR_ICEPICKCODE 0x05U
#define IR_CONNECT     0x07U

/*
 * The type-C value is taken from SPRUH35, the type-D value is
 * from a BeagleBone Black Industrial (AM3358BZCZA100)
 */
#define ICEPICK_TYPE_MASK 0xfff0U
#define ICEPICK_TYPE_C    0x1cc0U
#define ICEPICK_TYPE_D    0xb3d0U

#define ICEPICK_MAJOR_SHIFT 28U
#define ICEPICK_MAJOR_MASK  0xfU
#define ICEPICK_MINOR_SHIFT 24U
#define ICEPICK_MINOR_MASK  0xfU

void icepick_write_ir(jtag_dev_s *device, uint8_t ir);

void icepick_router_handler(const uint8_t dev_index)
{
	jtag_dev_s *const device = &jtag_devs[dev_index];

	/* Switch the ICEPick TAP into its controller identification mode */
	icepick_write_ir(device, IR_ICEPICKCODE);
	/* Then read out the 32-bit controller ID code */
	uint32_t icepick_idcode = 0U;
	jtag_dev_shift_dr(dev_index, (uint8_t *)&icepick_idcode, NULL, 32U);

	/* Check it's a suitable ICEPick controller, and abort if not */
	if ((icepick_idcode & ICEPICK_TYPE_MASK) != ICEPICK_TYPE_D) {
		DEBUG_ERROR("ICEPick is not a type-D controller (%08" PRIx32 ")\n", icepick_idcode);
		return;
	}
	DEBUG_INFO("ICEPick type-D controller v%u.%u (%08" PRIx32 ")\n",
		(icepick_idcode >> ICEPICK_MAJOR_SHIFT) & ICEPICK_MAJOR_MASK,
		(icepick_idcode >> ICEPICK_MINOR_SHIFT) & ICEPICK_MINOR_MASK, icepick_idcode);
}

void icepick_write_ir(jtag_dev_s *const device, const uint8_t ir)
{
	/* Set all the other devices IR's to being in bypass */
	for (size_t device_index = 0; device_index < jtag_dev_count; device_index++)
		jtag_devs[device_index].current_ir = UINT32_MAX;
	/* Put the current device IR into the requested state */
	device->current_ir = ir;

	/* Do the work to make the scanchain match the jtag_devs state */
	jtagtap_shift_ir();
	/* Once in Shift-IR, clock out 1's till we hit the right device in the chain */
	jtag_proc.jtagtap_tdi_seq(false, ones, device->ir_prescan);
	/* Then clock out the new IR value and drop into Exit1-IR on the last cycle if we're the last device */
	jtag_proc.jtagtap_tdi_seq(!device->ir_postscan, &ir, device->ir_len);
	/* Make sure we're in Exit1-IR having clocked out 1's for any more devices on the chain */
	jtag_proc.jtagtap_tdi_seq(true, ones, device->ir_postscan);
	/* Now go to Update-IR but do not go back to Idle */
	jtagtap_return_idle(0);
}
