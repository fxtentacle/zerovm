/*
 * Copyright (c) 2011 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * NaCl Simple/secure ELF loader (NaCl SEL).
 *
 * NOTE: This header is ALSO included by assembler files and hence
 *       must not include any C code
 */
#ifndef NATIVE_CLIENT_SERVICE_RUNTIME_NACL_CONFIG_H_
#define NATIVE_CLIENT_SERVICE_RUNTIME_NACL_CONFIG_H_

#include "src/include/nacl_base.h"

/* maximum number of elf program headers allowed. */
#define NACL_MAX_PROGRAM_HEADERS  128

/*
 * d'b: taken from "src/include/nacl_asm.h"
 * macros to provide uniform access to identifiers from assembly due
 * to different C -> asm name mangling conventions and other platform-specific
 * requirements
 */
#define IDENTIFIER(n)  n
#define HIDDEN(n)  .hidden IDENTIFIER(n)
#define DEFINE_GLOBAL_HIDDEN_IDENTIFIER(n) \
  .globl IDENTIFIER(n); HIDDEN(n); IDENTIFIER(n)

/*
 * this value must be consistent with NaCl compiler flags
 * -falign-functions -falign-labels -and nacl-align.
 */
#define NACL_BLOCK_SHIFT 5
#define NACL_INSTR_BLOCK_SHIFT        (NACL_BLOCK_SHIFT)
#define NACL_INSTR_BLOCK_SIZE         (1 << NACL_INSTR_BLOCK_SHIFT)

/* this must be a multiple of the system page size */
#define NACL_PAGESHIFT                12
#define NACL_PAGESIZE                 (1U << NACL_PAGESHIFT)

#define NACL_MAP_PAGESHIFT            16
#define NACL_MAP_PAGESIZE             (1U << NACL_MAP_PAGESHIFT)

#if NACL_MAP_PAGESHIFT < NACL_PAGESHIFT
# error "NACL_MAP_PAGESHIFT smaller than NACL_PAGESHIFT"
#endif

/* NACL_MAP_PAGESIFT >= NACL_PAGESHIFT must hold */
#define NACL_PAGES_PER_MAP            (1 << (NACL_MAP_PAGESHIFT-NACL_PAGESHIFT))

#define NACL_MEMORY_ALLOC_RETRY_MAX   256 /* see win/sel_memory.c */

/*
 * NACL_KERN_STACK_SIZE: The size of the secure stack allocated for
 * use while a NaCl thread is in kernel mode, i.e., during
 * startup/exit and during system call processing.
 *
 * This must be greater than 64K for address space squatting, but
 * that uses up too much memory and decreases the maximum number of
 * threads that the system can spawn.  Instead, we grab the vm lock
 * when spawning threads.
 */
#define NACL_KERN_STACK_SIZE          (64 << 10)

/*
 * NACL_CONFIG_PATH_MAX: What is the maximum file path allowed in the
 * NaClSysOpen syscall?  This is on the kernel stack for validation
 * during the processing of the syscall, so beware running into
 * NACL_KERN_STACK_SIZE above.
 */
#define NACL_CONFIG_PATH_MAX          1024

/*
 * newfd value for dup2 must be below this value.
 */
#define NACL_MAX_FD                   4096

/*
 * Macro for the start address of the trampolines
 * The first 64KB (16 pages) are inaccessible.  On x86, this is to prevent
 * addr16/data16 attacks.
 */
#define NACL_SYSCALL_START_ADDR       (16 << NACL_PAGESHIFT)

/* Macro for the start address of a specific trampoline.  */
#define NACL_SYSCALL_ADDR(syscall_number) \
    (NACL_SYSCALL_START_ADDR + (syscall_number << NACL_SYSCALL_BLOCK_SHIFT))

/*
 * Syscall trampoline code have a block size that may differ from the
 * alignment restrictions of the executable.  The ELF executable's
 * alignment restriction (16 or 32) defines what are potential entry
 * points, so the trampoline region must still respect that.  We
 * prefill the trampoline region with HLT, so non-syscall entry points
 * will not cause problems as long as our trampoline instruction
 * sequence never grows beyond 16 bytes, so the "odd" potential entry
 * points for a 16-byte aligned ELF will not be able to jump into the
 * middle of the trampoline code.
 */
#define NACL_SYSCALL_BLOCK_SHIFT      5
#define NACL_SYSCALL_BLOCK_SIZE       (1 << NACL_SYSCALL_BLOCK_SHIFT)

/*
 * the extra space for the trampoline syscall code and the thread
 * contexts must be a multiple of the page size.
 *
 * The address space begins with a 64KB region that is inaccessible to
 * handle NULL pointers and also to reinforce protection agasint abuse of
 * addr16/data16 prefixes.
 * NACL_TRAMPOLINE_START gives the address of the first trampoline.
 * NACL_TRAMPOLINE_END gives the address of the first byte after the
 * trampolines.
 */
#define NACL_NULL_REGION_SHIFT  16
#define NACL_TRAMPOLINE_START   (1 << NACL_NULL_REGION_SHIFT)
#define NACL_TRAMPOLINE_SHIFT   16
#define NACL_TRAMPOLINE_SIZE    (1 << NACL_TRAMPOLINE_SHIFT)
#define NACL_TRAMPOLINE_END     (NACL_TRAMPOLINE_START + NACL_TRAMPOLINE_SIZE)

/*
 * Extra required space at the end of static text (and dynamic text,
 * if any).  The intent is to stop a thread from "walking off the end"
 * of a text region into another.  Four bytes suffices for all
 * currently supported architectures (halt is one byte on x86-32 and
 * x86-64, and 4 bytes on ARM), but for x86 we want to provide
 * paranoia-in-depth: if the NX bit (x86-64, arm) or the %cs segment
 * protection (x86-32) fails on weird borderline cases and the
 * validator also fails to detect a weird instruction sequence, we may
 * have the following situation: a partial multi-byte instruction is
 * placed on the end of the last page, and by "absorbing" the trailing
 * HALT instructions (esp if there are none) as immediate data (or
 * something similar), cause the PC to go outside of the untrusted
 * code space, possibly into data memory.  By requiring 32 bytes of
 * space to fill with HALT instructions, we (attempt to) ensure that
 * such op-code absorption cannot happen, and at least one of these
 * HALTs will cause the untrusted thread to abort, and take down the
 * whole NaCl app.
 */
#define NACL_HALT_SLED_SIZE     32

/*
 * If NACL_MASK_INODES is defined to be 1, then NACL_FAKE_INODE_NUM is
 * used throughout as inode number returned in stat/fstat/getdents
 * system calls.  If NACL_MASK_INODES is defined to be 0, then the
 * service runtime will let the real inode number through.  Exposing
 * inode numbers are a miniscule information leak; more importantly,
 * it is yet another platform difference since none of the standard
 * Windows filesystems have inode numbers.
 */
#if !defined(NACL_MASK_INODES)
# define NACL_MASK_INODES 1
#endif
#if !defined(NACL_FAKE_INODE_NUM) /* allow alternate value */
# define NACL_FAKE_INODE_NUM     0x6c43614e
#endif

#  define NACL_USERRET_FIX        (0x8)
#  define NACL_SYSARGS_FIX        (-0x18)
#  define NACL_SYSCALLRET_FIX     (0x10)
/*
 * System V Application Binary Interface, AMD64 Architecture Processor
 * Supplement, at http://www.x86-64.org/documentation/abi.pdf, section
 * 3.2.2 discusses stack alignment.
 */
#  define NACL_STACK_ALIGN_MASK   (0xf)
#  define NACL_STACK_GETS_ARG     (0)
#  define NACL_STACK_PAD_BELOW_ALIGN (8)

/* d'b: macro definitions for the user space allocation */
#define FOURGIG     (((size_t) 1) << 32)
#define GUARDSIZE   (10 * FOURGIG)
#define ALIGN_BITS  (32)
#define R15_CONST   ((void*)0x440000000000) /* d'b: base address to mmap to */
#define RELATIVE_MMAP (MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE) /* d'b */
#define ABSOLUTE_MMAP (RELATIVE_MMAP | MAP_FIXED) /* d'b */
#define START_OF_USER_SPACE ((uintptr_t)R15_CONST)
#define END_OF_USER_SPACE (START_OF_USER_SPACE + FOURGIG + 2 * GUARDSIZE)
#define LEAST_USER_HEAP_SIZE 8388608 /* d'b: 8mb */

#endif  /* NATIVE_CLIENT_SERVICE_RUNTIME_NACL_CONFIG_H_ */
