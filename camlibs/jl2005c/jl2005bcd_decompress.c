/* decompress.c
 *
 * Converts raw output from Jeilin JL2005B/C/D into PPM files.
 *
 * The jl2005bcd_raw_converter is
 *   Copyright (c) 2010 Theodore Kilgore <kilgota@auburn.edu>
 *
 * The decompression code used is
 *   Copyright (c) 2010 Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>

#include "jl2005bcd_decompress.h"
#include "jpeg_memsrcdest.c"
#include <bayer.h>
#include "img_enhance.h"
#include <math.h>

#include <gphoto2/gphoto2.h>
#include <gphoto2/gphoto2-port.h>

#define GP_MODULE "jl2005c"


#define JPEG_HEADER_SIZE	338
#define JPEG_HEIGHT_OFFSET	 94

static int
find_eoi (uint8_t *jpeg_data, int jpeg_data_idx, int jpeg_data_size)
{
	int i;

	for (i = jpeg_data_idx; i < (jpeg_data_size - 1); i++)
		if (jpeg_data[i] == 0xff && jpeg_data[i + 1] == 0xd9)
			break;

	if (i >= (jpeg_data_size - 1)) {
		printf("AAI\n");
		return -1;
	}

	return i + 2; /* + 2 -> Point to after EOI marker */
}

int
jl2005bcd_decompress (unsigned char *output, unsigned char *input,
					int inputsize, int get_thumbnail)
{
	int out_headerlen;
	uint16_t *thumb = NULL;
	unsigned char *header;
	uint8_t jpeg_stripe[500000];
	uint8_t out[5000000];
	unsigned char *jpeg_data;
	int q, width, height;
	int thumbnail_width, thumbnail_height;
	struct jpeg_compress_struct cinfo;
	struct jpeg_decompress_struct dinfo;
	struct jpeg_error_mgr jcerr, jderr;
	JOCTET *jpeg_header = NULL;
	int outputsize = 0;
	unsigned long jpeg_header_size = 0;
	int i, x, y, x1, y1, jpeg_data_size, jpeg_data_idx, eoi, size, ret;


	GP_DEBUG("Running jl2005bcd_decompress() function.\n");

	header = input;
	q = header[3] & 0x7f;
	height = header[4] * 8;
	width = header[5] * 8;
	printf("quality is %d\n", q);
	printf("size: %dx%d\n", width, height);
	switch (header[9] & 0xf0) {
	case 0xf0:
		thumbnail_width = 128;
		thumbnail_height = 120;
		break;
	case 0x60:
		thumbnail_width = 96;
		thumbnail_height = 64;
		break;
	default:
		thumbnail_width = 0;
		thumbnail_height = 0;
	}
	if (header[1] & 3)
		thumbnail_width = 0;
	if (get_thumbnail) {
		if (!thumbnail_width) {
			GP_DEBUG("No thumbnail is present!\n");
			return GP_ERROR_NOT_SUPPORTED;
		} else {
			thumb = input + 16;
			for (i = 0; i < thumbnail_width * thumbnail_height;
									i++) {
				thumb[i] = ntohs(thumb[i]);
				out[i * 3 + 0] = (thumb[i] & 0xf800) >> 8;
				out[i * 3 + 1] = (thumb[i] & 0x07e0) >> 3;
				out[i * 3 + 2] = (thumb[i] & 0x001f) << 3;
			}

			out_headerlen = snprintf((char *)output, 256,
				"P6\n"
				"# CREATOR: gphoto2, JL2005BCD library\n"
				"%d %d\n"
				"255\n",
				thumbnail_width,
				thumbnail_height);
			white_balance (out, thumbnail_width * thumbnail_height,
									1.6);
			memcpy(output + out_headerlen, out,
				thumbnail_width * thumbnail_height * 3);
			outputsize = thumbnail_width * thumbnail_height * 3 +
						out_headerlen;
			return outputsize;
		}
	}
	/*
	 * And the fun begins, first of all create a dummy jpeg, which we use
	 * to get the headers from to feed to libjpeg when decompressing the
	 * stripes. This way we can use libjpeg's quant table handling
	 * (and built in default huffman tables).
	 */
	cinfo.err = jpeg_std_error (&jcerr);
	jpeg_create_compress (&cinfo);
	jpeg_mem_dest (&cinfo, &jpeg_header, &jpeg_header_size);
	cinfo.image_width = 16;
	cinfo.image_height = 16;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults (&cinfo);
	/* Make comp[0] (which will be green) 1x2 subsampled */
	cinfo.comp_info[0].h_samp_factor = 1;
	cinfo.comp_info[0].v_samp_factor = 2;
	/* Make comp[1] and [2] use huffman table and quanttable 0, as all
	 * components use luminance settings with the jl2005b/c/d */
	cinfo.comp_info[1].quant_tbl_no = 0;
	cinfo.comp_info[1].dc_tbl_no = 0;
	cinfo.comp_info[1].ac_tbl_no = 0;
	cinfo.comp_info[2].quant_tbl_no = 0;
	cinfo.comp_info[2].dc_tbl_no = 0;
	cinfo.comp_info[2].ac_tbl_no = 0;
	/* Apply the quality setting from the header */
	if (q <= 0)
		i = 5000;
	else if (q <= 50)
		i = 5000 / q;
	else if (q <= 100)
		i = 2 * (100 - q);
	else
		i = 0;
	jpeg_set_linear_quality(&cinfo, i, TRUE);

	jpeg_start_compress (&cinfo, TRUE);
	while( cinfo.next_scanline < cinfo.image_height ) {
		JOCTET row[16 * 3];
		JSAMPROW row_pointer[1] = { row };
		jpeg_write_scanlines (&cinfo, row_pointer, 1);
	}
	jpeg_finish_compress (&cinfo);
	jpeg_destroy_compress (&cinfo);

	JSAMPLE green[8 * 16];
	JSAMPLE red[8 * 8];
	JSAMPLE blue[8 * 8];
	JSAMPROW green_row_pointer[16];
	JSAMPROW red_row_pointer[8];
	JSAMPROW blue_row_pointer[8];

	for (i = 0; i < 16; i++)
		green_row_pointer[i] = green + i * 8;

	for (i = 0; i < 8; i++) {
		red_row_pointer[i] = red + i * 8;
		blue_row_pointer[i] = blue + i * 8;
	}

	JSAMPARRAY samp_image[3] = { green_row_pointer,
				     red_row_pointer,
				     blue_row_pointer };

	memcpy(jpeg_stripe, jpeg_header, JPEG_HEADER_SIZE);
	jpeg_stripe[JPEG_HEIGHT_OFFSET    ] = height >> 8;
	jpeg_stripe[JPEG_HEIGHT_OFFSET + 1] = height;
	jpeg_stripe[JPEG_HEIGHT_OFFSET + 2] = 0;
	jpeg_stripe[JPEG_HEIGHT_OFFSET + 3] = 8;
	free (jpeg_header);
	jpeg_data = input + 16 + 2 * thumbnail_width * thumbnail_height;
	jpeg_data_size = inputsize - 16
				- 2 * thumbnail_width * thumbnail_height;

	jpeg_data_idx = 0;

	memset(out, 0, width * height * 3);

	dinfo.err = jpeg_std_error (&jderr);
	jpeg_create_decompress (&dinfo);
	for (x = 0; x < width; x += 16) {
		eoi = find_eoi(jpeg_data, jpeg_data_idx, jpeg_data_size);
		if (eoi < 0)
			return eoi;

		size = eoi - jpeg_data_idx;
		if ((JPEG_HEADER_SIZE + size) > sizeof(jpeg_stripe)) {
			printf("AAAIIIIII\n");
			return 1;
		}
		memcpy (jpeg_stripe + JPEG_HEADER_SIZE,
			jpeg_data + jpeg_data_idx, size);

		jpeg_mem_src (&dinfo, jpeg_stripe, JPEG_HEADER_SIZE + size);
		jpeg_read_header (&dinfo, TRUE);
		dinfo.raw_data_out = TRUE;
#if JPEG_LIB_VERSION >= 70
		dinfo.do_fancy_upsampling = FALSE;
#endif
		jpeg_start_decompress (&dinfo);
		for (y = 0; y < height; y += 16) {
			jpeg_read_raw_data (&dinfo, samp_image, 16);
			for (y1 = 0; y1 < 16; y1 += 2) {
				for (x1 = 0; x1 < 16; x1 += 2) {
					out[((y + y1 + 0) * width
							+ x + x1 + 0) * 3]
						= red[y1 * 4 + x1 / 2];
					out[((y + y1 + 0) * width
							+ x + x1 + 1) * 3 + 1]
						= green[y1 * 8 + x1 / 2];
					out[((y + y1 + 1) * width
							+ x + x1 + 0) * 3 + 1]
						= green[y1 * 8 + 8 + x1 / 2];
					out[((y + y1 + 1) * width
							+ x + x1 + 1) * 3 + 2]
						= blue[y1 * 4 + x1 / 2];
				}
			}
		}
		jpeg_finish_decompress (&dinfo);

		/* Set jpeg_data_idx for the next stripe */
		jpeg_data_idx = (jpeg_data_idx + size + 0x0f) & ~0x0f;
	}
	jpeg_destroy_decompress(&dinfo);

	ret = gp_ahd_interpolate(out, width, height, BAYER_TILE_BGGR);
	if (ret < 0) {
		printf("HEUH?\n");
		return ret;
	}
	white_balance (out, width*height, 1.6);

	out_headerlen = snprintf((char *)output, 256,
				"P6\n"
				"# CREATOR: gphoto2, JL2005BCD library\n"
				"%d %d\n255\n",
				width,
				height);
	GP_DEBUG("out_headerlen = %d\n", out_headerlen);
	memcpy(output + out_headerlen, out, width * height * 3);
	outputsize = out_headerlen + width * height * 3;
	return outputsize;
}