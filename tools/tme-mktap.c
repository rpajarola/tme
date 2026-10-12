/* tools/tme-mktap.c - makes and lists SIMH tape images: */

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* usage: tme-mktap [ -b BLOCK-SIZE ] -o IMAGE FILE...
          tme-mktap -t IMAGE

   the first form makes a SIMH tape image (the .tap format used by
   simh, and by tme's tape element) with each FILE as one tape file,
   cut into records of BLOCK-SIZE bytes (512 by default, as on QIC
   cartridge tapes; the last record of a file may be shorter).  each
   tape file is followed by a tape mark, and the tape ends with a
   second tape mark and an end-of-medium marker.

   the second form lists the tape files in a SIMH tape image, with
   the sizes of their records.

   in a SIMH tape image, each record is a 32-bit little-endian header
   holding its length, the data, a pad byte if the length is odd, and
   the header again.  a zero header is a tape mark, and 0xffffffff is
   the end of the medium: */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static const char *prog;

static void
usage(void)
{
  fprintf(stderr, "usage: %s [ -b BLOCK-SIZE ] -o IMAGE FILE...\n", prog);
  fprintf(stderr, "       %s -t IMAGE\n", prog);
  exit(1);
}

/* this writes a 32-bit little-endian value: */
static void
put_word(FILE *out, unsigned long value)
{
  putc(value & 0xff, out);
  putc((value >> 8) & 0xff, out);
  putc((value >> 16) & 0xff, out);
  putc((value >> 24) & 0xff, out);
}

/* this reads a 32-bit little-endian value, returning nonzero on
   success: */
static int
get_word(FILE *in, unsigned long *_value)
{
  unsigned char bytes[4];

  if (fread(bytes, 1, sizeof(bytes), in) != sizeof(bytes)) {
    return (0);
  }
  *_value = (((unsigned long) bytes[3] << 24)
	     | ((unsigned long) bytes[2] << 16)
	     | ((unsigned long) bytes[1] << 8)
	     | bytes[0]);
  return (1);
}

/* this makes a SIMH tape image: */
static int
make(const char *image, unsigned long block_size, char **files, int count)
{
  FILE *out, *in;
  unsigned char *buffer;
  size_t length;
  unsigned long records;
  int file_i;

  buffer = malloc(block_size);
  out = fopen(image, "wb");
  if (buffer == NULL || out == NULL) {
    fprintf(stderr, "%s: %s: %s\n", prog, image, strerror(errno));
    return (1);
  }

  for (file_i = 0; file_i < count; file_i++) {
    in = fopen(files[file_i], "rb");
    if (in == NULL) {
      fprintf(stderr, "%s: %s: %s\n", prog, files[file_i], strerror(errno));
      fclose(out);
      remove(image);
      return (1);
    }

    /* the file's records: */
    records = 0;
    while ((length = fread(buffer, 1, block_size, in)) > 0) {
      put_word(out, length);
      fwrite(buffer, 1, length, out);
      if (length & 1) {
	putc(0, out);
      }
      put_word(out, length);
      records++;
    }
    if (ferror(in)) {
      fprintf(stderr, "%s: %s: %s\n", prog, files[file_i], strerror(errno));
      fclose(in);
      fclose(out);
      remove(image);
      return (1);
    }
    fclose(in);

    /* the tape mark after the file: */
    put_word(out, 0);
    printf("file %d: %s, %lu records\n", file_i + 1, files[file_i], records);
  }

  /* the end of the tape: */
  put_word(out, 0);
  put_word(out, 0xffffffff);
  if (fclose(out) != 0) {
    fprintf(stderr, "%s: %s: %s\n", prog, image, strerror(errno));
    remove(image);
    return (1);
  }
  free(buffer);
  return (0);
}

/* this lists a SIMH tape image: */
static int
list(const char *image)
{
  FILE *in;
  unsigned long header, trailer, length, class;
  unsigned long file, records, bytes, size_min, size_max;
  long offset;
  int marks_in_a_row;

  in = fopen(image, "rb");
  if (in == NULL) {
    fprintf(stderr, "%s: %s: %s\n", prog, image, strerror(errno));
    return (1);
  }

  file = 1;
  records = bytes = size_min = size_max = 0;
  marks_in_a_row = 0;
  for (;;) {
    offset = ftell(in);
    if (!get_word(in, &header)) {
      if (records > 0) {
	printf("file %lu: %lu records, %lu bytes (no tape mark at the end of the image)\n",
	       file, records, bytes);
      }
      printf("end of image\n");
      break;
    }
    if (header == 0xffffffff) {
      printf("end of medium\n");
      break;
    }
    class = header >> 28;
    length = header & 0x0fffffff;

    /* a marker without data: */
    if (class == 0xe || class == 0xf) {
      continue;
    }

    /* a tape mark ends a file: */
    if (header == 0) {
      if (records > 0 || marks_in_a_row == 0) {
	if (size_min == size_max) {
	  printf("file %lu: %lu records of %lu bytes, %lu bytes\n",
		 file, records, size_max, bytes);
	}
	else {
	  printf("file %lu: %lu records of %lu to %lu bytes, %lu bytes\n",
		 file, records, size_min, size_max, bytes);
	}
      }
      else {
	printf("(tape mark)\n");
      }
      file++;
      records = bytes = size_min = size_max = 0;
      marks_in_a_row++;
      continue;
    }
    marks_in_a_row = 0;

    /* a record: */
    if (fseek(in, length + (length & 1), SEEK_CUR) != 0
	|| !get_word(in, &trailer)
	|| trailer != header) {
      fprintf(stderr, "%s: %s: bad record at offset %ld; not a SIMH tape image?\n",
	      prog, image, offset);
      fclose(in);
      return (1);
    }
    if (class != 0x0 && class != 0x8) {
      continue;
    }
    if (records == 0 || length < size_min) {
      size_min = length;
    }
    if (length > size_max) {
      size_max = length;
    }
    records++;
    bytes += length;
  }
  fclose(in);
  return (0);
}

int
main(int argc, char **argv)
{
  const char *image, *list_image;
  unsigned long block_size;
  char *end;
  int arg_i;

  prog = strrchr(argv[0], '/');
  prog = (prog == NULL ? argv[0] : prog + 1);
  image = NULL;
  list_image = NULL;
  block_size = 512;
  for (arg_i = 1; arg_i < argc && argv[arg_i][0] == '-'; arg_i++) {
    if (strcmp(argv[arg_i], "-b") == 0 && arg_i + 1 < argc) {
      block_size = strtoul(argv[++arg_i], &end, 0);
      if (*end != '\0' || block_size == 0 || block_size > 0x0fffffff) {
	fprintf(stderr, "%s: bad block size %s\n", prog, argv[arg_i]);
	usage();
      }
    }
    else if (strcmp(argv[arg_i], "-o") == 0 && arg_i + 1 < argc) {
      image = argv[++arg_i];
    }
    else if (strcmp(argv[arg_i], "-t") == 0 && arg_i + 1 < argc) {
      list_image = argv[++arg_i];
    }
    else {
      usage();
    }
  }

  if (list_image != NULL) {
    if (image != NULL || arg_i != argc) {
      usage();
    }
    return (list(list_image));
  }
  if (image == NULL || arg_i == argc) {
    usage();
  }
  return (make(image, block_size, argv + arg_i, argc - arg_i));
}
