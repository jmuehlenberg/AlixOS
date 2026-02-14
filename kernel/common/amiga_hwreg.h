/* SPDX-License-Identifier: GPL-2.0 */
/*
 * amiga_hwreg.h - Amiga hardware register definitions for AlixOS
 *
 * Memory-mapped register addresses for Amiga custom chips, CIA chips,
 * and other hardware accessible via the PiStorm32-lite bus interface.
 *
 * Reference: Amiga Hardware Reference Manual (3rd Edition)
 */

#ifndef _AMIGA_HWREG_H
#define _AMIGA_HWREG_H

/* ========================================================================
 * CIA-A (Complex Interface Adapter A) - Active on ODD addresses
 * Base: $BFE001, stride 0x100
 * ======================================================================== */

#define AMIGA_CIAA_PRA      0xBFE001  /* Port A: /FIR1,/FIR0,/RDY,/TK0,/WPRO,/CHNG,/LED,OVL */
#define AMIGA_CIAA_PRB      0xBFE101  /* Port B: Parallel port data */
#define AMIGA_CIAA_DDRA     0xBFE201  /* Data Direction Register A */
#define AMIGA_CIAA_DDRB     0xBFE301  /* Data Direction Register B */
#define AMIGA_CIAA_TALO     0xBFE401  /* Timer A Low byte */
#define AMIGA_CIAA_TAHI     0xBFE501  /* Timer A High byte */
#define AMIGA_CIAA_TBLO     0xBFE601  /* Timer B Low byte */
#define AMIGA_CIAA_TBHI     0xBFE701  /* Timer B High byte */
#define AMIGA_CIAA_TODLO    0xBFE801  /* TOD counter bits 7-0 */
#define AMIGA_CIAA_TODMID   0xBFE901  /* TOD counter bits 15-8 */
#define AMIGA_CIAA_TODHI    0xBFEA01  /* TOD counter bits 23-16 */
#define AMIGA_CIAA_SDR      0xBFEC01  /* Serial Data Register (KEYBOARD) */
#define AMIGA_CIAA_ICR      0xBFED01  /* Interrupt Control Register */
#define AMIGA_CIAA_CRA      0xBFEE01  /* Control Register A */
#define AMIGA_CIAA_CRB      0xBFEF01  /* Control Register B */

/* CIA-A PRA bit definitions */
#define CIAA_PRA_OVL        (1 << 0)  /* Memory overlay */
#define CIAA_PRA_LED        (1 << 1)  /* Power LED (active low) */
#define CIAA_PRA_CHNG       (1 << 2)  /* Disk change (active low) */
#define CIAA_PRA_WPRO       (1 << 3)  /* Write protect (active low) */
#define CIAA_PRA_TK0        (1 << 4)  /* Track 0 (active low) */
#define CIAA_PRA_RDY        (1 << 5)  /* Disk ready (active low) */
#define CIAA_PRA_FIR0       (1 << 6)  /* Fire button 0 / LMB (active low) */
#define CIAA_PRA_FIR1       (1 << 7)  /* Fire button 1 / Joy fire (active low) */

/* CIA-A ICR bit definitions */
#define CIAA_ICR_TA         (1 << 0)  /* Timer A underflow */
#define CIAA_ICR_TB         (1 << 1)  /* Timer B underflow */
#define CIAA_ICR_ALRM       (1 << 2)  /* TOD alarm */
#define CIAA_ICR_SP         (1 << 3)  /* Serial port (keyboard byte ready) */
#define CIAA_ICR_FLG        (1 << 4)  /* FLAG pin (accent index pulse) */
#define CIAA_ICR_SETCLR     (1 << 7)  /* Set/Clear control (write only) */

/* CIA-A CRA bit definitions */
#define CIAA_CRA_START      (1 << 0)  /* Start Timer A */
#define CIAA_CRA_PBON       (1 << 1)  /* PB6 output enable */
#define CIAA_CRA_OUTMODE    (1 << 2)  /* Toggle/pulse mode */
#define CIAA_CRA_RUNMODE    (1 << 3)  /* One-shot/continuous */
#define CIAA_CRA_LOAD       (1 << 4)  /* Force load */
#define CIAA_CRA_INMODE     (1 << 5)  /* Count 02 pulses / CNT transitions */
#define CIAA_CRA_SPMODE     (1 << 6)  /* Serial port direction: 0=input, 1=output */
#define CIAA_CRA_TODIN      (1 << 7)  /* 50/60 Hz select */

/* ========================================================================
 * CIA-B (Complex Interface Adapter B) - Active on EVEN addresses
 * Base: $BFD000, stride 0x100
 * ======================================================================== */

#define AMIGA_CIAB_PRA      0xBFD000  /* Port A: /DTR,/RTS,/CD,/CTS,/DSR,SEL,POUT,BUSY */
#define AMIGA_CIAB_PRB      0xBFD100  /* Port B: Floppy motor/select/step/direction/side */
#define AMIGA_CIAB_DDRA     0xBFD200  /* Data Direction Register A */
#define AMIGA_CIAB_DDRB     0xBFD300  /* Data Direction Register B */
#define AMIGA_CIAB_TALO     0xBFD400  /* Timer A Low byte */
#define AMIGA_CIAB_TAHI     0xBFD500  /* Timer A High byte */
#define AMIGA_CIAB_TBLO     0xBFD600  /* Timer B Low byte */
#define AMIGA_CIAB_TBHI     0xBFD700  /* Timer B High byte */
#define AMIGA_CIAB_TODLO    0xBFD800  /* TOD counter bits 7-0 */
#define AMIGA_CIAB_TODMID   0xBFD900  /* TOD counter bits 15-8 */
#define AMIGA_CIAB_TODHI    0xBFDA00  /* TOD counter bits 23-16 */
#define AMIGA_CIAB_SDR      0xBFDC00  /* Serial Data Register */
#define AMIGA_CIAB_ICR      0xBFDD00  /* Interrupt Control Register */
#define AMIGA_CIAB_CRA      0xBFDE00  /* Control Register A */
#define AMIGA_CIAB_CRB      0xBFDF00  /* Control Register B */

/* ========================================================================
 * Custom Chip Registers - Base: $DFF000
 * ======================================================================== */

/* Joystick / Mouse registers */
#define AMIGA_JOY0DAT       0xDFF00A  /* Joystick/mouse port 1 data (16-bit) */
#define AMIGA_JOY1DAT       0xDFF00C  /* Joystick/mouse port 2 data (16-bit) */
#define AMIGA_JOYTEST       0xDFF036  /* Joystick test register (write) */

/* Potentiometer / button registers */
#define AMIGA_POTGOR        0xDFF016  /* Pot port data read (16-bit, read-only) */
#define AMIGA_POTGO         0xDFF034  /* Pot port data write + start (16-bit, write-only) */

/* POTGOR / POTGO bit definitions */
#define POTGO_OUTLX         (1 << 8)   /* Port 1 left pot output enable */
#define POTGO_DATLX         (1 << 9)   /* Port 1 left pot data */
#define POTGO_OUTLY         (1 << 10)  /* Port 1 right pot output enable */
#define POTGO_DATLY         (1 << 11)  /* Port 1 right pot data (RMB) */
#define POTGO_OUTRX         (1 << 12)  /* Port 2 left pot output enable */
#define POTGO_DATRX         (1 << 13)  /* Port 2 left pot data */
#define POTGO_OUTRY         (1 << 14)  /* Port 2 right pot output enable */
#define POTGO_DATRY         (1 << 15)  /* Port 2 right pot data (Joy fire 2) */

/* Interrupt registers */
#define AMIGA_INTENAR       0xDFF01C  /* Interrupt enable read (16-bit) */
#define AMIGA_INTREQR       0xDFF01E  /* Interrupt request read (16-bit) */
#define AMIGA_INTENA        0xDFF09A  /* Interrupt enable write (16-bit) */
#define AMIGA_INTREQ        0xDFF09C  /* Interrupt request write (16-bit) */

/* Serial port registers (Paula) */
#define AMIGA_SERDATR       0xDFF018  /* Serial port data + status read (16-bit) */
#define AMIGA_SERDAT        0xDFF030  /* Serial port data write (16-bit) */
#define AMIGA_SERPER        0xDFF032  /* Serial port period/baud (16-bit) */

/* SERDATR bit definitions */
#define SERDATR_OVRUN       (1 << 15)  /* Overrun error */
#define SERDATR_RBF         (1 << 14)  /* Receive buffer full */
#define SERDATR_TBE         (1 << 13)  /* Transmit buffer empty */
#define SERDATR_TSRE        (1 << 12)  /* Transmit shift register empty */
#define SERDATR_RXD         (1 << 11)  /* RXD pin state (active low) */
/* Bits 8-0: received data (bit 8 = stop bit if 9-bit mode) */

/* SERPER bit definitions */
#define SERPER_LONG         (1 << 15)  /* 9-bit mode (0=8-bit, 1=9-bit) */
/* Bits 14-0: baud rate divisor. Period = (divisor+1) * 0.2794 µs (NTSC)
 *   Common values:
 *     9600 baud:  372 (0x174)
 *     19200 baud: 185 (0x0B9)
 *     38400 baud: 92  (0x05C)
 *     57600 baud: 61  (0x03D)
 *     115200 baud: 30 (0x01E)
 *   Formula (PAL):  divisor = (3546895 / baud) - 1
 *   Formula (NTSC): divisor = (3579545 / baud) - 1
 */

/* CIA-B PRA bit definitions (serial port handshaking) */
#define CIAB_PRA_BUSY       (1 << 0)  /* Parallel: BUSY (active low) */
#define CIAB_PRA_POUT       (1 << 1)  /* Parallel: Paper Out */
#define CIAB_PRA_SEL        (1 << 2)  /* Parallel: Select */
#define CIAB_PRA_DSR        (1 << 3)  /* Serial: Data Set Ready (active low) */
#define CIAB_PRA_CTS        (1 << 4)  /* Serial: Clear To Send (active low) */
#define CIAB_PRA_CD         (1 << 5)  /* Serial: Carrier Detect (active low) */
#define CIAB_PRA_RTS        (1 << 6)  /* Serial: Request To Send (active low) */
#define CIAB_PRA_DTR        (1 << 7)  /* Serial: Data Terminal Ready (active low) */

/* Interrupt bit definitions (for INTENA/INTREQ) */
#define AMIGA_INTF_TBE      (1 << 0)   /* Serial transmit buffer empty */
#define AMIGA_INTF_RBF      (1 << 11)  /* Serial receive buffer full */

/* DMA control */
#define AMIGA_DMACON        0xDFF096  /* DMA control write */
#define AMIGA_DMACONR       0xDFF002  /* DMA control read */

/* DMACON bit definitions */
#define AMIGA_DMAF_SETCLR   (1 << 15)  /* Set/clear control (write) */
#define AMIGA_DMAF_DMAEN    (1 << 9)   /* DMA master enable */
#define AMIGA_DMAF_DSKEN    (1 << 4)   /* Disk DMA enable */

/* Video position (useful for VBlank detection) */
#define AMIGA_VPOSR         0xDFF004  /* Vertical beam position (high) */
#define AMIGA_VHPOSR        0xDFF006  /* Vertical/horizontal beam position */

/* ========================================================================
 * Floppy Disk Controller (Agnus DMA + CIA-B PRB)
 * ======================================================================== */

/* Disk DMA registers */
#define AMIGA_DSKPTH        0xDFF020  /* Disk DMA pointer high word (write) */
#define AMIGA_DSKPTL        0xDFF022  /* Disk DMA pointer low word (write) */
#define AMIGA_DSKLEN        0xDFF024  /* Disk DMA length + control (write) */
#define AMIGA_DSKDAT        0xDFF026  /* Disk DMA data (write) */
#define AMIGA_DSKBYTR       0xDFF01A  /* Disk byte + status (read) */
#define AMIGA_DSKSYNC       0xDFF07E  /* Disk sync pattern (write) */

/* Audio/Disk control */
#define AMIGA_ADKCON        0xDFF09E  /* Audio/disk control (write) */
#define AMIGA_ADKCONR       0xDFF010  /* Audio/disk control (read) */

/* DSKLEN bit definitions */
#define DSKLEN_DMAEN        (1 << 15)  /* DMA enable (write twice to start) */
#define DSKLEN_WRITE        (1 << 14)  /* Write mode (0=read, 1=write) */
/* Bits 13-0: transfer length in words */

/* DSKBYTR bit definitions */
#define DSKBYTR_DSKBYT      (1 << 15)  /* Byte available */
#define DSKBYTR_DMAON       (1 << 14)  /* DMA active */
#define DSKBYTR_DISKWRITE   (1 << 13)  /* Write mode active */
#define DSKBYTR_WORDEQUAL   (1 << 12)  /* Sync word matched */
/* Bits 7-0: current data byte */

/* ADKCON bit definitions */
#define ADKCON_SETCLR       (1 << 15)  /* Set/clear control */
#define ADKCON_PRECOMP1     (1 << 14)  /* Precompensation bit 1 */
#define ADKCON_PRECOMP0     (1 << 13)  /* Precompensation bit 0 */
#define ADKCON_MFMPREC      (1 << 12)  /* MFM precomp enable */
#define ADKCON_WORDSYNC     (1 << 10)  /* Enable disk word sync */
#define ADKCON_MSBSYNC      (1 << 9)   /* MSB sync (0 for Amiga format) */
#define ADKCON_FAST         (1 << 8)   /* Fast disk (2µs mode) */

/* Interrupt bits (additional) */
#define AMIGA_INTF_SETCLR   (1 << 15)  /* Set/clear control */
#define AMIGA_INTF_DSKBLK   (1 << 1)   /* Disk block finished */
#define AMIGA_INTF_DSKSYN   (1 << 12)  /* Disk sync word detected */

/* CIA-B PRB bit definitions (floppy drive control) */
#define CIAB_PRB_STEP       (1 << 0)   /* /STEP pulse (active low, falling edge) */
#define CIAB_PRB_DIR        (1 << 1)   /* Direction: 0=inward, 1=outward (track 0) */
#define CIAB_PRB_SIDE       (1 << 2)   /* /SIDE: 0=upper(1), 1=lower(0) */
#define CIAB_PRB_SEL0       (1 << 3)   /* /SEL0 internal drive (active low) */
#define CIAB_PRB_SEL1       (1 << 4)   /* /SEL1 external drive 1 */
#define CIAB_PRB_SEL2       (1 << 5)   /* /SEL2 external drive 2 */
#define CIAB_PRB_SEL3       (1 << 6)   /* /SEL3 external drive 3 */
#define CIAB_PRB_MTR        (1 << 7)   /* /MTR motor (active low) */

/* ========================================================================
 * Gayle (IDE/PCMCIA controller, A1200)
 * ======================================================================== */

#define AMIGA_GAYLE_BASE    0xD80000
#define AMIGA_GAYLE_SIZE    0x070000

/* ========================================================================
 * RTC — Ricoh RP5C01A (Battery-backed Real-Time Clock)
 * Base: $DC0000, register stride: 4 bytes
 * Data width: 4 bits (lower nibble only)
 * ======================================================================== */

#define AMIGA_RTC_BASE      0xDC0000
#define AMIGA_RTC_SIZE      0x010000
#define AMIGA_RTC_REG(n)    (AMIGA_RTC_BASE + ((n) << 2))

/* Bank 0 registers (time — active when CTRL bits 1:0 = 00) */
#define RTC_SEC1            0   /* Seconds ones (0-9) */
#define RTC_SEC10           1   /* Seconds tens (0-5) */
#define RTC_MIN1            2   /* Minutes ones (0-9) */
#define RTC_MIN10           3   /* Minutes tens (0-5) */
#define RTC_HR1             4   /* Hours ones (0-9) */
#define RTC_HR10            5   /* Hours tens (0-2) */
#define RTC_DOW             6   /* Day of week (0-6, 0=Sunday) */
#define RTC_DAY1            7   /* Day ones (0-9) */
#define RTC_DAY10           8   /* Day tens (0-3) */
#define RTC_MON1            9   /* Month ones (0-9) */
#define RTC_MON10           10  /* Month tens (0-1) */
#define RTC_YR1             11  /* Year ones (0-9) */
#define RTC_YR10            12  /* Year tens (0-9) */

/* Control registers (same address in all banks) */
#define RTC_CTRL            13  /* Mode/control register */
#define RTC_TEST            14  /* Test register (write 0) */
#define RTC_RESET           15  /* Reset register */

/* RTC_CTRL bit definitions */
#define RTC_CTRL_ALARM      (1 << 3)  /* Alarm enable */
#define RTC_CTRL_TIMER      (1 << 2)  /* Timer enable (clock runs) */
/* Bits 1-0: bank select (0-3) */

#endif /* _AMIGA_HWREG_H */
