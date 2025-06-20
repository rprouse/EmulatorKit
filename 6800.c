/*
 *	6800/6801/6802/6803/6808/6303 CPU and internal I/O emulation
 *	(except for later 6303 with extended I/O)
 *
 *	The 6803 is a 6800 but with some timing differences and the following
 *	extras
 *
 *	ABX
 *	ADDD
 *	ASLD/LSLD
 *	BHS		(extra name for BCC)
 *	BLO		(extra name for BCS)
 *	BRN		(branch never)
 *	JSR direct
 *	LDD
 *	LSL		(actually ASL... same thing)
 *	LSRD
 *	MUL
 *	PSHX
 *	PULX
 *	STD
 *	SUBD
 *	CPX		(adds more conditions)
 *
 *	New interrupt vectors
 *	FFF8	IRQ1/IS3
 *	FFF6	ICF	}
 *	FFF4	OCF	}	IRQ2
 *	FFF2	TOF	}
 *	FFF0	SCI	}
 *
 *	A set of I/O space registers
 *
 *	We do not model most of the I/O and we don't model all the fill
 *	cycles fetching 0xFFFF etc as emulator callbacks. We also as a result
 *	don't model the fact timers are read mid elapsed instruction time.
 *
 *	6303 changes some timings and adds more instructions
 *
 *	6301X/Y adds a second output compare, 8bit up counter and time
 *	constant register
 *
 *	TODO:
 *	6303 address error if execute 0000-001F
 *	strictly an interrupt that occurs in the last cycle of
 *	an instruction is ignored. Also we should model the TPA
 *	and SEI subtle delay behaviours. Even more so on 6303
 *	6800 and 6803 exact emulation for undocumented
 *
 *	68HC11 TODO:
 *	Add the rest of the 68HC11 interrupt source/vectors
 *	Expand the very basic 68HC11 I/O model
 *	Special mode and vectors
 *	Test with something like Buffalo ?
 *	interrupt enable 1 clock delay on CLI and TAP
 *
 *	6803 undocumenteds we need to dig into adding
 *
 *	02		if C is set A = 255, else A = 0 (no flag effect)
 *	03		A = 0xFF
 *	12		A = A - B - carry
 *	13		A = A - B - 1
 *	14		B = A - !C
 *	15/1D		A = B - 1
 *	18 or 1A	A += B but different C/H behaviour
 *	1E		B = A
 *	1F		A = B, set carry
 *	41		TSTA variant
 *	42/52/62/72	negate with carry (SBC from 0) on byte
 *			42 A 52 B
 *	45		LSR A
 *	4B		DEC A
 *	51		TSTB variant
 *	55		LSR B
 *	5B		DEC B
 *	61		TST x variant
 *	62		negate x
 *	65		LSR x
 *	6B		DEC x
 *	71		TST M
 *	72		NEG M
 *	75		LSR m
 *	7B		DEC m
 *
 *	The NAMCO processors (60A1, 63A1) also add
 *	12/13		addx 1,sp
 *
 *	For 6800 see Byte 12/77 article
 */


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "6800.h"

#define REG_D	((cpu->a << 8) | (cpu->b))
#define CARRY	(cpu->p & P_C)
#define HALFCARRY	(cpu->p & P_H)

/* Fake a boot rom so special mode vectors work. For now our rom
   just reports E9E9 for the part which will do for a bit */

#ifdef WITH_HC11

static const uint8_t dummy_bootrom[256] = {
/* 00 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 10 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 20 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 30 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 40 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 50 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 60 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 70 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 80 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 90 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* A0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* B0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* C0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* D0 */
	0x00, 0x41, 0x00, 0x00, 0xE9, 0xE9, 0x00, 0xC4,
	0x00, 0xC7, 0x00, 0xCA, 0x00, 0xCD, 0x00, 0xD0,
/* E0 */
	0x00, 0xD3, 0x00, 0xD6, 0x00, 0xD9, 0x00, 0xDC,
	0x00, 0xDF, 0x00, 0xE2, 0x00, 0xE5, 0x00, 0xE8,
/* F0 */
	0x00, 0xEB, 0x00, 0xEE, 0x00, 0xF1, 0x00, 0xF4,
	0x00, 0xF7, 0x00, 0xFA, 0x00, 0xFD, 0x00, 0x00
};

#endif

/*
 *	Timing and validity table
 *
 *	The 6803 is much like the 6800 but indexing is a clock faster,
 *	branches are a clock faster. Basically any ops involving a 16bit
 *	add are a cycle faster. 6303 is pipelined so usually one less
 *	than the 6803.
 *
 *	CPU-wise the 6808 and 6802 are the same so we don't treat the CPU
 *	part differently. The 6808 in fact to us is just a 6800.
 *
 *	The 68HC11 is a somewhat extended CPU but basically a 6803 with
 *	extras, and lacking the 6303 pipelining.
 *
 *	Timing for undocumented instructions is uncertain. Needs confirming
 */


static uint8_t clock_count[256][4] = {
	{ 0, 0, 0, 0 },
	{ 2, 2, 1, 1 }, 		/* NOP */
	{ 0, 2, 0, 0 },			/* undoc SEX 68HC11: IDIV */
	{ 0, 2, 0, 0 },			/* undoc LDA 255, 68HC11: FDIV */
	{ 0, 3, 1, 1 },			/* LSRD */
	{ 0, 3, 1, 1 },			/* ASLD */
	{ 2, 2, 1, 1 },			/* TAP */
	{ 2, 2, 1, 1 },			/* TPA */
	{ 4, 3, 1, 1 },			/* INX */
	{ 4, 3, 1, 1 },			/* DEX */
	{ 2, 2, 1, 1 },			/* CLV */
	{ 2, 2, 1, 1 },			/* SEV */
	{ 2, 2, 1, 1 },			/* CLC */
	{ 2, 2, 1, 1 },			/* SEC */
	{ 2, 2, 1, 1 },			/* CLI */
	{ 2, 2, 1, 1 },			/* SEI */
	/* 0x10 */
	{ 2, 2, 1, 1 },			/* SBA */
	{ 2, 2, 1, 1 },			/* CBA */
	/* Timing for NAMCO guessed on these two FIXME */
	{ 0, 2, 0, 4 },			/* Undoc SBC B from A. NAMCO Add ,S to X */
	{ 0, 2, 0, 4 },			/* Undoc SUB B+1 from A, NAMCO Add ,S t X, 68HC11: BRCLR dir */
	{ 2, 2, 1, 1 },			/* undoc NBA, undoc B = A - !C,  68HC11: BRSET dir */
	{ 0, 0, 0, 0 },			/* 68HC11: BCLR dir */
	{ 2, 2, 1, 1 },			/* TAB */
	{ 2, 2, 1, 1 },			/* TBA */
	{ 0, 2, 2, 2 },			/* Undoc add 6303: XGDX  (Y prefix on HC11) */
	{ 2, 2, 1, 1 },			/* DAA */
	{ 0, 2, 4, 4 },			/* Undoc add, SLP , 68HC11: prefix 1A */
	{ 2, 2, 1, 1 },			/* ABA */
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },			/* 68HC11: BCLR indexed */
	{ 0, 2, 0, 0 },			/* Undoc TAB 68HC11: BRSET indexed */
	{ 0, 2, 0, 0 },			/* Undoc TBA set carry, 68HC11: BRCLR indexed */
	/* 0x20 */
	{ 4, 3, 3, 3 },			/* BRA */
	{ 4, 3, 3, 3 },			/* BRN - undocumented on 6800 */
	{ 4, 3, 3, 3 },			/* BHI */
	{ 4, 3, 3, 3 },			/* BLS */
	{ 4, 3, 3, 3 },			/* BCC */
	{ 4, 3, 3, 3 },			/* BCS */
	{ 4, 3, 3, 3 },			/* BNE */
	{ 4, 3, 3, 3 },			/* BEQ */
	{ 4, 3, 3, 3 },			/* BVC */
	{ 4, 3, 3, 3 },			/* BVS */
	{ 4, 3, 3, 3 },			/* BPL */
	{ 4, 3, 3, 3 },			/* BMI */
	{ 4, 3, 3, 3 },			/* BGE */
	{ 4, 3, 3, 3 },			/* BLT */
	{ 4, 3, 3, 3 },			/* BGT */
	{ 4, 3, 3, 3 },			/* BLE */
	/* 0x30 */
	{ 4, 3, 1, 1 },			/* TSX */
	{ 4, 3, 1, 1 },			/* INS */
	{ 4, 4, 3, 3 },			/* PULA */
	{ 4, 4, 3, 3 },			/* PULB */
	{ 4, 3, 1, 1 },			/* DES */
	{ 4, 3, 1, 1 },			/* TXS */
	{ 4, 3, 4, 4 },			/* PSHA */
	{ 4, 3, 4, 4 },			/* PSHB */
	{ 0, 5, 4, 4 },			/* PULX */
	{ 5, 5, 5, 5 },			/* RTS */
	{ 0, 3, 1, 1 },			/* ABX */
	{ 10, 10, 10, 10 },		/* RTI */
	{ 0, 4, 5, 5 },			/* PSHX */
	{ 0, 10, 7, 7 },		/* MUL */
	{ 9, 9, 9, 9 },			/* WAI */
	{ 12, 12, 12, 12 },		/* SWI */
	/* 0x40 A form */
	{ 2, 2, 1, 1 },			/* NEGA */
	{ 0, 2, 0, 0 },			/* TSTA alias on 6803 */
	{ 0, 2, 0, 0 },			/* Undoc negate variant on 6803 */
	{ 2, 2, 1, 1 },			/* COMA */
	{ 2, 2, 1, 1 },			/* LSRA */
	{ 0, 2, 0, 0 },			/* undoc LSRA alias on 6803 */
	{ 2, 2, 1, 1 },			/* RORA */
	{ 2, 2, 1, 1 },			/* ASRA */
	{ 2, 2, 1, 1 },			/* ASLA */
	{ 2, 2, 1, 1 },			/* ROLA */
	{ 2, 2, 1, 1 },			/* DECA */
	{ 0, 2, 0, 0 },			/* undoc DECA alias on 6803 */
	{ 2, 2, 1, 1 },			/* INCA */
	{ 2, 2, 1, 1 },			/* TSTA */
	{ 255, 255, 255, 255 },		/* HCF / T */
	{ 2, 2, 1, 1 },			/* CLRA */
	/* 0x50 B form */
	{ 2, 2, 1, 1 },			/* NEGB */
	{ 0, 2, 0, 0 },			/* TSTB alias undoc */
	{ 0, 2, 0, 0 },			/* undoc negate variant on 6803 */
	{ 2, 2, 1, 1 },			/* COMB */
	{ 2, 2, 1, 1 },			/* LSRB */
	{ 0, 2, 0, 0 },			/* LSRB alias, undoc */
	{ 2, 2, 1, 1 },			/* RORB */
	{ 2, 2, 1, 1 },			/* ASRB */
	{ 2, 2, 1, 1 },			/* ASLB */
	{ 2, 2, 1, 1 },			/* ROLB */
	{ 2, 2, 1, 1 },			/* DECB */
	{ 0, 2, 0, 0 },			/* DECB alias undoc */
	{ 2, 2, 1, 1 },			/* INCB */
	{ 2, 2, 1, 1 },			/* TSTB */
	{ 2, 2, 1, 1 },			/* HCF / T */
	{ 2, 2, 1, 1 },			/* CLRB */
	/* 0x60 : indexed form */
	{ 7, 6, 6, 6 },			/* NEG */
	{ 0, 0, 7, 7 },			/* AIM */
	{ 0, 0, 7, 7 },			/* OIM */
	{ 7, 6, 6, 6 },			/* COM */
	{ 7, 6, 6, 6 },			/* LSR */
	{ 0, 0, 6, 6 },			/* EIM */
	{ 7, 6, 6, 6 },			/* ROR */
	{ 7, 6, 6, 6 },			/* ASR */
	{ 7, 6, 6, 6 },			/* ASL */
	{ 7, 6, 6, 6 },			/* ROL */
	{ 7, 6, 6, 6 },			/* DEC */
	{ 0, 0, 5, 5 },			/* TIM */
	{ 7, 6, 6, 6 },			/* INC */
	{ 7, 6, 4, 4 },			/* TST */
	{ 4, 3, 3, 3 },			/* JMP */
	{ 7, 6, 5, 5 },			/* CLR */
	/* 70: extended form (immediate for AIM/OIM etc) */
	{ 6, 6, 6, 6 },			/* NEG */
	{ 0, 0, 6, 6 },			/* AIM */
	{ 0, 0, 6, 6 },			/* OIM */
	{ 6, 6, 6, 6 },			/* COM */
	{ 6, 6, 6, 6 },			/* LSR */
	{ 0, 0, 6, 6 },			/* EIM */
	{ 6, 6, 6, 6 },			/* ROR */
	{ 6, 6, 6, 6 },			/* ASR */
	{ 6, 6, 6, 6 },			/* ASL */
	{ 6, 6, 6, 6 },			/* ROL */
	{ 6, 6, 6, 6 },			/* DEC */
	{ 0, 0, 4, 4 },			/* TIM */
	{ 6, 6, 6, 6 },			/* INC */
	{ 6, 6, 4, 4 },			/* TST */
	{ 3, 3, 3, 3 },			/* JMP */
	{ 6, 6, 5, 5 },			/* CLR */
	/* 80: A ops, immed */
	{ 2, 2, 2, 2 },			/* SUBA */
	{ 2, 2, 2, 2 },			/* CMPA */
	{ 2, 2, 2, 2 },			/* SBCA */
	{ 0, 4, 3, 3 },			/* SUBD */
	{ 2, 2, 2, 2 },			/* ANDA */
	{ 2, 2, 2, 2 },			/* BITA */
	{ 2, 2, 2, 2 },			/* LDAA */
	{ 3, 3, 3, 3 },			/* STAA immed (timing uncertain) */
	{ 2, 2, 2, 2 },			/* EORA */
	{ 2, 2, 2, 2 },			/* ADCA */
	{ 2, 2, 2, 2 },			/* ORAA */
	{ 2, 2, 2, 2 },			/* ADDA */
	{ 3, 4, 3, 3 },			/* CPX */
	{ 8, 6, 5, 5 },			/* BSR */
	{ 3, 3, 3, 3 },			/* LDS */
	{ 3, 3, 3, 3 },			/* STS immed (timing uncertain) 68HC11 only: XGDX */
	/* 90: A ops, dir */
	{ 3, 3, 3, 3 },			/* SUBA */
	{ 3, 3, 3, 3 },			/* CMPA */
	{ 3, 3, 3, 3 },			/* SBCA */
	{ 0, 5, 4, 4 },			/* SUBD */
	{ 3, 3, 3, 3 },			/* ANDA */
	{ 3, 3, 3, 3 },			/* BITA */
	{ 3, 3, 3, 3 },			/* LDAA */
	{ 4, 3, 3, 3 },			/* STAA */
	{ 3, 3, 3, 3 },			/* EORA */
	{ 3, 3, 3, 3 },			/* ADCA */
	{ 3, 3, 3, 3 },			/* ORAA */
	{ 3, 3, 3, 3 },			/* ADDA */
	{ 4, 5, 4, 4 },			/* CPX */
	{ 1, 5, 5, 5 },			/* JSR  (HCF on 6800)*/
	{ 4, 4, 4, 4 },			/* LDS */
	{ 5, 4, 4, 4 },			/* STS */
	/* A0: A ops, indexed */
	{ 5, 4, 4, 4 },			/* SUBA */
	{ 5, 4, 4, 4 },			/* CMPA */
	{ 5, 4, 4, 4 },			/* SBCA */
	{ 0, 6, 5, 5 },			/* SUBD */
	{ 5, 4, 4, 4 },			/* ANDA */
	{ 5, 4, 4, 4 },			/* BITA */
	{ 5, 4, 4, 4 },			/* LDAA */
	{ 6, 4, 4, 4 },			/* STAA */
	{ 5, 4, 4, 4 },			/* EORA */
	{ 5, 4, 4, 4 },			/* ADCA */
	{ 5, 4, 4, 4 },			/* ORAA */
	{ 5, 4, 4, 4 },			/* ADDA */
	{ 6, 6, 5, 5 },			/* CPX */
	{ 8, 6, 5, 5 },			/* JSR */
	{ 6, 5, 5, 5 },			/* LDS */
	{ 7, 5, 5, 5 },			/* STS */
	/* B0: A ops, extended */
	{ 4, 4, 4, 4 },			/* SUBA */
	{ 4, 4, 4, 4 },			/* CMPA */
	{ 4, 4, 4, 4 },			/* SBCA */
	{ 0, 6, 5, 5 },			/* SUBD */
	{ 4, 4, 4, 4 },			/* ANDA */
	{ 4, 4, 4, 4 },			/* BITA */
	{ 4, 4, 4, 4 },			/* LDAA */
	{ 4, 4, 4, 4 },			/* STAA */
	{ 4, 4, 4, 4 },			/* EORA */
	{ 4, 4, 4, 4 },			/* ADCA */
	{ 4, 4, 4, 4 },			/* ORAA */
	{ 4, 4, 4, 4 },			/* ADDA */
	{ 5, 6, 5, 5 },			/* CPX */
	{ 9, 6, 6, 6 },			/* JSR */
	{ 5, 5, 5, 5 },			/* LDS */
	{ 6, 5, 5, 5 },			/* STS */
	/* C0: B ops, immed */
	{ 2, 2, 2, 2 },			/* SUBB */
	{ 2, 2, 2, 2 },			/* CMPB */
	{ 2, 2, 2, 2 },			/* SBCB */
	{ 0, 4, 4, 4 },			/* ADDD */
	{ 2, 2, 2, 2 },			/* ANDB */
	{ 2, 2, 2, 2 },			/* BITB */
	{ 2, 2, 2, 2 },			/* LDAB */
	{ 2, 2, 2, 2 },			/* STAB immed undoc */
	{ 2, 2, 2, 2 },			/* EORB */
	{ 2, 2, 2, 2 },			/* ADCB */
	{ 2, 2, 2, 2 },			/* ORAB */
	{ 2, 2, 2, 2 },			/* ADDB */
	{ 0, 3, 3, 3 },			/* LDD */
	{ 0, 0, 0, 0 },			/* 68HC11:Prefix CD */
	{ 3, 3, 3, 3 },			/* LDX */
	{ 3, 3, 3, 3 },			/* STX undoc, 68HC11: STOP */
	/* D0: B ops, dir */
	{ 3, 3, 3, 3 },			/* SUBB */
	{ 3, 3, 3, 3 },			/* CMPB */
	{ 3, 3, 3, 3 },			/* SBCB */
	{ 0, 5, 5, 5 },			/* ADDD */
	{ 3, 3, 3, 3 },			/* ANDB */
	{ 3, 3, 3, 3 },			/* BITB */
	{ 3, 3, 3, 3 },			/* LDAB */
	{ 4, 3, 3, 3 },			/* STAB */
	{ 3, 3, 3, 3 },			/* EORB */
	{ 3, 3, 3, 3 },			/* ADCB */
	{ 3, 3, 3, 3 },			/* ORAB */
	{ 3, 3, 3, 3 },			/* ADDB */
	{ 0, 4, 4, 4 },			/* LDD */
	{ 1, 4, 4, 4 },			/* STD (HCF on 6800) */
	{ 4, 4, 4, 4 },			/* LDX */
	{ 5, 4, 4, 4 },			/* STX */
	/* E0: B ops, indexed */
	{ 5, 4, 4, 4 },			/* SUBB */
	{ 5, 4, 4, 4 },			/* CMPB */
	{ 5, 4, 4, 4 },			/* SBCB */
	{ 0, 6, 5, 5 },			/* ADDD */
	{ 5, 4, 4, 4 },			/* ANDB */
	{ 5, 4, 4, 4 },			/* BITB */
	{ 5, 4, 4, 4 },			/* LDAB */
	{ 6, 4, 4, 4 },			/* STAB */
	{ 5, 4, 4, 4 },			/* EORB */
	{ 5, 4, 4, 4 },			/* ADCB */
	{ 5, 4, 4, 4 },			/* ORAB */
	{ 5, 4, 4, 4 },			/* ADDB */
	{ 0, 5, 5, 5 },			/* LDD */
	{ 0, 5, 5, 5 },			/* STD */
	{ 6, 5, 5, 5 },			/* LDX */
	{ 7, 5, 5, 5 },			/* STX */
	/* F0: B ops, extended */
	{ 4, 4, 4, 4 },			/* SUBB */
	{ 4, 4, 4, 4 },			/* CMPB */
	{ 4, 4, 4, 4 },			/* SBCB */
	{ 0, 6, 6, 6 },			/* ADDD */
	{ 4, 4, 4, 4 },			/* ANDB */
	{ 4, 4, 4, 4 },			/* BITB */
	{ 4, 4, 4, 4 },			/* LDAB */
	{ 5, 4, 4, 4 },			/* STAB */
	{ 4, 4, 4, 4 },			/* EORB */
	{ 4, 4, 4, 4 },			/* ADCB */
	{ 4, 4, 4, 4 },			/* ORAB */
	{ 4, 4, 4, 4 },			/* ADDB */
	{ 0, 5, 5, 5 },			/* LDD */
	{ 0, 5, 5, 5 },			/* STD */
	{ 5, 5, 5, 5 },			/* LDX */
	{ 6, 5, 5, 5 }			/* STX */
};

/* By page 00/18/1A/CD */
static uint8_t clock_hc11[256][4] = {
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 }, 		/* NOP */
	{ 41, 0, 0, 0 },		/* IDIV */
	{ 41, 0, 0, 0 },		/* FDIV */
	{ 3, 0, 0, 0 },			/* LSRD */
	{ 3, 0, 0, 0 },			/* ASLD */
	{ 2, 0, 0, 0 },			/* TAP */
	{ 2, 0, 0, 0 },			/* TPA */
	{ 3, 3, 0, 0 },			/* INX */
	{ 3, 3, 0, 0 },			/* DEX */
	{ 2, 0, 0, 0 },			/* CLV */
	{ 2, 0, 0, 0 },			/* SEV */
	{ 2, 0, 0, 0 },			/* CLC */
	{ 2, 0, 0, 0 },			/* SEC */
	{ 2, 0, 0, 0 },			/* CLI */
	{ 2, 0, 0, 0 },			/* SEI */
	/* 0x10 */
	{ 2, 0, 0, 0 },			/* SBA */
	{ 2, 0, 0, 0 },			/* CBA */
	{ 6, 0, 0, 0 },			/* BRSET dir */
	{ 6, 0, 0, 0 },			/* BRCLR dir */
	{ 6, 0, 0, 0 },			/* BSET dir */
	{ 6, 0, 0, 0 },			/* BCLR dir */
	{ 2, 0, 0, 0 },			/* TAB */
	{ 2, 0, 0, 0 },			/* TBA */
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* DAA */
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* ABA */
	{ 7, 7, 0, 0 },			/* BSET indexed */
	{ 7, 7, 0, 0 },			/* BCLR indexed */
	{ 7, 7, 0, 0 },			/* BRSET indexed */
	{ 7, 7, 0, 0 },			/* BRCLR indexed */
	/* 0x20 */
	{ 3, 0, 0, 0 },			/* BRA */
	{ 3, 0, 0, 0 },			/* BRN */
	{ 3, 0, 0, 0 },			/* BHI */
	{ 3, 0, 0, 0 },			/* BLS */
	{ 3, 0, 0, 0 },			/* BCC */
	{ 3, 0, 0, 0 },			/* BCS */
	{ 3, 0, 0, 0 },			/* BNE */
	{ 3, 0, 0, 0 },			/* BEQ */
	{ 3, 0, 0, 0 },			/* BVC */
	{ 3, 0, 0, 0 },			/* BVS */
	{ 3, 0, 0, 0 },			/* BPL */
	{ 3, 0, 0, 0 },			/* BMI */
	{ 3, 0, 0, 0 },			/* BGE */
	{ 3, 0, 0, 0 },			/* BLT */
	{ 3, 0, 0, 0 },			/* BGT */
	{ 3, 0, 0, 0 },			/* BLE */
	/* 0x30 */
	{ 3, 3, 0, 0 },			/* TSX */
	{ 3, 0, 0, 0 },			/* INS */
	{ 3, 0, 0, 0 },			/* PULA */
	{ 3, 0, 0, 0 },			/* PULB */
	{ 3, 0, 0, 0 },			/* DES */
	{ 3, 3, 0, 0 },			/* TXS */
	{ 3, 0, 0, 0 },			/* PSHA */
	{ 3, 0, 0, 0 },			/* PSHB */
	{ 5, 5, 0, 0 },			/* PULX/Y */
	{ 5, 0, 0, 0 },			/* RTS */
	{ 3, 3, 0, 0 },			/* ABX */
	{ 12, 0, 0, 0 },		/* RTI */
	{ 4, 4, 0, 0 },			/* PSHX/Y */
	{ 10, 0, 0, 0 },		/* MUL */
	{ 12, 0, 0, 0 },		/* WAI */
	{ 14, 0, 0, 0 },		/* SWI */
	/* 0x40 A form */
	{ 2, 0, 0, 0 },			/* NEGA */
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* COMA */
	{ 2, 0, 0, 0 },			/* LSRA */
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* RORA */
	{ 2, 0, 0, 0 },			/* ASRA */
	{ 2, 0, 0, 0 },			/* ASLA */
	{ 2, 0, 0, 0 },			/* ROLA */
	{ 2, 0, 0, 0 },			/* DECA */
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* INCA */
	{ 2, 0, 0, 0 },			/* TSTA */
	{ 255, 255, 255, 255 },		/* HCF / T */
	{ 2, 0, 0, 0 },			/* CLRA */
	/* 0x50 B form */
	{ 2, 0, 0, 0 },			/* NEGB */
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* COMB */
	{ 2, 0, 0, 0 },			/* LSRB */
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* RORB */
	{ 2, 0, 0, 0 },			/* ASRB */
	{ 2, 0, 0, 0 },			/* ASLB */
	{ 2, 0, 0, 0 },			/* ROLB */
	{ 2, 0, 0, 0 },			/* DECB */
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* INCB */
	{ 2, 0, 0, 0 },			/* TSTB */
	{ 255, 255, 255, 255 },		/* HCF / T */
	{ 2, 0, 0, 0 },			/* CLRB */
	/* 0x60 : indexed form */
	{ 6, 6, 0, 0 },			/* NEG */
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 6, 6, 0, 0 },			/* COM */
	{ 6, 6, 0, 0 },			/* LSR */
	{ 0, 0, 0, 0 },
	{ 6, 6, 0, 0 },			/* ROR */
	{ 6, 6, 0, 0 },			/* ASR */
	{ 6, 6, 0, 0 },			/* ASL */
	{ 6, 6, 0, 0 },			/* ROL */
	{ 6, 6, 0, 0 },			/* DEC */
	{ 0, 0, 0, 0 },
	{ 6, 6, 0, 0 },			/* INC */
	{ 6, 6, 0, 0 },			/* TST */
	{ 3, 3, 0, 0 },			/* JMP */
	{ 6, 6, 0, 0 },			/* CLR */
	/* 70: extended form (immediate for AIM/OIM etc) */
	{ 6, 0, 0, 0 },			/* NEG */
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 6, 0, 0, 0 },			/* COM */
	{ 6, 0, 0, 0 },			/* LSR */
	{ 0, 0, 0, 0 },
	{ 6, 0, 0, 0 },			/* ROR */
	{ 6, 0, 0, 0 },			/* ASR */
	{ 6, 0, 0, 0 },			/* ASL */
	{ 6, 0, 0, 0 },			/* ROL */
	{ 6, 0, 0, 0 },			/* DEC */
	{ 0, 0, 0, 0 },
	{ 6, 0, 0, 0 },			/* INC */
	{ 6, 0, 0, 0 },			/* TST */
	{ 3, 0, 0, 0 },			/* JMP */
	{ 6, 0, 0, 0 },			/* CLR */
	/* 80: A ops, immed */
	{ 2, 0, 0, 0 },			/* SUBA */
	{ 2, 0, 0, 0 },			/* CMPA */
	{ 2, 0, 0, 0 },			/* SBCA */
	{ 4, 0, 4, 0 },			/* SUBD / CPD */
	{ 2, 0, 0, 0 },			/* ANDA */
	{ 2, 0, 0, 0 },			/* BITA */
	{ 2, 0, 0, 0 },			/* LDAA */
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* EORA */
	{ 2, 0, 0, 0 },			/* ADCA */
	{ 2, 0, 0, 0 },			/* ORAA */
	{ 2, 0, 0, 0 },			/* ADDA */
	{ 4, 4, 0, 0 },			/* CPX / CPY */
	{ 6, 0, 0, 0 },			/* BSR */
	{ 3, 0, 0, 0 },			/* LDS */
	{ 3, 3, 0, 0 },			/* XGDX / XGDY */
	/* 90: A ops, dir */
	{ 3, 0, 0, 0 },			/* SUBA */
	{ 3, 0, 0, 0 },			/* CMPA */
	{ 3, 0, 0, 0 },			/* SBCA */
	{ 5, 0, 5, 0 },			/* SUBD / CPD */
	{ 3, 0, 0, 0 },			/* ANDA */
	{ 3, 0, 0, 0 },			/* BITA */
	{ 3, 0, 0, 0 },			/* LDAA */
	{ 3, 0, 0, 0 },			/* STAA */
	{ 3, 0, 0, 0 },			/* EORA */
	{ 3, 0, 0, 0 },			/* ADCA */
	{ 3, 0, 0, 0 },			/* ORAA */
	{ 3, 0, 0, 0 },			/* ADDA */
	{ 5, 5, 0, 0 },			/* CPX */
	{ 5, 0, 0, 0 },			/* JSR */
	{ 4, 0, 0, 0 },			/* LDS */
	{ 4, 0, 0, 0 },			/* STS */
	/* A0: A ops, indexed */
	{ 4, 4, 0, 0 },			/* SUBA */
	{ 4, 4, 0, 0 },			/* CMPA */
	{ 4, 4, 0, 0 },			/* SBCA */
	{ 6, 6, 6, 6 },			/* SUBD / CPD */
	{ 4, 4, 0, 0 },			/* ANDA */
	{ 4, 4, 0, 0 },			/* BITA */
	{ 4, 4, 0, 0 },			/* LDAA */
	{ 4, 4, 0, 0 },			/* STAA */
	{ 4, 4, 0, 0 },			/* EORA */
	{ 4, 4, 0, 0 },			/* ADCA */
	{ 4, 4, 0, 0 },			/* ORAA */
	{ 4, 4, 0, 0 },			/* ADDA */
	{ 6, 6, 6, 6 },			/* CPX / CPY */
	{ 6, 6, 0, 0 },			/* JSR */
	{ 5, 5, 0, 0 },			/* LDS */
	{ 5, 5, 0, 0 },			/* STS */
	/* B0: A ops, extended */
	{ 4, 0, 0, 0 },			/* SUBA */
	{ 4, 0, 0, 0 },			/* CMPA */
	{ 4, 0, 0, 0 },			/* SBCA */
	{ 6, 0, 6, 0 },			/* SUBD / CPD */
	{ 4, 0, 0, 0 },			/* ANDA */
	{ 4, 0, 0, 0 },			/* BITA */
	{ 4, 0, 0, 0 },			/* LDAA */
	{ 4, 0, 0, 0 },			/* STAA */
	{ 4, 0, 0, 0 },			/* EORA */
	{ 4, 0, 0, 0 },			/* ADCA */
	{ 4, 0, 0, 0 },			/* ORAA */
	{ 4, 0, 0, 0 },			/* ADDA */
	{ 6, 6, 0, 0 },			/* CPX / CPY */
	{ 6, 0, 0, 0 },			/* JSR */
	{ 6, 0, 0, 0 },			/* LDS */
	{ 6, 0, 0, 0 },			/* STS */
	/* C0: B ops, immed */
	{ 2, 0, 0, 0 },			/* SUBB */
	{ 2, 0, 0, 0 },			/* CMPB */
	{ 2, 0, 0, 0 },			/* SBCB */
	{ 4, 0, 0, 0 },			/* ADDD */
	{ 2, 0, 0, 0 },			/* ANDB */
	{ 2, 0, 0, 0 },			/* BITB */
	{ 2, 0, 0, 0 },			/* LDAB */
	{ 0, 0, 0, 0 },
	{ 2, 0, 0, 0 },			/* EORB */
	{ 2, 0, 0, 0 },			/* ADCB */
	{ 2, 0, 0, 0 },			/* ORAB */
	{ 2, 0, 0, 0 },			/* ADDB */
	{ 3, 0, 0, 0 },			/* LDD */
	{ 0, 0, 0, 0 },
	{ 3, 3, 0, 0 },			/* LDX / LDY */
	{ 2, 0, 0, 0 },
	/* D0: A ops, dir */
	{ 3, 0, 0, 0 },			/* SUBB */
	{ 3, 0, 0, 0 },			/* CMPB */
	{ 3, 0, 0, 0 },			/* SBCB */
	{ 5, 0, 0, 0 },			/* ADDD */
	{ 3, 0, 0, 0 },			/* ANDB */
	{ 3, 0, 0, 0 },			/* BITB */
	{ 3, 0, 0, 0 },			/* LDAB */
	{ 3, 0, 0, 0 },			/* STAB */
	{ 3, 0, 0, 0 },			/* EORB */
	{ 3, 0, 0, 0 },			/* ADCB */
	{ 3, 0, 0, 0 },			/* ORAB */
	{ 3, 0, 0, 0 },			/* ADDB */
	{ 4, 0, 0, 0 },			/* LDD */
	{ 4, 0, 0, 0 },			/* STD */
	{ 4, 4, 0, 0 },			/* LDX / STY */
	{ 4, 4, 0, 0 },			/* STX / STY */
	/* E0: B ops, indexed */
	{ 4, 4, 0, 0 },			/* SUBB */
	{ 4, 4, 0, 0 },			/* CMPB */
	{ 4, 4, 0, 0 },			/* SBCB */
	{ 6, 6, 0, 0 },			/* ADDD */
	{ 4, 4, 0, 0 },			/* ANDB */
	{ 4, 4, 0, 0 },			/* BITB */
	{ 4, 4, 0, 0 },			/* LDAB */
	{ 4, 4, 0, 0 },			/* STAB */
	{ 4, 4, 0, 0 },			/* EORB */
	{ 4, 4, 0, 0 },			/* ADCB */
	{ 4, 4, 0, 0 },			/* ORAB */
	{ 4, 4, 0, 0 },			/* ADDB */
	{ 5, 5, 0, 0 },			/* LDD */
	{ 5, 5, 0, 0 },			/* STD */
	{ 5, 5, 5, 5 },			/* LDX / LDY */
	{ 5, 5, 5, 5 },			/* STX / STY */
	/* F0: B ops, extended */
	{ 4, 0, 0, 0 },			/* SUBB */
	{ 4, 0, 0, 0 },			/* CMPB */
	{ 4, 0, 0, 0 },			/* SBCB */
	{ 6, 0, 0, 0 },			/* ADDD */
	{ 4, 0, 0, 0 },			/* ANDB */
	{ 4, 0, 0, 0 },			/* BITB */
	{ 4, 0, 0, 0 },			/* LDAB */
	{ 4, 0, 0, 0 },			/* STAB */
	{ 4, 0, 0, 0 },			/* EORB */
	{ 4, 0, 0, 0 },			/* ADCB */
	{ 4, 0, 0, 0 },			/* ORAB */
	{ 4, 0, 0, 0 },			/* ADDB */
	{ 5, 0, 0, 0 },			/* LDD */
	{ 5, 0, 0, 0 },			/* STD */
	{ 6, 6, 0, 0 },			/* LDX / LDY */
	{ 6, 6, 0, 0 }			/* STX / STY */
};

/*
 *	Debug and trace support
 */

static char *m6800_flags(struct m6800 *cpu)
{
	static char buf[7];
	int i;

	for (i = 0; i < 6; i++) {
		if (cpu->p & (1 << i))
			buf[i] = "CVZNIH"[i];
		else
			buf[i] = '-';
	}
	return buf;
}

static void m6800_cpu_state(struct m6800 *cpu)
{
	if (cpu->type == CPU_68HC11)
		fprintf(stderr, "%04X : %6s %02X|%02X %04X %04X %04X | ",
			cpu->pc, m6800_flags(cpu), cpu->a, cpu->b, cpu->x, cpu->y, cpu->s);
	else
		fprintf(stderr, "%04X : %6s %02X|%02X %04X %04X | ",
			cpu->pc, m6800_flags(cpu), cpu->a, cpu->b, cpu->x, cpu->s);
}

static char *opmap[256] = {
	/* 0x00 */
	NULL,
	"NOP",
	NULL,
	NULL,

	"LSRD",
	"ASLD",
	"TAP",
	"TPA",

	"INX",
	"DEX",
	"CLV",
	"SEV",

	"CLC",
	"SEC",
	"CLI",
	"SEI",

	/* 0x10 */
	"SBA",
	"CBA",
	NULL,
	NULL,

	NULL,
	NULL,
	"TAB",
	"TBA",

	"XGDX",
	"DAA",
	NULL,
	"ABA",

	NULL,
	NULL,
	NULL,
	NULL,

	/* 0x20 */
	"BRA b",
	"BRN b",
	"BHI b",
	"BLS b",

	"BCC b",
	"BCS b",
	"BNE b",
	"BEQ b",

	"BVC b",
	"BVS b",
	"BPL b",
	"BMI b",

	"BGE b",
	"BLT b",
	"BGT b",
	"BLE b",

	/* 0x30 */
	"TSX",
	"INS",
	"PULA",
	"PULB",

	"DES",
	"TXS",
	"PSHA",
	"PSHB",

	"PULX",
	"RTS",
	"ABX",
	"RTI",

	"PSHX",
	"MUL",
	"WAI",
	"SWI",

	/* 0x40 */
	"NEGA",
	NULL,
	NULL,
	"COMA",

	"LSRA",
	NULL,
	"RORA",
	"ASRA",

	"ASLA",
	"ROLA",
	"DECA",
	NULL,

	"INCA",
	"TSTA",
	"T(HCF)",
	"CLRA",

	/* 0x50 */
	"NEGB",
	NULL,
	NULL,
	"COMB",

	"LSRB",
	NULL,
	"RORB",
	"ASRB",

	"ASLB",
	"ROLB",
	"DECB",
	NULL,

	"INCB",
	"TSTB",
	"T(HCF)",
	"CLRB",

	/* 0x60 */
	"NEG x,X",
	NULL,
	NULL,
	"COM x,X",

	"LSR x,X",
	NULL,
	"ROR x,X",
	"ASR x,X",

	"ASL x,X",
	"ROL x,X",
	"DEC x,X",
	NULL,

	"INC x,X",
	"TST x,X",
	"JMP x,X0",
	"CLR x,X",

	/* 0x70 */
	"NEG e",
	NULL,
	NULL,
	"COM e",

	"LSR e",
	NULL,
	"ROR e",
	"ASR e",

	"ASL e",
	"ROL e",
	"DEC e",
	NULL,

	"INC e",
	"TST e",
	"JMP e0",
	"CLR e",

	/* 0x80 */
	"SUBA #i",
	"CMPA #i",
	"SBCA #i",
	"SUBD #ii",

	"ANDA #i",
	"BITA #i",
	"LDAA #i",
	NULL,

	"EORA #i",
	"ADCA #i",
	"ORAA #i",
	"ADDA #i",

	"CPX #ii",
	"BSR b",
	"LDS #ii",
	NULL,

	/* 0x90 */
	"SUBA d",
	"CMPA d",
	"SBCA d",
	"SUBD d2",

	"ANDA d",
	"BITA d",
	"LDAA d",
	"STAA d",

	"EORA d",
	"ADCA d",
	"ORAA d",
	"ADDA d",

	"CPX d2",
	"JSR d0",
	"LDS d2",
	"STS d2",

	/* 0xA0 */
	"SUBA x,X",
	"CMPA x,X",
	"SBCA x,X",
	"SUBD x,X",

	"ANDA x,X",
	"BITA x,X",
	"LDAA x,X",
	"STAA x,X",

	"EORA x,X",
	"ADCA x,X",
	"ORAA x,X",
	"ADDA x,X",

	"CPX x,X2",
	"JSR x,X0",
	"LDS x,X2",
	"STS x,X2",

	/* 0xB0 */
	"SUBA e",
	"CMPA e",
	"SBCA e",
	"SUBD e",

	"ANDA e",
	"BITA e",
	"LDAA e",
	"STAA e",

	"EORA e",
	"ADCA e",
	"ORAA e",
	"ADDA e",

	"CPX e2",
	"JSR e0",
	"LDS e2",
	"STS e2",

	/* 0xC0 */
	"SUBB #i",
	"CMPB #i",
	"SBCB #i",
	"ADDD #ii",

	"ANDB #i",
	"BITB #i",
	"LDAB #i",
	NULL,

	"EORB #i",
	"ADCB #i",
	"ORAB #i",
	"ADDB #i",

	"LDD #ii",
	NULL,
	"LDX #ii",
	NULL,

	/* 0xD0 */
	"SUBB d",
	"CMPB d",
	"SBCB d",
	"ADDD d2",

	"ANDB d",
	"BITB d",
	"LDAB d",
	"STAB d",

	"EORB d",
	"ADCB d",
	"ORAB d",
	"ADDB d",

	"LDD d2",
	"STD d2",
	"LDX d2",
	"STX d2",

	/* 0xE0 */
	"SUBB x,X",
	"CMPB x,X",
	"SBCB x,X",
	"ADDD x,X",

	"ANDB x,X",
	"BITB x,X",
	"LDAB x,X",
	"STAB x,X",

	"EORB x,X",
	"ADCB x,X",
	"ORAB x,X",
	"ADDB x,X",

	"LDD x,X2",
	"STD x,X2",
	"LDX x,X2",
	"STX x,X2",

	/* 0xF0 */
	"SUBB e",
	"CMPB e",
	"SBCB e",
	"ADDD e",

	"ANDB e",
	"BITB e",
	"LDAB e",
	"STAB e",

	"EORB e",
	"ADCB e",
	"ORAB e",
	"ADDB e",

	"LDD e2",
	"STD e2",
	"LDX e2",
	"STX e2"
};

static char *opmap_11[256] = {
	/* 0x00 */
	"TEST",
	"NOP",
	"IDIV",
	"EDIV",

	"LSRD",
	"ASLD",
	"TAP",
	"TPA",

	"INy",
	"DEy",
	"CLV",
	"SEV",

	"CLC",
	"SEC",
	"CLI",
	"SEI",

	/* 0x10 */
	"SBA",
	"CBA",
	"BRSET i i b",
	"BRCLR i i b",

	"BSET i i",
	"BCLR i i",
	"TAB",
	"TBA",

	"PAGE2",
	"DAA",
	"PAGE3",
	"ABA",

	"BSET i,y i",
	"BCLR i,y i",
	"BRSET i,y i b",
	"BRCLR i,y i b",

	/* 0x20 */
	"BRA b",
	"BRN b",
	"BHI b",
	"BLS b",

	"BCC b",
	"BCS b",
	"BNE b",
	"BEQ b",

	"BVC b",
	"BVS b",
	"BPL b",
	"BMI b",

	"BGE b",
	"BLT b",
	"BGT b",
	"BLE b",

	/* 0x30 */
	"TSy",
	"INS",
	"PULA",
	"PULB",

	"DES",
	"TyS",
	"PSHA",
	"PSHB",

	"PULy",
	"RTS",
	"ABy",
	"RTI",

	"PSHy",
	"MUL",
	"WAI",
	"SWI",

	/* 0x40 */
	"NEGA",
	NULL,
	NULL,
	"COMA",

	"LSRA",
	NULL,
	"RORA",
	"ASRA",

	"ASLA",
	"ROLA",
	"DECA",
	NULL,

	"INCA",
	"TSTA",
	"T(HCF)",
	"CLRA",

	/* 0x50 */
	"NEGB",
	NULL,
	NULL,
	"COMB",

	"LSRB",
	NULL,
	"RORB",
	"ASRB",

	"ASLB",
	"ROLB",
	"DECB",
	NULL,

	"INCB",
	"TSTB",
	"T(HCF)",
	"CLRB",

	/* 0x60 */
	"NEG x,y",
	NULL,
	NULL,
	"COM x,y",

	"LSR x,y",
	NULL,
	"ROR x,y",
	"ASR x,y",

	"ASL x,y",
	"ROL x,y",
	"DEC x,y",
	NULL,

	"INC x,y",
	"TST x,y",
	"JMP x,y0",
	"CLR x,y",

	/* 0x70 */
	"NEG e",
	NULL,
	NULL,
	"COM e",

	"LSR e",
	NULL,
	"ROR e",
	"ASR e",

	"ASL e",
	"ROL e",
	"DEC e",
	NULL,

	"INC e",
	"TST e",
	"JMP e0",
	"CLR e",

	/* 0x80 */
	"SUBA #i",
	"CMPA #i",
	"SBCA #i",
	"SUBD #ii",

	"ANDA #i",
	"BITA #i",
	"LDAA #i",
	NULL,

	"EORA #i",
	"ADCA #i",
	"ORAA #i",
	"ADDA #i",

	"CPy #ii",
	"BSR b",
	"LDS #ii",
	"XGDy",

	/* 0x90 */
	"SUBA d",
	"CMPA d",
	"SBCA d",
	"SUBD d2",

	"ANDA d",
	"BITA d",
	"LDAA d",
	"STAA d",

	"EORA d",
	"ADCA d",
	"ORAA d",
	"ADDA d",

	"CPy d2",
	"JSR d0",
	"LDS d2",
	"STS d2",

	/* 0xA0 */
	"SUBA x,y",
	"CMPA x,y",
	"SBCA x,y",
	"SUBD x,y",

	"ANDA x,y",
	"BITA x,y",
	"LDAA x,y",
	"STAA x,y",

	"EORA x,y",
	"ADCA x,y",
	"ORAA x,y",
	"ADDA x,y",

	"CPy x,y2",
	"JSR x,y0",
	"LDS x,y2",
	"STS x,y2",

	/* 0xB0 */
	"SUBA e",
	"CMPA e",
	"SBCA e",
	"SUBD e",

	"ANDA e",
	"BITA e",
	"LDAA e",
	"STAA e",

	"EORA e",
	"ADCA e",
	"ORAA e",
	"ADDA e",

	"CPy e2",
	"JSR e0",
	"LDS e2",
	"STS e2",

	/* 0xC0 */
	"SUBB #i",
	"CMPB #i",
	"SBCB #i",
	"ADDD #ii",

	"ANDB #i",
	"BITB #i",
	"LDAB #i",
	NULL,

	"EORB #i",
	"ADCB #i",
	"ORAB #i",
	"ADDB #i",

	"LDD #ii",
	"PAGE4",
	"LDy #ii",
	"STOP",

	/* 0xD0 */
	"SUBB d",
	"CMPB d",
	"SBCB d",
	"ADDD d2",

	"ANDB d",
	"BITB d",
	"LDAB d",
	"STAB d",

	"EORB d",
	"ADCB d",
	"ORAB d",
	"ADDB d",

	"LDD d2",
	"STD d2",
	"LDy d2",
	"STy d2",

	/* 0xE0 */
	"SUBB x,y",
	"CMPB x,y",
	"SBCB x,y",
	"ADDD x,y",

	"ANDB x,y",
	"BITB x,y",
	"LDAB x,y",
	"STAB x,y",

	"EORB x,y",
	"ADCB x,y",
	"ORAB x,y",
	"ADDB x,y",

	"LDD x,y2",
	"STD x,y2",
	"LDX x,y2",
	"STX x,y2",

	/* 0xF0 */
	"SUBB e",
	"CMPB e",
	"SBCB e",
	"ADDD e",

	"ANDB e",
	"BITB e",
	"LDAB e",
	"STAB e",

	"EORB e",
	"ADCB e",
	"ORAB e",
	"ADDB e",

	"LDD e2",
	"STD e2",
	"LDy e2",
	"STy e2"
};

static void m6800_disassemble(struct m6800 *cpu, uint16_t pc)
{
	uint8_t op = m6800_do_debug_read(cpu, pc++);
	uint16_t data;
	uint16_t addr;
	int pcontent = 0;
	int ppair = 0;
	int page2;
	char *x = NULL;

	m6800_cpu_state(cpu);

	if (cpu->type == CPU_68HC11) {
		page2 = 0;
		if (op == 0x18) {
			page2 = 1;
			op = m6800_do_debug_read(cpu, pc++);
			x = opmap_11[op];
		} else if (op == 0x1A) {
			op = m6800_do_debug_read(cpu, pc++);
			switch(op) {
			case 0x83:
				x = "CPD #ii";
				break;
			case 0x93:
				x = "CPD d";
				break;
			case 0xA3:
				x = "CPD x2,X";
				break;
			case 0xB3:
				x = "CPD e2";
				break;
			case 0xAC:
				x = "CPY x2,X";
				break;
			case 0xEE:
				x = "LDY x2,X";
				break;
			case 0xEF:
				x = "STY x2,X";
				break;
			default:
				x = NULL;
			}
		} else if (op == 0xCD) {
			page2 = 1;
			op = m6800_do_debug_read(cpu, pc++);
			switch(op) {
			case 0xA3:
				x = "CPD x2,Y";
				break;
			case 0xAC:
				x = "CPX x2,Y";
				break;
			case 0xEE:
				x = "LDX x2,Y";
				break;
			case 0xEF:
				x = "STX x2,Y";
				break;
			default:
				x = NULL;
			}
		} else {
			x = opmap_11[op];
		}
	} else
		x = opmap[op];

	if (x == NULL) {
		fprintf(stderr, "<ILLEGAL %02X>\n", op);
		return;
	}
	while(*x) {
		/* For double load/stores */
		if (*x == '2') {
			ppair = 1;
			x++;
			continue;
		}
		/* Supress data info for JMP/JSR etc */
		if (*x == '0') {
			pcontent = 0;
			x++;
			continue;
		}
		if (*x == 'd') {
			/* Direct */
			addr = m6800_do_debug_read(cpu, pc++);
			pcontent = 1;
			fprintf(stderr, "%02X", addr);
		} else if (*x == 'e') {
			/* Extended */
			addr = m6800_do_debug_read(cpu, pc++) << 8;
			addr |= m6800_do_debug_read(cpu, pc++);
			pcontent = 1;
			fprintf(stderr, "%04X", addr);
		} else if (*x == 'b') {
			/* Branch */
			data = m6800_do_debug_read(cpu, pc++);
			data = (int8_t)data + pc;
			pcontent = 1;
			fprintf(stderr, "%04X", data);
		} else if (*x == 'x') {
			/* Indexed */
			addr = m6800_do_debug_read(cpu, pc++);
			fprintf(stderr, "%02X", addr);
			if (page2 == 1)
				addr += cpu->y;
			else
				addr += cpu->x;
			pcontent = 1;
		} else if (*x == 'i')
			fprintf(stderr, "%02X", m6800_do_debug_read(cpu, pc++));
		else if (*x == 'a')
			fprintf(stderr, " (%04X)", addr);
		else if (*x == 'y')
			fprintf(stderr, "%c", "XY"[page2]);
		else
			fputc(*x, stderr);
		x++;
	}
	if (pcontent) {
		if (ppair) {
			data = m6800_do_debug_read(cpu, addr++) << 8;
			data |= m6800_do_debug_read(cpu, addr);
			fprintf(stderr, " [%04X]", data);
		} else {
			fprintf(stderr, " [%02X]", m6800_do_debug_read(cpu, addr));
		}
	}
	fprintf(stderr, "\n");
}

/*
 *	The 6803 stack operations
 */

static void m6800_push(struct m6800 *cpu, uint8_t val)
{
	m6800_do_write(cpu, cpu->s--, val);
}

static void m6800_push16(struct m6800 *cpu, uint16_t val)
{
	m6800_push(cpu, val);
	m6800_push(cpu, val >> 8);
}

static uint8_t m6800_pull(struct m6800 *cpu)
{
	return m6800_do_read(cpu, ++cpu->s);
}

static uint16_t m6800_pull16(struct m6800 *cpu)
{
	uint16_t r = m6800_pull(cpu) << 8;
	r |= m6800_pull(cpu);
	return r;
}

static void m6800_push_interrupt(struct m6800 *cpu)
{
	m6800_push16(cpu, cpu->pc);
	if (cpu->type == CPU_68HC11)
		m6800_push16(cpu, cpu->y);
	m6800_push16(cpu, cpu->x);
	m6800_push(cpu, cpu->a);
	m6800_push(cpu, cpu->b);
	m6800_push(cpu, cpu->p);
}

static int m6800_vector(struct m6800 *cpu, uint16_t vector)
{
	int clocks = 2;
	if (cpu->wait == 0) {
		m6800_push_interrupt(cpu);
		if (cpu->type == CPU_68HC11) {
			clocks += 12;
			if (cpu->io.hprio & HPRIO_SMOD)
				vector &= ~0x4000;
		} else
			clocks += 10;
	}
	cpu->p |= P_I;
	/* What's the vector Victor ? */
	cpu->pc = m6800_do_read(cpu, vector) << 8;
	cpu->pc |= m6800_do_read(cpu, vector + 1);
	cpu->wait = 0;
	if (cpu->debug)
		fprintf(stderr, "*** Vector %04X\n", vector);
	return clocks;
}

static int m6800_vector_masked(struct m6800 *cpu, uint16_t vector)
{
	cpu->wait  = 0;
	if (cpu->p & P_I)
		return 0;
	return m6800_vector(cpu, vector);
}

/*
 *	The different flag behaviours
 */

/* Set N and Z according to the result only */
static void m6800_flags_nz(struct m6800 *cpu, uint8_t r)
{
	cpu->p &= ~(P_Z|P_N);
	if (r == 0)
		cpu->p |= P_Z;
	if (r & 0x80)
		cpu->p |= P_N;
}

/* 8bit maths operation: ABA ADC ADD */
static uint8_t m6800_maths8(struct m6800 *cpu, uint8_t a, uint8_t b, uint8_t r)
{
	cpu->p &= ~(P_H|P_N|P_Z|P_V|P_C);
	if (r & 0x80)
		cpu->p |= P_N;
	if (r == 0)
		cpu->p |= P_Z;
	/* V for addition is (!r & x & m) | (r & !x & !m) */
	if (r & 0x80) {
		if (!((a | b) & 0x80))
			cpu->p |= P_V;
	} else {
		if (a & b & 0x80)
			cpu->p |= P_V;
	}
	/* C for addition is (a & b) | (a & !r) | (b & !r) */
	if (a & b & 0x80)
		cpu->p |= P_C;
	if (a & ~r & 0x80)
		cpu->p |= P_C;
	if (b & ~r & 0x80)
		cpu->p |= P_C;
	/* And half carry for DAA */
	if ((a & b & 0x08) || ((b & ~r) & 0x08) || ((a & ~r) & 0x08))
		cpu->p |= P_H;
	return r;
}

/* 8bit maths operation without half carry - used by most instructions
   only ADC/ADD/ABA support H/C */
static uint8_t m6800_maths8_noh(struct m6800 *cpu, uint8_t a, uint8_t b, uint8_t r)
{
	cpu->p &= ~(P_N|P_Z|P_V|P_C);
	if (r & 0x80)
		cpu->p |= P_N;
	if (r == 0)
		cpu->p |= P_Z;
	if (a & 0x80) {
		if (!((b | r) & 0x80))
			cpu->p |= P_V;
	} else {
		if (b & r & 0x80)
			cpu->p |= P_V;
	}
	if (~a & b & 0x80)
		cpu->p |= P_C;
	if (b & r & 0x80)
		cpu->p |= P_C;
	if (~a & r & 0x80)
		cpu->p |= P_C;
	return r;
}

/* 8bit logic */
static void m6800_logic8(struct m6800 *cpu, uint8_t r)
{
	m6800_flags_nz(cpu, r);
	cpu->p &= ~P_V;
}

/* 8bit shifts */
static void m6800_shift8(struct m6800 *cpu, uint8_t r, int c)
{
	m6800_flags_nz(cpu, r);
	cpu->p &= ~(P_C | P_V);
	if (c)
		cpu->p |= P_C;
	/* Grumble - C needs a ^^ operator */
	if (c && !(cpu->p & P_N))
		cpu->p |= P_V;
	if (!c && (cpu->p & P_N))
		cpu->p |= P_V;
}

/* 16bit maths like this does affect N and V but not necessarily usefully.
   However the behaviour is documented */
static uint16_t m6800_maths16_add(struct m6800 *cpu, uint16_t a, uint16_t b, uint16_t r)
{
	cpu->p &= ~(P_C|P_Z|P_N|P_V);
	if (r == 0)
		cpu->p |= P_Z;
	if (r & 0x8000)
		cpu->p |= P_N;
	if (a & 0x8000) {
		if (!((b | r) & 0x8000))
			cpu->p |= P_V;
	} else {
		if (b & r & 0x8000)
			cpu->p |= P_V;
	}
	if (a & b & 0x8000)
		cpu->p |= P_C;
	if (a & ~r & 0x8000)
		cpu->p |= P_C;
	if (b & ~r & 0x8000)
		cpu->p |= P_C;
	return r;
}

static uint16_t m6800_maths16_noh(struct m6800 *cpu, uint16_t a, uint16_t b, uint16_t r)
{
	cpu->p &= ~(P_C|P_Z|P_N|P_V);
	if (r == 0)
		cpu->p |= P_Z;
	if (r & 0x8000)
		cpu->p |= P_N;
	if (a & 0x8000) {
		if (!((b | r) & 0x8000))
			cpu->p |= P_V;
	} else {
		if (b & r & 0x8000)
			cpu->p |= P_V;
	}
	if (~a & b & 0x8000)
		cpu->p |= P_C;
	if (b & r & 0x8000)
		cpu->p |= P_C;
	if (~a & r & 0x8000)
		cpu->p |= P_C;
	return r;
}

/* 16bit logic */
static void m6800_logic16(struct m6800 *cpu, uint16_t r)
{
	cpu->p &= ~(P_Z|P_N);
	if (r == 0)
		cpu->p |= P_Z;
	if (r & 0x8000)
		cpu->p |= P_N;
	cpu->p &= ~P_V;
}

/* 16bit shift : based on 6303 document */
static void m6800_shift16(struct m6800 *cpu, int c)
{
	uint16_t d = REG_D;
	cpu->p &= ~(P_Z|P_N|P_V|P_C);
	if (d == 0)
		cpu->p |= P_Z;
	if (cpu->a & 0x80)
		cpu->p |= P_N;
	if (c)
		cpu->p |= P_C;
	if (((cpu->p & (P_C|P_N)) == P_C) || ((cpu->p & (P_C|P_N)) == P_N))
		cpu->p |= P_V;
}

/* CPX depends on the CPU variant. With the 6803 and later you can sensibly
   use the carry based comparisons, with the 6800 whilst V is set up the C
   flag is not affected */
static void m6800_cpx(struct m6800 *cpu, uint16_t a, uint16_t b)
{
	if (cpu->type == CPU_6800) {
		uint16_t r = (a & 0xFF00) - (b & 0xFF00);
		cpu->p &= ~(P_Z|P_N|P_V);
		if (a == b)
			cpu->p |= P_Z;
		if (r & 0x8000)
			cpu->p |= P_N;
		if (a & 0x8000) {
			if (!((b | r) & 0x8000))
				cpu->p |= P_V;
		} else {
			if (b & r & 0x8000)
				cpu->p |= P_V;
		}
	} else
		m6800_maths16_noh(cpu, a, b, a - b);
}

static void m6800_bra(struct m6800 *cpu, uint8_t data8, uint8_t taken)
{
	if (taken)
		cpu->pc += (int8_t)data8;
}

static void m6800_hcf(struct m6800 *cpu)
{
	fprintf(stderr, "HCF executed at %04X\n", cpu->pc - 1);
	exit(1);
}

static int m6800_execute_one(struct m6800 *cpu)
{
	uint16_t fetch_pc = cpu->pc;
	uint16_t opcode = m6800_do_read(cpu, cpu->pc);
	uint8_t data8, tmp8;
	uint16_t data16, tmp16;
	uint8_t tmpc, tmp2;
	uint8_t add;
	int clocks = 0;
	int table = 0;

	if (cpu->debug)
		m6800_disassemble(cpu, cpu->pc);

	cpu->pc++;


	/* The 68HC11 has some prefixes so treat them as a single 16bit opcode
	   where 00xx matches the other processors. Multiple prefixes will mean
	   only the last one is considered which I believe is correct */
	if (cpu->type == CPU_68HC11) {
		while ((opcode & 0xFF)== 0x18 || (opcode & 0xFF) == 0x1A ||
		       (opcode & 0xFF) == 0xCD) {
			switch(opcode) {
			case 0x18:
				table = 1;
				break;
			case 0x1A:
				table = 2;
				break;
			case 0xCD:
				table = 3;
				break;
			}
			opcode <<= 8;
			opcode |= m6800_do_read(cpu, cpu->pc);
			cpu->pc++;
			clocks++;
		}
	}
	/* Fetch address/data for non immediate opcodes */
	switch(opcode & 0xF0) {
		case 0x80:	/* Immediate 8/16bit */
		case 0xC0:	/* Immediate 8/16bit */
			/* FIXME: 8D is weird, CD undefined - how does CD really work ? */
			if (opcode != 0x8D && ((opcode & 0x0F) >= 0x0C || (opcode & 0x0F) == 3)) {
				/* The apparent store immediates (and 68HC11 XGDX) break the
				   decode rule */
				if ((opcode & 0xFF) != 0x8F && (opcode & 0xFF) != 0xCF) {
					data16 = m6800_do_read(cpu, cpu->pc++) << 8;
					data16 |= m6800_do_read(cpu, cpu->pc++);
				}
				break;
			}
		case 0x20:	/* Branches */
		case 0x90:	/* Direct */
		case 0xD0:	/* Direct */
			data8 = m6800_do_read(cpu, cpu->pc++);
			break;
		case 0x60:	/* Indexed */
		case 0xA0:	/* Indexed */
		case 0xE0:	/* Indexed */
			/* Save the first byte for the strange 6303 logic immediate ops */
			data8 = m6800_do_read(cpu, cpu->pc++);
			data16 = data8 + cpu->x;
			/* 0x18: Use Y, index via Y
			   0x1A: Use Y, index via X
			   0xCD: Use X, index via Y - some exceptions */
			if ((opcode & 0xFF00) == 0x1800 || (opcode & 0xFF00) == 0xCD00)
				data16 = data8 + cpu->y;
			break;
		case 0x70:	/* Extended */
		case 0xB0:	/* Extended */
		case 0xF0:	/* Extended */
			/* Ordering ? */
			data16 = m6800_do_read(cpu, cpu->pc++) << 8;
			data16 |= m6800_do_read(cpu, cpu->pc++);
			break;
	}
	/* 68HC11 prefixed opcodes match the non prefix form plus the clock.
	   We need per prefix tables adding because a 68HC11 doens't implement
	   all prefixes of all forms */
	if (cpu->type == CPU_68HC11)
		tmp8 = clock_hc11[opcode & 0xFF][table];
	else
		tmp8 = clock_count[opcode][cpu->type];
	clocks += tmp8;
	/* Not valid on this processor */
	if (tmp8 == 0) {
		/* The HC11 pushes the prefix address */
		if (cpu->type == CPU_6303 || cpu->type == CPU_68HC11) {
			/* TRAP pushes the faulting address */
			fprintf(stderr, "illegal instruction %02X:%02X at %04X\n",
				opcode, m6800_do_read(cpu, fetch_pc + 1), fetch_pc);
			cpu->pc = fetch_pc;
			if (cpu->type == CPU_6303)
				m6800_vector(cpu, 0xFFEE);
			else
				m6800_vector(cpu, 0xFFF8);
			return clocks;	/* Not correct */
		} else {
			/* An invalid instruction we don't yet model */
			fprintf(stderr, "illegal instruction %02X at %04X\n",
				opcode, fetch_pc);
			return clocks;
		}
	}

	switch(opcode) {
	case 0x01:	/* NOP */
		/* No flags */
		return clocks;
	case 0x02:	/* IDIV */
		if (cpu->type == CPU_6803) {
			/* 6803 undocumented: SEX instruction. Is sometimes used in code */
			if (cpu->p & P_C)
				cpu->a = 0xFF;
			else
				cpu->a = 0x00;
			return clocks;
		}
		if (cpu->x == 0) {
			/* "In the case of divide by 0 the quotient is set to $FFFF
			    and the remainder is indeterminate */
			cpu->x = 0xFFFF;
			cpu->a = 0x68;
			cpu->b = 0x00;	/* Sneaky way to detect the emulator if needed */
			cpu->p |= P_C;
		} else {
			tmp16 = REG_D / cpu->x;
			data16 = REG_D % cpu->x;
			cpu->a = data16 >> 8;
			cpu->b = data16;
			cpu->x = tmp16;
			/* V always cleared, C set on divide by 0, Z on result 0 */
			cpu->p &= ~(P_C|P_V|P_Z);
			if (cpu->x == 0)
				cpu->p |= P_Z;
		}
		return clocks;
	case 0x03:	/* FDIV */
		if (cpu->type == CPU_6803) {	/* Set all bits 6803 undoc */
			cpu->a = 0xFF;
			return clocks;
		}
		if (cpu->x == 0) {
			cpu->x = 0xFFFF;
			/* Note: the 68HC11 reference manual is *wrong* here - it lists Z
			   twice but means C for the X == 0 case */
			cpu->p |= P_C;
		} else {
			/* FDIV is (D * 2^16) / X */
			uint32_t d32 = ((uint32_t)REG_D) << 16;
			uint32_t tmp32 = d32 / cpu->x;
			cpu->p &= ~(P_C|P_V|P_Z);
			/* V is set if the input X < D */
			if (cpu->x < REG_D)
				cpu->p |= P_V;
			/* Remainder into D, quotient into X */
			tmp16 = (uint16_t)(d32 % cpu->x);
			cpu->a = tmp16 >> 8;
			cpu->b = tmp16;
			cpu->x = (uint16_t)tmp32;
			/* Z is set if the quotient result is zero */
			if (cpu->x == 0)
				cpu->p |= P_Z;
		}
		return clocks;
	case 0x04:	/* LSRD */
		tmpc = cpu->b & 1;
		cpu->b >>= 1;
		if (cpu->a & 0x01)
			cpu->b |= 0x80;
		cpu->a >>= 1;
		m6800_shift16(cpu, tmpc);
		return clocks;
	case 0x05:	/* ASLD */
		tmpc = cpu->a & 0x80;
		cpu->a <<= 1;
		if (cpu->b & 0x80)
			cpu->a |= 0x01;
		cpu->b <<= 1;
		m6800_shift16(cpu, tmpc);
		return clocks;
	case 0x06:	/* TAP */
		/* FIXME: If I read the flowchart right then a pending interrupt
		   other than NMI does not occur the instruction after TAP (it acts
		   as if I was set for one instruction regardless of prev/new state) */
		cpu->p = cpu->a | 0xC0;
		return clocks;
	case 0x07:	/* TPA */
		cpu->a = cpu->p | 0xC0;
		return clocks;
	case 0x1808:	/* INY */
		cpu->y++;
		cpu->p &= ~P_Z;
		if (cpu->y == 0)
			cpu->p |= P_Z;
		return clocks;
	case 0x08:	/* INX */
		cpu->x++;
		cpu->p &= ~P_Z;
		if (cpu->x == 0)
			cpu->p |= P_Z;
		return clocks;
	case 0x1809:	/* DEY */
		cpu->y--;
		cpu->p &= ~P_Z;
		if (cpu->y == 0)
			cpu->p |= P_Z;
		return clocks;
	case 0x09:	/* DEX */
		cpu->x--;
		cpu->p &= ~P_Z;
		if (cpu->x == 0)
			cpu->p |= P_Z;
		return clocks;
	case 0x0A:	/* CLV */
		cpu->p &= ~P_V;
		return clocks;
	case 0x0B:	/* SEV */
		cpu->p |= P_V;
		return clocks;
	case 0x0C:	/* CLC */
		cpu->p &= ~P_C;
		return clocks;
	case 0x0D:	/* SEC */
		cpu->p |= P_C;
		return clocks;
	case 0x0E:	/* CLI */
		cpu->p &= ~P_I;
		return clocks;
	case 0x0F:	/* SEI */
		cpu->p |= P_I;
		return clocks;
	case 0x10:	/* SBA */
		cpu->a = m6800_maths8_noh(cpu, cpu->a, cpu->b, cpu->a - cpu->b);
		return clocks;
	case 0x11:	/* CBA */
		m6800_maths8_noh(cpu, cpu->a, cpu->b, cpu->a - cpu->b);
		return clocks;
	case 0x12:	/* BRSET direct */
		if (cpu->type == CPU_6803) {
			/* 6803 undoc - SBC B from A */
			cpu->a = m6800_maths8_noh(cpu, cpu->a, cpu->b, cpu->a - cpu->b - CARRY);
			return clocks;
		}
		if (cpu->type == CPU_6XA1) {
			cpu->x += m6800_do_read(cpu, cpu->s + 1);
			return clocks;
		}
		data8 = m6800_do_read(cpu, m6800_do_read(cpu, cpu->pc++));
		if((data8 & m6800_do_read(cpu, cpu->pc++)) == data8)
			m6800_bra(cpu, m6800_do_read(cpu, cpu->pc++), 1);
		else
			cpu->pc++;
		return clocks;
	case 0x13:	/* BRCLR direct */
		if (cpu->type == CPU_6803) {
			/* 6803 undoc - SUB B + 1from A */
			cpu->a = m6800_maths8_noh(cpu, cpu->a, cpu->b, cpu->a - cpu->b - 1);
			return clocks;
		}
		if (cpu->type == CPU_6XA1) {
			cpu->x += m6800_do_read(cpu, cpu->s + 1);
			return clocks;
		}
		data8 = m6800_do_read(cpu, m6800_do_read(cpu, cpu->pc++));
		if(!(data8 & m6800_do_read(cpu, cpu->pc++)))
			m6800_bra(cpu, m6800_do_read(cpu, cpu->pc++), 1);
		else
			cpu->pc++;
		return clocks;
	case 0x14:	/* BSET direct */
		if (cpu->type == CPU_6800) {
			cpu->a &= cpu->b;
			m6800_logic8(cpu, cpu->a);
			return clocks;
		}
		if (cpu->type == CPU_6803) {
			/* 6803 undoc - B = A - (1 - CARRY) */
			if (CARRY)
				cpu->b = m6800_maths8_noh(cpu, cpu->a, 0, cpu->a);
			else
				cpu->b = m6800_maths8_noh(cpu, cpu->a, 0, cpu->a - 1);
			return clocks;
		}
		data8 = m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data8) | m6800_do_read(cpu, cpu->pc++);
		m6800_logic8(cpu, tmp8);
		m6800_do_write(cpu, data8, tmp8);
		return clocks;
	case 0x15:	/* BCLR direct */
		data8 = m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data8) & ~m6800_do_read(cpu, cpu->pc++);
		m6800_logic8(cpu, tmp8);
		m6800_do_write(cpu, data8, tmp8);
		return clocks;
	case 0x16:	/* TAB */
		cpu->b = cpu->a;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0x17:	/* TBA */
		cpu->a = cpu->b;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x18:	/* XGDX (6303) - prefix on HC11 so will never reach here */
		if (cpu->type == CPU_6803) {
			/* Undoc adds B to A but with different flag behaviour. Think
			   flags is right but needs a review */
			cpu->a = m6800_maths8_noh(cpu, cpu->a, cpu->b, cpu->a + cpu->b);
			return clocks;
		}
		tmp16 = cpu->x;
		cpu->x = (cpu->a << 8) | cpu->b;
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0x19:	/* DAA */
		/* The algorithm for this is complicated but precisely described
		   except for what happens to V. 6303 says V is not affected */
		tmp8 = cpu->a >> 4;
		tmp2 = cpu->a & 0x0F;
		add = 0;
		tmpc = 0;
		if (!(cpu->p & P_C)) {
			if (tmp8 < 0x09 && tmp2 >= 0x0A && !(cpu->p & P_H))
				add = 0x06;
			if (tmp8 <= 0x09 && tmp2 <= 0x03 && (cpu->p & P_H))
				add = 0x06;
			if (tmp8 >= 0x0A && tmp2 <= 0x09 && !(cpu->p & P_H)) {
				tmpc = 1;
				add = 0x60;
			}
			if (tmp8 >= 0x09 && tmp2 >= 0x0A && !(cpu->p & P_H)) {
				tmpc = 1;
				add = 0x66;
			}
			if (tmp8 >= 0x0A && tmp2 <= 0x03 && (cpu->p & P_H)) {
				tmpc = 1;
				add = 0x66;
			}
		} else {
			if (tmp8 <= 0x02 && tmp2 <= 0x09 && !(cpu->p & P_H)) {
				tmpc = 1;
				add = 0x60;
			}
			if (tmp8 <= 0x02 && tmp2 >= 0x0A && !(cpu->p & P_H)) {
				tmpc = 1;
				add = 0x66;
			}
			if (tmp8 <= 0x03 && tmp2 <= 0x03 && (cpu->p & P_H)) {
				tmpc = 1;
				add = -0x66;
			}
		}
		cpu->a += add;
		m6800_flags_nz(cpu, cpu->a);
		cpu->p &= ~P_C;
		if (tmpc)
			cpu->p |= P_C;
		/* FIXME: V is "not defined" on 6803 */
		return clocks;
	case 0x1A:	/* SLP (6303) */
		if (cpu->type == CPU_6803) {
			/* Undoc adds B to A but with different flag behaviour. Think
			   flags is right but needs a review - same as 0x18 */
			cpu->a = m6800_maths8_noh(cpu, cpu->a, cpu->b, cpu->a + cpu->b);
			return clocks;
		}
		cpu->wait = 1;
		return clocks;
	case 0x1B:	/* ABA */
		cpu->a = m6800_maths8(cpu, cpu->a, cpu->b, cpu->a + cpu->b);
		return clocks;
	case 0x181C:	/* BSET indexed,Y */
		data16 = cpu->y + m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data16) | tmp8;
		m6800_logic8(cpu, tmp8);
		m6800_do_write(cpu, data16, tmp8);
		return clocks;
	case 0x1C:	/* BSET indexed */
		data16 = cpu->x + m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data16) | tmp8;
		m6800_logic8(cpu, tmp8);
		m6800_do_write(cpu, data16, tmp8);
		return clocks;
	case 0x181D:	/* BCLR indexed,Y */
		data16 = cpu->y + m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data16) & ~tmp8;
		m6800_logic8(cpu, tmp8);
		m6800_do_write(cpu, data16, tmp8);
		return clocks;
	case 0x1D:	/* BCLR indexed */
		data16 = cpu->x + m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data16) & ~tmp8;
		m6800_logic8(cpu, tmp8);
		m6800_do_write(cpu, data16, tmp8);
		return clocks;
	case 0x181E:
		data8 = m6800_do_read(cpu, cpu->y + m6800_do_read(cpu, cpu->pc++));
		if((data8 & m6800_do_read(cpu, cpu->pc++)) == data8)
			m6800_bra(cpu, m6800_do_read(cpu, cpu->pc++), 1);
		else
			cpu->pc++;
		return clocks;
	case 0x1E:	/* BRSET indexed */
		if (cpu->type == CPU_6803) {
			cpu->b = cpu->a;
			m6800_logic8(cpu, cpu->b);
			return clocks;
		}
		data8 = m6800_do_read(cpu, cpu->x + m6800_do_read(cpu, cpu->pc++));
		if((data8 & m6800_do_read(cpu, cpu->pc++)) == data8)
			m6800_bra(cpu, m6800_do_read(cpu, cpu->pc++), 1);
		else
			cpu->pc++;
		return clocks;
	case 0x181F:
		data8 = m6800_do_read(cpu, cpu->y + m6800_do_read(cpu, cpu->pc++));
		if((data8 & m6800_do_read(cpu, cpu->pc++)) == 0)
			m6800_bra(cpu, m6800_do_read(cpu, cpu->pc++), 1);
		else
			cpu->pc++;
		return clocks;
	case 0x1F:	/* BRCLR indexed */
		if (cpu->type == CPU_6803) {
			/* Undoc TBA and set C */
			cpu->b = cpu->a;
			/* Not sure flags are right here TODO */
			m6800_logic8(cpu, cpu->b);
			cpu->p |= P_C;
			return clocks;
		}
		data8 = m6800_do_read(cpu, cpu->x + m6800_do_read(cpu, cpu->pc++));
		if((data8 & m6800_do_read(cpu, cpu->pc++)) == 0)
			m6800_bra(cpu, m6800_do_read(cpu, cpu->pc++), 1);
		else
			cpu->pc++;
		return clocks;
	/* 2x is branches : 3 or 4 cycle check - 4 on 6800, 3 on 6803 ? */
	case 0x20:	/* BRA */
		m6800_bra(cpu, data8, 1);
		return clocks;
	case 0x21:	/* BRN */
		m6800_bra(cpu, data8, 0);
		return clocks;
	case 0x22:	/* BHI */
		m6800_bra(cpu, data8, !(cpu->p & (P_C|P_Z)));
		return clocks;
	case 0x23:	/* BLS */
		m6800_bra(cpu, data8, cpu->p & (P_C|P_Z));
		return clocks;
	case 0x24:	/* BCC */
		m6800_bra(cpu, data8, !(cpu->p & P_C));
		return clocks;
	case 0x25:	/* BCS */
		m6800_bra(cpu, data8, cpu->p & P_C);
		return clocks;
	case 0x26:	/* BNE */
		m6800_bra(cpu, data8, !(cpu->p & P_Z));
		return clocks;
	case 0x27:	/* BEQ */
		m6800_bra(cpu, data8, cpu->p & P_Z);
		return clocks;
	case 0x28:	/* BVC */
		m6800_bra(cpu, data8, !(cpu->p & P_V));
		return clocks;
	case 0x29:	/* BVS */
		m6800_bra(cpu, data8, cpu->p & P_V);
		return clocks;
	case 0x2A:	/* BPL */
		m6800_bra(cpu, data8, !(cpu->p & P_N));
		return clocks;
	case 0x2B:	/* BMI */
		m6800_bra(cpu, data8, cpu->p & P_N);
		return clocks;
	case 0x2C:	/* BGE */
		/* no ^^ in C */
		tmp8 = cpu->p & (P_N|P_V);
		if(tmp8 == (P_N|P_V))
			tmp8 = 0;
		m6800_bra(cpu, data8, !tmp8);
		return clocks;
	case 0x2D:	/* BLT : yum yum */
		tmp8 = cpu->p & (P_N|P_V);
		if(tmp8 == (P_N|P_V))
			tmp8 = 0;
		m6800_bra(cpu, data8, tmp8);
		return clocks;
	case 0x2E:	/* BGT */
		tmp8 = cpu->p & (P_N|P_V);
		if(tmp8 == (P_N|P_V))
			tmp8 = 0;
		if (cpu->p & P_Z)
			tmp8 = 1;
		m6800_bra(cpu, data8, !tmp8);
		return clocks;
	case 0x2F:	/* BLE */
		tmp8 = cpu->p & (P_N|P_V);
		if(tmp8 == (P_N|P_V))
			tmp8 = 0;
		if (cpu->p & P_Z)
			tmp8 = 1;
		m6800_bra(cpu, data8, tmp8);
		return clocks;
	/* 3x is stack stuff mostly */
	case 0x1830:	/* TSY */
		cpu->y = cpu->s + 1;
		/* No flags */
		return clocks;		/* 4 on 6800 ? */
	case 0x30:	/* TSX */
		cpu->x = cpu->s + 1;
		/* No flags */
		return clocks;		/* 4 on 6800 ? */
	case 0x31:	/* INS */
		cpu->s++;
		/* No flags */
		return clocks;
	case 0x32:	/* PULA */
		cpu->a = m6800_pull(cpu);
		/* No flags */
		return clocks;
	case 0x33:	/* PULB */
		cpu->b = m6800_pull(cpu);
		/* No flags */
		return clocks;
	case 0x34:	/* DES */
		cpu->s--;
		/* No flags */
		return clocks;
	case 0x1835:	/* TYS */
		cpu->s = cpu->y - 1;
		/* No flags */
		return clocks;		/* 4 on 6800 ? */
	case 0x35:	/* TXS */
		cpu->s = cpu->x - 1;
		/* No flags */
		return clocks;		/* 4 on 6800 ? */
	case 0x36:	/* PSHA */
		m6800_push(cpu, cpu->a);
		/* No flags */
		return clocks;	/* 3 or 4 - 4 on 6800 ? */
	case 0x37:	/* PSHB */
		m6800_push(cpu, cpu->b);
		/* No flags */
		return clocks;
	case 0x1838:	/* PULY */
		cpu->y = m6800_pull16(cpu);
		/* No flags */
		return clocks;
	case 0x38:	/* PULX */
		cpu->x = m6800_pull16(cpu);
		/* No flags */
		return clocks;
	case 0x39:	/* RTS */
		cpu->pc = m6800_pull16(cpu);
		/* No flags */
		return clocks;
	case 0x183A:	/* ABY */
		cpu->y += cpu->b;
		/* No flags */
		return clocks;
	case 0x3A:	/* ABX */
		cpu->x += cpu->b;
		/* No flags */
		return clocks;
	case 0x3B:	/* RTI */
		cpu->p = m6800_pull(cpu);
		cpu->b = m6800_pull(cpu);
		cpu->a = m6800_pull(cpu);
		cpu->x = m6800_pull16(cpu);
		if (cpu->type == CPU_68HC11)
			cpu->y = m6800_pull16(cpu);
		cpu->pc = m6800_pull16(cpu);
		return clocks;
	case 0x183C:
		m6800_push16(cpu, cpu->y);
		/* No flags */
		return clocks;
	case 0x3C:	/* PSHX */
		m6800_push16(cpu, cpu->x);
		/* No flags */
		return clocks;
	case 0x3D:	/* MUL */
		tmp16 = cpu->a * cpu->b;
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		cpu->p &= ~P_C;
		/* This is bizzare but what the 6303 manual says - the 6803 one
		   doesn't really help */
		if (cpu->b & 0x80)
			cpu->p |= P_C;
		return clocks;
	case 0x3E:	/* WAI */
		m6800_push_interrupt(cpu);
		/* Note we are in wait */
		cpu->wait = 1;
		return clocks;
	case 0x3F:	/* SWI */
		m6800_push_interrupt(cpu);
		cpu->p |= P_I;
		if (cpu->type == CPU_68HC11) {
			cpu->pc = m6800_do_read(cpu, 0xFFF7);
			cpu->pc |= m6800_do_read(cpu, 0xFFF6) << 8;
		} else {
			cpu->pc = m6800_do_read(cpu, 0xFFFB);
			cpu->pc |= m6800_do_read(cpu, 0xFFFA) << 8;
		}
		return clocks;
	/* Implicit logic on A */
	case 0x40:	/* NEGA */
		cpu->a = m6800_maths8_noh(cpu, 0, cpu->a, -cpu->a);
		return clocks;
	case 0x41:	/* undoc TSTA alias */
		m6800_logic8(cpu, cpu->a);
		cpu->p &= ~P_C;
		return clocks;
	case 0x42:	/* negate including carry */
		cpu->a = m6800_maths8_noh(cpu, 0, cpu->a, -cpu->a - CARRY);
	case 0x43:	/* COMA */
		cpu->a = ~cpu->a;
		m6800_logic8(cpu, cpu->a);
		cpu->p |= P_C;
		return clocks;
	case 0x44:	/* LSRA */
	case 0x45:	/* LSRA alias on 6803 */
		tmp8 = cpu->a & 0x01;
		cpu->a >>= 1;
		m6800_shift8(cpu, cpu->a, tmp8);
		return clocks;
	case 0x46:	/* RORA */
		tmpc = cpu->a & 0x01;
		cpu->a >>= 1;
		if (CARRY)
			cpu->a |= 0x80;
		m6800_shift8(cpu, cpu->a, tmpc);
		return clocks;
	case 0x47:	/* ASRA */
		tmp8 = cpu->a & 0x01;
		if (cpu->a & 0x80)
			cpu->a = (cpu->a >> 1) | 0x80;
		else
			cpu->a >>= 1;
		m6800_shift8(cpu, cpu->a, tmp8);
		return clocks;
	case 0x48:	/* ASLA */
		tmp8 = cpu->a & 0x80;	/* Carry bit */
		cpu->a <<= 1;
		m6800_shift8(cpu, cpu->a, tmp8);
		return clocks;
	case 0x49:	/* ROLA */
		tmp8 = cpu->a & 0x80;
		cpu->a = (cpu->a << 1) | CARRY;
		m6800_shift8(cpu, cpu->a, tmp8);
		return clocks;
	case 0x4A: 	/* DECA */
	case 0x4B:	/* DECA alias undoc */
		/* Weird as they don't affect C */
		cpu->a--;
		m6800_logic8(cpu, cpu->a);
		if (cpu->a == 0x7F)	/* DEC from 0x80 */
			cpu->p |= P_V;
		return clocks;
	case 0x4C:	/* INCA */
		cpu->a++;
		m6800_logic8(cpu, cpu->a);
		if (cpu->a == 0x80)	/* INC from 0x7F) */
			cpu->p |= P_V;
		return clocks;
	case 0x4D:	/* TSTA */
		m6800_logic8(cpu, cpu->a);
		cpu->p &= ~P_C;
		return clocks;
	case 0x4E:	/* Halt and catch fire */
		m6800_hcf(cpu);
		return clocks;
	case 0x4F:	/* CLRA */
		cpu->a = 0;
		cpu->p &= ~(P_N|P_V|P_C);
		cpu->p |= P_Z;
		return clocks;
	/* Same as 0x4x but with B */
	case 0x50:	/* NEGB */
		cpu->b = m6800_maths8_noh(cpu, 0, cpu->b, -cpu->b);
		return clocks;
	case 0x51:	/* undoc alias of TSTB */
		m6800_logic8(cpu, cpu->b);
		cpu->p &= ~P_C;
		return clocks;
	case 0x52:	/* NEGB variant with carry - undoc */
		cpu->b = m6800_maths8_noh(cpu, 0, cpu->b, -cpu->b - CARRY);
		return clocks;
	case 0x53:	/* COMB */
		cpu->b = ~cpu->b;
		m6800_logic8(cpu, cpu->b);
		cpu->p |= P_C;
		return clocks;
	case 0x54:	/* LSRB */
	case 0x55:	/* alias of LSRB undoc */
		tmp8 = cpu->b & 1;
		cpu->b >>= 1;
		m6800_shift8(cpu, cpu->b, tmp8);
		return clocks;
	case 0x56:	/* RORB */
		tmpc = cpu->b & 0x01;
		cpu->b >>= 1;
		if (CARRY)
			cpu->b |= 0x80;
		m6800_shift8(cpu, cpu->b, tmpc);
		return clocks;
	case 0x57:	/* ASRB */
		tmp8 = cpu->b & 0x01;
		if (cpu->b & 0x80)
			cpu->b = (cpu->b >> 1) | 0x80;
		else
			cpu->b >>= 1;
		m6800_shift8(cpu, cpu->b, tmp8);
		return clocks;
	case 0x58:	/* ASLB */
		tmp8 = cpu->b & 0x80;
		cpu->b <<= 1;
		m6800_shift8(cpu, cpu->b, tmp8);
		return clocks;
	case 0x59:	/* ROLB */
		tmp8 = cpu->b & 0x80;
		cpu->b = (cpu->b << 1) | CARRY;
		m6800_shift8(cpu, cpu->b, tmp8);
		return clocks;
	case 0x5A: 	/* DECB */
	case 0x5B:	/* alias of DECB undoc */
		/* Weird as the don't affect C */
		cpu->b--;
		m6800_logic8(cpu, cpu->b);
		if (cpu->b == 0x7F)	/* DEC from 0x80 */
			cpu->p |= P_V;
		return clocks;
	case 0x5C:	/* INCB */
		cpu->b++;
		m6800_logic8(cpu, cpu->b);
		if (cpu->b == 0x80)	/* INC from 0x7F */
			cpu->p |= P_V;
		return clocks;
	case 0x5D:	/* TSTB */
		m6800_logic8(cpu, cpu->b);
		cpu->p &= ~P_C;
		return clocks;
	case 0x5E:	/* Halt and catch fire */
		m6800_hcf(cpu);
		return clocks;
	case 0x5F:	/* CLRB */
		cpu->b = 0;
		cpu->p &= ~(P_N|P_V|P_C);
		cpu->p |= P_Z;
		return clocks;
	/* As 0x4x but indexed and includes jumps */
	/* And 0x70 is the same but extended */
	case 0x1860:	/* NEG ,Y */
	case 0x60:	/* NEG ,X */
	case 0x70:	/* NEG addr */
		/* FIXME: check flags on NEG */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_do_write(cpu, data16, m6800_maths8_noh(cpu, 0, tmp8, -tmp8));
		return clocks;
	case 0x61:	/* AIM direct (6303) and also BCLR */
		if (cpu->type == CPU_6803) {
			/* TST undoc alias */
			tmp8 = m6800_do_read(cpu, data16);
			m6800_logic8(cpu, tmp8);
			cpu->p &= ~P_C;
			return clocks;
		}
		/* These have strange encodings of the data */
		data16 = cpu->x + m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data16);
		tmp8 &= data8;
		m6800_do_write(cpu, data16 & 0xFF, tmp8);
		m6800_logic8(cpu, tmp8);
		return clocks;
	case 0x71:	/* AIM ,X (6303) and also BCLR */
		/* These have strange encodings of the data */
		tmp8 = m6800_do_read(cpu, data16 & 0xFF);
		tmp8 &= data16 >> 8;
		m6800_do_write(cpu, data16 & 0xFF, tmp8);
		m6800_logic8(cpu, tmp8);
		return clocks;
	case 0x62:	/* OIM direct (6303) and also BSET */
		if (cpu->type == CPU_6803) {
			/* NEG x,X */
			tmp8 = m6800_do_read(cpu, data16);
			m6800_do_write(cpu, data16, m6800_maths8_noh(cpu, 0, tmp8, -tmp8 - CARRY));
			return clocks;
		}
		/* These have strange encodings of the data */
		data16 = cpu->x + m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data16);
		tmp8 |= data8;
		m6800_do_write(cpu, data16 & 0xFF, tmp8);
		m6800_logic8(cpu, tmp8);
		return clocks;
	case 0x72:	/* OIM ,X (6303) and also BSET */
		if (cpu->type == CPU_6803) {
			/* NEG x,X */
			tmp8 = m6800_do_read(cpu, data16);
			m6800_do_write(cpu, data16, m6800_maths8_noh(cpu, 0, tmp8, -tmp8 - CARRY));
			return clocks;
		}
		/* These have strange encodings of the data */
		tmp8 = m6800_do_read(cpu, data16 & 0xFF);
		tmp8 |= data16 >> 8;
		m6800_do_write(cpu, data16 & 0xFF, tmp8);
		m6800_logic8(cpu, tmp8);
		return clocks;
	case 0x1863:	/* COM ,Y */
	case 0x63:	/* COM ,X */
	case 0x73:	/* COM addr */
		tmp8 = ~m6800_do_read(cpu, data16);
		m6800_do_write(cpu, data16, tmp8);
		m6800_logic8(cpu, tmp8);
		cpu->p |= P_C;
		return clocks;
	case 0x1864:	/* LSR ,Y */
	case 0x64:	/* LSR ,X */
	case 0x74:	/* LSR addr */
		tmp8 = m6800_do_read(cpu, data16);
		tmpc = tmp8 & 0x01;
		tmp8 >>= 1;
		m6800_do_write(cpu, data16, tmp8);
		m6800_shift8(cpu, tmp8, tmpc);
		return clocks;
	case 0x65:	/* EIM direct (6303) and also BTGL */
		if (cpu->type == CPU_6803) {
			/* Undoc aliases of LSR on 6803 */
			tmp8 = m6800_do_read(cpu, data16);
			tmpc = tmp8 & 0x01;
			tmp8 >>= 1;
			m6800_do_write(cpu, data16, tmp8);
			m6800_shift8(cpu, tmp8, tmpc);
			return clocks;
		}
		/* These have strange encodings of the data */
		data16 = cpu->x + m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data16);
		tmp8 ^= data8;
		m6800_do_write(cpu, data16 & 0xFF, tmp8);
		m6800_logic8(cpu, tmp8);
		return clocks;
	case 0x75:	/* EIM ,X (6303) and also BTGL */
		if (cpu->type == CPU_6803) {
			/* Undoc aliases of LSR on 6803 */
			tmp8 = m6800_do_read(cpu, data16);
			tmpc = tmp8 & 0x01;
			tmp8 >>= 1;
			m6800_do_write(cpu, data16, tmp8);
			m6800_shift8(cpu, tmp8, tmpc);
			return clocks;
		}
		/* These have strange encodings of the data */
		tmp8 = m6800_do_read(cpu, data16 & 0xFF);
		tmp8 = data16 >> 8;
		m6800_do_write(cpu, data16 & 0xFF, tmp8);
		m6800_logic8(cpu, tmp8);
		return clocks;
	case 0x1866:	/* ROR, Y */
	case 0x66:	/* ROR ,X */
	case 0x76:	/* ROR addr */
		tmp8 = m6800_do_read(cpu, data16);
		tmpc = tmp8 & 0x01;
		tmp8 >>= 1;
		if (CARRY)
			tmp8 |= 0x80;
		m6800_shift8(cpu, tmp8, tmpc);
		m6800_do_write(cpu, data16, tmp8);
		return clocks;
	case 0x1867:	/* ASR ,Y */
	case 0x67:	/* ASR ,X */
	case 0x77:	/* ASR addr */
		tmp8 = m6800_do_read(cpu, data16);
		tmpc = tmp8 & 0x01;
		if (tmp8 & 0x80)
			tmp8 = (tmp8 >> 1) | 0x80;
		else
			tmp8 >>= 1;
		m6800_shift8(cpu, tmp8, tmpc);
		m6800_do_write(cpu, data16, tmp8);
		return clocks;
	case 0x1868:	/* ASL ,Y */
	case 0x68:	/* ASL ,X */
	case 0x78:	/* ASL addr */
		tmp16 = m6800_do_read(cpu, data16);
		tmp16 <<= 1;
		m6800_shift8(cpu, tmp16 & 0xFF, tmp16 & 0x100);
		m6800_do_write(cpu, data16, (uint8_t)tmp16);
		return clocks;
	case 0x1869:	/* ROL ,Y */
	case 0x69:	/* ROL ,X */
	case 0x79:	/* ROL addr */
		tmp8 = m6800_do_read(cpu, data16);
		tmpc = tmp8 & 0x80;
		tmp8 = (tmp8 << 1) | CARRY;
		m6800_do_write(cpu, data16, tmp8);
		m6800_shift8(cpu, tmp8, tmpc);
		return clocks;
	case 0x186A:	/* DEC ,Y */
	case 0x6A: 	/* DEC ,X */
	case 0x7A:	/* DEC addr */
		/* Weird as the don't affect C */
		tmp8 = m6800_do_read(cpu, data16) - 1;
		m6800_logic8(cpu, tmp8);
		if (tmp8 == 0x7F)	/* DEC from 0x80) */
			cpu->p |= P_V;
		m6800_do_write(cpu, data16, tmp8);
		return clocks;
	case 0x6B:	/* BTST direct (6303) and TIM */
		if (cpu->type == CPU_6803) {
			/* Undoc aliases of DEC on 6803 */
			/* Weird as the don't affect C */
			tmp8 = m6800_do_read(cpu, data16) - 1;
			m6800_logic8(cpu, tmp8);
			if (tmp8 == 0x7F)	/* DEC from 0x80) */
				cpu->p |= P_V;
			m6800_do_write(cpu, data16, tmp8);
			return clocks;
		}
		/* These have strange encodings of the data */
		data16 = cpu->x + m6800_do_read(cpu, cpu->pc++);
		tmp8 = m6800_do_read(cpu, data16);
		tmp8 &= data8;
		m6800_logic8(cpu, tmp8);
		return clocks;
	case 0x7B:	/* BTST ,X (6303) and TIM */
		if (cpu->type == CPU_6803) {
			/* Undoc aliases of DEC on 6803 */
			/* Weird as the don't affect C */
			tmp8 = m6800_do_read(cpu, data16) - 1;
			m6800_logic8(cpu, tmp8);
			if (tmp8 == 0x7F)	/* DEC from 0x80) */
				cpu->p |= P_V;
			m6800_do_write(cpu, data16, tmp8);
			return clocks;
		}
		/* These have strange encodings of the data */
		tmp8 = m6800_do_read(cpu, data16 & 0xFF);
		tmp8 &= data16 >> 8;
		m6800_logic8(cpu, tmp8);
		return clocks;
	case 0x186C:	/* INC ,Y */
	case 0x6C:	/* INC ,X */
	case 0x7C:	/* INC addr */
		tmp8 = m6800_do_read(cpu, data16);
		tmp8++;
		m6800_logic8(cpu, tmp8);
		if (tmp8 == 0x80)	/* INC from 0x7F) */
			cpu->p |= P_V;
		m6800_do_write(cpu, data16, tmp8);
		return clocks;
	case 0x186D:
	case 0x6D:	/* TST ,X */
	case 0x7D:	/* TST addr */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_logic8(cpu, tmp8);
		cpu->p &= ~P_C;
		return clocks;
	case 0x186E:	/* JMP ,Y */
	case 0x6E:	/* JMP ,X */
	case 0x7E:	/* JMP addr */
		cpu->pc = data16;
		/* No flags */
		return clocks;
	case 0x186F:	/* CLR ,Y */
	case 0x6F:	/* CLR ,X */
	case 0x7F:	/* CLR addr */
		m6800_do_write(cpu, data16, 0);
		cpu->p &= ~(P_N|P_V|P_C);
		cpu->p |= P_Z;
		return clocks;
	/* Logical operation blocks these almost make sense but there are oddities */
	/* 0x8X immediate operations on A */
	case 0x80:	/* SUBA immed */
		cpu->a = m6800_maths8_noh(cpu, cpu->a, data8, cpu->a - data8);
		return clocks;
	case 0x81:	/* CMPA immed */
		m6800_maths8_noh(cpu, cpu->a, data8, cpu->a - data8);
		return clocks;
	case 0x82:	/* SBCA immed */
		cpu->a = m6800_maths8_noh(cpu, cpu->a, data8, cpu->a - data8 - CARRY);
		return clocks;
	case 0x1A83:	/* CPD immed16 */
		m6800_maths16_noh(cpu, REG_D, data16, REG_D - data16);
		return clocks;
	case 0x83:	/* SUBD immed16 */
		tmp16 = m6800_maths16_noh(cpu, REG_D, data16, REG_D - data16);
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0x84:	/* ANDA immed */
		cpu->a &= data8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x85:	/* BITA */
		m6800_logic8(cpu, cpu->a & data8);
		return clocks;
	case 0x86:	/* LDAA */
		cpu->a = data8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x87:	/* Undocumented: STAA immed */
		cpu->pc++;
		m6800_do_write(cpu, cpu->pc++, cpu->a);
		return clocks;
	case 0x88:	/* EORA */
		cpu->a ^= data8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x89:	/* ADCA */
		cpu->a = m6800_maths8(cpu, cpu->a, data8, cpu->a + data8 + CARRY);
		return clocks;
	case 0x8A:	/* ORAA */
		cpu->a |= data8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x8B:	/* ADDA */
		cpu->a = m6800_maths8(cpu, cpu->a, data8, cpu->a + data8);
		return clocks;
	case 0x188C:	/* CPY */
		m6800_cpx(cpu, cpu->y, data16);
		return clocks;
	case 0x8C:	/* CPX */
		m6800_cpx(cpu, cpu->x, data16);
		return clocks;
	case 0x8D:	/* BSR */
		m6800_push16(cpu, cpu->pc);
		cpu->pc += (int8_t)data8;
		/* No flags */
		return clocks;
	case 0x8E:	/* LDS */
		cpu->s = data16;
		/* Weirdly LDS *does* affect flags */
		m6800_logic16(cpu, cpu->s);
		return clocks;
	/* 0x8F would be SDS immediate which is meaningless. 68HC11 uses it
	   for XGDX */
	case 0x188F:	/* XGDY */
		tmp16 = cpu->y;
		cpu->y = (cpu->a << 8) | cpu->b;
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0x8F:	/* XGDX (68HC11 version) */
		if (cpu->type != CPU_68HC11) {
			/* On 6800 it's STS immed - undocumented */
			cpu->pc++;
			m6800_do_write(cpu, cpu->pc++, cpu->s >> 8);
			m6800_do_write(cpu, cpu->pc++, cpu->s);
		} else {
			tmp16 = cpu->x;
			cpu->x = (cpu->a << 8) | cpu->b;
			cpu->a = tmp16 >> 8;
			cpu->b = tmp16;
		}
		return clocks;
	/* Same again for direct */
	case 0x90:	/* SUBA dir */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->a = m6800_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
		return clocks;
	case 0x91:	/* CMPA dir */
		tmp8 = m6800_do_read(cpu, data8);
		m6800_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
		return clocks;
	case 0x92:	/* SBCA dir */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->a = m6800_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8 - CARRY);
		return clocks;
	case 0x1A93: /* CPD dir */
		tmp16 = m6800_do_read(cpu, data8) << 8;
		tmp16 |= m6800_do_read(cpu, data8 + 1);
		m6800_maths16_noh(cpu, REG_D, tmp16, REG_D - tmp16);
		return clocks;
	case 0x93:	/* SUBD dir */
		tmp16 = m6800_do_read(cpu, data8) << 8;
		tmp16 |= m6800_do_read(cpu, data8 + 1);
		tmp16 = m6800_maths16_noh(cpu, REG_D, tmp16, REG_D - tmp16);
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0x94:	/* ANDA dir */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->a &= tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x95:	/* BITA */
		tmp8 = m6800_do_read(cpu, data8);
		m6800_logic8(cpu, cpu->a & tmp8);
		return clocks;
	case 0x96:	/* LDAA */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->a = tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x97:	/* STAA */
		m6800_do_write(cpu, data8, cpu->a);
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x98:	/* EORA */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->a ^= tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x99:	/* ADCA */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->a = m6800_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8 + CARRY);
		return clocks;
	case 0x9A:	/* ORAA */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->a |= tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x9B:	/* ADDA */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->a = m6800_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8);
		return clocks;
	case 0x189C:	/* CPY */
		tmp16 = m6800_do_read(cpu, data8) << 8;
		tmp16 |= m6800_do_read(cpu, data8 + 1);
		m6800_cpx(cpu, cpu->y, tmp16);
		return clocks;
	case 0x9C:	/* CPX */
		tmp16 = m6800_do_read(cpu, data8) << 8;
		tmp16 |= m6800_do_read(cpu, data8 + 1);
		m6800_cpx(cpu, cpu->x, tmp16);
		return clocks;
	case 0x9D:	/* JSR */
		if (cpu->type == CPU_6800)
			m6800_hcf(cpu);
		m6800_push16(cpu, cpu->pc);
		cpu->pc = data8;
		/* No flags */
		return clocks;
	case 0x9E:	/* LDS */
		tmp16 = m6800_do_read(cpu, data8) << 8;
		tmp16 |= m6800_do_read(cpu, data8 + 1);
		/* Weirdly LDS *does* affect flags */
		cpu->s = tmp16;
		m6800_logic16(cpu, cpu->s);
		return clocks;
	case 0x9F:	/* STS */
		m6800_do_write(cpu, data8, cpu->s >> 8);
		m6800_do_write(cpu, data8 + 1, cpu->s);	/* Do these wrap ?? */
		m6800_logic16(cpu, cpu->s);
		return clocks;
	/* 0xAX: indexed version */
	case 0x18A0:
	case 0xA0:	/* SUBA indexed */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = m6800_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
		return clocks;
	case 0x18A1:/* CMPA ,Y */
	case 0xA1:	/* CMPA indexed */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
		return clocks;
	case 0x18A2:
	case 0xA2:	/* SBCA indexed */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = m6800_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8 - CARRY);
		return clocks;
	case 0x1AA3:	/* CPD indexed */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		m6800_maths16_noh(cpu, REG_D, tmp16, REG_D - tmp16);
		return clocks;
	case 0xCDA3:	/* CPD indexed,Y */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		tmp16 = m6800_maths16_noh(cpu, REG_D, tmp16, REG_D - tmp16);
		return clocks;
	case 0x18A3:
	case 0xA3:	/* SUBD indexed */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		tmp16 = m6800_maths16_noh(cpu, REG_D, tmp16, REG_D - tmp16);
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0x18A4:	/* ANDA index,Y */
	case 0xA4:	/* ANDA indexed */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a &= tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x18A5:	/* BITA */
	case 0xA5:	/* BITA */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_logic8(cpu, cpu->a & tmp8);
		return clocks;
	case 0x18A6:
	case 0xA6:	/* LDAA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x18A7:
	case 0xA7:	/* STAA */
		m6800_do_write(cpu, data16, cpu->a);
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x18A8:
	case 0xA8:	/* EORA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a ^= tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x18A9:	/* ABCA index,Y */
	case 0xA9:	/* ADCA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = m6800_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8 + CARRY);
		return clocks;
	case 0x18AA:	/* ORA ,Y */
	case 0xAA:	/* ORAA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a |= tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0x18AB:	/* ADDA index,Y */
	case 0xAB:	/* ADDA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = m6800_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8);
		return clocks;
	case 0x1AAC:	/* CPY ,X */
	case 0x18AC:	/* CPY ,Y*/
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		m6800_cpx(cpu, cpu->y, tmp16);
		return clocks;
	case 0xCDAC:	/* CPX ,Y */
	case 0xAC:	/* CPX ,X*/
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		m6800_cpx(cpu, cpu->x, tmp16);
		return clocks;
	case 0x18AD:	/* JSR ,Y */
	case 0xAD:	/* JSR ,X */
		m6800_push16(cpu, cpu->pc);
		cpu->pc = data16;
		/* No flags */
		return clocks;
	case 0x18AE:
	case 0xAE:	/* LDS */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		/* Weirdly LDS *does* affect flags */
		cpu->s = tmp16;
		m6800_logic16(cpu, cpu->s);
		return clocks;
	case 0x18AF:
	case 0xAF:	/* STS */
		m6800_do_write(cpu, data16, cpu->s >> 8);
		m6800_do_write(cpu, data16 + 1, cpu->s);
		m6800_logic16(cpu, cpu->s);
		return clocks;
	/* 0xBX: extended */
	case 0xB0:	/* SUBA extended */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = m6800_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
		return clocks;
	case 0xB1:	/* CMPA extended */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
		return clocks;
	case 0xB2:	/* SBCA extended */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = m6800_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8 - CARRY);
		return clocks;
	case 0x1AB3:	/* CPD extended */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		m6800_maths16_noh(cpu, REG_D, tmp16, REG_D - tmp16);
		return clocks;
	case 0xB3:	/* SUBD extended */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		tmp16 = m6800_maths16_noh(cpu, REG_D, tmp16, REG_D - tmp16);
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0xB4:	/* ANDA extended */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a &= tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0xB5:	/* BITA */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_logic8(cpu, cpu->a & tmp8);
		return clocks;
	case 0xB6:	/* LDAA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0xB7:	/* STAA */
		m6800_do_write(cpu, data16, cpu->a);
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0xB8:	/* EORA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a ^= tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0xB9:	/* ADCA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = m6800_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8 + CARRY);
		return clocks;
	case 0xBA:	/* ORAA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a |= tmp8;
		m6800_logic8(cpu, cpu->a);
		return clocks;
	case 0xBB:	/* ADDA */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->a = m6800_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8);
		return clocks;
	case 0x18BC:	/* CPY */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		m6800_cpx(cpu, cpu->y, tmp16);
		return clocks;
	case 0xBC:	/* CPX */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		m6800_cpx(cpu, cpu->x, tmp16);
		return clocks;
	case 0xBD:	/* JSR ext */
		m6800_push16(cpu, cpu->pc);
		cpu->pc = data16;
		/* No flags */
		return clocks;
	case 0xBE:	/* LDS */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		cpu->s = tmp16;
		m6800_logic16(cpu, cpu->s);
		return clocks;
	case 0xBF:	/* STS */
		m6800_do_write(cpu, data16, cpu->s >> 8);
		m6800_do_write(cpu, data16 + 1, cpu->s);
		m6800_logic16(cpu, cpu->s);
		return clocks;
	/* And then repeat for B instead of A and X instead of S, and ADDD not
	   SUBD */
	case 0xC0:	/* SUBB immed */
		cpu->b = m6800_maths8_noh(cpu, cpu->b, data8, cpu->b - data8);
		return clocks;
	case 0xC1:	/* CMPB immed */
		m6800_maths8_noh(cpu, cpu->b, data8, cpu->b - data8);
		return clocks;
	case 0xC2:	/* SBCB immed */
		cpu->b = m6800_maths8_noh(cpu, cpu->b, data8, cpu->b - data8 - CARRY);
		return clocks;
	case 0xC3:	/* ADDD immed16 : weird case where the arg is 16bit */
		tmp16 = m6800_maths16_add(cpu, REG_D, data16, REG_D + data16);
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0xC4:	/* ANDB immed */
		cpu->b &= data8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xC5:	/* BITB */
		m6800_logic8(cpu, cpu->b & data8);
		return clocks;
	case 0xC6:	/* LDAB */
		cpu->b = data8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xC7:	/* Undocumented: STAB immed */
		cpu->pc++;
		m6800_do_write(cpu, cpu->pc++, cpu->a);
		return clocks;
	case 0xC8:	/* EORB */
		cpu->b ^= data8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xC9:	/* ADCB */
		cpu->b = m6800_maths8(cpu, cpu->b, data8, cpu->b + data8 + CARRY);
		return clocks;
	case 0xCA:	/* ORAB */
		cpu->b |= data8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xCB:	/* ADDB */
		cpu->b = m6800_maths8(cpu, cpu->b, data8, cpu->b + data8);
		return clocks;
	case 0xCC:	/* LDD immediate */
		cpu->a = data16 >> 8;
		cpu->b = data16;
		m6800_logic16(cpu, REG_D);
		return clocks;
	/* No STDD immediate: prefix on HC11 */
	case 0x18CE:	/* LDY immediate */
		cpu->y = data16;
		m6800_logic16(cpu, cpu->y);
		return clocks;
	case 0xCE:	/* LDX immediate */
		cpu->x = data16;
		m6800_logic16(cpu, cpu->x);
		return clocks;
	case 0xCF:	/* 68HC11 stop */
		if (cpu->type == CPU_68HC11) {
			if (!(cpu->p & P_S))
				cpu->wait = 2;
			return clocks;
		}
		/* Non 6800 this is the bizarre undocumented STX pc+2 */
		cpu->pc++;
		m6800_do_write(cpu, cpu->pc++, cpu->x >> 8);
		m6800_do_write(cpu, cpu->pc++, cpu->x);
		m6800_logic16(cpu, cpu->x);
		return clocks;
	/* Same again for direct */
	case 0xD0:	/* SUBB dir */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->b = m6800_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
		return clocks;
	case 0xD1:	/* CMPB dir */
		tmp8 = m6800_do_read(cpu, data8);
		m6800_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
		return clocks;
	case 0xD2:	/* SBCB dir */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->b = m6800_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8 - CARRY);
		return clocks;
	case 0xD3:	/* ADDD dir */
		tmp16 = m6800_do_read(cpu, data8) << 8;
		tmp16 |= m6800_do_read(cpu, data8 + 1);
		tmp16 = m6800_maths16_add(cpu, REG_D, tmp16, REG_D + tmp16);
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0xD4:	/* ANDB dir */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->b &= tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xD5:	/* BITB */
		tmp8 = m6800_do_read(cpu, data8);
		m6800_logic8(cpu, cpu->b & tmp8);
		return clocks;
	case 0xD6:	/* LDAB */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->b = tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xD7:	/* STAB */
		m6800_do_write(cpu, data8, cpu->b);
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xD8:	/* EORB */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->b ^= tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xD9:	/* ADCB */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->b = m6800_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8 + CARRY);
		return clocks;
	case 0xDA:	/* ORAB */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->b |= tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xDB:	/* ADDB */
		tmp8 = m6800_do_read(cpu, data8);
		cpu->b = m6800_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8);
		return clocks;
	case 0xDC:	/* LDD direct */
		cpu->a = m6800_do_read(cpu, data8);
		cpu->b = m6800_do_read(cpu, data8 + 1);
		m6800_logic16(cpu, REG_D);
		return clocks;
	case 0xDD:	/* STD direct */
		if (cpu->type == CPU_6800)
			m6800_hcf(cpu);
		m6800_do_write(cpu, data8, cpu->a);
		m6800_do_write(cpu, data8 + 1, cpu->b);
		m6800_logic16(cpu, REG_D);
		return clocks;
	case 0x18DE:	/* LDY direct */
		cpu->y = m6800_do_read(cpu, data8) << 8;
		cpu->y |= m6800_do_read(cpu, data8 + 1);
		m6800_logic16(cpu, cpu->y);
		return clocks;
	case 0xDE:	/* LDX direct */
		cpu->x = m6800_do_read(cpu, data8) << 8;
		cpu->x |= m6800_do_read(cpu, data8 + 1);
		m6800_logic16(cpu, cpu->x);
		return clocks;
	case 0x18DF:	/* STY direct */
		m6800_do_write(cpu, data8, cpu->y >> 8);
		m6800_do_write(cpu, data8 + 1, cpu->y);
		m6800_logic16(cpu, cpu->y);
		return clocks;
	case 0xDF:	/* STX direct */
		m6800_do_write(cpu, data8, cpu->x >> 8);
		m6800_do_write(cpu, data8 + 1, cpu->x);
		m6800_logic16(cpu, cpu->x);
		return clocks;
	/* 0xEX: indexed version */
	case 0x18E0:
	case 0xE0:	/* SUBB indexed */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = m6800_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
		return clocks;
	case 0x18E1:/* CMPB ,Y */
	case 0xE1:	/* CMPB indexed */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
		return clocks;
	case 0x18E2:
	case 0xE2:	/* SBCB indexed */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = m6800_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8 - CARRY);
		return clocks;
	case 0x18E3:	/* ADDD index,Y */
	case 0xE3:	/* ADDD indexed */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		tmp16 = m6800_maths16_add(cpu, REG_D, tmp16, REG_D + tmp16);
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0x18E4:	/* ANDB index,Y */
	case 0xE4:	/* ANDB indexed */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b &= tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0x18E5:	/* BITB */
	case 0xE5:	/* BITB */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_logic8(cpu, cpu->b & tmp8);
		return clocks;
	case 0x18E6:
	case 0xE6:	/* LDAB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0x18E7:
	case 0xE7:	/* STAB */
		m6800_do_write(cpu, data16, cpu->b);
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0x18E8:
	case 0xE8:	/* EORB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b ^= tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0x18E9:	/* ABCA index,Y */
	case 0xE9:	/* ADCB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = m6800_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8 + CARRY);
		return clocks;
	case 0x18EA:	/* ORAB index,Y */
	case 0xEA:	/* ORAB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b |= tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0x18EB:	/* ADDA index,Y */
	case 0xEB:	/* ADDB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = m6800_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8);
		return clocks;
	case 0x18EC:
	case 0xEC:	/* LDD indexed */
		cpu->a = m6800_do_read(cpu, data16);
		cpu->b = m6800_do_read(cpu, data16 + 1);
		m6800_logic16(cpu, REG_D);
		return clocks;
	case 0x18ED:
	case 0xED:	/* STD indexed */
		m6800_do_write(cpu, data16, cpu->a);
		m6800_do_write(cpu, data16 + 1, cpu->b);
		m6800_logic16(cpu, REG_D);
		return clocks;
	case 0x18EE:	/* LDY index,Y */
	case 0x1AEE:	/* LDY index,X */
		cpu->y = m6800_do_read(cpu, data16) << 8;
		cpu->y |= m6800_do_read(cpu, data16 + 1);
		m6800_logic16(cpu, cpu->y);
		return clocks;
	case 0xCDEE:	/* LDX index,Y */
	case 0xEE:	/* LDX indexed */
		cpu->x = m6800_do_read(cpu, data16) << 8;
		cpu->x |= m6800_do_read(cpu, data16 + 1);
		m6800_logic16(cpu, cpu->x);
		return clocks;
	case 0x1AEF:	/* STY ,X */
	case 0x18EF:	/* STY ,Y */
		m6800_do_write(cpu, data16, cpu->y >> 8);
		m6800_do_write(cpu, data16 + 1, cpu->y);
		m6800_logic16(cpu, cpu->y);
		return clocks;
	case 0xCDEF:	/* STX index,Y */
	case 0xEF:	/* STX indexed */
		m6800_do_write(cpu, data16, cpu->x >> 8);
		m6800_do_write(cpu, data16 + 1, cpu->x);
		m6800_logic16(cpu, cpu->x);
		return clocks;
	/* 0xFX: extended */
	case 0xF0:	/* SUBB extended */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = m6800_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
		return clocks;
	case 0xF1:	/* CMPB extended */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
		return clocks;
	case 0xF2:	/* SBCB extended */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = m6800_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8 - CARRY);
		return clocks;
	case 0xF3:	/* ADDD extended */
		tmp16 = m6800_do_read(cpu, data16) << 8;
		tmp16 |= m6800_do_read(cpu, data16 + 1);
		tmp16 = m6800_maths16_add(cpu, REG_D, tmp16, REG_D + tmp16);
		cpu->a = tmp16 >> 8;
		cpu->b = tmp16;
		return clocks;
	case 0xF4:	/* ANDB extended */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b &= tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xF5:	/* BITB */
		tmp8 = m6800_do_read(cpu, data16);
		m6800_logic8(cpu, cpu->b & tmp8);
		return clocks;
	case 0xF6:	/* LDAB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xF7:	/* STAB */
		m6800_do_write(cpu, data16, cpu->b);
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xF8:	/* EORB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b ^= tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xF9:	/* ADCB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = m6800_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8 + CARRY);
		return clocks;
	case 0xFA:	/* ORAB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b |= tmp8;
		m6800_logic8(cpu, cpu->b);
		return clocks;
	case 0xFB:	/* ADDB */
		tmp8 = m6800_do_read(cpu, data16);
		cpu->b = m6800_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8);
		return clocks;
	case 0xFC:	/* LDD extended */
		cpu->a = m6800_do_read(cpu, data16);
		cpu->b = m6800_do_read(cpu, data16 + 1);
		m6800_logic16(cpu, REG_D);
		return clocks;
	case 0xFD:	/* STD extended */
		m6800_do_write(cpu, data16, cpu->a);
		m6800_do_write(cpu, data16 + 1, cpu->b);
		m6800_logic16(cpu, REG_D);
		return clocks;
	case 0x18FE:	/* LDY extended */
		cpu->y = m6800_do_read(cpu, data16) << 8;
		cpu->y |= m6800_do_read(cpu, data16 + 1);
		m6800_logic16(cpu, cpu->y);
		return clocks;
	case 0xFE:	/* LDX extended */
		cpu->x = m6800_do_read(cpu, data16) << 8;
		cpu->x |= m6800_do_read(cpu, data16 + 1);
		m6800_logic16(cpu, cpu->x);
		return clocks;
	case 0x18FF:	/* STY extened */
		m6800_do_write(cpu, data16, cpu->y >> 8 );
		m6800_do_write(cpu, data16 + 1, cpu->y);
		m6800_logic16(cpu, cpu->y);
		return clocks;
	case 0xFF:	/* STX extened */
		m6800_do_write(cpu, data16, cpu->x >> 8 );
		m6800_do_write(cpu, data16 + 1, cpu->x);
		m6800_logic16(cpu, cpu->x);
		return clocks;
	default:
		fprintf(stderr, "Internal error on op %02X\n", opcode);
		exit(1);
	}
}


/* Exception handling */

/* An exception takes 12 cycles. The first two we re-read the opcode
   (not modelled) then we push then read the vector then go */

/* FIXME: The 6303 is pipelined. This code is correct for a 6803 but a 6303
   the logical I mask follows reality by 2 instructions so for example
   "CLI NOP SEI" is not interruptible. We need some kind of shift reg
   holding the state of I by cycle */
static int m6800_pre_execute(struct m6800 *cpu)
{
	/* Interrupts are not latched */
	if (cpu->irq & IRQ_NMI) {
		cpu->irq &= ~IRQ_NMI;
		return m6800_vector(cpu, 0xFFFC);
	}

	if (cpu->irq & IRQ_IRQ1)
		return m6800_vector_masked(cpu, 0xFFF8);
	if (cpu->irq & IRQ_ICF)
		return m6800_vector_masked(cpu, 0xFFF6);
	if (cpu->irq & IRQ_OCF)
		return m6800_vector_masked(cpu, 0xFFF4);
	if (cpu->irq & IRQ_TOF)
		return m6800_vector_masked(cpu, 0xFFF2);
	if (cpu->irq & IRQ_SCI)
		return m6800_vector_masked(cpu, 0xFFF0);
	return 0;
}

#ifdef WITH_HC11
static int m68hc11_pre_execute(struct m6800 *cpu)
{
	/* Interrupts are not latched */
	if (cpu->irq & IRQ_NMI) {
		if (!(cpu->p & P_X)) {
			cpu->p |= P_X;
			return m6800_vector(cpu, 0xFFF4);
		} else {
			/* Pop the frame and continue for NMI masked */
			cpu->p += 9;
		}
		cpu->wait = 0;
	}
	if (cpu->irq & IRQ_IRQ1)	/* External IRQ */
		return m6800_vector_masked(cpu, 0xFFF2);
	if (cpu->irq & IRQ_RTI)
		return m6800_vector_masked(cpu, 0xFFF0);
	if (cpu->irq & IRQ_IC1)		/* IC1 */
		return m6800_vector_masked(cpu, 0xFFEE);
	if (cpu->irq & IRQ_IC2)		/* IC2 */
		return m6800_vector_masked(cpu, 0xFFEC);
	if (cpu->irq & IRQ_IC3)		/* IC3 */
		return m6800_vector_masked(cpu, 0xFFEA);
	if (cpu->irq & IRQ_OC1)		/* OC1 */
		return m6800_vector_masked(cpu, 0xFFE8);
	if (cpu->irq & IRQ_OC2)		/* OC2 */
		return m6800_vector_masked(cpu, 0xFFE6);
	if (cpu->irq & IRQ_OC3)		/* OC3 */
		return m6800_vector_masked(cpu, 0xFFE4);
	if (cpu->irq & IRQ_OC4)		/* OC4 */
		return m6800_vector_masked(cpu, 0xFFE2);
	if (cpu->irq & IRQ_IC4OC5)	/* OC5 or IC4 */
		return m6800_vector_masked(cpu, 0xFFE0);
	if (cpu->irq & IRQ_TOF)		/* Timer overflow */
		return m6800_vector_masked(cpu, 0xFFDE);
	if (cpu->irq & IRQ_PAOV)	/* Pulse overflow */
		return m6800_vector_masked(cpu, 0xFFDC);
	if (cpu->irq & IRQ_PAI)		/* Pulse input */
		return m6800_vector_masked(cpu, 0xFFDA);
	if (cpu->irq & IRQ_SPI)		/* SPI */
		return m6800_vector_masked(cpu, 0xFFD8);
	if (cpu->irq & IRQ_SCI)		/* SCI serial */
		return m6800_vector_masked(cpu, 0xFFD6);
	return 0;
}
#endif

void m6800_clear_interrupt(struct m6800 *cpu, int irq)
{
	cpu->irq &= ~irq;
}

void m6800_raise_interrupt(struct m6800 *cpu, int irq)
{
	cpu->irq |= irq;
}

/*
 *	Execute a machine cycle and return how many clocks
 *	we took doing it.
 */
int m6800_execute(struct m6800 *cpu)
{
	int cycles;
	uint32_t n;
	/* Interrupts ? */
	cycles = m6800_pre_execute(cpu);
	/* A cycle passes but we are waiting */
	if (cpu->wait)
		cycles = 1;
	else
		cycles += m6800_execute_one(cpu);

	/* FIXME: skip the unneeded stuff on 6800/6808 */

	/* See if we passed the output compare, and as we don't check every
	   cycle deal with wraps */
	n = cpu->counter + cycles;
	if (cpu->oc_hold == 0 && cpu->counter >= cpu->ocr && cpu->counter < n) {
		cpu->tcsr |= TCSR_OCF;	/* OCF */
		if (cpu->tcsr & TCSR_EOCI)
			m6800_raise_interrupt(cpu, IRQ_OCF);
		cpu->tcsr ^= TCSR_OLVL;
	}
	cpu->oc_hold = 0;
	if (n > 0xFFFF) {
		cpu->tcsr |= TCSR_TOF;	/* TOF */
		if (cpu->tcsr & TCSR_ETOI)
			m6800_raise_interrupt(cpu, IRQ_TOF);
	}
	cpu->counter = (uint16_t)n;
	return cycles;
}

void m6800_reset(struct m6800 *cpu, int type, int io, int mode)
{
	memset(cpu, 0, sizeof(*cpu));

	cpu->p = P_I;
	cpu->ramcr = RAMCR_RAME;	/* Internal RAM on FIXME check */
	cpu->tcsr = 0;
	cpu->counter = 0;	/* Really this is E clocks after reset FIXME */
	cpu->rmcr = 0;
	cpu->trcsr = TRCSR_TDRE;
	cpu->mode = mode;
	cpu->type = type;
	cpu->intio = io;
	cpu->p2dr = mode << 5;
	cpu->p2ddr = 0;
	cpu->p1ddr = 0;
	cpu->iram_base = 0x80;		/* We don't yet emulate X/Y1 CPUs */
	cpu->pc = m6800_do_read(cpu, 0xFFFE) << 8;
	cpu->pc |= m6800_do_read(cpu, 0xFFFF);
}

#ifdef WITH_HC11

/*
 *	Model a 68HC11 E clock
 *
 *	See Figure 10-1 in the M68HC11 RM
 */

static int prescaler(struct prescaler *p)
{
	if (p->count++ >= p->limit) {
		p->count = 0;
		return 1;
	}
	return 0;
}

static void m68hc11_e_clock(struct m6800 *cpu)
{
	/* Our emulation timer for an SPI transfer. This counts down E clocks
	   between the start and end of an SPI transfer (master emulated only) */
	if (cpu->io.spi_ticks) {
		if (!--cpu->io.spi_ticks) {
			/* An SPI transfer completed: we don't emulate any double
			   buffering */
			cpu->io.spdr_r = m68hc11_spi_done(cpu);
			cpu->io.spsr |= SPSR_SPIF;
			if (cpu->io.spcr & SPCR_SPIE)
				m6800_raise_interrupt(cpu, IRQ_SPI);
		}
	}

	/* 64 cycle lock */
	if (cpu->io.lock)
		cpu->io.lock--;

	/* A 2^13 divider feeds into the RTI and COP */
	if (prescaler(&cpu->io.e13)) {
		/* 1 2 4 or 8 fom RTR[1:0] */
		if (prescaler(&cpu->io.rti)) {
			cpu->io.tflg2 |= TF2_RTIF;
		}
		/* Always by 4 then by 1/4/16/64 ccording to CR[1:0] */
		if (prescaler(&cpu->io.cop)) {
			if (!(cpu->io.config_latch & CFG_NOCOP)) {
				/* We took a COP reset */
				/* TODO */
			}
		}
	}

	/* The tcnt scaler affects all of the ic/oc side */
	if (!prescaler(&cpu->io.pr_tcnt))
		return;
	/* Free running counter */
	cpu->io.tcnt++;
	if (cpu->io.tcnt == 0)
		cpu->io.tflg2 |= TF2_TOF;
	/* Comparators. Set the relevant flags, we will compute their effects
	   later on */
	if (cpu->io.tcnt == cpu->io.toc1)
		cpu->io.tflg1 |= TF1_OC1F;
	if (cpu->io.tcnt == cpu->io.toc2)
		cpu->io.tflg1 |= TF1_OC2F;
	if (cpu->io.tcnt == cpu->io.toc3)
		cpu->io.tflg1 |= TF1_OC3F;
	if (cpu->io.tcnt == cpu->io.toc4)
		cpu->io.tflg1 |= TF1_OC4F;
	if (cpu->io.tcnt == cpu->io.toc5)
		cpu->io.tflg1 |= TF1_OC5F;
	/* We don't model input counts on IC1-IC3 but if we did it would go
	   here */

	/* Turn compare flags into IRQ bits */
	if ((cpu->io.tflg1 & cpu->io.tmsk1) & 0x80)
		m6800_raise_interrupt(cpu, IRQ_OC1);
	else
		m6800_clear_interrupt(cpu, IRQ_OC1);
	if ((cpu->io.tflg1 & cpu->io.tmsk1) & 0x40)
		m6800_raise_interrupt(cpu, IRQ_OC2);
	else
		m6800_clear_interrupt(cpu, IRQ_OC2);
	if ((cpu->io.tflg1 & cpu->io.tmsk1) & 0x20)
		m6800_raise_interrupt(cpu, IRQ_OC3);
	else
		m6800_clear_interrupt(cpu, IRQ_OC3);
	if ((cpu->io.tflg1 & cpu->io.tmsk1) & 0x10)
		m6800_raise_interrupt(cpu, IRQ_OC4);
	else
		m6800_clear_interrupt(cpu, IRQ_OC4);
	/* This one is special - its OC5 or IC4  */
	if ((cpu->io.tflg1 & cpu->io.tmsk1) & 0x08)
		m6800_raise_interrupt(cpu, IRQ_IC4OC5);
	else
		m6800_clear_interrupt(cpu, IRQ_IC4OC5);
	if ((cpu->io.tflg1 & cpu->io.tmsk1) & 0x04)
		m6800_raise_interrupt(cpu, IRQ_IC1);
	else
		m6800_clear_interrupt(cpu, IRQ_IC1);
	if ((cpu->io.tflg1 & cpu->io.tmsk1) & 0x02)
		m6800_raise_interrupt(cpu, IRQ_IC2);
	else
		m6800_clear_interrupt(cpu, IRQ_IC2);
	if ((cpu->io.tflg1 & cpu->io.tmsk1) & 0x01)
		m6800_raise_interrupt(cpu, IRQ_IC3);
	else
		m6800_clear_interrupt(cpu, IRQ_IC3);
	if ((cpu->io.tflg2 & cpu->io.tmsk2) & 0x80)
		m6800_raise_interrupt(cpu, IRQ_TOF);
	else
		m6800_clear_interrupt(cpu, IRQ_TOF);
	if ((cpu->io.tflg2 & cpu->io.tmsk2) & 0x40)
		m6800_raise_interrupt(cpu, IRQ_RTI);
	else
		m6800_clear_interrupt(cpu, IRQ_RTI);
	if ((cpu->io.tflg2 & cpu->io.tmsk2) & 0x20)
		m6800_raise_interrupt(cpu, IRQ_PAOV);
	else
		m6800_clear_interrupt(cpu, IRQ_PAOV);
	if ((cpu->io.tflg2 & cpu->io.tmsk2) & 0x10)
		m6800_raise_interrupt(cpu, IRQ_PAI);
	else
		m6800_clear_interrupt(cpu, IRQ_PAI);
}


/*
 *	Execute a machine cycle and return how many clocks
 *	we took doing it.
 */

int m68hc11_execute(struct m6800 *cpu)
{
	int cycles, i;

	/* Interrupts ? */
	cycles = m68hc11_pre_execute(cpu);
	/* A cycle passes but we are waiting */
	if (cpu->wait)
		cycles = 1;
	else
		cycles += m6800_execute_one(cpu);

	/* The timers do not run if you halt with interrupts off waiting for
	   an XIRQ. They do run if you wait with interrupts on */
	if (cpu->wait && (cpu->p & P_I))
		return cycles;

	/* Run the timers for these E cycles */
	for (i = 0; i < cycles; i++)
		m68hc11_e_clock(cpu);

	return cycles;
}

void m68hc11e_reset(struct m6800 *cpu, int type, uint8_t cfg, const uint8_t *rom, uint8_t *eerom)
{
	memset(cpu, 0, sizeof(*cpu));

	cpu->type = CPU_68HC11;
	cpu->intio = INTIO_HC11;

	cpu->p = P_I | P_S | P_X;
	cpu->io.rom = rom;
	cpu->io.eerom = eerom;
	cpu->io.bootrom = dummy_bootrom;

	cpu->io.padr = 0x8F;
	cpu->io.pioc = 0x03;
	cpu->io.ddrc = 0x00;
	cpu->io.pddr = 0xFF;
	cpu->io.ddrd = 0x00;
	cpu->io.pedr = 0x00;
	cpu->io.oc1m = 0x00;
	cpu->io.oc1d = 0x00;
	cpu->io.tcnt = 0x00;
	cpu->io.toc1 = 0xFFFF;
	cpu->io.toc2 = 0xFFFF;
	cpu->io.toc3 = 0xFFFF;
	cpu->io.toc4 = 0xFFFF;
	cpu->io.toc5 = 0xFFFF;
	cpu->io.tctl1 = 0x00;
	cpu->io.tctl2 = 0x00;
	cpu->io.tmsk1 = 0x00;
	cpu->io.tmsk2 = 0x00;
	cpu->io.tflg1 = 0x00;
	cpu->io.tflg2 = 0x00;
	cpu->io.pactl = 0x00;
	cpu->io.pacnt = 0x00;
	cpu->io.spcr = 0x04;
	cpu->io.spsr = 0x00;
	cpu->io.baud = 0x00;
	cpu->io.sccr1 = 0xC0;
	cpu->io.sccr2 = 0x00;
	cpu->io.scsr = 0xC0;
	cpu->io.adctl = 0x00;
	cpu->io.bprot = 0x1F;
	cpu->io.eprog = 0x00;
	cpu->io.option = 0x10;		/* TODO */
	cpu->io.coprst = 0x00;
	cpu->io.pprog = 0x00;		/* TODO */
	cpu->io.hprio = 0x26;		/* Expanded mode */
	cpu->io.init = 0x01;
	cpu->io.config = cfg;
	cpu->io.config_latch = cfg;

	cpu->io.pr_tcnt.count = 0;
	cpu->io.pr_tcnt.limit = 1;	/* tmsk2 starts 0 so we start divide by 1 */
	cpu->io.e13.count = 0;
	cpu->io.e13.limit = 16384;	/* 2^13 divider */
	cpu->io.rti.count = 0;
	cpu->io.rti.limit = 1;		/* pactl starts 0 so we start divide by 1 */
	cpu->io.cop.count = 0;
	cpu->io.cop.limit = 4;		/* Default is by 4 */

	cpu->io.iobase = 0x1000;
	cpu->io.ioend = 0x103f;
	cpu->io.irambase = 0;
	switch(type) {
	/* Internal EEROM/EPROM/ROM is not yet modelled */
	case 9:	/* As 1 with 12K of ROM or EPROM */
		cpu->io.rombase = 0xD000;
	case 1:	/* 68HC11E1, 512 bytes IRAM, 512 bytes EEPROM */
		cpu->io.erombase = 0xB600;
		cpu->io.eromend = 0xB7FF;
	case 0:	/* 68HC11E0, 512 bytes IRAM no ROM/EPROM/EEPROM */
		cpu->io.iramend = cpu->io.iramsize = 512;
		break;
#if 0
	case 20: /* 768 bytes RAM, 20K EPROM : not emulated yet */
		cpu->io.iramend = cpu->io.iramsize = 768;
		break;
#endif
	case 2:	/* 256 bytes RAM, 2K EEPROM  68HC811E2 */
		cpu->io.iramend = cpu->io.iramsize = 256;
		/* The EEPROM location is configurable */
		cpu->io.erombase = 0x0800 + ((cfg & 0xF0) << 8);
		cpu->io.eromend = 0x0FFF + ((cfg & 0xF0) << 8);
		cpu->io.flags |= CPUIO_HC811_CONFIG;
		break;
	default:
		fprintf(stderr, "Invalid 68HC11E variant.\n");
		exit(1);
	}

	cpu->io.lock = 64;		/* Some stuff locks after 64 cycles */

	/* Must be last so the CPU config is correct for things like internal ROM */
	cpu->pc = m6800_do_read(cpu, 0xFFFE) << 8;
	cpu->pc |= m6800_do_read(cpu, 0xFFFF);

}

void m68hc11a_reset(struct m6800 *cpu, int type, uint8_t cfg, const uint8_t *rom, uint8_t *eerom)
{
	memset(cpu, 0, sizeof(*cpu));

	cpu->type = CPU_68HC11;
	cpu->intio = INTIO_HC11;

	cpu->p = P_I | P_S | P_X;

	cpu->io.rom = rom;
	cpu->io.eerom = eerom;
	cpu->io.bootrom = dummy_bootrom;

	cpu->io.padr = 0x8F;
	cpu->io.pioc = 0x03;
	cpu->io.ddrc = 0x00;
	cpu->io.pddr = 0xFF;
	cpu->io.ddrd = 0x00;
	cpu->io.pedr = 0x00;
	cpu->io.oc1m = 0x00;
	cpu->io.oc1d = 0x00;
	cpu->io.tcnt = 0x00;
	cpu->io.toc1 = 0xFFFF;
	cpu->io.toc2 = 0xFFFF;
	cpu->io.toc3 = 0xFFFF;
	cpu->io.toc4 = 0xFFFF;
	cpu->io.toc5 = 0xFFFF;
	cpu->io.tctl1 = 0x00;
	cpu->io.tctl2 = 0x00;
	cpu->io.tmsk1 = 0x00;
	cpu->io.tmsk2 = 0x00;
	cpu->io.tflg1 = 0x00;
	cpu->io.tflg2 = 0x00;
	cpu->io.pactl = 0x00;
	cpu->io.pacnt = 0x00;
	cpu->io.spcr = 0x04;
	cpu->io.spsr = 0x00;
	cpu->io.baud = 0x00;
	cpu->io.sccr1 = 0xC0;
	cpu->io.sccr2 = 0x00;
	cpu->io.scsr = 0xC0;
	cpu->io.adctl = 0x00;
	cpu->io.bprot = 0x1F;
	cpu->io.eprog = 0x00;
	cpu->io.option = 0x10;		/* TODO */
	cpu->io.coprst = 0x00;
	cpu->io.pprog = 0x00;		/* TODO */
	cpu->io.hprio = 0x26;		/* Expanded mode */
	cpu->io.init = 0x01;
	cpu->io.config = cfg;
	cpu->io.config_latch = cfg;

	cpu->io.pr_tcnt.count = 0;
	cpu->io.pr_tcnt.limit = 1;	/* tmsk2 starts 0 so we start divide by 1 */
	cpu->io.e13.count = 0;
	cpu->io.e13.limit = 16384;	/* 2^13 divider */
	cpu->io.rti.count = 0;
	cpu->io.rti.limit = 1;		/* pactl starts 0 so we start divide by 1 */
	cpu->io.cop.count = 0;
	cpu->io.cop.limit = 4;		/* Default is by 4 */

	cpu->io.iobase = 0x1000;
	cpu->io.ioend = 0x103f;
	cpu->io.irambase = 0;
	switch(type) {
	/* Internal EEROM/EPROM/ROM is not yet modelled */
	case 8:	/* As 1 with 8K of ROM or EPROM */
		cpu->io.rombase = 0xE000;
	case 1:	/* 68HC11A1, 256 bytes IRAM, 256 bytes EEPROM */
		cpu->io.erombase = 0xB600;
		cpu->io.eromend = 0xB6FF;
	case 0:	/* 68HC11A0, 256 bytes IRAM no ROM/EPROM/EEPROM */
		cpu->io.iramend = cpu->io.iramsize = 256;
		break;
	default:
		fprintf(stderr, "Invalid 68HC11A variant.\n");
		exit(1);
	}

	cpu->io.lock = 64;		/* Some stuff locks after 64 cycles */

	/* Must be last so the CPU config is correct for things like internal ROM */
	cpu->pc = m6800_do_read(cpu, 0xFFFE) << 8;
	cpu->pc |= m6800_do_read(cpu, 0xFFFF);
}

#endif

/*
 *	6803 device model
 */


/* Counter interrupts */
static void m6800_counter_ints(struct m6800 *cpu)
{
	if ((cpu->tcsr & (TCSR_ETOI|TCSR_TOF)) == (TCSR_ETOI|TCSR_TOF))
		m6800_raise_interrupt(cpu, IRQ_TOF);
	else
		m6800_clear_interrupt(cpu, IRQ_TOF);

	if ((cpu->tcsr & (TCSR_EOCI|TCSR_OCF)) == (TCSR_EOCI|TCSR_OCF))
		m6800_raise_interrupt(cpu, IRQ_OCF);
	else
		m6800_clear_interrupt(cpu, IRQ_OCF);
	if ((cpu->tcsr & (TCSR_EICI|TCSR_ICF)) == (TCSR_EICI|TCSR_ICF))
		m6800_raise_interrupt(cpu, IRQ_ICF);
	else
		m6800_clear_interrupt(cpu, IRQ_ICF);
}

/* SCI interrupts */
static void m6800_sci_ints(struct m6800 *cpu)
{
	int irq = 0;
	if ((cpu->trcsr & (TRCSR_TDRE|TRCSR_TIE)) == (TRCSR_TDRE|TRCSR_TIE))
		irq = 1;
	if ((cpu->trcsr & (TRCSR_RDRF|TRCSR_RIE)) == (TRCSR_RDRF|TRCSR_RIE))
		irq = 1;
	if ((cpu->trcsr & (TRCSR_ORFE|TRCSR_RIE)) == (TRCSR_ORFE|TRCSR_RIE))
		irq = 1;
	if (irq)
		m6800_raise_interrupt(cpu, IRQ_SCI);
	else
		m6800_clear_interrupt(cpu, IRQ_SCI);
}

/* We have received a byte of external data */
void m6800_rx_byte(struct m6800 *cpu, uint8_t c)
{
	if (cpu->trcsr & TRCSR_RE) {
		if (cpu->trcsr & TRCSR_RDRF)
			cpu->trcsr |= TRCSR_ORFE;
		else {
			cpu->trcsr |= TRCSR_RDRF;
			cpu->rdr = c;
			m6800_sci_ints(cpu);
		}
	}
}

void m6800_tx_done(struct m6800 *cpu)
{
	if (!(cpu->trcsr & TRCSR_TDRE)) {
		cpu->trcsr |= TRCSR_TDRE;
		m6800_sci_ints(cpu);
	}
}

uint8_t m6800_read_io(struct m6800 *cpu, uint8_t addr)
{
	uint8_t val;
	switch(addr) {
		case 0x00:
			return cpu->p1ddr;
		case 0x01:
			return cpu->p2ddr;
		case 0x02:
			val = m6800_port_input(cpu, 1) & ~cpu->p1ddr;
			val |= cpu->p1dr & cpu->p1ddr;
			return val;
		case 0x03:
			/* FIXME: what happens with serial enabled */
			/* P21 is special but we leave that to the called code */
			val = m6800_port_input(cpu, 2) & ~cpu->p2ddr;
			val |= cpu->p2dr & cpu->p2ddr;
			return val;
		/* 4-7 form a hole */
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
			return m6800_read(cpu, addr);
		case 0x08:	/* Timer control and status */
			val = cpu->tcsr;
			/* Reading it clears several bits maybe: docs unclear */
			return val;
		case 0x09:	/* Free running counter */
			/* When this is read 0x0A is latched for one cycle
			   and returns the latched bits to avoid wrap errors. This
			   works by magic because we don't do per clock cycle emulation */
			cpu->tcsr &= ~(TCSR_TOF|TCSR_OCF);
			m6800_counter_ints(cpu);
			return cpu->counter >> 8;
		case 0x0A:
			return cpu->counter;
		case 0x0B:	/* Output compare */
			return cpu->ocr >> 8;
		case 0x0C:
			return cpu->ocr;
		case 0x0D:	/* Input capture is not emulated */
			cpu->tcsr &= ~TCSR_ICF;
			m6800_counter_ints(cpu);
		case 0x0E:
			return 0;
		case 0x0F:	/* hole in expanded */
			return m6800_read(cpu, addr);
		/* 0x10 is WO */
		case 0x11:	/* TRCSR */
			/* Some of the side effects need review as they are tangled
			   with actual other port read/writes and the doc isn't clear */
			val = cpu->trcsr;
			return val;
		case 0x12:      /* RDR */
			cpu->trcsr &= ~(TRCSR_RDRF|TRCSR_ORFE);	/* RDRF off */
			m6800_sci_ints(cpu);
			return cpu->rdr;
		case 0x14:	/* RAMCR */
			return cpu->ramcr;
		/* On the later 6303
		    0x14 becomes RAM/port5cr
		    0x15 becomes port5 (R)
		    0x16 becomes port6 ddr (W)
		    0x17 becomes port6 (RW)
		    0x18 becomes port7 (RW)
		    0x19 becomes OCR2H
		    0x1A becomes OCR2L
		    0x1B becomes TCSR3
		    0x1C becomes TCR
		    0x1D becomes T2UP

		    And latest of them
		    0x1e becomes TXRXCSR2
		    0x1f becomes test
		    0x20 becomes port5 ddr
		    0x21 becomes port6 csr
		*/
		default:
			/* FIXME: log this */
			return 0xFF;	/* Reserved */
	}
}

void m6800_write_io(struct m6800 *cpu, uint8_t addr, uint8_t val)
{
	switch(addr) {
		case 0x00:
			cpu->p1ddr = val;
			m6800_port_output(cpu, 1);
			break;
		case 0x01:
			/* FIXME: what bits are writable - is the latched top fixed */
			cpu->p2ddr = val;
			m6800_port_output(cpu, 2);
			break;
		case 0x02:
			cpu->p2dr = val;
			m6800_port_output(cpu, 1);
			break;
		case 0x03:
			/* FIXME: what happens with serial enabled */
			cpu->p2dr = val;
			m6800_port_output(cpu, 2);
			break;
		/* 4-7 form a hole */
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
			m6800_write(cpu, addr, val);
			break;
		case 0x08:
			cpu->tcsr = val;
			break;
		/* FIXME: on 6303 a double store *only* writes the timer correctly,
			a single causes the FFF8 effect as on 6803 */
		case 0x09:	/* Timer - test function set to FFF8 */
			cpu->counter = 0xFFF8;
			break;
		case 0x0A:	/* Timer - no effect */
			break;
		case 0x0B:	/* Output compare high */
			cpu->ocr &= 0xFF;
			cpu->ocr |= (val << 8);
			cpu->tcsr &= ~0x40;	/* Clear OCF */
			/* Review : 1 insn or one clock ? */
			cpu->oc_hold = 1;
			break;
		case 0x0C:	/* Output compare low */
			cpu->ocr &= 0xFF00;
			cpu->ocr |= val;
			cpu->tcsr &= ~0x40;	/* Clear OCF */
			break;
		case 0x0D:	/* Input capture (not emulated) */
		case 0x0E:
			break;
		case 0x0F:	/* Port 3 : hole for expanded mode */
			m6800_write(cpu, addr, val);
			break;
		case 0x10:
			cpu->rmcr = val & 0x0F;
			/* TODO: model the serial clocking from this */
			/* Call back so the caller knows the serial state changed */
			m6800_sci_change(cpu);
			break;
		case 0x11:
			cpu->trcsr &= ~0x1F;
			cpu->trcsr |= (val & 0x1E);
			m6800_sci_ints(cpu);
			/* Call back so the caller knows the serial state changed */
			m6800_sci_change(cpu);
			break;
		case 0x12:
			/* Check RO */
			break;
		case 0x13:
			/* TX enabled, TDRE empty */
			/* FIXME: this doesn't correctly model writing a byte then setting
			   TE later */
			if ((cpu->trcsr & (TRCSR_TDRE|TRCSR_TE)) == (TRCSR_TDRE|TRCSR_TE)) {
				m6800_tx_byte(cpu, val);
				m6800_sci_ints(cpu);
			}
			break;
		case 0x14:
			/* FIXME: we need to watch bit 6 */
			cpu->ramcr = val;
			break;
	}
}

#ifdef WITH_HC11

/* SCI interrupts */
static void m68hc11_sci_ints(struct m6800 *cpu)
{
	int irq = 0;
	if ((cpu->io.sccr2 & SCCR2_TIE) && (cpu->io.scsr & SCSR_TDRE))
		irq = 1;
	if ((cpu->io.sccr2 & SCCR2_RIE) && (cpu->io.scsr & SCSR_RDRF))
		irq = 1;
	/* We don't yet model TCIE or ILIE */
	if (irq)
		m6800_raise_interrupt(cpu, IRQ_SCI);
	else
		m6800_clear_interrupt(cpu, IRQ_SCI);
}

/* We have received a byte of external data */
void m68hc11_rx_byte(struct m6800 *cpu, uint8_t c)
{
	if (cpu->io.sccr2 & SCCR2_RE) {
		if (cpu->io.scsr & SCSR_RDRF)
			cpu->io.scsr |= SCSR_OR;
		else {
			cpu->io.scsr |= SCSR_RDRF;
			cpu->io.scdr_r = c;
			m68hc11_sci_ints(cpu);
		}
	}
}

/* The emulator completed transmitting the pending serial byte */
void m68hc11_tx_done(struct m6800 *cpu)
{
	if (!(cpu->io.scsr & SCSR_TDRE)) {
		cpu->io.scsr |= SCSR_TDRE;
		m68hc11_sci_ints(cpu);
	}
}

/*
 *	The 68HC11 has a different and much more sophisticated internal
 *	I/O feature set. We only model a few bits for now.
 */
static uint8_t m68hc11_read_io(struct m6800 *cpu, uint8_t addr)
{
	uint8_t val;
	uint8_t mask;

	switch(addr) {
		case 0x00:	/* Port A */
			/* Port A bits 2, 1, 0 */
			mask = 0x07;
			/* PA7 might be an input */
			if (!(cpu->io.pactl & 0x80))
				mask |= 0x80;
			/* We return input bits plus the output bit values set */
			val = m6800_port_input(cpu, 1) & mask;
			val |= cpu->io.padr & ~mask;
			return val;
		case 0x01:	/* Port A direction register */
			return cpu->io.padr;/*??*/
		case 0x02:	/* Port I/O control */
			return cpu->io.pioc;
		case 0x03:	/* Port C (not modelled yet not in expanded mode) */
			return 0xFF;	/* TODO */
		case 0x04:	/* Port B (ditto) */
			return 0xFF;
		case 0x05:	/* Port C L (ditto) */
			return 0xFF;
		case 0x07:	/* Port C direction */
			return cpu->io.ddrc;
		case 0x08:	/* Port D */
			val = m6800_port_input(cpu, 4) & ~cpu->io.ddrd;
			val |= cpu->io.pddr & cpu->io.ddrd;
			val &= 0x3F;
			return val;
		case 0x09:
			return cpu->io.ddrd;
		case 0x0A:
			return m6800_port_input(cpu, 5);
		case 0x0B:
			return cpu->io.cforc;
		case 0x0C:
			return cpu->io.oc1m;
		case 0x0D:
			return cpu->io.oc1d;
		/* Note: the fact we simulate tcnt by instruction not clock means
		   the LDD behaviour works for now */
		case 0x0E:
			return cpu->io.tcnt >> 8;
		case 0x0F:
			return cpu->io.tcnt & 0xFF;
		case 0x10:
			return cpu->io.tic1 >> 8;
		case 0x11:
			return cpu->io.tic1 & 0xFF;
		case 0x12:
			return cpu->io.tic2 >> 8;
		case 0x13:
			return cpu->io.tic2 & 0xFF;
		case 0x14:
			return cpu->io.tic3 >> 8;
		case 0x15:
			return cpu->io.tic3 & 0xFF;
		case 0x16:
			return cpu->io.toc1 >> 8;
		case 0x17:
			return cpu->io.toc1 & 0xFF;
		case 0x18:
			return cpu->io.toc2 >> 8;
		case 0x19:
			return cpu->io.toc2 & 0xFF;
		case 0x1A:
			return cpu->io.toc3 >> 8;
		case 0x1B:
			return cpu->io.toc3 & 0xFF;
		case 0x1C:
			return cpu->io.toc4 >> 8;
		case 0x1D:
			return cpu->io.toc4 & 0xFF;
		case 0x1E:
			return cpu->io.toc5 >> 8;
		case 0x1F:
			return cpu->io.toc5 & 0xFF;
		case 0x20:
			return cpu->io.tctl1;
		case 0x21:
			return cpu->io.tctl2;
		case 0x22:
			return cpu->io.tmsk1;
		case 0x23:
			return cpu->io.tflg1;
		case 0x24:
			return cpu->io.tmsk2;
		case 0x25:
			return cpu->io.tflg2;
		case 0x26:
			return cpu->io.pactl;
		case 0x27:
			return cpu->io.pacnt;
		case 0x28:
			return cpu->io.spcr;
		case 0x29:
			return cpu->io.spsr;
		case 0x2A:	/* SPI data is loaded by the transmit event response */
			cpu->io.spsr &= ~SPSR_SPIF;
			return cpu->io.spdr_r;
		case 0x2B:
			return cpu->io.baud;
		case 0x2C:
			return cpu->io.sccr1;
		case 0x2D:
			return cpu->io.sccr2;
		case 0x2E:
			/* Save this for SCDR handling */
			cpu->io.last_scsr_read = cpu->io.scsr;
			return cpu->io.scsr;
		case 0x2F:
			/* Clear only the flags set by the last scsr read. TDRE and TC
			   are cleared by a write instead */
			cpu->io.scsr &= ~(cpu->io.scsr & ~(SCSR_TDRE|SCSR_TC));
			m68hc11_sci_ints(cpu);
			return cpu->io.scdr_r;
		case 0x30:
			return cpu->io.adctl;
		case 0x31:
			return cpu->io.adr1;
		case 0x32:
			return cpu->io.adr2;
		case 0x33:
			return cpu->io.adr3;
		case 0x34:
			return cpu->io.adr4;
		case 0x35:
			return cpu->io.bprot;
		case 0x36:
			return cpu->io.eprog;
		case 0x39:
			return cpu->io.option;
		case 0x3C:
			return cpu->io.hprio;
		case 0x3D:
			return cpu->io.init;
		case 0x3E:
			return 0xFF;	/* Test 1 */
		case 0x3F:
			return cpu->io.config;
		/* TODO: K series MMU */
		/* FIXME: log this */
		default:
			return 0xFF;	/* Reserved */
	}
}

/*
 *	Very basic for now - we need to implement things like the
 *	locks on registers.
 */
static void m68hc11_write_io(struct m6800 *cpu, uint8_t addr, uint8_t val)
{
	static const unsigned int cop_limit[4] = { 1, 4, 16, 64 };
	switch(addr) {
		case 0x00:
			cpu->io.padr = val;
			m6800_port_output(cpu, 1);
			break;
		case 0x01:
			cpu->io.padr = val;
			m68hc11_port_direction(cpu, 1);
			break;
		case 0x02:
			cpu->io.pioc = val;
			/* TODO: side effects */
			break;
		case 0x03:	/* Port C is not useful in expanded mode */
			break;
		case 0x04:	/* Port B is not useful in expnded mode */
			break;
		case 0x05:	/* Port C latch ditto */
			break;
		case 0x07:
			cpu->io.ddrc = val;
			break;
		case 0x08:
			cpu->io.pddr = val & 0x3F;
			m6800_port_output(cpu, 4);
			break;
		case 0x09:
			cpu->io.ddrd = val & 0x3F;
			m68hc11_port_direction(cpu, 4);
			break;
		case 0x0A:
			cpu->io.pedr = val;
			/* TODO */
			break;
		case 0x0B:
			cpu->io.cforc = val & 0xF8;
			break;
		case 0x0C:
			cpu->io.oc1m = val;
			break;
		case 0x0D:
			cpu->io.oc1d = val;
			break;
		case 0x0E:	/* TCNT is read only */
		case 0x0F:
			break;
		case 0x10:	/* Input capture is read only */
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
			break;
		case 0x16:	/* Timer output compare */
			cpu->io.toc1 &= 0xFF;
			cpu->io.toc1 |= val;
			break;
		case 0x17:
			cpu->io.toc1 &= 0xFF00;
			cpu->io.toc1 |= val;
			break;
		case 0x18:
			cpu->io.toc2 &= 0xFF;
			cpu->io.toc2 |= val << 8;
			break;
		case 0x19:
			cpu->io.toc2 &= 0xFF00;
			cpu->io.toc2 |= val;
			break;
		case 0x1A:
			cpu->io.toc3 &= 0xFF;
			cpu->io.toc3 |= val << 8;
			break;
		case 0x1B:
			cpu->io.toc3 &= 0xFF00;
			cpu->io.toc3 |= val;
			break;
		case 0x1C:
			cpu->io.toc4 &= 0xFF;
			cpu->io.toc4 |= val << 8;
			break;
		case 0x1D:
			cpu->io.toc4 &= 0xFF00;
			cpu->io.toc4 |= val;
			break;
		case 0x1E:
			cpu->io.toc5 &= 0xFF;
			cpu->io.toc5 |= val << 8;
			break;
		case 0x1F:
			cpu->io.toc5 &= 0xFF00;
			cpu->io.toc5 |= val;
			break;
		case 0x20:	/* tctl1/2 */
			cpu->io.tctl1 = val;
			break;
			/* TODO */
		case 0x21:
			cpu->io.tctl2 = val;
			break;
		case 0x22:
			cpu->io.tmsk1 = val;
			break;
		case 0x23:
			/* Clear flags by writing a 1 bit to the bits to clear */
			cpu->io.tflg1 &= ~val;
			break;
		case 0x24:
			if (!cpu->io.lock && !(cpu->io.hprio & HPRIO_SMOD)) {
				val &= ~3;
				val |= cpu->io.tmsk2 & 3;
			}
			cpu->io.tmsk2 = val;
			/* The low 2 bits control the tcnt scaling */
			switch(val & 3) {
			case 0:
				cpu->io.pr_tcnt.limit = 1;
				break;
			case 1:
				cpu->io.pr_tcnt.limit = 4;
				break;
			case 2:
				cpu->io.pr_tcnt.limit = 8;
				break;
			case 3:
				cpu->io.pr_tcnt.limit = 16;
				break;
			}
			break;
		case 0x25:
			/* Clear flags by writing a 1 bit into the bit position */
			cpu->io.tflg2 &= ~(val & 0xF0);
			break;
		case 0x26:
			cpu->io.pactl = val;
			/* Adjust the rti scaling */
			cpu->io.rti.limit = 1 << (val & 3);
			break;
		case 0x27:
			cpu->io.pacnt = val;
			break;
		case 0x28:
			cpu->io.spcr = val;
			break;
		case 0x29:
			break;
		case 0x2A:
			/* SPI is off */
			if (!(cpu->io.spcr & SPCR_SPE))
				break;
			/* Write is inhibited until the received data is read - even if
			   you just need to discard it */
			if (cpu->io.spsr & SPSR_SPIF)
				break;
			/* Tell the emulator an SPI transfer is beginning */
			/* We only emulate master so the caller must set DDRD bits
			   3 and 4 to get data. We don't check 0x20 for SS as that's
			   *way* more complicated */
			if (cpu->io.ddrd & 0x18)
				m68hc11_spi_begin(cpu, val);
			else
				m68hc11_spi_begin(cpu, 0xFF);
			/* Flat out is E/2 */
			cpu->io.spi_ticks = 16;
			if (cpu->io.spcr & 1)	/* Divide by 2 */
				cpu->io.spi_ticks <<= 1;
			if (cpu->io.spcr & 2)	/* Divide by 8 */
				cpu->io.spi_ticks <<= 3;
			break;
		case 0x2B:	/* Baud rate */
			cpu->io.baud = val;
			m6800_sci_change(cpu);
			break;
		case 0x2C:
			cpu->io.sccr1 = val;
			m6800_sci_change(cpu);
			break;
		case 0x2D:
			cpu->io.sccr2 = val;
			m6800_sci_ints(cpu);
			break;
		case 0x2E:
			break;
		case 0x2F:
			cpu->io.scdr_w = val;
			cpu->io.scsr &= ~(SCSR_TDRE|SCSR_TC);
			if (cpu->io.sccr2 & SCCR2_TE)
				m6800_tx_byte(cpu, val);
			m68hc11_sci_ints(cpu);
			break;
		case 0x30:
			cpu->io.adctl &= 0x80;
			cpu->io.adctl |= val & 0x3F;
			break;
		case 0x31:
			cpu->io.adr1 = val;
			break;
		case 0x32:
			cpu->io.adr2 = val;
			break;
		case 0x33:
			cpu->io.adr3 = val;
			break;
		case 0x34:
			cpu->io.adr4 = val;
			break;
		case 0x35:
			if (!cpu->io.lock && !(cpu->io.hprio & HPRIO_SMOD))
				val |= cpu->io.bprot;
			cpu->io.bprot = val & 0x1F;
		case 0x36:
			if (!cpu->io.lock)
				return;
			/* Not that we emulated the eeprom */
			cpu->io.eprog = val & 0xBF;
			break;
		case 0x39:
			if (!cpu->io.lock && !(cpu->io.hprio & HPRIO_SMOD)) {
				val &= 0xC8;
				val |= (cpu->io.option & 0x33);
			}
			cpu->io.option = val & 0xFB;
			cpu->io.cop.limit = cop_limit[val & 3];
			break;
		case 0x3A:
			if (cpu->io.coprst == 0x55 && val == 0xAA)
				cpu->io.cop.count = 0;
			cpu->io.coprst = val;
			break;
		case 0x3C:
			/* Cannot write with interrupts unmasked */
			if (!(cpu->p & P_I))
				break;
			/* Cannot set RBOOT without SMOD, cannot set SMOD once clear */
			/* Cannot change MDA without SMOD */
			if (!(cpu->io.hprio & HPRIO_SMOD)) {
				val &= ~(HPRIO_RBOOT|HPRIO_SMOD|HPRIO_MDA);
				val |= cpu->io.hprio & HPRIO_MDA;
			}
			cpu->io.hprio = val;
			break;
		case 0x3D:
			if (!cpu->io.lock && !(cpu->io.hprio & HPRIO_SMOD))
				return;
			cpu->io.init = val;
			cpu->io.iobase = (val & 0x0FU) << 12;
			cpu->io.ioend = cpu->io.iobase + 0x3F;
			cpu->io.irambase = (val & 0xF0U) << 8;
			cpu->io.iramend = cpu->io.irambase + cpu->io.iramsize - 1;
			break;
		case 0x3E:
			/* This is actually test1 if we ever care */
			break;
		case 0x3F:
			/* This does nothing to the running config in normal modes */
			if (cpu->io.flags & CPUIO_HC811_CONFIG)
				cpu->io.config = val;
			else
				cpu->io.config = val & 0x0F;
			if (cpu->io.hprio & 0x40) {
				cpu->io.config_latch = cpu->io.config;
				if (cpu->io.flags & CPUIO_HC811_CONFIG) {
					cpu->io.erombase = 0x0800 + ((cpu->io.config & 0xF0) << 8);
					cpu->io.eromend = 0x0FFF + ((cpu->io.config & 0xF0) << 8);
				}
			}
			break;
	}
}
#endif
/* We only support mode 2 and mode 3 on the other parts for now */


/* 0x40-0xFF are IRAM on the later 6303 parts */
uint8_t m6800_do_read(struct m6800 *cpu, uint16_t addr)
{
	switch (cpu->intio) {
	case INTIO_6802:
		if (addr < 128)
			return cpu->iram[addr];
		/* Fall through */
	case INTIO_NONE:
	default:
		return m6800_read(cpu, addr);
	case INTIO_6803:
		if (addr >= 0x0100)
			return m6800_read(cpu, addr);
		/* FIXME: check if i fetch here and if so fault on 6303 */
		if (addr < 0x20)
			return m6800_read_io(cpu, addr);
		if (cpu->mode == 2 && (cpu->ramcr & RAMCR_RAME) && (addr >= cpu->iram_base && addr <= 0xFF))
			return cpu->iram[addr - cpu->iram_base];
		return m6800_read(cpu, addr);
#ifdef WITH_HC11
	case INTIO_HC11:
		if (addr >= cpu->io.iobase && addr <= cpu->io.ioend)
			return m68hc11_read_io(cpu, addr);
		if (addr >= cpu->io.irambase && addr <= cpu->io.iramend)
			return cpu->iram[addr];
		if (addr >= cpu->io.erombase && addr <= cpu->io.eromend &&
				(cpu->io.config_latch & CFG_EEON))
			return cpu->io.eerom[addr - cpu->io.erombase];
		if (addr >= 0xBF00 && addr <= 0xBFFF && (cpu->io.hprio & HPRIO_RBOOT))
			return cpu->io.bootrom[addr & 0xFF];
		if (addr >= cpu->io.rombase && (cpu->io.config_latch & CFG_ROMON))
			return cpu->io.rom[addr - cpu->io.rombase];
		return m6800_read(cpu, addr);
#endif
	}
}

void m6800_do_write(struct m6800 *cpu, uint16_t addr, uint8_t val)
{
	switch (cpu->intio) {
	case INTIO_6802:
		if (addr < 128) {
			cpu->iram[addr] = val;
			break;
		}
	default:
	case INTIO_NONE:
		m6800_write(cpu, addr, val);
		break;
	case INTIO_6803:
		if (addr >= 0x0100)
			m6800_write(cpu, addr, val);
		else if (addr < 0x20)
			m6800_write_io(cpu, addr, val);
		else if (cpu-> mode == 2 && (cpu->ramcr & RAMCR_RAME) && (addr >= cpu->iram_base && addr <= 0xFF))
			cpu->iram[addr - cpu->iram_base] = val;
		else
			m6800_write(cpu, addr, val);
		break;
#ifdef WITH_HC11
	case INTIO_HC11:
		/* No emulation of writable EEROM yet */
		if (addr >= cpu->io.rombase && (cpu->io.config_latch & CFG_ROMON))
			return;
		else if (addr >= cpu->io.erombase && addr <= cpu->io.eromend &&
				(cpu->io.config_latch & CFG_EEON))
			return;
		if (addr >= cpu->io.iobase && addr <= cpu->io.ioend)
			m68hc11_write_io(cpu, addr, val);
		else if (addr >= cpu->io.irambase && addr <= cpu->io.iramend)
			cpu->iram[addr] = val;
		else if (addr >= 0xBF00 && addr <= 0xBFFF && (cpu->io.hprio & HPRIO_RBOOT))
			return;
		else
			m6800_write(cpu, addr, val);
		break;
#endif
	}
}

uint8_t m6800_do_debug_read(struct m6800 *cpu, uint16_t addr)
{
	switch (cpu->intio) {
	case INTIO_6802:
		if (addr < 128)
			return cpu->iram[addr];
		/* Fall through */
	default:
	case INTIO_NONE:
		return m6800_debug_read(cpu, addr);
	case INTIO_6803:
		if (addr >= 0x0100)
			return m6800_debug_read(cpu, addr);
		if (addr < 0x20)	/* I/O */
			return 0xFF;
		if (cpu->mode == 2 && (cpu->ramcr & RAMCR_RAME) && (addr >= cpu->iram_base && addr <= 0xFF))
			return cpu->iram[addr - cpu->iram_base];
		return m6800_debug_read(cpu, addr);
#ifdef WITH_HC11
	case INTIO_HC11:
		/* Nothing with side effects */
		if (addr >= cpu->io.iobase && addr <= cpu->io.ioend)
			return 0xff;
		if (addr >= cpu->io.irambase && addr <= cpu->io.iramend)
			return cpu->iram[addr];
		if (addr >= cpu->io.erombase && addr <= cpu->io.eromend &&
				(cpu->io.config_latch & CFG_EEON))
			return cpu->io.eerom[addr - cpu->io.erombase];
		if (addr >= 0xBF00 && addr <= 0xBFFF && (cpu->io.hprio & HPRIO_RBOOT))
			return cpu->io.bootrom[addr & 0xFF];
		if (addr >= cpu->io.rombase && (cpu->io.config_latch & CFG_ROMON))
			return cpu->io.rom[addr - cpu->io.rombase];
		return m6800_debug_read(cpu, addr);
#endif
	}
}
