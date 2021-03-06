/**
 * @file fbt_actions.c
 * This module defines some generic default actions that are used to translate
 * specific ARM machine codes.
 *
 * Copyright (c) 2011 ETH Zurich
 * @author Mathias Payer <mathias.payer@nebelwelt.net>
 * @author Felipe O. Carvalho <felipekde@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <assert.h>
#include <stdint.h>

#include "../generic/fbt_libc.h"
#include "../generic/fbt_llio.h"
#include "../fbt_actions.h"
#include "../fbt_datatypes.h"
#include "../fbt_debug.h"
#include "../fbt_code_cache.h"
#include "../fbt_mem_mgmt.h"
#include "../fbt_translate.h"
#include "fbt_arm_opcode.h"

enum translation_state action_none(struct translate *ts __attribute__((unused))) {
  PRINT_DEBUG_FUNCTION_START("action_none(*ts=%p)", ts);
  /* do nothing */
  PRINT_DEBUG_FUNCTION_END("-> neutral");
  return NEUTRAL;
}

enum translation_state action_copy(struct translate *ts) {
  Code *addr = ts->cur_instr;
  Code* transl_addr = ts->transl_instr;

  PRINT_DEBUG_FUNCTION_START(
      "action_copy(*addr=%p, *transl_addr=%p)", addr, transl_addr);

  /* copy instruction verbatim to translated version */
  *(transl_addr++) = *addr;

  if ((ts->cur_instr_info->opcode_flags & 0x1FFF) == SWI) {
    PRINT_DEBUG("Encountered an interrupt - closing TU with some glue code");
    PRINT_DEBUG_FUNCTION_END("-> CLOSE_GLUE");
    return CLOSE_GLUE;
  }

  PRINT_DEBUG_FUNCTION_END("-> NEUTRAL");
  return NEUTRAL;
}

enum translation_state action_warn(struct translate *ts) {
  PRINT_DEBUG_FUNCTION_START("action_warn(*ts=%p)", ts);
  PRINT_DEBUG("unhandled opcode encountered in TU at %p: %s", ts->cur_instr,
              MEMDUMP(ts->cur_instr, ts->next_instr - ts->cur_instr));
  PRINT_DEBUG("Will try if it works to simply copy the instruction into the "
              "code cache, but something bad could happen now...");
  PRINT_DEBUG_FUNCTION_END("-> ???");
  return action_copy(ts);
}

enum translation_state action_fail(struct translate *ts __attribute__((unused))) {
  PRINT_DEBUG_FUNCTION_START("action_fail(*ts=%p)", ts);
  PRINT_DEBUG("unhandled opcode encountered in TU at %p: %s", ts->cur_instr,
              MEMDUMP(ts->cur_instr, ts->next_instr - ts->cur_instr));
  PRINT_DEBUG("giving up!!!");
  PRINT_DEBUG_FUNCTION_END("-> FAIL");

#ifdef DEBUG
  llprintf("\n\n\t\tERROR: unhandled opcode encountered in TU at %p: %s\n\n",
              ts->cur_instr, MEMDUMP(ts->cur_instr,
              ts->next_instr - ts->cur_instr));
  llprintf("\nERROR: unhandled opcode encountered in TU at %p\nBtOX will "
           "exit now!\n", ts->cur_instr);
#endif
#if defined(SLEEP_ON_FAIL)
  llprintf("Something bad happened (action_fail). Attach a debugger!\n");
  while (1);
#endif
  fbt_suicide(255);
  return CLOSE;
}

// TODO: the equivalent of action_jmp on ARM
// TODO: the equivalent of action_jmp_indirect on ARM
// TODO: the equivalent of action_jmp_jcc on ARM

enum translation_state action_branch(struct translate *ts) {
  // TODO(philix): implement action_branch for ARM
  fbt_suicide_str(__func__);
  return CLOSE;
}

enum translation_state action_branch_and_link(struct translate *ts) {
  // TODO(philix): implement action_branch_and_link for ARM
  fbt_suicide_str(__func__);
  return CLOSE;
}

enum translation_state action_sysenter(struct translate *ts) {
  // TODO(philix): implement action_sysenter for ARM
  fbt_suicide_str(__func__);
  return CLOSE;
}

#if !defined(STACKPROTECTION)
enum translation_state action_ret(struct translate *ts) {
  // TODO(philix): implement action_ret for ARM
  fbt_suicide_str(__func__);
  return CLOSE;
}
#endif
