/**
 * @file fbt_code_cache.c
 * Implementation of methods needed to handle the code cache.
 * The code cache stores translated program code and uses the mapping table
 * to map between untranslated and translated code.
 *
 * Copyright (c) 2011 ETH Zurich
 * @author Mathias Payer <mathias.payer@nebelwelt.net>
 *
 * $Date: 2013-02-13 23:03:12 +0100 (Wed, 13 Feb 2013) $
 * $LastChangedDate: 2013-02-13 23:03:12 +0100 (Wed, 13 Feb 2013) $
 * $LastChangedBy: payerm $
 * $Revision: 1591 $
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

#include "generic/fbt_libc.h"
#include "generic/fbt_llio.h"
#include "fbt_code_cache.h"
#include "fbt_datatypes.h"
#include "fbt_debug.h"
#include "fbt_mem_mgmt.h"
#include "fbt_mem_pool.h"
#include "fbt_syscall.h"
#include "fbt_trampoline.h"
#include "libfastbt.h"
#if defined(__i386__)
# include "ia32/fbt_asm_macros.h"
#elif defined(__arm__)
# include "arm/fbt_arm_opcode.h"
# include "arm/fbt_asm_macros.h"
#endif

struct ccache_entry {
  ulong_t *src;
  ulong_t *dst;
};

void *fbt_ccache_find(struct thread_local_data *tld, void *orig_address) {
  PRINT_DEBUG_FUNCTION_START("fbt_ccache_find(*tld=%p, *orig_address=%p)",
                             tld, orig_address);

  assert(tld != NULL);

  /* calculate offset into hashtable (this instruction is our hash function) */
  ulong_t offset = C_MAPPING_FUNCTION((ulong_t)orig_address);
  ulong_t pos = 0;
  struct ccache_entry *entry = tld->mappingtable + offset;

#if defined(FBT_STATISTIC)
  fbt_nr_tcache_slow_lookups++;
#endif

  /* check entry if src address equals orig_address */
  while (entry->src != 0) {
    if (orig_address == entry->src) {
      /* return corresponding dest address */
      PRINT_DEBUG_FUNCTION_END("-> %p", entry->dst);
      assert(entry->dst != NULL);
      if (pos!=0) {
        /* not optimal entry! swap suboptimal entry! */
        void *tmp;
        struct ccache_entry *firstentry = tld->mappingtable +
          C_MAPPING_FUNCTION((ulong_t)orig_address);
        tmp = firstentry->src;
        firstentry->src = entry->src;
        entry->src = tmp;
        tmp = firstentry->dst;
        firstentry->dst = entry->dst;
        entry->dst = tmp;
        entry = firstentry;
      }
      return entry->dst;
    }
    /* We mustn't access memory beyond the hashtable!!
     * Bitwise AND with (HASHTABLE_SIZE - 1) is the same as
     * modulo HASHTABLE_SIZE. */
    offset = (offset + sizeof(struct ccache_entry)) & (MAPPINGTABLE_SIZE-1);
    pos++;
    entry = tld->mappingtable + offset;
  }

  PRINT_DEBUG_FUNCTION_END("-> %p", NULL);
  return NULL;
}

void fbt_ccache_add_entry(struct thread_local_data *tld, void *orig_address,
                          void *transl_address) {
  PRINT_DEBUG_FUNCTION_START("fbt_ccache_add_entry(*tld=%p, *orig_address=%p, "
                             "*transl_address=%p)", tld, orig_address,
                             transl_address);
  /* calculate offset into hashtable that corresponds to this orig_address*/
  ulong_t offset = C_MAPPING_FUNCTION((ulong_t) orig_address);
  struct ccache_entry *entry = tld->mappingtable + offset;

  int count = 0;

#ifdef INLINE_CALLS
  if (tld->trans.inline_call_RIP != NULL) {
    PRINT_DEBUG("We are currently inlining, it would be a bad idea to add this "
                "location to the lookup-table");
    PRINT_DEBUG_FUNCTION_END(" ");
    return;
  }
#endif /* INLINE_CALLS */

#if defined(FBT_STATISTIC)
  fbt_nr_ccf++;
#endif
  /* search the hastable for a free position, beginning at offset */
  while (entry->src != 0) {
    offset = (offset + sizeof(struct ccache_entry)) & (MAPPINGTABLE_SIZE - 1);
    entry = tld->mappingtable + offset;
    count++;
    if (count>=MAPPINGTABLE_MAXENTRIES/10) {
      fbt_suicide_str("ERROR: mappingtable out of space (fbt_code_cache.c)\n");
    }
  }

#if defined(FBT_STATISTIC)
  switch (count) {
  case 0:
    fbt_nr_tcache_direct++;
    break;
  case 1:
    fbt_nr_tcache_1++;
    break;
  case 2:
    fbt_nr_tcache_2++;
    break;
  case 3:
  case 4:
    fbt_nr_tcache_4++;
    break;
  case 5:
  case 6:
  case 7:
  case 8:
    fbt_nr_tcache_8++;
    break;
  default:
    fbt_nr_tcache_8p++;
    printf("Target is far away in hashtable: %d (%p)\n", count, orig_address);
  }
#endif
  /* insert entry into hashtable */
  entry->src = orig_address;
  entry->dst = transl_address;
  DUMP_JMP_TABLE_ENTRY(orig_address, transl_address);
  PRINT_DEBUG_FUNCTION_END(" ");

}

void fbt_ccache_flush(struct thread_local_data *tld) {
  PRINT_DEBUG_FUNCTION_START("fbt_ccache_flush(*tld=%p)", tld);
#if defined(AUTHORIZE_SYSCALLS) && defined(HANDLE_SIGNAL)
  /* save signal handlers (trampolines will be removed in fbt_mem_free) */
  void *signal_handler_targets[MAX_NR_SIGNALS];
  long i;
  for (i = 0; i < MAX_NR_SIGNALS; ++i) {
    struct trampoline *might_be_trampoline = tld->signal_handlers[i];
    signal_handler_targets[i] = NULL;
    /* check if the current handler points to a trampoline, so that we can
     * save the target of the trampoline */
    struct mem_info *mem_info = tld->chunk;
    while (mem_info != NULL) {
      if (PTR_IN_REGION(might_be_trampoline, mem_info->ptr, mem_info->size)) {
        /* different types of internal memory that could match */
        switch (mem_info->type) {
        case MT_TRAMPOLINE:
          /* save target of this trampoline */
          PRINT_DEBUG("Saving target of trampoline (%p) for signal handler\n",
                      might_be_trampoline);
          signal_handler_targets[i] = might_be_trampoline->target;
          break;
        default:
          fbt_suicide_str("Signal handler points into internal BT data " \
                          "(fbt_code_cache.c)\n");
        }
      }
      mem_info = mem_info->next;
    }
  }
#endif
  void *commit_function = fbt_ccache_find_reverse(tld, fbt_end_transaction);
  fbt_reinit_tls(tld);
  fbt_transaction_init(tld, commit_function);
  fbt_initialize_trampolines(tld);
#if defined(AUTHORIZE_SYSCALLS)
  fbt_init_syscalls(tld);
#endif
#if defined(AUTHORIZE_SYSCALLS) && defined(HANDLE_SIGNAL)
  for (i = 0; i < MAX_NR_SIGNALS; ++i) {
    if (signal_handler_targets[i] != NULL) {
      tld->signal_handlers[i] =
          fbt_create_trampoline(tld, signal_handler_targets[i],
                                0x0),
                                ORIGIN_ABSOLUTE);
    }
  }
#endif
  PRINT_DEBUG_FUNCTION_END(" ");
}

void *fbt_ccache_find_reverse(struct thread_local_data *tld,
                              void *transl_address) {
  PRINT_DEBUG_FUNCTION_START("fbt_ccache_find_reverse(*tld=%p,"
                             " *transl_address=%p)", tld, transl_address);
  struct ccache_entry *entry = tld->mappingtable;
  struct ccache_entry *end = tld->mappingtable + MAPPINGTABLE_SIZE;
  /* search the hastable for a free position, beginning at offset */
  while (entry < end) {
    if (entry->dst == transl_address) {
      PRINT_DEBUG_FUNCTION_END("-> %p", entry->src);
      return entry->src;
    }
    entry++;
  }
  PRINT_DEBUG_FUNCTION_END("-> %p", NULL);
  return NULL;
}

struct trampoline *fbt_create_trampoline(struct thread_local_data *tld,
                                         void *call_target, void *origin,
                                         enum origin_type origin_t) {
  if (tld->trans.trampos == NULL) {
    fbt_allocate_new_trampolines(tld);
  }

  struct trampoline *trampos = tld->trans.trampos;
  tld->trans.trampos = tld->trans.trampos->next;

  trampos->target = call_target;
  trampos->origin = origin;
  trampos->origin_t = origin_t;

  Code *code = (Code *)&(trampos->code);

  PRINT_DEBUG("allocated trampolines: %p, target: %p, origin: %p", trampos,
              trampos->target, trampos->origin);

  /* write code to trampoline */
#if defined(__i386__)
  MOV_ESP_MEM32(code, (tld->stack - 1));  /* 6 bytes long */
  MOV_IMM32_ESP(code, (tld->stack - 1));  /* 5 bytes long */
  CALL_REL32(code, tld->unmanaged_code_trampoline); /* 5 bytes long */
#elif defined(__arm__)
  GEN_LDR_IMM32(code, SP, (uint32_t)(tld->stack - 1));
  GEN_B_ABS(code, tld->unmanaged_code_trampoline);
#endif

  return trampos;
}
