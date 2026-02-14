// SPDX-License-Identifier: GPL-2.0
/*
 * floppy — AlixOS Floppy Disk Utility
 *
 * Command-line tool to read Amiga DD floppy disks via the alix_floppy
 * kernel driver (/dev/alix-floppy).
 *
 * Usage:
 *   floppy read  <output.adf>    Read entire disk to ADF file
 *   floppy write <input.adf>     Write ADF file to floppy disk
 *   floppy dump  <output.adf>    Alias for 'read'
 *   floppy status                Show drive status
 *   floppy motor on|off          Control drive motor
 *   floppy seek  <cylinder>      Seek to cylinder (0-79)
 *   floppy info  <file.adf>      Show ADF file information
 *   floppy verify <file.adf>     Read disk and compare with ADF file
 *
 * Examples:
 *   floppy read game.adf
 *   floppy write game.adf
 *   floppy status
 *   floppy info game.adf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>

/* Must match kernel driver definitions */
#define ALIX_FLP_MAGIC      'F'
#define ALIX_FLP_MOTOR_ON   _IO(ALIX_FLP_MAGIC, 1)
#define ALIX_FLP_MOTOR_OFF  _IO(ALIX_FLP_MAGIC, 2)
#define ALIX_FLP_SEEK       _IOW(ALIX_FLP_MAGIC, 3, int)
#define ALIX_FLP_GET_STATUS _IOR(ALIX_FLP_MAGIC, 4, int)

#define FLP_STATUS_TK0      (1 << 0)
#define FLP_STATUS_CHNG     (1 << 1)
#define FLP_STATUS_WPRO     (1 << 2)
#define FLP_STATUS_RDY      (1 << 3)
#define FLP_STATUS_MOTOR    (1 << 4)

#define DEVICE_PATH         "/dev/alix-floppy"
#define ADF_SIZE            901120
#define TRACK_SIZE          5632
#define TOTAL_TRACKS        160
#define CYLINDERS           80
#define SECTORS_PER_TRACK   11
#define BYTES_PER_SECTOR    512

/* ---- Helpers ---------------------------------------------------------- */

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"AlixOS Floppy Disk Utility\n"
		"\n"
		"Usage:\n"
		"  %s read  <output.adf>    Read entire disk to ADF file\n"
		"  %s write <input.adf>     Write ADF file to floppy disk\n"
		"  %s status                Show drive status\n"
		"  %s motor on|off          Control drive motor\n"
		"  %s seek  <cylinder>      Seek to cylinder (0-79)\n"
		"  %s info  <file.adf>      Show ADF file information\n"
		"  %s verify <file.adf>     Read disk and compare with ADF\n",
		prog, prog, prog, prog, prog, prog, prog);
}

static int open_device(void)
{
	int fd = open(DEVICE_PATH, O_RDONLY);

	if (fd < 0) {
		if (errno == ENOENT)
			fprintf(stderr, "Error: %s not found.\n"
				"Is the alix_floppy module loaded?\n"
				"  sudo modprobe alix_floppy\n", DEVICE_PATH);
		else if (errno == EACCES)
			fprintf(stderr, "Error: Permission denied. Try:\n"
				"  sudo %s ...\n", DEVICE_PATH);
		else
			perror(DEVICE_PATH);
	}
	return fd;
}

static void print_progress(int track, int total, time_t start)
{
	int pct = (track * 100) / total;
	time_t elapsed = time(NULL) - start;
	int bar_width = 40;
	int filled = (track * bar_width) / total;
	int i;

	printf("\r  [");
	for (i = 0; i < bar_width; i++)
		putchar(i < filled ? '#' : '.');
	printf("] %3d%%  Track %3d/%d", pct, track, total);

	if (elapsed > 2 && track > 0) {
		int eta = (int)((elapsed * (total - track)) / track);
		printf("  ETA: %d:%02d", eta / 60, eta % 60);
	}

	printf("  ");
	fflush(stdout);
}

/* ---- Commands --------------------------------------------------------- */

static int cmd_read(const char *filename)
{
	int fd, outfd;
	unsigned char buf[TRACK_SIZE];
	ssize_t n;
	int track = 0;
	size_t total_written = 0;
	time_t start;

	fd = open_device();
	if (fd < 0)
		return 1;

	outfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (outfd < 0) {
		perror(filename);
		close(fd);
		return 1;
	}

	printf("Reading Amiga floppy disk to: %s\n", filename);
	printf("  Format: DD (880 KB, 160 tracks, 11 sectors/track)\n\n");

	start = time(NULL);

	while (total_written < ADF_SIZE) {
		print_progress(track, TOTAL_TRACKS, start);

		n = read(fd, buf, TRACK_SIZE);
		if (n < 0) {
			printf("\n");
			if (errno == ETIMEDOUT)
				fprintf(stderr,
					"\nError: Read timeout at track %d.\n"
					"  Check that a disk is inserted and the drive is working.\n",
					track);
			else
				fprintf(stderr, "\nError reading track %d: %s\n",
					track, strerror(errno));
			close(outfd);
			close(fd);
			return 1;
		}

		if (n == 0)
			break;

		if (write(outfd, buf, n) != n) {
			perror("\nWrite error");
			close(outfd);
			close(fd);
			return 1;
		}

		total_written += n;
		track = total_written / TRACK_SIZE;
	}

	print_progress(TOTAL_TRACKS, TOTAL_TRACKS, start);
	printf("\n\n");

	close(outfd);
	close(fd);

	if (total_written == ADF_SIZE) {
		time_t elapsed = time(NULL) - start;

		printf("Success! Written %zu bytes (%d KB) in %ld seconds.\n",
		       total_written, (int)(total_written / 1024), elapsed);
		printf("  File: %s\n", filename);
	} else {
		fprintf(stderr, "Warning: Only %zu of %d bytes read.\n",
			total_written, ADF_SIZE);
		return 1;
	}

	return 0;
}

static int cmd_write(const char *filename)
{
	int fd, infd;
	unsigned char buf[TRACK_SIZE];
	ssize_t n;
	int track = 0;
	size_t total_written = 0;
	struct stat st;
	time_t start;

	/* Verify input file */
	if (stat(filename, &st) < 0) {
		perror(filename);
		return 1;
	}

	if (st.st_size != ADF_SIZE) {
		fprintf(stderr, "Error: %s is not a standard DD ADF file.\n"
			"  Expected: %d bytes (880 KB), got: %lld bytes\n",
			filename, ADF_SIZE, (long long)st.st_size);
		return 1;
	}

	infd = open(filename, O_RDONLY);
	if (infd < 0) {
		perror(filename);
		return 1;
	}

	fd = open(DEVICE_PATH, O_WRONLY);
	if (fd < 0) {
		if (errno == EROFS)
			fprintf(stderr, "Error: Disk is write-protected.\n"
				"  Close the write-protect tab on the disk "
				"and try again.\n");
		else if (errno == ENOENT)
			fprintf(stderr, "Error: %s not found.\n"
				"Is the alix_floppy module loaded?\n"
				"  sudo modprobe alix_floppy\n", DEVICE_PATH);
		else if (errno == EACCES)
			fprintf(stderr, "Error: Permission denied. Try:\n"
				"  sudo floppy write %s\n", filename);
		else
			perror(DEVICE_PATH);
		close(infd);
		return 1;
	}

	printf("Writing ADF to floppy disk: %s\n", filename);
	printf("  Format: DD (880 KB, 160 tracks, 11 sectors/track)\n");
	printf("  WARNING: All data on the disk will be overwritten!\n\n");

	start = time(NULL);

	while (total_written < ADF_SIZE) {
		print_progress(track, TOTAL_TRACKS, start);

		n = read(infd, buf, TRACK_SIZE);
		if (n <= 0) {
			printf("\n");
			fprintf(stderr,
				"\nError reading %s at track %d: %s\n",
				filename, track,
				n < 0 ? strerror(errno) : "unexpected EOF");
			close(fd);
			close(infd);
			return 1;
		}

		if (write(fd, buf, n) != n) {
			printf("\n");
			if (errno == EROFS)
				fprintf(stderr,
					"\nError: Disk is write-protected.\n");
			else if (errno == ETIMEDOUT)
				fprintf(stderr,
					"\nError: Write timeout at track %d.\n"
					"  Check that the drive is working.\n",
					track);
			else
				fprintf(stderr,
					"\nError writing track %d: %s\n",
					track, strerror(errno));
			close(fd);
			close(infd);
			return 1;
		}

		total_written += n;
		track = total_written / TRACK_SIZE;
	}

	print_progress(TOTAL_TRACKS, TOTAL_TRACKS, start);
	printf("\n\n");

	close(fd);
	close(infd);

	{
		time_t elapsed = time(NULL) - start;

		printf("Success! Written %zu bytes (%d KB) in %ld seconds.\n",
		       total_written, (int)(total_written / 1024), elapsed);
		printf("  Disk is ready to use in any Amiga.\n");
	}

	return 0;
}

static int cmd_status(void)
{
	int fd, status;

	fd = open_device();
	if (fd < 0)
		return 1;

	if (ioctl(fd, ALIX_FLP_GET_STATUS, &status) < 0) {
		perror("ioctl GET_STATUS");
		close(fd);
		return 1;
	}

	close(fd);

	printf("Amiga Floppy Drive Status:\n");
	printf("  Motor:          %s\n",
	       (status & FLP_STATUS_MOTOR) ? "ON" : "OFF");
	printf("  Track 0:        %s\n",
	       (status & FLP_STATUS_TK0) ? "Yes (head at track 0)" : "No");
	printf("  Drive Ready:    %s\n",
	       (status & FLP_STATUS_RDY) ? "Yes (motor at speed)" : "No");
	printf("  Disk Changed:   %s\n",
	       (status & FLP_STATUS_CHNG) ? "Yes (disk was removed/inserted)" : "No");
	printf("  Write Protect:  %s\n",
	       (status & FLP_STATUS_WPRO) ? "Yes (tab open)" : "No");

	return 0;
}

static int cmd_motor(const char *state)
{
	int fd;
	unsigned long cmd;

	if (strcmp(state, "on") == 0)
		cmd = ALIX_FLP_MOTOR_ON;
	else if (strcmp(state, "off") == 0)
		cmd = ALIX_FLP_MOTOR_OFF;
	else {
		fprintf(stderr, "Error: Use 'motor on' or 'motor off'\n");
		return 1;
	}

	fd = open_device();
	if (fd < 0)
		return 1;

	if (ioctl(fd, cmd) < 0) {
		perror("ioctl motor");
		close(fd);
		return 1;
	}

	close(fd);
	printf("Motor turned %s.\n", state);
	return 0;
}

static int cmd_seek(const char *cyl_str)
{
	int fd, cyl;

	cyl = atoi(cyl_str);
	if (cyl < 0 || cyl >= CYLINDERS) {
		fprintf(stderr, "Error: Cylinder must be 0-%d\n", CYLINDERS - 1);
		return 1;
	}

	fd = open_device();
	if (fd < 0)
		return 1;

	if (ioctl(fd, ALIX_FLP_SEEK, &cyl) < 0) {
		perror("ioctl seek");
		close(fd);
		return 1;
	}

	close(fd);
	printf("Seeked to cylinder %d.\n", cyl);
	return 0;
}

static int cmd_info(const char *filename)
{
	struct stat st;
	FILE *fp;
	unsigned char bootblock[1024];
	size_t n;
	const char *type;

	if (stat(filename, &st) < 0) {
		perror(filename);
		return 1;
	}

	printf("ADF File Information: %s\n", filename);
	printf("  File size: %lld bytes", (long long)st.st_size);

	if (st.st_size == ADF_SIZE)
		printf(" (standard DD disk)\n");
	else if (st.st_size == ADF_SIZE * 2)
		printf(" (HD disk)\n");
	else
		printf(" (non-standard size!)\n");

	fp = fopen(filename, "rb");
	if (!fp) {
		perror(filename);
		return 1;
	}

	n = fread(bootblock, 1, sizeof(bootblock), fp);
	fclose(fp);

	if (n < 4) {
		fprintf(stderr, "Error: File too small to read bootblock.\n");
		return 1;
	}

	/* Check bootblock type */
	if (memcmp(bootblock, "DOS", 3) == 0) {
		int flags = bootblock[3];

		switch (flags & 0x07) {
		case 0: type = "OFS (Original File System)"; break;
		case 1: type = "FFS (Fast File System)"; break;
		case 2: type = "OFS + International"; break;
		case 3: type = "FFS + International"; break;
		case 4: type = "OFS + Dir Cache"; break;
		case 5: type = "FFS + Dir Cache"; break;
		case 6: type = "OFS + Long Filenames"; break;
		case 7: type = "FFS + Long Filenames"; break;
		default: type = "Unknown"; break;
		}

		printf("  Filesystem: %s\n", type);

		/* Check if bootable */
		if (bootblock[8] != 0 || bootblock[9] != 0 ||
		    bootblock[10] != 0 || bootblock[11] != 0)
			printf("  Bootable: Yes (has boot code)\n");
		else
			printf("  Bootable: No\n");
	} else if (memcmp(bootblock, "KICK", 4) == 0) {
		printf("  Type: Kickstart ROM image\n");
	} else {
		printf("  Filesystem: Unknown (no DOS header)\n");
		printf("  Bootblock bytes: %02X %02X %02X %02X\n",
		       bootblock[0], bootblock[1], bootblock[2], bootblock[3]);
	}

	printf("  Geometry: %d cylinders, %d sides, %d sectors/track\n",
	       CYLINDERS, 2, SECTORS_PER_TRACK);
	printf("  Sector size: %d bytes\n", BYTES_PER_SECTOR);

	return 0;
}

static int cmd_verify(const char *filename)
{
	int fd, filefd;
	unsigned char disk_buf[TRACK_SIZE], file_buf[TRACK_SIZE];
	ssize_t dn, fn;
	int track = 0;
	int errors = 0;
	size_t total = 0;
	time_t start;

	fd = open_device();
	if (fd < 0)
		return 1;

	filefd = open(filename, O_RDONLY);
	if (filefd < 0) {
		perror(filename);
		close(fd);
		return 1;
	}

	printf("Verifying disk against: %s\n\n", filename);
	start = time(NULL);

	while (total < ADF_SIZE) {
		print_progress(track, TOTAL_TRACKS, start);

		dn = read(fd, disk_buf, TRACK_SIZE);
		fn = read(filefd, file_buf, TRACK_SIZE);

		if (dn <= 0 || fn <= 0)
			break;

		if (dn != fn || memcmp(disk_buf, file_buf, dn) != 0) {
			printf("\n  MISMATCH at track %d (cyl %d, side %d)\n",
			       track, track / 2, track % 2);
			errors++;
		}

		total += dn;
		track++;
	}

	print_progress(TOTAL_TRACKS, TOTAL_TRACKS, start);
	printf("\n\n");

	close(filefd);
	close(fd);

	if (errors == 0)
		printf("Verification PASSED — disk matches file.\n");
	else
		printf("Verification FAILED — %d track(s) differ.\n", errors);

	return errors > 0 ? 1 : 0;
}

/* ---- Main ------------------------------------------------------------- */

int main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "read") == 0 || strcmp(argv[1], "dump") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Error: Missing output filename.\n"
				"Usage: %s read <output.adf>\n", argv[0]);
			return 1;
		}
		return cmd_read(argv[2]);
	}

	if (strcmp(argv[1], "write") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Error: Missing input filename.\n"
				"Usage: %s write <input.adf>\n", argv[0]);
			return 1;
		}
		return cmd_write(argv[2]);
	}

	if (strcmp(argv[1], "status") == 0)
		return cmd_status();

	if (strcmp(argv[1], "motor") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: %s motor on|off\n", argv[0]);
			return 1;
		}
		return cmd_motor(argv[2]);
	}

	if (strcmp(argv[1], "seek") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: %s seek <cylinder 0-79>\n",
				argv[0]);
			return 1;
		}
		return cmd_seek(argv[2]);
	}

	if (strcmp(argv[1], "info") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: %s info <file.adf>\n",
				argv[0]);
			return 1;
		}
		return cmd_info(argv[2]);
	}

	if (strcmp(argv[1], "verify") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: %s verify <file.adf>\n",
				argv[0]);
			return 1;
		}
		return cmd_verify(argv[2]);
	}

	if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
	    strcmp(argv[1], "-h") == 0) {
		print_usage(argv[0]);
		return 0;
	}

	fprintf(stderr, "Unknown command: %s\n", argv[1]);
	print_usage(argv[0]);
	return 1;
}
