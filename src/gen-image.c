/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2016 Laurent Pinchart <laurent.pinchart@ideasonboard.com> */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/videodev2.h>

#define ARRAY_SIZE(a)		(sizeof(a) / sizeof(a[0]))

#define min(a, b)		({	\
	typeof(a) _a = (a);		\
	typeof(b) _b = (b);		\
	_a < _b ? _a : _b;		\
})

#define max(a, b)		({	\
	typeof(a) _a = (a);		\
	typeof(b) _b = (b);		\
	_a > _b ? _a : _b;		\
})

#define clamp(val, low, high)	max(low, min(high, val));

#define swap(a, b)			\
do {	\
	typeof(a) __tmp = (a);		\
	(a) = (b);			\
	(b) = __tmp;			\
} while (0)

#define div_round_up(n, d)	(((n) + (d) - 1) / (d))

enum format_type {
	FORMAT_RGB,
	FORMAT_YUV,
	FORMAT_HSV,
};

struct format_color_component {
	unsigned char length;
	unsigned char offset;
};

struct format_rgb_info {
	unsigned int bpp;
	struct format_color_component red;
	struct format_color_component green;
	struct format_color_component blue;
	struct format_color_component alpha;
};

struct format_hsv_info {
	unsigned int bpp;
	struct format_color_component hue;
	struct format_color_component saturation;
	struct format_color_component value;
	struct format_color_component alpha;
};

enum format_yuv_order {
	YUV_YCbCr = 1,
	YUV_YCrCb = 2,
	YUV_YC = 4,
	YUV_CY = 8,
};

struct format_yuv_info {
	unsigned int num_planes;
	enum format_yuv_order order;
	unsigned int xsub;
	unsigned int ysub;
};

struct format_info {
	const char *name;
	enum format_type type;
	struct format_rgb_info rgb;
	struct format_hsv_info hsv;
	struct format_yuv_info yuv;
};

struct image_rect {
	int left;
	int top;
	unsigned int width;
	unsigned int height;
};

struct image {
	const struct format_info *format;
	unsigned int width;
	unsigned int height;
	unsigned int size;
	void *data;
};

struct params {
	unsigned int alpha;
	enum v4l2_ycbcr_encoding encoding;
	enum v4l2_quantization quantization;
	bool no_chroma_average;
};

enum histogram_type {
	HISTOGRAM_HGO,
	HISTOGRAM_HGT,
};

struct options {
	const char *input_filename;
	const char *output_filename;
	const char *histo_filename;
	const char *clu_filename;
	const char *lut_filename;

	const struct format_info *input_format;
	const struct format_info *output_format;
	unsigned int output_height;
	unsigned int output_width;

	bool hflip;
	bool vflip;
	bool rotate;
	unsigned int compose;
	struct params params;
	bool crop;
	struct image_rect inputcrop;
	enum histogram_type histo_type;
	uint8_t histo_areas[12];
};

/* -----------------------------------------------------------------------------
 * Format information
 */

#define MAKE_HSV_INFO(hl, ho, sl, so, vl, vo, al, ao) \
	.hue = { (hl), (ho) }, .saturation = { (sl), (so) }, \
	.value = { (vl), (vo) }, .alpha = { (al), (ao) }

#define MAKE_RGB_INFO(rl, ro, gl, go, bl, bo, al, ao) \
	.red = { (rl), (ro) }, .green = { (gl), (go) }, \
	.blue = { (bl), (bo) }, .alpha = { (al), (ao) }

static const struct format_info format_info[] = {
	/*
	 * The alpha channel maps to the X (don't care) bits for the XRGB
	 * formats.
	 */
	{ "RGB332",	FORMAT_RGB, .rgb = { 8,  MAKE_RGB_INFO(3, 5, 3, 2, 2, 0, 0, 0) } },
	{ "ARGB444",	FORMAT_RGB, .rgb = { 16, MAKE_RGB_INFO(4, 8, 4, 4, 4, 0, 4, 12) } },
	{ "XRGB444",	FORMAT_RGB, .rgb = { 16, MAKE_RGB_INFO(4, 8, 4, 4, 4, 0, 4, 12) } },
	{ "ARGB555",	FORMAT_RGB, .rgb = { 16, MAKE_RGB_INFO(5, 10, 5, 5, 5, 0, 1, 15) } },
	{ "XRGB555",	FORMAT_RGB, .rgb = { 16, MAKE_RGB_INFO(5, 10, 5, 5, 5, 0, 1, 15) } },
	{ "RGB565",	FORMAT_RGB, .rgb = { 16, MAKE_RGB_INFO(5, 11, 6, 5, 5, 0, 0, 0) } },
	{ "BGR24",	FORMAT_RGB, .rgb = { 24, MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 0, 0) } },
	{ "RGB24",	FORMAT_RGB, .rgb = { 24, MAKE_RGB_INFO(8, 0, 8, 8, 8, 16, 0, 0) } },
	{ "ABGR32",	FORMAT_RGB, .rgb = { 32, MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 8, 24) } },
	{ "XBGR32",	FORMAT_RGB, .rgb = { 32, MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 8, 24) } },
	{ "ARGB32",	FORMAT_RGB, .rgb = { 32, MAKE_RGB_INFO(8, 8, 8, 16, 8, 24, 8, 0) } },
	{ "XRGB32",	FORMAT_RGB, .rgb = { 32, MAKE_RGB_INFO(8, 8, 8, 16, 8, 24, 8, 0) } },
	{ "HSV24",	FORMAT_HSV, .hsv = { 24, MAKE_HSV_INFO(8, 0, 8, 8, 8, 16, 0, 0) } },
	{ "HSV32",	FORMAT_HSV, .hsv = { 32, MAKE_HSV_INFO(8, 8, 8, 16, 8, 24, 8, 0) } },
	{ "UYVY",	FORMAT_YUV, .yuv = { 1, YUV_YCbCr | YUV_CY, 2, 1 } },
	{ "VYUY",	FORMAT_YUV, .yuv = { 1, YUV_YCrCb | YUV_CY, 2, 1 } },
	{ "YUYV",	FORMAT_YUV, .yuv = { 1, YUV_YCbCr | YUV_YC, 2, 1 } },
	{ "YVYU",	FORMAT_YUV, .yuv = { 1, YUV_YCrCb | YUV_YC, 2, 1 } },
	{ "NV12M",	FORMAT_YUV, .yuv = { 2, YUV_YCbCr, 2, 2 } },
	{ "NV21M",	FORMAT_YUV, .yuv = { 2, YUV_YCrCb, 2, 2 } },
	{ "NV16M",	FORMAT_YUV, .yuv = { 2, YUV_YCbCr, 2, 1 } },
	{ "NV61M",	FORMAT_YUV, .yuv = { 2, YUV_YCrCb, 2, 1 } },
	{ "YUV420M",	FORMAT_YUV, .yuv = { 3, YUV_YCbCr, 2, 2 } },
	{ "YVU420M",	FORMAT_YUV, .yuv = { 3, YUV_YCrCb, 2, 2 } },
	{ "YUV422M",	FORMAT_YUV, .yuv = { 3, YUV_YCbCr, 2, 1 } },
	{ "YVU422M",	FORMAT_YUV, .yuv = { 3, YUV_YCrCb, 2, 1 } },
	{ "YUV444M",	FORMAT_YUV, .yuv = { 3, YUV_YCbCr, 1, 1 } },
	{ "YVU444M",	FORMAT_YUV, .yuv = { 3, YUV_YCrCb, 1, 1 } },
	{ "YUV24",	FORMAT_YUV, .yuv = { 1, YUV_YCbCr | YUV_YC, 1, 1 } },
};

static const struct format_info *format_by_name(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(format_info); i++) {
		if (!strcmp(name, format_info[i].name))
			return &format_info[i];
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * File I/O
 */

static int file_read(int fd, void *buffer, size_t size)
{
	unsigned int offset = 0;

	while (offset < size) {
		ssize_t nbytes;

		nbytes = read(fd, buffer + offset, size - offset);
		if (nbytes < 0) {
			if (errno == EINTR)
				continue;

			return -errno;
		}

		if (nbytes == 0)
			return offset;

		offset += nbytes;
	}

	return size;
}

static int file_write(int fd, const void *buffer, size_t size)
{
	unsigned int offset = 0;

	while (offset < size) {
		ssize_t nbytes;

		nbytes = write(fd, buffer + offset, size - offset);
		if (nbytes < 0) {
			if (errno == EINTR)
				continue;

			return -errno;
		}

		offset += nbytes;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Image initialization
 */

static struct image *image_new(const struct format_info *format,
			       unsigned int width, unsigned int height)
{
	struct image *image;

	image = malloc(sizeof(*image));
	if (!image)
		return NULL;

	memset(image, 0, sizeof(*image));
	image->format = format;
	image->width = width;
	image->height = height;

	switch (format->type) {
	case FORMAT_RGB:
		image->size = image->width * image->height
			     * format->rgb.bpp / 8;
		break;

	case FORMAT_HSV:
		image->size = image->width * image->height
			     * format->hsv.bpp / 8;
		break;

	case FORMAT_YUV:
		image->size = image->width * image->height
			     * (8 + 2 * 8 / format->yuv.xsub / format->yuv.ysub)
			     / 8;
		break;
	}

	image->data = malloc(image->size);
	if (!image->data) {
		printf("Not enough memory for image data\n");
		free(image);
		return NULL;
	}

	return image;
}

static void image_delete(struct image *image)
{
	if (!image)
		return;

	free(image->data);
	free(image);
}

/* -----------------------------------------------------------------------------
 * Image read and write
 */

static int pnm_read_bytes(int fd, char *buffer, size_t size)
{
	int ret;

	ret = file_read(fd, buffer, size);
	if (ret < 0) {
		printf("Unable to read PNM file: %s (%d)\n", strerror(-ret),
		       ret);
		return ret;
	}
	if ((size_t)ret != size) {
		printf("Invalid PNM file: file too short\n");
		return -ENODATA;
	}

	return 0;
}

static int pnm_read_integer(int fd)
{
	unsigned int value = 0;
	int ret;
	char c;

	do {
		ret = pnm_read_bytes(fd, &c, 1);
	} while (!ret && isspace(c));

	if (ret)
		return ret;

	while (!ret && isdigit(c)) {
		value = value * 10 + c - '0';
		ret = pnm_read_bytes(fd, &c, 1);
	}

	if (ret)
		return ret;

	if (!isspace(c))
		return -EINVAL;

	return value;
}

static struct image *pnm_read(const char *filename)
{
	struct image *image;
	unsigned int width;
	unsigned int height;
	char buffer[2];
	int ret;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		printf("Unable to open PNM file %s: %s (%d)\n", filename,
		       strerror(errno), errno);
		return NULL;
	}

	/* Read and validate the header. */
	ret = pnm_read_bytes(fd, buffer, 2);
	if (ret < 0)
		goto done;

	if (buffer[0] != 'P' || buffer[1] != '6') {
		printf("Invalid PNM file: invalid signature\n");
		ret = -EINVAL;
		goto done;
	}

	/* Read the width, height and depth. */
	ret = pnm_read_integer(fd);
	if (ret < 0) {
		printf("Invalid PNM file: invalid width\n");
		goto done;
	}

	width = ret;

	ret = pnm_read_integer(fd);
	if (ret < 0) {
		printf("Invalid PNM file: invalid height\n");
		goto done;
	}

	height = ret;

	ret = pnm_read_integer(fd);
	if (ret < 0) {
		printf("Invalid PNM file: invalid depth\n");
		goto done;
	}

	if (ret != 255) {
		printf("Invalid PNM file: unsupported depth %u\n", ret);
		ret = -EINVAL;
		goto done;
	}

	/* Allocate the image and read the data. */
	image = image_new(format_by_name("RGB24"), width, height);
	if (!image)
		goto done;

	ret = pnm_read_bytes(fd, image->data, image->size);
	if (ret < 0) {
		image_delete(image);
		image = NULL;
	}

done:
	close(fd);

	return ret ? NULL : image;
}

static struct image *image_read(const char *filename)
{
	return pnm_read(filename);
}

static int image_write(const struct image *image, const char *filename)
{
	int ret;
	int fd;

	fd = open(filename, O_WRONLY | O_CREAT,
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		printf("Unable to open output file %s: %s (%d)\n", filename,
		       strerror(errno), errno);
		return -errno;
	}

	ret = file_write(fd, image->data, image->size);
	if (ret < 0)
		printf("Unable to write output image: %s (%d)\n",
		       strerror(-ret), ret);
	else
		ftruncate(fd, image->size);

	close(fd);
	return ret;
}

/* -----------------------------------------------------------------------------
 * Image formatting
 */

static void image_format_rgb8(const struct image *input, struct image *output,
			      const struct params *params)
{
	const uint8_t *idata = input->data;
	uint8_t *odata = output->data;
	uint8_t r, g, b;
	unsigned int x, y;

	idata = input->data;
	odata = output->data;

	for (y = 0; y < input->height; ++y) {
		for (x = 0; x < input->width; ++x) {
			/* There's only one RGB8 variant supported, hardcode it. */
			r = *idata++ >> 5;
			g = *idata++ >> 5;
			b = *idata++ >> 6;

			*odata++ = (r << 5) | (g << 2) | b;
		}
	}
}

static void image_format_rgb16(const struct image *input, struct image *output,
			       const struct params *params)
{
	const struct format_info *format = output->format;
	const uint8_t *idata = input->data;
	uint16_t *odata = output->data;
	uint8_t r, g, b, a;
	unsigned int x, y;

	for (y = 0; y < input->height; ++y) {
		for (x = 0; x < input->width; ++x) {
			r = *idata++ >> (8 - format->rgb.red.length);
			g = *idata++ >> (8 - format->rgb.green.length);
			b = *idata++ >> (8 - format->rgb.blue.length);
			a = params->alpha >> (8 - format->rgb.alpha.length);

			*odata++ = (r << format->rgb.red.offset)
				 | (g << format->rgb.green.offset)
				 | (b << format->rgb.blue.offset)
				 | (a << format->rgb.alpha.offset);
		}
	}
}

static void image_format_rgb24(const struct image *input, struct image *output,
			       const struct params *params)
{
	struct color_rgb24 {
		unsigned int value:24;
	} __attribute__((__packed__));

	const struct format_info *format = output->format;
	const uint8_t *idata = input->data;
	struct color_rgb24 *odata = output->data;
	uint8_t r, g, b, a;
	unsigned int x, y;

	idata = input->data;
	odata = output->data;

	for (y = 0; y < input->height; ++y) {
		for (x = 0; x < input->width; ++x) {
			r = *idata++ >> (8 - format->rgb.red.length);
			g = *idata++ >> (8 - format->rgb.green.length);
			b = *idata++ >> (8 - format->rgb.blue.length);
			a = params->alpha >> (8 - format->rgb.alpha.length);

			*odata++ = (struct color_rgb24){ .value =
					(r << format->rgb.red.offset) |
					(g << format->rgb.green.offset) |
					(b << format->rgb.blue.offset) |
					(a << format->rgb.alpha.offset) };
		}
	}
}

static void image_format_rgb32(const struct image *input, struct image *output,
			       const struct params *params)
{
	const struct format_info *format = output->format;
	const uint8_t *idata = input->data;
	uint32_t *odata = output->data;
	uint8_t r, g, b, a;
	unsigned int x, y;

	for (y = 0; y < input->height; ++y) {
		for (x = 0; x < input->width; ++x) {
			r = *idata++ >> (8 - format->rgb.red.length);
			g = *idata++ >> (8 - format->rgb.green.length);
			b = *idata++ >> (8 - format->rgb.blue.length);
			a = params->alpha >> (8 - format->rgb.alpha.length);

			*odata++ = (r << format->rgb.red.offset)
				 | (g << format->rgb.green.offset)
				 | (b << format->rgb.blue.offset)
				 | (a << format->rgb.alpha.offset);
		}
	}
}

static void image_format_hsv24(const struct image *input, struct image *output,
			       const struct params *params)
{
	memcpy(output->data, input->data, input->width * input->height * 3);
}

static void image_format_hsv32(const struct image *input, struct image *output,
			       const struct params *params)
{
	const struct format_info *format = output->format;
	const uint8_t *idata = input->data;
	uint32_t *odata = output->data;
	uint8_t h, s, v, a;
	unsigned int x, y;

	for (y = 0; y < input->height; ++y) {
		for (x = 0; x < input->width; ++x) {
			h = *idata++;
			s = *idata++;
			v = *idata++;
			a = params->alpha;

			*odata++ = (h << format->hsv.hue.offset)
				 | (s << format->hsv.saturation.offset)
				 | (v << format->hsv.value.offset)
				 | (a << format->hsv.alpha.offset);
		}
	}
}

/*
 * In YUV packed and planar formats, when subsampling horizontally average the
 * chroma components of the two pixels to match the hardware behaviour.
 */
static void image_format_yuv_packed(const struct image *input, struct image *output,
				    const struct params *params)
{
	const struct format_info *format = output->format;
	const uint8_t *idata = input->data;
	uint8_t *o_y = output->data + ((format->yuv.order & YUV_YC) ? 0 : 1);
	uint8_t *o_c = output->data + ((format->yuv.order & YUV_CY) ? 0 : 1);
	unsigned int u_offset = (format->yuv.order & YUV_YCrCb) ? 2 : 0;
	unsigned int v_offset = (format->yuv.order & YUV_YCbCr) ? 2 : 0;
	unsigned int x;
	unsigned int y;

	for (y = 0; y < output->height; ++y) {
		for (x = 0; x < output->width; x += 2) {
			o_y[2*x] = idata[3*x];
			o_y[2*x + 2] = idata[3*x + 3];

			if (params->no_chroma_average) {
				o_c[2*x + u_offset] = idata[3*x + 1];
				o_c[2*x + v_offset] = idata[3*x + 2];
			} else {
				o_c[2*x + u_offset] = (idata[3*x + 1] + idata[3*x + 4]) / 2;
				o_c[2*x + v_offset] = (idata[3*x + 2] + idata[3*x + 5]) / 2;
			}
		}

		o_y += input->width * 2;
		o_c += input->width * 2;
		idata += input->width * 3;
	}
}

static void image_format_yuv_planar(const struct image *input, struct image *output,
				    const struct params *params)
{
	const struct format_info *format = output->format;
	const uint8_t *idata;
	uint8_t *o_y = output->data;
	uint8_t *o_c = o_y + output->width * output->height;
	uint8_t *o_u;
	uint8_t *o_v;
	unsigned int xsub = format->yuv.xsub;
	unsigned int ysub = format->yuv.ysub;
	unsigned int c_stride;
	unsigned int x;
	unsigned int y;

	if (format->yuv.num_planes == 2) {
		o_u = (format->yuv.order & YUV_YCbCr) ? o_c : o_c + 1;
		o_v = (format->yuv.order & YUV_YCrCb) ? o_c : o_c + 1;
		c_stride = 2;
	} else {
		unsigned int c_size = output->width * output->height
				    / xsub / ysub;

		o_u = (format->yuv.order & YUV_YCbCr) ? o_c : o_c + c_size;
		o_v = (format->yuv.order & YUV_YCrCb) ? o_c : o_c + c_size;
		c_stride = 1;
	}

	idata = input->data;
	for (y = 0; y < output->height; ++y) {
		for (x = 0; x < output->width; ++x)
			*o_y++ = idata[3*x];

		idata += input->width * 3;
	}

	idata = input->data;
	for (y = 0; y < output->height / ysub; ++y) {
		if (xsub == 1 || params->no_chroma_average) {
			for (x = 0; x < output->width; x += xsub) {
				o_u[x*c_stride/xsub] = idata[3*x + 1];
				o_v[x*c_stride/xsub] = idata[3*x + 2];
			}
		} else {
			for (x = 0; x < output->width; x += xsub) {
				o_u[x*c_stride/xsub] = (idata[3*x + 1] + idata[3*x + 4]) / 2;
				o_v[x*c_stride/xsub] = (idata[3*x + 2] + idata[3*x + 5]) / 2;
			}
		}

		o_u += output->width * c_stride / xsub;
		o_v += output->width * c_stride / xsub;
		idata += input->width * 3 * ysub;
	}
}

/* -----------------------------------------------------------------------------
 * Colorspace handling
 *
 * The code is inspired by the v4l2-tpg Linux kernel driver.
 */

static void colorspace_matrix(enum v4l2_ycbcr_encoding encoding,
			      enum v4l2_quantization quantization,
			      int (*matrix)[3][3])
{
#define COEFF(v, r) ((int)(0.5 + (v) * (r) * 256.0))

	static const int bt601[3][3] = {
		{ COEFF(0.299, 219),  COEFF(0.587, 219),  COEFF(0.114, 219)  },
		{ COEFF(-0.169, 224), COEFF(-0.331, 224), COEFF(0.5, 224)    },
		{ COEFF(0.5, 224),    COEFF(-0.419, 224), COEFF(-0.081, 224) },
	};
	static const int bt601_full[3][3] = {
		{ COEFF(0.299, 255),  COEFF(0.587, 255),  COEFF(0.114, 255)  },
		{ COEFF(-0.169, 255), COEFF(-0.331, 255), COEFF(0.5, 255)    },
		{ COEFF(0.5, 255),    COEFF(-0.419, 255), COEFF(-0.081, 255) },
	};
	static const int rec709[3][3] = {
		{ COEFF(0.2126, 219),  COEFF(0.7152, 219),  COEFF(0.0722, 219)  },
		{ COEFF(-0.1146, 224), COEFF(-0.3854, 224), COEFF(0.5, 224)     },
		{ COEFF(0.5, 224),     COEFF(-0.4542, 224), COEFF(-0.0458, 224) },
	};
	static const int rec709_full[3][3] = {
		{ COEFF(0.2126, 255),  COEFF(0.7152, 255),  COEFF(0.0722, 255)  },
		{ COEFF(-0.1146, 255), COEFF(-0.3854, 255), COEFF(0.5, 255)     },
		{ COEFF(0.5, 255),     COEFF(-0.4542, 255), COEFF(-0.0458, 255) },
	};
	static const int smpte240m[3][3] = {
		{ COEFF(0.212, 219),  COEFF(0.701, 219),  COEFF(0.087, 219)  },
		{ COEFF(-0.116, 224), COEFF(-0.384, 224), COEFF(0.5, 224)    },
		{ COEFF(0.5, 224),    COEFF(-0.445, 224), COEFF(-0.055, 224) },
	};
	static const int smpte240m_full[3][3] = {
		{ COEFF(0.212, 255),  COEFF(0.701, 255),  COEFF(0.087, 255)  },
		{ COEFF(-0.116, 255), COEFF(-0.384, 255), COEFF(0.5, 255)    },
		{ COEFF(0.5, 255),    COEFF(-0.445, 255), COEFF(-0.055, 255) },
	};
	static const int bt2020[3][3] = {
		{ COEFF(0.2627, 219),  COEFF(0.6780, 219),  COEFF(0.0593, 219)  },
		{ COEFF(-0.1396, 224), COEFF(-0.3604, 224), COEFF(0.5, 224)     },
		{ COEFF(0.5, 224),     COEFF(-0.4598, 224), COEFF(-0.0402, 224) },
	};
	static const int bt2020_full[3][3] = {
		{ COEFF(0.2627, 255),  COEFF(0.6780, 255),  COEFF(0.0593, 255)  },
		{ COEFF(-0.1396, 255), COEFF(-0.3604, 255), COEFF(0.5, 255)     },
		{ COEFF(0.5, 255),     COEFF(-0.4698, 255), COEFF(-0.0402, 255) },
	};

	bool full = quantization == V4L2_QUANTIZATION_FULL_RANGE;
	unsigned int i;
	const int (*m)[3][3];

	switch (encoding) {
	case V4L2_YCBCR_ENC_601:
	default:
		m = full ? &bt601_full : &bt601;
		break;
	case V4L2_YCBCR_ENC_709:
		m = full ? &rec709_full : &rec709;
		break;
	case V4L2_YCBCR_ENC_BT2020:
		m = full ? &bt2020_full : &bt2020;
		break;
	case V4L2_YCBCR_ENC_SMPTE240M:
		m = full ? &smpte240m_full : &smpte240m;
		break;
	}

	for (i = 0; i < ARRAY_SIZE(*m); ++i)
		memcpy((*matrix)[i], (*m)[i], sizeof((*m)[i]));
}

static void colorspace_rgb2ycbcr(int m[3][3],
				 enum v4l2_quantization quantization,
				 const uint8_t rgb[3], uint8_t ycbcr[3])
{
	bool full = quantization == V4L2_QUANTIZATION_FULL_RANGE;
	unsigned int y_offset = full ? 0 : 16;
	int r, g, b;
	int y, cb, cr;
	int div;

	r = rgb[0] << 4;
	g = rgb[1] << 4;
	b = rgb[2] << 4;

	div = (1 << (8 + 4)) * 255;
	y  = (m[0][0] * r + m[0][1] * g + m[0][2] * b + y_offset * div) / div;
	cb = (m[1][0] * r + m[1][1] * g + m[1][2] * b + 128 * div) / div;
	cr = (m[2][0] * r + m[2][1] * g + m[2][2] * b + 128 * div) / div;

	ycbcr[0] = y;
	ycbcr[1] = cb;
	ycbcr[2] = cr;
}

static void image_colorspace_rgb_to_yuv(const struct image *input,
					struct image *output,
					const struct format_info *format,
					const struct params *params)
{
	int matrix[3][3];
	const uint8_t *idata = input->data;
	uint8_t *odata = output->data;
	unsigned int x;
	unsigned int y;

	colorspace_matrix(params->encoding, params->quantization, &matrix);

	for (y = 0; y < output->height; ++y) {
		for (x = 0; x < output->width; ++x) {
			colorspace_rgb2ycbcr(matrix, params->quantization,
					     &idata[3*x], &odata[3*x]);
		}
		if (format->yuv.xsub == 2) {
			for (x = 1; x < output->width - 1; x += 2) {
				odata[3*x + 1] = (odata[3*(x-1) + 1] + odata[3*(x+1) + 1]) / 2;
				odata[3*x + 2] = (odata[3*(x-1) + 2] + odata[3*(x+1) + 2]) / 2;
			}
		}
		idata += 3 * output->width;
		odata += 3 * output->width;
	}
}

static void image_convert_rgb_to_rgb(const struct image *input,
				     struct image *output,
				     const struct format_info *format)
{
	const uint8_t *idata = input->data;
	uint8_t *odata = output->data;
	unsigned int x;
	unsigned int y;
	uint8_t r, g, b;

	for (y = 0; y < output->height; ++y) {
		for (x = 0; x < output->width; ++x) {
			r = *idata++ & (0xff << (8 - format->rgb.red.length));
			g = *idata++ & (0xff << (8 - format->rgb.green.length));
			b = *idata++ & (0xff << (8 - format->rgb.blue.length));
			*odata++ = r;
			*odata++ = g;
			*odata++ = b;
		}
	}
}

/* -----------------------------------------------------------------------------
 * RGB to HSV conversion (as performed by the Renesas VSP HST)
 */

#define K 4
static uint8_t hst_calc_h(uint8_t r, uint8_t g, uint8_t b)
{
	uint8_t max, min;
	int delta;
	int diff;
	unsigned int third;
	int aux;

	max = max(r, max(g, b));
	min = min(r, min(g, b));
	delta = max - min;

	if (!delta)
		return 0;

	if (max == r) {
		diff = g - b;
		third = 0;
	} else if (max == g) {
		diff = b - r;
		third = 256 * K;
	} else {
		diff = r - g;
		third = 512 * K;
	}

	aux = diff;
	aux *= 128;
	aux *= K;

	/* Round up */
	if (aux >= 0)
		aux += delta - 1;
	else
		aux -= delta - 1;

	aux /= delta;
	aux += third;

	if (diff < 0 && third)
		aux--;

	/*
	 * Divide by three and remove K scaling
	 */
	if (aux > 0)
		aux += (3 * K)/2;
	else
		aux -= (3 * K)/2;
	aux /= 3 * K;

	aux &= 0xff;

	return  aux;
}

static uint8_t hst_calc_s(uint8_t r8, uint8_t g8, uint8_t b8)
{
	uint8_t max, min, delta;
	unsigned int s;

	max = max(r8, max(g8, b8));
	min = min(r8, min(g8, b8));

	delta = max - min;
	if (!delta)
		return 0;

	s = delta * 255;

	/* Special rounding,
	 *
	 * If the minimum RGB component is less then 128 the calculated
	 * S value should be rounded half down else half should be
	 * rounded up.
	 */
	if (min < 128)
		return (s * 2 + max - 1) / max / 2;
	else
		return (s * 2 + max) / max / 2;
}

static uint8_t hst_calc_v(uint8_t r, uint8_t g, uint8_t b)
{
	return max(r, max(g, b));
}

static void hst_rgb_to_hsv(const uint8_t rgb[3], uint8_t hsv[3])
{
	hsv[0] = hst_calc_h(rgb[0], rgb[1], rgb[2]);
	hsv[1] = hst_calc_s(rgb[0], rgb[1], rgb[2]);
	hsv[2] = hst_calc_v(rgb[0], rgb[1], rgb[2]);
}

static void image_rgb_to_hsv(const struct image *input,
			     struct image *output,
			     const struct params *params)
{
	const uint8_t *idata = input->data;
	uint8_t *odata = output->data;
	unsigned int x;
	unsigned int y;

	for (y = 0; y < output->height; ++y) {
		for (x = 0; x < output->width; ++x) {
			hst_rgb_to_hsv(idata, odata);
			idata += 3;
			odata += 3;
		}
	}
}

/* -----------------------------------------------------------------------------
 * Image scaling
 */

static void image_scale_bilinear(const struct image *input, struct image *output)
{
#define _C0(x, y)	(idata[0][((y)*input->width+(x)) * 3])
#define _C1(x, y)	(idata[1][((y)*input->width+(x)) * 3])
#define _C2(x, y)	(idata[2][((y)*input->width+(x)) * 3])
	const uint8_t *idata[3] = { input->data, input->data + 1, input->data + 2 };
	uint8_t *odata = output->data;
	uint8_t c0, c1, c2;
	unsigned int u, v;

	for (v = 0; v < output->height; ++v) {
		double v_input = (double)v / (output->height - 1) * (input->height - 1);
		unsigned int y = floor(v_input);
		double v_ratio = v_input - y;

		for (u = 0; u < output->width; ++u) {
			double u_input = (double)u / (output->width - 1) * (input->width - 1);
			unsigned int x = floor(u_input);
			double u_ratio = u_input - x;

			c0 = (_C0(x, y)   * (1 - u_ratio) + _C0(x+1, y)   * u_ratio) * (1 - v_ratio)
			   + (_C0(x, y+1) * (1 - u_ratio) + _C0(x+1, y+1) * u_ratio) * v_ratio;
			c1 = (_C1(x, y)   * (1 - u_ratio) + _C1(x+1, y)   * u_ratio) * (1 - v_ratio)
			   + (_C1(x, y+1) * (1 - u_ratio) + _C1(x+1, y+1) * u_ratio) * v_ratio;
			c2 = (_C2(x, y)   * (1 - u_ratio) + _C2(x+1, y)   * u_ratio) * (1 - v_ratio)
			   + (_C2(x, y+1) * (1 - u_ratio) + _C2(x+1, y+1) * u_ratio) * v_ratio;

			*odata++ = c0;
			*odata++ = c1;
			*odata++ = c2;
		}
	}
#undef _C0
#undef _C1
#undef _C2
}

static void image_scale(const struct image *input, struct image *output,
			const struct params *params)
{
	image_scale_bilinear(input, output);
}

/* -----------------------------------------------------------------------------
 * Image composing
 */

static void image_compose(const struct image *input, struct image *output,
			  unsigned int num_inputs)
{
	const uint8_t *idata = input->data;
	uint8_t *odata = output->data;
	unsigned int offset = 50;
	unsigned int y;
	unsigned int i;

	memset(odata, 0, output->size);

	for (i = 0; i < num_inputs; ++i) {
		unsigned int dst_offset = (offset * output->width + offset) * 3;

		if (offset >= output->width || offset >= output->height)
			break;

		for (y = 0; y < output->height - offset; ++y)
			memcpy(odata + y * output->width * 3 + dst_offset,
			       idata + y * output->width * 3,
			       (output->width - offset) * 3);

		offset += 50;
	}
}

/* -----------------------------------------------------------------------------
 * Image rotation and flipping
 */

static void image_rotate(const struct image *input, struct image *output)
{
	const uint8_t *idata = input->data;
	uint8_t *odata;
	unsigned int stride = output->width * 3;
	unsigned int x, y;

	odata = output->data + stride - 3;

	for (y = 0; y < input->height; ++y) {
		for (x = 0; x < input->width; ++x) {
			odata[x*stride+0] = *idata++;
			odata[x*stride+1] = *idata++;
			odata[x*stride+2] = *idata++;
		}

		odata -= 3;
	}
}

static void image_flip(const struct image *input, struct image *output,
		       bool hflip, bool vflip)
{
	const uint8_t *idata = input->data;
	uint8_t *odata = output->data;
	unsigned int stride = output->width * 3;
	unsigned int x, y;
	int x_step, y_step;

	if (!hflip) {
		x_step = 3;
		y_step = !vflip ? 0 : -2 * stride;
		odata += !vflip ? 0 : stride * (output->height - 1);
	} else {
		x_step = -3;
		y_step = !vflip ? 2 * stride : 0;
		odata += !vflip ? stride - 3 : stride * output->height - 3;
	}

	for (y = 0; y < output->height; ++y) {
		for (x = 0; x < output->width; ++x) {
			odata[0] = *idata++;
			odata[1] = *idata++;
			odata[2] = *idata++;

			odata += x_step;
		}

		odata += y_step;
	}
}

/* -----------------------------------------------------------------------------
 * Image Cropping
 */

static void image_crop(const struct image *input, const struct image *output,
		       const struct image_rect *crop)
{
	unsigned int offset = (crop->top * input->width + crop->left) * 3;
	const uint8_t *idata = input->data + offset;
	uint8_t *odata = output->data;
	unsigned int y;

	for (y = 0; y < output->height; ++y) {
		memcpy(odata, idata, output->width * 3);
		odata += output->width * 3;
		idata += input->width * 3;
	}
}

/* -----------------------------------------------------------------------------
 * Look Up Table
 */

static int image_lut_1d(const struct image *input, struct image *output,
			const char *filename)
{
	const uint8_t *idata = input->data;
	uint8_t *odata = output->data;
	unsigned int comp_map[3];
	uint8_t c0, c1, c2;
	unsigned int x, y;
	uint8_t lut[1024];
	int ret;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		printf("Unable to open LUT file %s: %s (%d)\n", filename,
		       strerror(errno), errno);
		return -errno;
	}

	ret = file_read(fd, lut, sizeof(lut));
	close(fd);
	if (ret < 0) {
		printf("Unable to read 1D LUT file: %s (%d)\n", strerror(-ret),
		       ret);
		return ret;
	}
	if ((size_t)ret != sizeof(lut)) {
		printf("Invalid 1D LUT file: file too short\n");
		return -ENODATA;
	}

	if (input->format->type == FORMAT_YUV)
		memcpy(comp_map, (unsigned int[3]){ 1, 0, 2 },
		       sizeof(comp_map));
	else
		memcpy(comp_map, (unsigned int[3]){ 2, 1, 0 },
		       sizeof(comp_map));

	for (y = 0; y < input->height; ++y) {
		for (x = 0; x < input->width; ++x) {
			c0 = *idata++;
			c1 = *idata++;
			c2 = *idata++;

			*odata++ = lut[c0*4 + comp_map[0]];
			*odata++ = lut[c1*4 + comp_map[1]];
			*odata++ = lut[c2*4 + comp_map[2]];
		}
	}

	return 0;
}

static int image_lut_3d(const struct image *input, struct image *output,
			const char *filename)
{
	const uint8_t *idata = input->data;
	uint8_t *odata = output->data;
	unsigned int comp_map[3];
	unsigned int x, y;
	uint32_t lut[17*17*17];
	int ret;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		printf("Unable to open 3D LUT file %s: %s (%d)\n", filename,
		       strerror(errno), errno);
		return -errno;
	}

	ret = file_read(fd, lut, sizeof(lut));
	close(fd);
	if (ret < 0) {
		printf("Unable to read 3D LUT file: %s (%d)\n", strerror(-ret),
		       ret);
		return ret;
	}
	if ((size_t)ret != sizeof(lut)) {
		printf("Invalid 3D LUT file: file too short\n");
		return -ENODATA;
	}

	if (input->format->type == FORMAT_YUV)
		memcpy(comp_map, (unsigned int[3]){ 2, 0, 1 },
		       sizeof(comp_map));
	else
		memcpy(comp_map, (unsigned int[3]){ 0, 1, 2 },
		       sizeof(comp_map));

	for (y = 0; y < input->height; ++y) {
		for (x = 0; x < input->width; ++x) {
			double a1_ratio, a2_ratio, a3_ratio;
			unsigned int a1, a2, a3;
			double c0, c1, c2;
			uint8_t c[3];

			c[0] = idata[comp_map[0]];
			c[1] = idata[comp_map[1]];
			c[2] = idata[comp_map[2]];

			a1 = c[0] >> 4;
			a2 = c[1] >> 4;
			a3 = c[2] >> 4;

			/*
			 * Implement the hardware MVS (Max Value Stretch)
			 * behaviour: move the point by one step towards the
			 * upper limit of the grid if we're closer than 0.5 to
			 * that limit.
			 */
			a1_ratio = ((c[0] & 0xf) + (c[0] >= 0xf8 ? 1 : 0)) / 16.;
			a2_ratio = ((c[1] & 0xf) + (c[1] >= 0xf8 ? 1 : 0)) / 16.;
			a3_ratio = ((c[2] & 0xf) + (c[2] >= 0xf8 ? 1 : 0)) / 16.;

#define _LUT(a1, a2, a3, offset)	((lut[(a1)+(a2)*17+(a3)*17*17] >> (offset)) & 0xff)
			c0 = _LUT(a1,   a2,   a3,   16) * (1 - a1_ratio) * (1 - a2_ratio) * (1 - a3_ratio)
			   + _LUT(a1,   a2,   a3+1, 16) * (1 - a1_ratio) * (1 - a2_ratio) * a3_ratio
			   + _LUT(a1,   a2+1, a3,   16) * (1 - a1_ratio) * a2_ratio       * (1 - a3_ratio)
			   + _LUT(a1,   a2+1, a3+1, 16) * (1 - a1_ratio) * a2_ratio       * a3_ratio
			   + _LUT(a1+1, a2,   a3,   16) * a1_ratio       * (1 - a2_ratio) * (1 - a3_ratio)
			   + _LUT(a1+1, a2,   a3+1, 16) * a1_ratio       * (1 - a2_ratio) * a3_ratio
			   + _LUT(a1+1, a2+1, a3,   16) * a1_ratio       * a2_ratio       * (1 - a3_ratio)
			   + _LUT(a1+1, a2+1, a3+1, 16) * a1_ratio       * a2_ratio       * a3_ratio;
			c1 = _LUT(a1,   a2,   a3,    8) * (1 - a1_ratio) * (1 - a2_ratio) * (1 - a3_ratio)
			   + _LUT(a1,   a2,   a3+1,  8) * (1 - a1_ratio) * (1 - a2_ratio) * a3_ratio
			   + _LUT(a1,   a2+1, a3,    8) * (1 - a1_ratio) * a2_ratio       * (1 - a3_ratio)
			   + _LUT(a1,   a2+1, a3+1,  8) * (1 - a1_ratio) * a2_ratio       * a3_ratio
			   + _LUT(a1+1, a2,   a3,    8) * a1_ratio       * (1 - a2_ratio) * (1 - a3_ratio)
			   + _LUT(a1+1, a2,   a3+1,  8) * a1_ratio       * (1 - a2_ratio) * a3_ratio
			   + _LUT(a1+1, a2+1, a3,    8) * a1_ratio       * a2_ratio       * (1 - a3_ratio)
			   + _LUT(a1+1, a2+1, a3+1,  8) * a1_ratio       * a2_ratio       * a3_ratio;
			c2 = _LUT(a1,   a2,   a3,    0) * (1 - a1_ratio) * (1 - a2_ratio) * (1 - a3_ratio)
			   + _LUT(a1,   a2,   a3+1,  0) * (1 - a1_ratio) * (1 - a2_ratio) * a3_ratio
			   + _LUT(a1,   a2+1, a3,    0) * (1 - a1_ratio) * a2_ratio       * (1 - a3_ratio)
			   + _LUT(a1,   a2+1, a3+1,  0) * (1 - a1_ratio) * a2_ratio       * a3_ratio
			   + _LUT(a1+1, a2,   a3,    0) * a1_ratio       * (1 - a2_ratio) * (1 - a3_ratio)
			   + _LUT(a1+1, a2,   a3+1,  0) * a1_ratio       * (1 - a2_ratio) * a3_ratio
			   + _LUT(a1+1, a2+1, a3,    0) * a1_ratio       * a2_ratio       * (1 - a3_ratio)
			   + _LUT(a1+1, a2+1, a3+1,  0) * a1_ratio       * a2_ratio       * a3_ratio;
#undef _LUT

			odata[comp_map[0]] = round(c0);
			odata[comp_map[1]] = round(c1);
			odata[comp_map[2]] = round(c2);

			idata += 3;
			odata += 3;
		}
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Histogram
 */

static void histogram_compute_hgo(const struct image *image, void *histo)
{
	const uint8_t *data = image->data;
	uint8_t comp_min[3] = { 255, 255, 255 };
	uint8_t comp_max[3] = { 0, 0, 0 };
	uint32_t comp_sums[3] = { 0, 0, 0 };
	uint32_t comp_bins[3][64];
	unsigned int comp_map[3];
	unsigned int x, y;
	unsigned int i, j;

	if (image->format->type == FORMAT_YUV)
		memcpy(comp_map, (unsigned int[3]){ 2, 0, 1 }, sizeof(comp_map));
	else
		memcpy(comp_map, (unsigned int[3]){ 0, 1, 2 }, sizeof(comp_map));

	memset(comp_bins, 0, sizeof(comp_bins));

	for (y = 0; y < image->height; ++y) {
		for (x = 0; x < image->width; ++x) {
			for (i = 0; i < 3; ++i) {
				comp_min[i] = min(*data, comp_min[i]);
				comp_max[i] = max(*data, comp_max[i]);
				comp_sums[i] += *data;
				comp_bins[i][*data >> 2]++;
				data++;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(comp_min); ++i) {
		*(uint8_t *)histo++ = comp_min[comp_map[i]];
		*(uint8_t *)histo++ = 0;
		*(uint8_t *)histo++ = comp_max[comp_map[i]];
		*(uint8_t *)histo++ = 0;
	}

	for (i = 0; i < ARRAY_SIZE(comp_sums); ++i) {
		*(uint32_t *)histo = comp_sums[comp_map[i]];
		histo += 4;
	}

	for (i = 0; i < ARRAY_SIZE(comp_bins); ++i) {
		for (j = 0; j < ARRAY_SIZE(comp_bins[i]); ++j) {
			*(uint32_t *)histo = comp_bins[comp_map[i]][j];
			histo += 4;
		}
	}
}

static void histogram_compute_hgt(const struct image *image, void *histo,
				  const uint8_t hue_areas[12])
{
	const uint8_t *data = image->data;
	uint8_t hue_indices[256];
	uint8_t smin = 255, smax = 0;
	uint32_t sum = 0;
	uint32_t hist[6][32];
	unsigned int hue_index;
	unsigned int x, y;
	unsigned int h;

	memset(hist, 0, sizeof(hist));

	/*
	 * Precompute the hue region index for all possible hue values. The
	 * index starts at 0 for the overlapping region between hue areas 5
	 * and 0.
	 *
	 * Hue area 0 can wrap around the H value space (for example include
	 * values greater than 240 and lower than 30) depending on whether 0L
	 * is higher than 5U or lower than 0U.
	 *
	 *              Area 0       Area 1       Area 2       Area 3       Area 4       Area 5       Area 0
	 *             ________     ________     ________     ________     ________     ________     _____
	 *        \   /|      |\   /|      |\   /|      |\   /|      |\   /|      |\   /|      |\   /|
	 *         \ / |      | \ / |      | \ / |      | \ / |      | \ / |      | \ / |      | \ / |
	 *          X  |      |  X  |      |  X  |      |  X  |      |  X  |      |  X  |      |  X  |
	 *         / \ |      | / \ |      | / \ |      | / \ |      | / \ |      | / \ |      | / \ |
	 *        /   \|      |/   \|      |/   \|      |/   \|      |/   \|      |/   \|      |/   \|
	 *       5U   0L      0U   1L      1U   2L      2U   3L      3U   4L      4U   5L      5U   0L
	 * RI   ]  0  ]   1  ]  2  ]   3  ]  4  ]   5  ]  6  ]   7  ]  8  ]   9  ]  10 ]  11  ]  0  ]   1
	 *
	 * NW  ..255><0.................................Hue Value..............................255><0.......
	 * W   .......255><0.................................Hue Value..............................255><0..
	 *
	 * RI: Hue region index
	 * W:  Area 0 wraps around the hue value space
	 * NW: Area 0 doesn't wrap around the hue value space
	 *
	 * Boundary values are included in the lower-value region.
	 */

	/*
	 * The first hue value after 5U falls in region index 0. However, if
	 * 5U == 0L, areas 5 and 0 don't overlap, region index 0 is empty and
	 * the first hue value falls in region index 1.
	 *
	 * Process the ]5U, 255] range first, followed by the [0, 5U] range.
	 */
	hue_index = hue_areas[11] == hue_areas[0] ? 1 : 0;

	for (h = hue_areas[11] + 1; h <= 255; ++h) {
		hue_indices[h] = hue_index;

		if (h == hue_areas[hue_index])
			hue_index++;
	}

	for (h = 0; h <= hue_areas[11]; ++h) {
		hue_indices[h] = hue_index;

		while (h == hue_areas[hue_index])
			hue_index++;
	}

	/* Compute the histogram */
	for (y = 0; y < image->height; ++y) {
		for (x = 0; x < image->width; ++x) {
			uint8_t rgb[3], hsv[3];
			unsigned int hist_n;

			rgb[0] = *data++;
			rgb[1] = *data++;
			rgb[2] = *data++;

			hst_rgb_to_hsv(rgb, hsv);

			smin = min(smin, hsv[1]);
			smax = max(smax, hsv[1]);
			sum += hsv[1];

			/* Compute the coordinates of the histogram bucket */
			hist_n = hsv[1] / 8;
			hue_index = hue_indices[hsv[0]];

			/*
			 * Attribute the H value to area(s). If the H value is
			 * inside one of the non-overlapping regions (hue_index
			 * is odd) the max weight (16) is attributed to the
			 * corresponding area. Otherwise the weight is split
			 * between the two adjacent areas based on the distance
			 * between the H value and the areas boundaries.
			 */
			if (hue_index % 2) {
				hist[hue_index/2][hist_n] += 16;
			} else {
				unsigned int dist, width, weight;
				unsigned int hue_index1, hue_index2;
				int hue1, hue2;

				hue_index1 = hue_index ? hue_index - 1 : 11;
				hue_index2 = hue_index;

				hue1 = hue_areas[hue_index1];
				hue2 = hue_areas[hue_index2];

				/*
				 * Calculate the width to be attributed to the
				 * left area. Handle the wraparound through
				 * modulo arithmetic.
				 */
				dist = (hue2 - hsv[0]) & 255;
				width = (hue2 - hue1) & 255;
				weight = div_round_up(dist * 16, width);

				/* Split weight between the two areas */
				hist[hue_index1/2][hist_n] += weight;
				hist[hue_index2/2][hist_n] += 16 - weight;
			}
		}
	}

	/* Format the data buffer */

	/* Min/Max Value of S Components */
	*(uint8_t *)histo++ = smin;
	*(uint8_t *)histo++ = 0;
	*(uint8_t *)histo++ = smax;
	*(uint8_t *)histo++ = 0;

	/* Sum of S Components */
	*(uint32_t *)histo = sum;
	histo += 4;

	/* Weighted Frequency of Hue Area-m and Saturation Area-n */
	for (x = 0; x < 6; x++) {
		for (y = 0; y < 32; y++) {
			*(uint32_t *)histo = hist[x][y];
			histo += 4;
		}
	}
}

#define HISTOGRAM_HGO_SIZE	(3*4 + 3*4 + 3*64*4)
#define HISTOGRAM_HGT_SIZE	(1*4 + 1*4 + 6*32*4)

static int histogram(const struct image *image, const char *filename,
		     enum histogram_type type, const uint8_t hgt_hue_areas[12])
{
	/*
	 * Data must be big enough to contain the largest possible histogram.
	 *
	 * HGO: (3 + 3 + 3*64) * 4 = 792
	 * HGT: (1 + 1 + 6*32) * 4 = 776
	 */
	uint8_t data[HISTOGRAM_HGO_SIZE];
	size_t size;
	int ret;
	int fd;

	switch (type) {
	case HISTOGRAM_HGO:
		size = HISTOGRAM_HGO_SIZE;
		histogram_compute_hgo(image, data);
		break;
	case HISTOGRAM_HGT:
		size = HISTOGRAM_HGT_SIZE;
		histogram_compute_hgt(image, data, hgt_hue_areas);
		break;
	default:
		printf("Unknown histogram type\n");
		return -EINVAL;
	}

	fd = open(filename, O_WRONLY | O_CREAT,
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		printf("Unable to open histogram file %s: %s (%d)\n", filename,
		       strerror(errno), errno);
		return -errno;
	}

	ret = file_write(fd, data, size);
	if (ret < 0)
		printf("Unable to write histogram: %s (%d)\n",
		       strerror(-ret), ret);
	else
		ftruncate(fd, size);

	close(fd);
	return ret;
}

/* -----------------------------------------------------------------------------
 * Processing pipeline
 */

static int process(const struct options *options)
{
	struct image *input = NULL;
	struct image *output = NULL;
	unsigned int output_width;
	unsigned int output_height;
	int ret = 0;

	/* Read the input image */
	input = image_read(options->input_filename);
	if (!input) {
		ret = -EINVAL;
		goto done;
	}

	/* Convert colorspace */
	if (options->input_format->type == FORMAT_YUV) {
		struct image *yuv;

		yuv = image_new(format_by_name("YUV24"), input->width,
				input->height);
		if (!yuv) {
			ret = -ENOMEM;
			goto done;
		}

		image_colorspace_rgb_to_yuv(input, yuv, options->input_format,
					    &options->params);
		image_delete(input);
		input = yuv;
	} else if (options->input_format->rgb.bpp < 24) {
		struct image *rgb;

		rgb = image_new(format_by_name("RGB24"), input->width,
				input->height);
		if (!rgb) {
			ret = -ENOMEM;
			goto done;
		}

		image_convert_rgb_to_rgb(input, rgb, options->input_format);
		image_delete(input);
		input = rgb;
	}

	/* Crop */
	if (options->crop) {
		struct image *cropped;

		cropped = image_new(input->format, options->inputcrop.width,
				options->inputcrop.height);
		if (!cropped) {
			ret = -ENOMEM;
			goto done;
		}

		image_crop(input, cropped, &options->inputcrop);
		image_delete(input);
		input = cropped;
	}

	/* Scale */
	if (options->output_width && options->output_height) {
		output_width = options->output_width;
		output_height = options->output_height;
	} else {
		output_width = input->width;
		output_height = input->height;
	}

	if (options->rotate)
		swap(output_width, output_height);

	if (input->width != output_width ||
	    input->height != output_height) {
		struct image *scaled;

		scaled = image_new(input->format, output_width, output_height);
		if (!scaled) {
			ret = -ENOMEM;
			goto done;
		}

		image_scale(input, scaled, &options->params);
		image_delete(input);
		input = scaled;
	}

	/* Compose */
	if (options->compose) {
		struct image *composed;

		composed = image_new(input->format, input->width, input->height);
		if (!composed) {
			ret = -ENOMEM;
			goto done;
		}

		image_compose(input, composed, options->compose);
		image_delete(input);
		input = composed;
	}

	/* Look-up tables */
	if (options->lut_filename) {
		struct image *lut;

		lut = image_new(input->format, input->width, input->height);
		if (!lut) {
			ret = -ENOMEM;
			goto done;
		}

		image_lut_1d(input, lut, options->lut_filename);
		image_delete(input);
		input = lut;
	}

	if (options->clu_filename) {
		struct image *clu;

		clu = image_new(input->format, input->width, input->height);
		if (!clu) {
			ret = -ENOMEM;
			goto done;
		}

		image_lut_3d(input, clu, options->clu_filename);
		image_delete(input);
		input = clu;
	}

	/* Compute the histogram */
	if (options->histo_filename) {
		ret = histogram(input, options->histo_filename, options->histo_type,
				options->histo_areas);
		if (ret)
			goto done;
	}

	/* Rotation and flipping */
	if (options->rotate) {
		struct image *rotated;

		rotated = image_new(input->format, input->height, input->width);
		if (!rotated) {
			ret = -ENOMEM;
			goto done;
		}

		image_rotate(input, rotated);
		image_delete(input);
		input = rotated;
	}

	if (options->hflip || options->vflip) {
		struct image *flipped;

		flipped = image_new(input->format, input->width, input->height);
		if (!flipped) {
			ret = -ENOMEM;
			goto done;
		}

		image_flip(input, flipped, options->hflip, options->vflip);
		image_delete(input);
		input = flipped;
	}

	/* Format the output */
	if (input->format->type != options->output_format->type &&
	    input->format->type != FORMAT_RGB) {
		printf("Format conversion with non-RGB input not supported\n");
		ret = -EINVAL;
		goto done;
	}

	if (input->format->type != options->output_format->type) {
		const struct format_info *format;
		struct image *converted;

		if (options->output_format->type == FORMAT_YUV)
			format = format_by_name("YUV24");
		else
			format = format_by_name("HSV24");

		converted = image_new(format, input->width, input->height);
		if (!converted) {
			ret = -ENOMEM;
			goto done;
		}

		if (options->output_format->type == FORMAT_YUV)
			image_colorspace_rgb_to_yuv(input, converted, format,
						    &options->params);
		else
			image_rgb_to_hsv(input, converted, &options->params);

		image_delete(input);
		input = converted;
	}

	output = image_new(options->output_format, input->width, input->height);
	if (!output) {
		ret = -ENOMEM;
		goto done;
	}

	switch (output->format->type) {
	case FORMAT_RGB:
		switch (output->format->rgb.bpp) {
		case 8:
			image_format_rgb8(input, output, &options->params);
			break;
		case 16:
			image_format_rgb16(input, output, &options->params);
			break;
		case 24:
			image_format_rgb24(input, output, &options->params);
			break;
		case 32:
			image_format_rgb32(input, output, &options->params);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;

	case FORMAT_HSV:
		switch (output->format->hsv.bpp) {
		case 24:
			image_format_hsv24(input, output, &options->params);
			break;
		case 32:
			image_format_hsv32(input, output, &options->params);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;

	case FORMAT_YUV:
		switch (output->format->yuv.num_planes) {
		case 1:
			image_format_yuv_packed(input, output, &options->params);
			break;
		case 2:
		case 3:
			image_format_yuv_planar(input, output, &options->params);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	}

	if (ret < 0) {
		printf("Output formatting failed\n");
		goto done;
	}

	/* Write the output image */
	if (options->output_filename) {
		ret = image_write(output, options->output_filename);
		if (ret)
			goto done;
	}

	ret = 0;

done:
	image_delete(input);
	image_delete(output);
	return ret;
}

/* -----------------------------------------------------------------------------
 * Usage, argument parsing and main
 */

static void usage(const char *argv0)
{
	printf("Usage: %s [options] <infile.pnm>\n\n", argv0);
	printf("Convert the input image stored in <infile> in PNM format to\n");
	printf("the target format and resolution and store the resulting\n");
	printf("image in raw binary form\n\n");
	printf("Supported options:\n");
	printf("-a, --alpha value		Set the alpha value. Valid syntaxes are floating\n");
	printf("				point values ([0.0 - 1.0]), fixed point values ([0-255])\n");
	printf("				or percentages ([0%% - 100%%]). Defaults to 1.0\n");
	printf("-c, --compose n			Compose n copies of the image offset by (50,50) over a black background\n");
	printf("-C, --no-chroma-average		Disable chroma averaging for odd pixels on output\n");
	printf("    --crop (X,Y)/WxH		Crop the input image\n");
	printf("-e, --encoding enc		Set the YCbCr encoding method. Valid values are\n");
	printf("				BT.601, REC.709, BT.2020 and SMPTE240M\n");
	printf("-f, --format format		Set the output image format\n");
	printf("				Defaults to RGB24 if not specified\n");
	printf("				Use -f help to list the supported formats\n");
	printf("-h, --help			Show this help screen\n");
	printf("    --hflip			Flip the image horizontally\n");
	printf("-H, --histogram file		Compute histogram on the output image and store it to file\n");
	printf("    --histogram-areas areas	Configure the HGT histogram hue areas.\n");
	printf("				Must be specified for HGT histograms.\n");
	printf("				Areas are expressed as a comma-separated list of\n");
	printf("				lower and upper boundaries for areas 0 to 5 ([0-255])\n");
	printf("    --histogram-type type	Set the histogram type. Valid values are hgo and hgt.\n");
	printf("				Defaults to hgo if not specified\n");
	printf("-i, --in-format format		Set the input image format\n");
	printf("				Defaults to RGB24 if not specified\n");
	printf("				Use -i help to list the supported formats\n");
	printf("-l, --lut file			Apply 1D Look Up Table from file\n");
	printf("-L, --clu file			Apply 3D Look Up Table from file\n");
	printf("-o, --output file		Store the output image to file\n");
	printf("-q, --quantization q		Set the quantization method. Valid values are\n");
	printf("				limited or full\n");
	printf("-r, --rotate			Rotate the image clockwise by 90°\n");
	printf("-s, --size WxH			Set the output image size\n");
	printf("				Defaults to the input size if not specified\n");
	printf("    --vflip			Flip the image vertically\n");
}

static void list_formats(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(format_info); i++)
		printf("%s\n", format_info[i].name);
}

#define OPT_HFLIP		256
#define OPT_VFLIP		257
#define OPT_CROP		258
#define OPT_HISTOGRAM_TYPE	259
#define OPT_HISTOGRAM_AREAS	260

static struct option opts[] = {
	{"alpha", 1, 0, 'a'},
	{"clu", 1, 0, 'L'},
	{"compose", 1, 0, 'c'},
	{"crop", 1, 0, OPT_CROP},
	{"encoding", 1, 0, 'e'},
	{"format", 1, 0, 'f'},
	{"help", 0, 0, 'h'},
	{"hflip", 0, 0, OPT_HFLIP},
	{"histogram", 1, 0, 'H'},
	{"histogram-areas", 1, 0, OPT_HISTOGRAM_AREAS},
	{"histogram-type", 1, 0, OPT_HISTOGRAM_TYPE},
	{"in-format", 1, 0, 'i'},
	{"lut", 1, 0, 'l'},
	{"no-chroma-average", 1, 0, 'C'},
	{"output", 1, 0, 'o'},
	{"quantization", 1, 0, 'q'},
	{"rotate", 0, 0, 'r'},
	{"size", 1, 0, 's'},
	{"vflip", 0, 0, OPT_VFLIP},
	{0, 0, 0, 0}
};

static void parser_print_error(const char *p, const char *end)
{
	int pos;

	pos = end - p + 1;

	if (pos < 0)
		pos = 0;
	if (pos > (int) strlen(p) + 1)
		pos = strlen(p) + 1;

	printf("\n");
	printf(" %s\n", p);
	printf(" %*s\n", pos, "^");
}

static int parse_crop(struct image_rect *crop, const char *string)
{
	/* (X,Y)/WxH */
	const char *p = string;
	char *endptr;
	int value;

	if (*p != '(') {
		printf("Invalid crop format, expected '('\n");
		goto error;
	}

	p++;
	crop->left = strtol(p, &endptr, 10);
	if (p == endptr) {
		printf("Invalid crop format, expected x coordinate\n");
		goto error;
	}

	p = endptr;
	if (*p != ',') {
		printf("Invalid crop format, expected ','\n");
		goto error;
	}

	p++;
	crop->top = strtol(p, &endptr, 10);
	if (p == endptr) {
		printf("Invalid crop format, expected y coordinate\n");
		goto error;
	}

	p = endptr;
	if (*p != ')') {
		printf("Invalid crop format, expected ')'\n");
		goto error;
	}

	p++;
	if (*p != '/') {
		printf("Invalid crop format, expected '/'\n");
		goto error;
	}

	p++;
	value = strtol(p, &endptr, 10);
	if (p == endptr) {
		printf("Invalid crop format, expected width\n");
		goto error;
	}

	if (value < 0) {
		printf("width must be positive\n");
		goto error;
	}

	crop->width = value;

	p = endptr;
	if (*p != 'x') {
		printf("Invalid crop format, expected 'x'\n");
		goto error;
	}

	p++;
	value = strtol(p, &endptr, 10);
	if (p == endptr) {
		printf("Invalid crop format, expected height\n");
		goto error;
	}

	if (value < 0) {
		printf("height must be positive\n");
		goto error;
	}

	crop->height = value;

	p = endptr;
	if (*p != 0) {
		printf("Invalid crop format, garbage at end of input\n");
		goto error;
	}

	return 0;

error:
	parser_print_error(string, p);
	return 1;
}

static int parse_args(struct options *options, int argc, char *argv[])
{
	char *endptr;
	int c;

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	memset(options, 0, sizeof(*options));
	options->input_format = format_by_name("RGB24");
	options->output_format = format_by_name("RGB24");
	options->params.alpha = 255;
	options->params.encoding = V4L2_YCBCR_ENC_601;
	options->params.quantization = V4L2_QUANTIZATION_LIM_RANGE;
	options->histo_type = HISTOGRAM_HGO;

	opterr = 0;
	while ((c = getopt_long(argc, argv, "a:c:Ce:f:hH:i:l:L:o:q:rs:", opts, NULL)) != -1) {

		switch (c) {
		case 'a': {
			int alpha;

			if (strchr(optarg, '.')) {
				alpha = strtod(optarg, &endptr) * 255;
				if (*endptr != 0)
					alpha = -1;
			} else {
				alpha = strtoul(optarg, &endptr, 10);
				if (*endptr == '%')
					alpha = alpha * 255 / 100;
				else if (*endptr != 0)
					alpha = -1;
			}

			if (alpha < 0 || alpha > 255) {
				printf("Invalid alpha value '%s'\n", optarg);
				return 1;
			}

			options->params.alpha = alpha;
			break;
		}

		case 'c':
			  options->compose = strtoul(optarg, &endptr, 10);
			  if (*endptr != 0) {
				printf("Invalid compose value '%s'\n", optarg);
				return 1;
			  }
			  break;

		case 'C':
			  options->params.no_chroma_average = true;
			  break;

		case 'e':
			if (!strcmp(optarg, "BT.601")) {
				options->params.encoding = V4L2_YCBCR_ENC_601;
			} else if (!strcmp(optarg, "REC.709")) {
				options->params.encoding = V4L2_YCBCR_ENC_709;
			} else if (!strcmp(optarg, "BT.2020")) {
				options->params.encoding = V4L2_YCBCR_ENC_BT2020;
			} else if (!strcmp(optarg, "SMPTE240M")) {
				options->params.encoding = V4L2_YCBCR_ENC_SMPTE240M;
			} else {
				printf("Invalid encoding value '%s'\n", optarg);
				return 1;
			}
			break;

		case 'f':
			if (!strcmp("help", optarg)) {
				list_formats();
				return 1;
			}

			options->output_format = format_by_name(optarg);
			if (!options->output_format) {
				printf("Unsupported output format '%s'\n", optarg);
				return 1;
			}

			break;

		case 'h':
			usage(argv[0]);
			exit(0);
			break;

		case 'H':
			options->histo_filename = optarg;
			break;

		case 'i':
			if (!strcmp("help", optarg)) {
				list_formats();
				return 1;
			}

			options->input_format = format_by_name(optarg);
			if (!options->input_format) {
				printf("Unsupported input format '%s'\n", optarg);
				return 1;
			}
			break;

		case 'l':
			options->lut_filename = optarg;
			break;

		case 'L':
			options->clu_filename = optarg;
			break;

		case 'o':
			options->output_filename = optarg;
			break;

		case 'q':
			if (!strcmp(optarg, "limited")) {
				options->params.quantization = V4L2_QUANTIZATION_LIM_RANGE;
			} else if (!strcmp(optarg, "full")) {
				options->params.quantization = V4L2_QUANTIZATION_FULL_RANGE;
			} else {
				printf("Invalid quantization value '%s'\n", optarg);
				return 1;
			}
			break;

			break;

		case 'r':
			options->rotate = true;
			break;

		case 's':
			options->output_width = strtol(optarg, &endptr, 10);
			if (*endptr != 'x' || endptr == optarg) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}

			options->output_height = strtol(endptr + 1, &endptr, 10);
			if (*endptr != 0) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			break;

		case OPT_HFLIP:
			options->hflip = true;
			break;

		case OPT_VFLIP:
			options->vflip = true;
			break;

		case OPT_CROP:
			if (parse_crop(&options->inputcrop, optarg))
				return 1;

			if (options->inputcrop.left < 0 || options->inputcrop.top < 0) {
				printf("Invalid crop rectangle '%s': coordinates must be positive\n",
				       optarg);
				return 1;
			}

			options->crop = true;
			break;

		case OPT_HISTOGRAM_TYPE:
			if (!strcmp(optarg, "hgo")) {
				options->histo_type = HISTOGRAM_HGO;
			} else if (!strcmp(optarg, "hgt")) {
				options->histo_type = HISTOGRAM_HGT;
			} else {
				printf("Invalid histogram type '%s'\n", optarg);
				return 1;
			}
			break;

		case OPT_HISTOGRAM_AREAS: {
			int matched;

			matched = sscanf(optarg, "%hhu,%hhu,%hhu,%hhu,%hhu,%hhu,%hhu,%hhu,%hhu,%hhu,%hhu,%hhu",
					 &options->histo_areas[0], &options->histo_areas[1],
					 &options->histo_areas[2], &options->histo_areas[3],
					 &options->histo_areas[4], &options->histo_areas[5],
					 &options->histo_areas[6], &options->histo_areas[7],
					 &options->histo_areas[8], &options->histo_areas[9],
					 &options->histo_areas[10], &options->histo_areas[11]);
			if (matched != 12) {
				printf("Invalid hgt hue areas '%s'\n", optarg);
				return 1;
			}
			break;
		}

		default:
			printf("Invalid option -%c\n", c);
			printf("Run %s -h for help.\n", argv[0]);
			return 1;
		}
	}

	if (optind != argc - 1) {
		usage(argv[0]);
		return 1;
	}

	options->input_filename = argv[optind];

	return 0;
}

int main(int argc, char *argv[])
{
	struct options options;
	int ret;

	ret = parse_args(&options, argc, argv);
	if (ret)
		return ret;

	ret = process(&options);
	if (ret)
		return 1;

	return 0;
}
