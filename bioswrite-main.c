/** \file
 * SPI flash command line tool.
 *
 * Much simpler than flashrom, but far less flexible.
 * The flash ROM needs to be in an unlocked state before this can
 * be used. Doing so is left as an exercise to the user.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <getopt.h>
#include "bioswrite.h"

static int force = 0;
static int verbose = 0;

static const struct option long_options[] = {
	{ "force",		0, NULL, 'f' },
	{ "verbose",		0, NULL, 'v' },
	{ "read",		1, NULL, 'r' },
	{ "write",		1, NULL, 'w' },
	{ "offset",		1, NULL, 'O' },
	{ "length",		1, NULL, 'n' },
	{ "help",		0, NULL, 'h' },
	{ NULL,			0, NULL, 0 },
};


static const char usage[] =
"Usage: sudo bioswrite [options]\n"
"\n"
"-h | -? | --help       This help\n"
"-v | --verbose         Increase verbosity\n"
"-r | --read file       Read the ROM range and dump to file\n"
"-w | --write file      Read the file and write to the ROM range\n"
"-O | --offset N        Flash offset to start writing at, otherwise 0\n"
"-n | --length N        Length in bytes to read/write (default whole ROM)\n"
"-f | --force           Write all flash pages, not just the changed ones\n"
"\n"
"WARNING: This tool can permanently brick your machine!\n"
"Use with caution, especially if you do not have an ISP to fix the\n"
"SPI flash ROM chip through hardware.\n"
"\n";


static int
read_from_spi(
	spiflash_t * const sp,
	const char * const filename,
	unsigned offset,
	unsigned length
)
{
	const unsigned flash_size = spiflash_size(sp);

	if (length == 0)
		length = flash_size - offset;
	
	if (offset + length > flash_size)
	{
		fprintf(stderr, "offset %08x + length %08x > flash_size %08x\n",
			offset,
			length,
			flash_size
		);
		return EXIT_FAILURE;
	}

	uint8_t * const buf = calloc(1, length);
	if (!buf)
	{
		perror("malloc");
		return EXIT_FAILURE;
	}

	if (verbose)
		printf("spiflash: reading from %08x: 0x%x bytes\n", offset, length);
	if (spiflash_read(sp, offset, buf, length) < 0)
	{
		fprintf(stderr, "spiflash_read(%08x,%08x) failed?\n",
			offset,
			length
		);
		return EXIT_FAILURE;
	}

	FILE * file;
	if (strcmp(filename, "-") == 0)
	{
		file = stdout;
	} else {
		file = fopen(filename, "w");
		if (!file)
		{
			perror(filename);
			return EXIT_FAILURE;
		}
	}

	fwrite(buf, 1, length, file);
	fclose(file);

	return EXIT_SUCCESS;
}


static int
write_to_spi(
	spiflash_t * const sp,
	const char * const filename,
	unsigned offset,
	unsigned length
)
{
	// if a filename was given, read it in
	FILE * file;
	if (strcmp(filename, "-") == 0)
	{
		file = stdin;
	} else {
		file = fopen(filename, "r");
		if (!file)
		{
			perror(filename);
			return EXIT_FAILURE;
		}
	}

	const unsigned flash_size = spiflash_size(sp);

	uint8_t * const buf = calloc(1, flash_size+1);
	const unsigned read_len = fread(buf, 1, flash_size+1, file);
	if (length == 0)
	{
		// they didn't tell us how much, use this value
		length = read_len;
	} else {
		// should we pad with 0xff if too short?
		if (read_len != length)
		{
			fprintf(stderr, "Read %x bytes, expected %x\n", read_len, length);
			return EXIT_FAILURE;
		}
	}

	if (offset + length > flash_size)
	{
		fprintf(stderr, "offset %08x + length %08x > flash size %08x\n",
			offset,
			length,
			flash_size
		);
		return EXIT_FAILURE;
	}

	if (spiflash_write_enable(sp) < 0)
	{
		fprintf(stderr, "spiflash: unable to enable writes\n");
		return EXIT_FAILURE;
	}

	if (verbose)
		printf("spiflash: writing to %08x: 0x%x bytes\n", offset, length);

	if (spiflash_program_buffer(sp, offset, buf, length) < 0)
	{
		fprintf(stderr, "program write failed!\n");
		return EXIT_FAILURE;
	}

	if (verbose)
		printf("success!\n");

	return EXIT_SUCCESS;
}

int
main(
	int argc,
	char ** argv
)
{
	const char * const prog_name = argv[0];

	int opt;
	int do_read = 0;
	int do_write = 0;
	unsigned offset = 0;
	unsigned length = 0;
	const char * filename = NULL;

	spiflash_t * sp = calloc(1, sizeof(*sp));
	if (!sp)
		return EXIT_FAILURE;

	while ((opt = getopt_long(argc, argv, "h?fvO:n:r:w:", long_options, NULL)) != -1)
	{
		switch(opt)
		{
		case 'O':
			offset = strtoul(optarg, NULL, 0);
			break;
		case 'n':
			length = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			verbose++;
			break;
		case 'f':
			force++;
			break;
		case 'r':
			do_read = 1;
			filename = optarg;
			break;
		case 'w':
			do_write = 1;
			filename = optarg;
			break;
		case '?': case 'h':
			printf("%s", usage);
			return EXIT_SUCCESS;
		default:
			fprintf(stderr, "%s", usage);
			return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;
	if (argc != 0)
	{
		fprintf(stderr, "%s: Excess arguments?\n", prog_name);
		return EXIT_FAILURE;
	}

	sp->verbose = verbose;

	if (spiflash_init(sp) < 0)
	{
		fprintf(stderr, "spiflash_init failed\n");
		return -1;
	}

	if (verbose)
	{
		printf("lpc: %p\n", sp->lpc_base);
		printf("spibar: %p\n", sp->spibar);
	}

	const unsigned flash_size = spiflash_size(sp);
	if (verbose)
		printf("flash size: 0x%08x\n", flash_size);

	if (offset > flash_size)
	{
		fprintf(stderr, "offset %08x > flash size %08x\n",
			offset,
			flash_size
		);
		return EXIT_FAILURE;
	}


	if (do_read && do_write)
	{
		fprintf(stderr, "Only one of read or write may be used\n");
		return EXIT_FAILURE;
	}

	if (do_read)
		return read_from_spi(sp, filename, offset, length);

	if (do_write)
		return write_to_spi(sp, filename, offset, length);

	return EXIT_SUCCESS;
}
