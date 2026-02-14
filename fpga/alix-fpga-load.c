/* SPDX-License-Identifier: GPL-2.0 */
/*
 * alix-fpga-load.c - PiStorm32-lite FPGA bitstream loader for AlixOS
 *
 * Loads an Efinix Trion T8 FPGA bitstream via SPI Passive x1 mode
 * using GPIO bitbanging on the Raspberry Pi CM4.
 *
 * Based on the PiStorm project's ps_efinix.c by Claude Schwarz et al.
 *
 * Usage: alix-fpga-load <bitstream.bin>
 *
 * This must run before any kernel modules that access the Amiga bus.
 * Typically invoked as a systemd service at early boot.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>

/* BCM2711 (Pi 4 / CM4) GPIO base */
#define BCM2711_PERI_BASE   0xFE000000
#define GPIO_BASE           (BCM2711_PERI_BASE + 0x200000)
#define GPIO_MAP_SIZE       0x1000

/* GPIO register offsets (as 32-bit word indices) */
#define GPFSEL0             (0x00 / 4)
#define GPFSEL1             (0x04 / 4)
#define GPFSEL2             (0x08 / 4)
#define GPSET0              (0x1C / 4)
#define GPCLR0              (0x28 / 4)
#define GPLEV0              (0x34 / 4)

/* PiStorm32-lite FPGA configuration pins */
#define PIN_CRESET1         6
#define PIN_CRESET2         7
#define PIN_CCK             22   /* Configuration clock */
#define PIN_CSS             24   /* Chip select */
#define PIN_TESTN           17   /* Test pin */

/* Configuration data bus CDI[0:7] */
#define PIN_CDI0            10
#define PIN_CDI1            25
#define PIN_CDI2            9
#define PIN_CDI3            8
#define PIN_CDI4            11
#define PIN_CDI5            1
#define PIN_CDI6            16
#define PIN_CDI7            13

/* Configuration bus control */
#define PIN_CBUS0           14
#define PIN_CBUS1           15
#define PIN_CBUS2           18

/* CDONE pin - FPGA signals configuration complete */
#define PIN_CDONE           23

static volatile unsigned int *gpio_base;

/* ---- GPIO helpers ---------------------------------------------------- */

static void gpio_set_output(int pin)
{
	int reg = pin / 10;
	int shift = (pin % 10) * 3;
	gpio_base[GPFSEL0 + reg] &= ~(7 << shift);
	gpio_base[GPFSEL0 + reg] |= (1 << shift);  /* Output mode */
}

static void gpio_set_input(int pin)
{
	int reg = pin / 10;
	int shift = (pin % 10) * 3;
	gpio_base[GPFSEL0 + reg] &= ~(7 << shift);  /* Input mode = 000 */
}

static inline void gpio_set(int pin)
{
	gpio_base[GPSET0] = 1 << pin;
}

static inline void gpio_clr(int pin)
{
	gpio_base[GPCLR0] = 1 << pin;
}

static inline int gpio_read(int pin)
{
	return (gpio_base[GPLEV0] >> pin) & 1;
}

static void delay_us(unsigned int us)
{
	struct timespec ts;
	ts.tv_sec = us / 1000000;
	ts.tv_nsec = (us % 1000000) * 1000;
	nanosleep(&ts, NULL);
}

static void delay_ms(unsigned int ms)
{
	delay_us(ms * 1000);
}

/* ---- FPGA configuration ---------------------------------------------- */

static void fpga_setup_pins(void)
{
	/* Set configuration pins as outputs */
	gpio_set_output(PIN_CRESET1);
	gpio_set_output(PIN_CRESET2);
	gpio_set_output(PIN_CCK);
	gpio_set_output(PIN_CSS);
	gpio_set_output(PIN_TESTN);

	/* CDI data bus as outputs */
	gpio_set_output(PIN_CDI0);
	gpio_set_output(PIN_CDI1);
	gpio_set_output(PIN_CDI2);
	gpio_set_output(PIN_CDI3);
	gpio_set_output(PIN_CDI4);
	gpio_set_output(PIN_CDI5);
	gpio_set_output(PIN_CDI6);
	gpio_set_output(PIN_CDI7);

	/* CBUS as outputs */
	gpio_set_output(PIN_CBUS0);
	gpio_set_output(PIN_CBUS1);
	gpio_set_output(PIN_CBUS2);

	/* CDONE as input (FPGA signals completion) */
	gpio_set_input(PIN_CDONE);

	/* Initial pin states */
	gpio_set(PIN_CRESET1);
	gpio_set(PIN_CRESET2);
	gpio_clr(PIN_CCK);
	gpio_set(PIN_CSS);
	gpio_set(PIN_TESTN);
}

static void fpga_reset(void)
{
	/* Assert CRESET (active low) */
	gpio_clr(PIN_CRESET1);
	gpio_clr(PIN_CRESET2);
	delay_ms(1);

	/* Deassert CRESET */
	gpio_set(PIN_CRESET1);
	gpio_set(PIN_CRESET2);
	delay_ms(5);
}

/*
 * Send a single byte via SPI Passive x1 mode.
 * Data is clocked on CDI0, bit by bit, MSB first.
 * Clock on CCK pin.
 */
static void fpga_send_byte(unsigned char byte)
{
	int i;

	for (i = 7; i >= 0; i--) {
		/* Set data bit on CDI0 */
		if (byte & (1 << i))
			gpio_set(PIN_CDI0);
		else
			gpio_clr(PIN_CDI0);

		/* Clock rising edge */
		gpio_set(PIN_CCK);

		/* Clock falling edge */
		gpio_clr(PIN_CCK);
	}
}

static int fpga_load_bitstream(const char *filename)
{
	FILE *fp;
	unsigned char *bitstream;
	long filesize;
	size_t i;
	int timeout;

	/* Read bitstream file */
	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Error: Cannot open bitstream file '%s': %s\n",
			filename, strerror(errno));
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	filesize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (filesize <= 0 || filesize > (1024 * 1024)) {
		fprintf(stderr, "Error: Invalid bitstream size: %ld bytes\n", filesize);
		fclose(fp);
		return -1;
	}

	bitstream = malloc(filesize);
	if (!bitstream) {
		fprintf(stderr, "Error: Cannot allocate memory for bitstream\n");
		fclose(fp);
		return -1;
	}

	if (fread(bitstream, 1, filesize, fp) != (size_t)filesize) {
		fprintf(stderr, "Error: Failed to read bitstream file\n");
		free(bitstream);
		fclose(fp);
		return -1;
	}
	fclose(fp);

	printf("AlixOS FPGA Loader: Bitstream '%s' (%ld bytes)\n", filename, filesize);

	/* Step 1: Reset FPGA */
	printf("  Resetting FPGA...\n");
	fpga_reset();

	/* Step 2: Enter SPI Passive configuration mode */
	gpio_clr(PIN_CSS);     /* Assert chip select (active low) */
	gpio_clr(PIN_CCK);     /* Clock idle low */
	delay_us(100);

	/* Step 3: Send bitstream */
	printf("  Loading bitstream...\n");
	for (i = 0; i < (size_t)filesize; i++) {
		fpga_send_byte(bitstream[i]);
	}
	free(bitstream);

	/* Step 4: Send extra clocks for FPGA initialization */
	gpio_clr(PIN_CDI0);
	for (i = 0; i < 100; i++) {
		gpio_set(PIN_CCK);
		gpio_clr(PIN_CCK);
	}

	/* Step 5: Deassert chip select */
	gpio_set(PIN_CSS);
	delay_ms(1);

	/* Step 6: Check CDONE pin */
	timeout = 100;
	while (timeout > 0) {
		if (gpio_read(PIN_CDONE)) {
			printf("  FPGA configuration complete (CDONE=1)\n");
			return 0;
		}
		delay_ms(1);
		timeout--;
	}

	fprintf(stderr, "Error: FPGA configuration timeout (CDONE not asserted)\n");
	return -1;
}

/* ---- GPIO memory mapping --------------------------------------------- */

static int gpio_map(void)
{
	int fd;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "Error: Cannot open /dev/mem (run as root): %s\n",
			strerror(errno));
		return -1;
	}

	gpio_base = mmap(NULL, GPIO_MAP_SIZE, PROT_READ | PROT_WRITE,
			 MAP_SHARED, fd, GPIO_BASE);
	close(fd);

	if (gpio_base == MAP_FAILED) {
		fprintf(stderr, "Error: GPIO mmap failed: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static void gpio_unmap(void)
{
	if (gpio_base && gpio_base != MAP_FAILED)
		munmap((void *)gpio_base, GPIO_MAP_SIZE);
}

/* ---- Main ------------------------------------------------------------ */

int main(int argc, char *argv[])
{
	int ret;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <bitstream.bin>\n", argv[0]);
		fprintf(stderr, "\nLoads PiStorm32-lite FPGA bitstream via SPI bitbang.\n");
		fprintf(stderr, "Must be run as root.\n");
		return 1;
	}

	if (gpio_map() != 0)
		return 1;

	fpga_setup_pins();
	ret = fpga_load_bitstream(argv[1]);

	gpio_unmap();

	if (ret == 0)
		printf("AlixOS FPGA Loader: Success\n");
	else
		fprintf(stderr, "AlixOS FPGA Loader: FAILED\n");

	return ret ? 1 : 0;
}
