/*
 *	Ampro Littleboard
 *	4MHz Z80
 *	64K DRAM
 *	4-32K ROM (repeats through low space)
 *	DART
 *	CTC (provides some DART timings)
 *	Printer port
 *	WD1772 floppy
 *
 *	PLUS has an NCR5380
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include "libz80/z80.h"
#include "z80dis.h"
#include "sasi.h"
#include "ncr5380.h"
#include "wd17xx.h"


static uint8_t ram[65536];
static uint8_t rom[32768];

static uint8_t fast = 0;
static uint8_t idport = 0x07;
static uint8_t bcr = 0;
static uint16_t eprom_mask;

static Z80Context cpu_z80;
static uint8_t int_recalc = 0;

struct sasi_bus *sasi;
struct ncr5380 *ncr;
struct wd17xx *wd;

/* IRQ source that is live */
static uint8_t live_irq;

#define IRQ_SIOA	1
#define IRQ_SIOB	2
#define IRQ_CTC		4

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_SIO	16
#define TRACE_CTC	32
#define TRACE_IRQ	64
#define TRACE_CPU	128
#define TRACE_SCSI	256
#define TRACE_FDC	512

static int trace = 0;

static void reti_event(void);

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate;
	uint8_t r;

	if (trace & TRACE_MEM)
		fprintf(stderr, "R");
	if (addr < 0x8000 && !(bcr & 0x40))
		r = rom[addr & eprom_mask];
	else
		r = ram[addr];
	if (trace & TRACE_MEM)
		fprintf(stderr, " %04X <- %02X\n", addr, r);

	/* Look for ED with M1, followed directly by 4D and if so trigger
	   the interrupt chain */
	if (cpu_z80.M1) {
		/* DD FD CB see the Z80 interrupt manual */
		if (r == 0xDD || r == 0xFD || r== 0xCB) {
			rstate = 2;
			return r;
		}
		/* Look for ED with M1, followed directly by 4D and if so trigger
		   the interrupt chain */
		if (r == 0xED && rstate == 0) {
			rstate = 1;
			return r;
		}
	}
	if (r == 0x4D && rstate == 1)
		reti_event();
	rstate = 0;
	return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (addr < 0x8000 && !(bcr & 0x40)) {
/*		if (trace & TRACE_MEM) */
			fprintf(stderr, "W %04X : ROM\n", addr);
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	ram[addr] = val;
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = mem_read(0, addr);
	fprintf(stderr, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return mem_read(0, addr);
}

static void z80_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z80.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED &&
		(z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z80.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while(nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z80.R1.br.A, cpu_z80.R1.br.F,
		cpu_z80.R1.wr.BC, cpu_z80.R1.wr.DE, cpu_z80.R1.wr.HL,
		cpu_z80.R1.wr.IX, cpu_z80.R1.wr.IY, cpu_z80.R1.wr.SP);
}

static int check_chario(void)
{
	fd_set i, o;
	struct timeval tv;
	unsigned int r = 0;

	FD_ZERO(&i);
	FD_SET(0, &i);
	FD_ZERO(&o);
	FD_SET(1, &o);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(2, &i, &o, NULL, &tv) == -1) {
		perror("select");
		exit(1);
	}
	if (FD_ISSET(0, &i))
		r |= 1;
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

static unsigned int next_char(void)
{
	char c;
	if (read(0, &c, 1) != 1) {
		printf("(tty read without ready byte)\n");
		return 0xFF;
	}
	if (c == 0x0A)
		c = '\r';
	return c;
}

static void recalc_interrupts(void)
{
	int_recalc = 1;
}

struct z80_sio_chan {
	uint8_t wr[8];
	uint8_t rr[3];
	uint8_t data[3];
	uint8_t dptr;
	uint8_t irq;
	uint8_t rxint;
	uint8_t txint;
	uint8_t intbits;
#define INT_TX	1
#define INT_RX	2
#define INT_ERR	4
	uint8_t pending;	/* Interrupt bits pending as an IRQ cause */
	uint8_t vector;		/* Vector pending to deliver */
};

static struct z80_sio_chan sio[2];

static void sio2_clear_int(struct z80_sio_chan *chan, uint8_t m)
{
	if (trace & TRACE_IRQ) {
		fprintf(stderr, "Clear intbits %d %x\n",
			(int)(chan - sio), m);
	}
	chan->intbits &= ~m;
	chan->pending &= ~m;
	/* Check me - does it auto clear down or do you have to reti it ? */
	if (!(sio->intbits | sio[1].intbits)) {
		sio->rr[1] &= ~0x02;
		chan->irq = 0;
	}
	recalc_interrupts();
}

static void sio2_raise_int(struct z80_sio_chan *chan, uint8_t m)
{
	uint8_t new = (chan->intbits ^ m) & m;
	uint8_t vector;
	chan->intbits |= m;
	if ((trace & TRACE_SIO) && new)
		fprintf(stderr, "SIO raise int %x new = %x\n", m, new);
	if (new) {
		if (!sio->irq) {
			chan->irq = 1;
			sio->rr[1] |= 0x02;
			vector = 0; /* sio[1].wr[2]; */
			/* This is a subset of the real options. FIXME: add
			   external status change */
			if (sio[1].wr[1] & 0x04) {
				vector &= 0xF1;
				if (chan == sio)
					vector |= 1 << 3;
				if (chan->intbits & INT_RX)
					vector |= 4;
				else if (chan->intbits & INT_ERR)
					vector |= 2;
			}
			if (trace & TRACE_SIO)
				fprintf(stderr, "SIO2 interrupt %02X\n", vector);
			chan->vector = vector;
			recalc_interrupts();
		}
	}
}

static void sio2_reti(struct z80_sio_chan *chan)
{
	/* Recalculate the pending state and vectors */
	/* FIXME: what really goes here */
	sio->irq = 0;
	recalc_interrupts();
}

static int sio2_check_im2(struct z80_sio_chan *chan)
{
	/* See if we have an IRQ pending and if so deliver it and return 1 */
	if (chan->irq) {
		/* FIXME: quick fix for now but the vector calculation should all be
		   done here it seems */
		if (sio[1].wr[1] & 0x04)
			chan->vector += (sio[1].wr[2] & 0xF1);
		else
			chan->vector += sio[1].wr[2];
		if (trace & (TRACE_IRQ|TRACE_SIO))
			fprintf(stderr, "New live interrupt pending is SIO (%d:%02X).\n",
				(int)(chan - sio), chan->vector);
		if (chan == sio)
			live_irq = IRQ_SIOA;
		else
			live_irq = IRQ_SIOB;
		Z80INT(&cpu_z80, chan->vector);
		return 1;
	}
	return 0;
}


/*
 *	The SIO replaces the last character in the FIFO on an
 *	overrun.
 */
static void sio2_queue(struct z80_sio_chan *chan, uint8_t c)
{
	if (trace & TRACE_SIO)
		fprintf(stderr, "SIO %d queue %d: ",
			(int) (chan - sio), c);
	/* Receive disabled */
	if (!(chan->wr[3] & 1)) {
		fprintf(stderr, "RX disabled.\n");
		return;
	}
	/* Overrun */
	if (chan->dptr == 2) {
		if (trace & TRACE_SIO)
			fprintf(stderr, "Overrun.\n");
		chan->data[2] = c;
		chan->rr[1] |= 0x20;	/* Overrun flagged */
		/* What are the rules for overrun delivery FIXME */
		sio2_raise_int(chan, INT_ERR);
	} else {
		/* FIFO add */
		if (trace & TRACE_SIO)
			fprintf(stderr, "Queued %d (mode %d)\n",
				chan->dptr, chan->wr[1] & 0x18);
		chan->data[chan->dptr++] = c;
		chan->rr[0] |= 1;
		switch (chan->wr[1] & 0x18) {
		case 0x00:
			break;
		case 0x08:
			if (chan->dptr == 1)
				sio2_raise_int(chan, INT_RX);
			break;
		case 0x10:
		case 0x18:
			sio2_raise_int(chan, INT_RX);
			break;
		}
	}
	/* Need to deal with interrupt results */
}

static void sio2_channel_timer(struct z80_sio_chan *chan, uint8_t ab)
{
	if (ab == 0) {
		int c = check_chario();
		if (c & 1)
			sio2_queue(chan, next_char());
		if (c & 2) {
			if (!(chan->rr[0] & 0x04)) {
				chan->rr[0] |= 0x04;
				if (chan->wr[1] & 0x02)
					sio2_raise_int(chan, INT_TX);
			}
		}
	} else {
		if (!(chan->rr[0] & 0x04)) {
			chan->rr[0] |= 0x04;
			if (chan->wr[1] & 0x02)
				sio2_raise_int(chan, INT_TX);
		}
	}
}

static void sio2_timer(void)
{
	sio2_channel_timer(sio, 0);
	sio2_channel_timer(sio + 1, 1);
}

static void sio2_channel_reset(struct z80_sio_chan *chan)
{
	chan->rr[0] = 0x2C;
	chan->rr[1] = 0x01;
	chan->rr[2] = 0;
	sio2_clear_int(chan, INT_RX | INT_TX | INT_ERR);
}

static void sio_reset(void)
{
	sio2_channel_reset(sio);
	sio2_channel_reset(sio + 1);
}

static uint8_t sio2_read(uint16_t addr)
{
	struct z80_sio_chan *chan = (addr & 2) ? sio + 1 : sio;
	if (addr & 1) {
		/* Control */
		uint8_t r = chan->wr[0] & 007;
		chan->wr[0] &= ~007;

		chan->rr[0] &= ~2;
		if (chan == sio && (sio[0].intbits | sio[1].intbits))
			chan->rr[0] |= 2;

		if (trace & TRACE_SIO)
			fprintf(stderr, "sio%c read reg %d = ",
				(addr & 1) ? 'b' : 'a', r);
		switch (r) {
		case 0:
		case 1:
			if (trace & TRACE_SIO)
				fprintf(stderr, "%02X\n", chan->rr[r]);
			return chan->rr[r];
		case 2:
			if (chan != sio) {
				if (trace & TRACE_SIO)
					fprintf(stderr, "%02X\n",
						chan->rr[2]);
				return chan->rr[2];
			}
		case 3:
			/* What does the hw report ?? */
			fprintf(stderr, "INVALID(0xFF)\n");
			return 0xFF;
		}
	} else {
		/* FIXME: irq handling */
		uint8_t c = chan->data[0];
		chan->data[0] = chan->data[1];
		chan->data[1] = chan->data[2];
		if (chan->dptr)
			chan->dptr--;
		if (chan->dptr == 0)
			chan->rr[0] &= 0xFE;	/* Clear RX pending */
		sio2_clear_int(chan, INT_RX);
		chan->rr[0] &= 0x3F;
		chan->rr[1] &= 0x3F;
		if (trace & TRACE_SIO)
			fprintf(stderr, "sio%c read data %d\n",
				(addr & 1) ? 'b' : 'a', c);
		if (chan->dptr && (chan->wr[1] & 0x10))
			sio2_raise_int(chan, INT_RX);
		return c;
	}
	return 0xFF;
}

static void sio2_write(uint16_t addr, uint8_t val)
{
	struct z80_sio_chan *chan = (addr & 2) ? sio + 1 : sio;
	uint8_t r;
	if (addr & 1) {
		if (trace & TRACE_SIO)
			fprintf(stderr,
				"sio%c write reg %d with %02X\n",
				(addr & 1) ? 'b' : 'a',
				chan->wr[0] & 7, val);
		switch (chan->wr[0] & 007) {
		case 0:
			chan->wr[0] = val;
			/* FIXME: CRC reset bits ? */
			switch (val & 070) {
			case 000:	/* NULL */
				break;
			case 010:	/* Send Abort SDLC */
				/* SDLC specific no-op for async */
				break;
			case 020:	/* Reset external/status interrupts */
				sio2_clear_int(chan, INT_ERR);
				chan->rr[1] &= 0xCF;	/* Clear status bits on rr0 */
				if (trace & TRACE_SIO)
					fprintf(stderr,
						"[extint reset]\n");
				break;
			case 030:	/* Channel reset */
				if (trace & TRACE_SIO)
					fprintf(stderr,
						"[channel reset]\n");
				sio2_channel_reset(chan);
				break;
			case 040:	/* Enable interrupt on next rx */
				chan->rxint = 1;
				break;
			case 050:	/* Reset transmitter interrupt pending */
				chan->txint = 0;
				break;
			case 060:	/* Reset the error latches */
				chan->rr[1] &= 0x8F;
				break;
			case 070:	/* Return from interrupt (channel A) */
				if (chan == sio) {
					sio->irq = 0;
					sio->rr[1] &= ~0x02;
					sio2_clear_int(sio,
						       INT_RX |
						       INT_TX | INT_ERR);
					sio2_clear_int(sio + 1,
						       INT_RX |
						       INT_TX | INT_ERR);
				}
				break;
			}
			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			r = chan->wr[0] & 7;
			if (trace & TRACE_SIO)
				fprintf(stderr, "sio%c: wrote r%d to %02X\n",
					(addr & 1) ? 'b' : 'a', r, val);
			chan->wr[r] = val;
			if (chan != sio && r == 2)
				chan->rr[2] = val;
			chan->wr[0] &= ~007;
			break;
		}
		/* Control */
	} else {
		/* Strictly we should emulate this as two bytes, one going out and
		   the visible queue - FIXME */
		/* FIXME: irq handling */
		chan->rr[0] &= ~(1 << 2);	/* Transmit buffer no longer empty */
		chan->txint = 1;
		/* Should check chan->wr[5] & 8 */
		sio2_clear_int(chan, INT_TX);
		if (trace & TRACE_SIO)
			fprintf(stderr, "sio%c write data %d\n",
				(addr & 1) ? 'b' : 'a', val);
		write(1, &val, 1);
	}
}

/*
 *	Z80 CTC
 */

struct z80_ctc {
	uint16_t count;
	uint16_t reload;
	uint8_t vector;
	uint8_t ctrl;
#define CTC_IRQ		0x80
#define CTC_COUNTER	0x40
#define CTC_PRESCALER	0x20
#define CTC_RISING	0x10
#define CTC_PULSE	0x08
#define CTC_TCONST	0x04
#define CTC_RESET	0x02
#define CTC_CONTROL	0x01
};

#define CTC_STOPPED(c)	(((c)->ctrl & (CTC_TCONST|CTC_RESET)) == (CTC_TCONST|CTC_RESET))

struct z80_ctc ctc[4];
uint8_t ctc_irqmask;

static void ctc_reset(struct z80_ctc *c)
{
	c->vector = 0;
	c->ctrl = CTC_RESET;
}

static void ctc_init(void)
{
	ctc_reset(ctc);
	ctc_reset(ctc + 1);
	ctc_reset(ctc + 2);
	ctc_reset(ctc + 3);
}

static void ctc_interrupt(struct z80_ctc *c)
{
	int i = c - ctc;
	if (c->ctrl & CTC_IRQ) {
		if (!(ctc_irqmask & (1 << i))) {
			ctc_irqmask |= 1 << i;
			recalc_interrupts();
			if (trace & TRACE_CTC)
				fprintf(stderr, "CTC %d wants to interrupt.\n", i);
		}
	}
}

static void ctc_reti(int ctcnum)
{
	ctc_irqmask &= ~(1 << ctcnum);
	if (trace & TRACE_IRQ)
		fprintf(stderr, "Acked interrupt from CTC %d.\n", ctcnum);
}

/* After a RETI or when idle compute the status of the interrupt line and
   if we are head of the chain this time then raise our interrupt */

static int ctc_check_im2(void)
{
	if (ctc_irqmask) {
		int i;
		for (i = 0; i < 4; i++) {	/* FIXME: correct order ? */
			if (ctc_irqmask & (1 << i)) {
				uint8_t vector = ctc[0].vector & 0xF8;
				vector += 2 * i;
				if (trace & TRACE_IRQ)
					fprintf(stderr, "New live interrupt is from CTC %d vector %x.\n", i, vector);
				live_irq = IRQ_CTC + i;
				Z80INT(&cpu_z80, vector);
				return 1;
			}
		}
	}
	return 0;
}

/* Model the chains between the CTC devices */
static void ctc_receive_pulse(int i);

static void ctc_pulse(int i)
{
}

/* We don't worry about edge directions just a logical pulse model */
static void ctc_receive_pulse(int i)
{
	struct z80_ctc *c = ctc + i;
	if (c->ctrl & CTC_COUNTER) {
		if (CTC_STOPPED(c))
			return;
		if (c->count >= 0x0100)
			c->count -= 0x100;	/* No scaling on pulses */
		if ((c->count & 0xFF00) == 0) {
			ctc_interrupt(c);
			ctc_pulse(i);
			c->count = c->reload << 8;
		}
	} else {
		if (c->ctrl & CTC_PULSE)
			c->ctrl &= ~CTC_PULSE;
	}
}

/* Model counters */
static void ctc_tick(unsigned int clocks)
{
	struct z80_ctc *c = ctc;
	int i;
	int n;
	int decby;

	for (i = 0; i < 4; i++, c++) {
		/* Waiting a value */
		if (CTC_STOPPED(c))
			continue;
		/* Pulse trigger mode */
		if (c->ctrl & CTC_COUNTER)
			continue;
		/* 256x downscaled */
		decby = clocks;
		/* 16x not 256x downscale - so increase by 16x */
		if (!(c->ctrl & CTC_PRESCALER))
			decby <<= 4;
		/* Now iterate over the events. We need to deal with wraps
		   because we might have something counters chained */
		n = c->count - decby;
		while (n < 0) {
			ctc_interrupt(c);
			ctc_pulse(i);
			if (c->reload == 0)
				n += 256 << 8;
			else
				n += c->reload << 8;
		}
		c->count = n;
	}
}

static void ctc_write(uint8_t channel, uint8_t val)
{
	struct z80_ctc *c = ctc + channel;
	if (c->ctrl & CTC_TCONST) {
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d constant loaded with %02X\n", channel, val);
		c->reload = val;
		if ((c->ctrl & (CTC_TCONST|CTC_RESET)) == (CTC_TCONST|CTC_RESET)) {
			c->count = (c->reload - 1) << 8;
			if (trace & TRACE_CTC)
				fprintf(stderr, "CTC %d constant reloaded with %02X\n", channel, val);
		}
		c->ctrl &= ~CTC_TCONST|CTC_RESET;
	} else if (val & CTC_CONTROL) {
		/* We don't yet model the weirdness around edge wanted
		   toggling and clock starts */
		/* Check rule on resets */
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d control loaded with %02X\n", channel, val);
		c->ctrl = val;
		if ((c->ctrl & (CTC_TCONST|CTC_RESET)) == CTC_RESET) {
			c->count = (c->reload - 1) << 8;
			if (trace & TRACE_CTC)
				fprintf(stderr, "CTC %d constant reloaded with %02X\n", channel, val);
		}
		/* Undocumented */
		if (!(c->ctrl & CTC_IRQ) && (ctc_irqmask & (1 << channel))) {
			ctc_irqmask &= ~(1 << channel);
			if (ctc_irqmask == 0) {
				if (trace & TRACE_IRQ)
					fprintf(stderr, "CTC %d irq reset.\n", channel);
				recalc_interrupts();
			}
		}
	} else {
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d vector loaded with %02X\n", channel, val);
		c->vector = val;
	}
}

static uint8_t ctc_read(uint8_t channel)
{
	uint8_t val = ctc[channel].count >> 8;
	if (trace & TRACE_CTC)
		fprintf(stderr, "CTC %d reads %02x\n", channel, val);
	return val;
}

static uint8_t wd1772_read(uint8_t addr)
{
	switch(addr & 0x07) {
	case 0x04:	/* read status */
		return wd17xx_status(wd);
	case 0x05:	/* read track */
		return wd17xx_read_track(wd);
	case 0x06:	/* read sector */
		return wd17xx_read_sector(wd);
	case 0x07:	/* read data */
		return wd17xx_read_data(wd);
	}
	return 0xFF;
}

static void wd1772_write(uint8_t addr, uint8_t val)
{
	switch(addr & 0x0F) {
	case 0x00:	/* write command */
		wd17xx_command(wd, val);
		return;
	case 0x01:	/* write track */
		wd17xx_write_track(wd, val);
		return;
	case 0x02:	/* write sector */
		wd17xx_write_sector(wd, val);
		return;
	case 0x03:	/* write data */
		wd17xx_write_data(wd, val);
		return;
	}
}

static void wd1772_update_bcr(uint8_t bcr)
{
	static uint8_t old_bcr;
	uint8_t delta = bcr ^ old_bcr;
	old_bcr = bcr;

	if (delta & 0x0F) {
		/* Set drive */
		switch(bcr & 0x0F) {
		case 1:
			wd17xx_set_drive(wd, 0);
			break;
		case 2:
			wd17xx_set_drive(wd, 1);
			break;
		case 4:
			wd17xx_set_drive(wd, 2);
			break;
		case 8:
			wd17xx_set_drive(wd, 3);
			break;
		default:
			/* Non valid values not emulated */
		case 0:
			wd17xx_no_drive(wd);
			break;
		}
	}
	if (delta & 0x10)
		wd17xx_set_side(wd, (bcr & 0x10) ? 1 : 0);
	/* 0x20 sets the density (not emulated)
	   0x80 sets the clock (not emulated) */
}

static uint8_t scsi_read(uint8_t addr)
{
	if (ncr) {
		fprintf(stderr, "[%04X]", cpu_z80.PC);
		if (addr == 0x29)
			return idport;
		if (addr > 0x29)
			return 0xFF;
		if (addr <= 0x28)
			return ncr5380_read(ncr, addr);
	}
	return 0xFF;
}

static void scsi_write(uint8_t addr, uint8_t val)
{
	if (addr > 0x28 || ncr == NULL)
		return;
	fprintf(stderr, "[%04X]", cpu_z80.PC);
	ncr5380_write(ncr, addr, val);
}

static void pp_out(uint8_t val)
{
}

static void pp_strobe(unsigned n)
{
}


/*
 *	There is a lot of partial decode NMOS here
 */
static uint8_t io_read(int unused, uint16_t addr)
{
	uint8_t r = 0xFF;
	addr &= 0xFF;
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if ((addr & 0xF0) == 0x20)
		r = scsi_read(addr);
	else switch(addr & 0xC0) {
		case 0x40:
			r = ctc_read(addr >> 4);
			break;
		case 0x80:
			r = sio2_read(addr >> 2);
			break;
		case 0xC0:
			r = wd1772_read(addr);
			break;
	}
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return r;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	addr &= 0xFF;
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if ((addr & 0xF8) == 0x00) {
		switch(addr & 3) {
		case 0x00:
			bcr = val;
			wd1772_update_bcr(val);
			break;
		case 0x01:
			pp_out(val);
			break;
		case 0x02:
			pp_strobe(1);
			break;
		case 0x03:
			pp_strobe(0);
		}
		return;
	}
	if ((addr & 0xF0) == 0x20) {
		scsi_write(addr, val);
		return;
	}
	switch (addr & 0xC0) {
	case 0x40:
		ctc_write(addr >> 4, val);
		break;
	case 0x80:
		sio2_write((addr >> 2), val);
		break;
	case 0xC0:
		wd1772_write(addr, val);
		break;
	}
	if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
}

static void reti_event(void)
{
	switch(live_irq) {
	case IRQ_SIOA:
		sio2_reti(sio);
		break;
	case IRQ_SIOB:
		sio2_reti(sio + 1);
		break;
	case IRQ_CTC:
	case IRQ_CTC + 1:
	case IRQ_CTC + 2:
	case IRQ_CTC + 3:
		ctc_reti(live_irq - IRQ_CTC);
		break;
	}
	live_irq = 0;
	if (!sio2_check_im2(sio) &&
		!sio2_check_im2(sio + 1))
			ctc_check_im2();
}


static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	exit(1);
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "littleboard: [-f] [-s path] [-r path] [-d debug] [-A disk] [-B disk]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	char *rompath = "ampro.rom";
	char *diskpath = NULL;
	static char *fdpath[4] = { NULL, NULL, NULL, NULL };

	while ((opt = getopt(argc, argv, "d:r:fs:A:B:C:D:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 's':
			diskpath = optarg;
			break;
		case 'A':
		case 'B':
		case 'C':
		case 'D':
			fdpath[opt - 'A'] = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	l = read(fd, rom, 32768);
	switch(l) {
	case 0x1000:
	case 0x2000:
	case 0x4000:
	case 0x8000:
		break;
	default:
		fprintf(stderr, "littleboard: invalid EPROM size.\n");
		exit(1);
	}
	close(fd);
	eprom_mask = l - 1;

	sio_reset();
	ctc_init();

	wd = wd17xx_create();
	/* Not clear what we do here - probably we need to add support
	   for proper disk metadata formats as the disks came with some
	   peculiar setings - like sectors numbering from 16 and 1K or 512
	   byte sector options */
	if (fdpath[0])
		wd17xx_attach(wd, 0, fdpath[0], 2, 80, 10, 512);
	if (fdpath[1])
		wd17xx_attach(wd, 0, fdpath[1], 2, 80, 10, 512);
	if (fdpath[2])
		wd17xx_attach(wd, 0, fdpath[2], 2, 80, 10, 512);
	if (fdpath[3])
		wd17xx_attach(wd, 0, fdpath[3], 2, 80, 10, 512);
	wd17xx_trace(wd, trace & TRACE_FDC);
		
	if (diskpath) {
		sasi = sasi_bus_create();
		sasi_disk_attach(sasi, 0, diskpath, 512);
		sasi_bus_reset(sasi);
		ncr = ncr5380_create(sasi);
		ncr5380_trace(ncr, trace & TRACE_SCSI);
	}

	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L;

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, cleanup);
		signal(SIGQUIT, cleanup);
		signal(SIGPIPE, cleanup);
		term.c_lflag &= ~(ICANON | ECHO);
		term.c_cc[VMIN] = 0;
		term.c_cc[VTIME] = 1;
		term.c_cc[VINTR] = 0;
		term.c_cc[VSUSP] = 0;
		term.c_cc[VSTOP] = 0;
		tcsetattr(0, TCSADRAIN, &term);
	}

	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;
	cpu_z80.trace = z80_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		int l;
		unsigned n;
		for (l = 0; l < 10; l++) {
			int i;
			/* 36400 T states */
			for (i = 0; i < 100; i++) {
				Z80ExecuteTStates(&cpu_z80, 364);
				sio2_timer();
				ctc_tick(364);
				for (n = 0; n < 182;n++) {
					ctc_receive_pulse(0);
					ctc_receive_pulse(1);
				}
			}
			if (ncr)
				ncr5380_activity(ncr);
			/* Do 5ms of I/O and delays */
			if (!fast)
				nanosleep(&tc, NULL);
			if (int_recalc) {
				/* If there is no pending Z80 vector IRQ but we think
				   there now might be one we use the same logic as for
				   reti */
				if (!live_irq)
					reti_event();
				/* Clear this after because reti_event may set the
				   flags to indicate there is more happening. We will
				   pick up the next state changes on the reti if so */
				if (!(cpu_z80.IFF1|cpu_z80.IFF2))
					int_recalc = 0;
			}
		}
	}
	exit(0);
}
