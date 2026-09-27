#pragma once
/* Code pointers stored in game memory -> C functions (PORTING.md "Code pointers").
 *
 * The programs keep far pointers to code in their data: the timer callback table (timer_fn[16]), TTM
 * opcode and dialog tables, exit hooks, the video driver's jump table (DS:1938 / 3174 / 351A). The port
 * stores the same relocated seg:off values in mem[] and maps them to C functions here.
 *
 * Program code is registered per program with its FN constant (file segment << 16 | offset; AP =
 * work/AP_flat.exe addresses); shared-library functions once for all three with their F_ id:
 *
 *     codeptr_register(PROG_PS, PS_FN_shell_tick, shell_tick);
 *     codeptr_register_shared(F_timer_isr, timer_isr);
 *     ds_far_wr(off, codeptr_far(PS_FN_shell_tick));              // store it as the original does
 *     CodeFn f = codeptr_lookup(ds_far(off));                        // call it later
 *
 * AP: far pointers kept in data usually point at a VROOMM stub entry (3C59:0020 ...). The flattened
 * image holds JMP FAR thunks there (unovl.py), and codeptr_lookup follows them to the overlay code.
 *
 * Code that exists only at run time (the video driver loaded into a DOS block; the port fills the jump
 * table with pointers into the driver block it allocates) is registered by absolute address with
 * codeptr_register_abs(); those entries are dropped at every program switch.
 *
 * Functions with other signatures are cast to CodeFn at registration and back at the call site. */
#include "mem.h"

typedef void (*CodeFn)(void);

void   codeptr_register(int prog, u32 fn_addr, CodeFn fn);
void   codeptr_register_shared(int f_id, CodeFn fn);       /* every program whose layout has it */
void   codeptr_register_abs(FarPtr p, CodeFn fn);
void   codeptr_clear_abs(void);

FarPtr codeptr_far(u32 fn_addr);            /* relocated far pointer for a FN constant */
FarPtr codeptr_far_shared(int f_id);        /* the current program's address of F_x */
CodeFn codeptr_lookup(FarPtr p);            /* current program; NULL for a null pointer; fatal if unknown */
CodeFn codeptr_lookup_near(u16 file_seg, u16 off);   /* near pointer into a known code segment */
bool   codeptr_known(FarPtr p);
