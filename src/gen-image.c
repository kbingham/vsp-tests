/*
 * Copyright (C) 2016 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

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
	bool is_yuv;
	struct format_rgb_info rgb;
	struct format_yuv_info yuv;
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
};

struct options {
	const char *input_filename;
	const char *output_filename;
	const char *histo_filename;
	const char *clu_filename;
	const char *lut_filename;

	const struct format_info *output_format;
	unsigned int output_height;
	unsigned int output_width;

	bool process_yuv;
	unsigned int compose;
	struct params params;
};

/* -----------------------------------------------------------------------------
 * Format information
 */

#define MAKE_RGB_INFO(rl, ro, gl, go, bl, bo, al, ao) \
	.red = { (rl), (ro) }, .green = { (gl), (go) }, \
	.blue = { (bl), (bo) }, .alpha = { (al), (ao) }

static const struct format_info format_info[] = {
	/*
	 * The alpha channel maps to the X (don't care) bits for the XRGB
	 * formats.
	 */
	{ "RGB332",	false, .rgb = { 8,  MAKE_RGB_INFO(3, 5, 3, 2, 2, 0, 0, 0) } },
	{ "ARGB444",	false, .rgb = { 16, MAKE_RGB_INFO(4, 8, 4, 4, 4, 0, 4, 12) } },
	{ "XRGB444",	false, .rgb = { 16, MAKE_RGB_INFO(4, 8, 4, 4, 4, 0, 4, 12) } },
	{ "ARGB555",	false, .rgb = { 16, MAKE_RGB_INFO(5, 10, 5, 5, 5, 0, 1, 15) } },
	{ "XRGB555",	false, .rgb = { 16, MAKE_RGB_INFO(5, 10, 5, 5, 5, 0, 1, 15) } },
	{ "RGB565",	false, .rgb = { 16, MAKE_RGB_INFO(5, 11, 6, 5, 5, 0, 0, 0) } },
	{ "BGR24",	false, .rgb = { 24, MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 0, 0) } },
	{ "RGB24",	false, .rgb = { 24, MAKE_RGB_INFO(8, 0, 8, 8, 8, 16, 0, 0) } },
	{ "ABGR32",	false, .rgb = { 32, MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 8, 24) } },
	{ "XBGR32",	false, .rgb = { 32, MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 8, 24) } },
	{ "ARGB32",	false, .rgb = { 32, MAKE_RGB_INFO(8, 8, 8, 16, 8, 24, 8, 0) } },
	{ "XRGB32",	false, .rgb = { 32, MAKE_RGB_INFO(8, 8, 8, 16, 8, 24, 8, 0) } },
	{ "UYVY",	true,  .yuv = { 1, YUV_YCbCr | YUV_CY, 2, 1 } },
	{ "VYUY",	true,  .yuv = { 1, YUV_YCrCb | YUV_CY, 2, 1 } },
	{ "YUYV",	true,  .yuv = { 1, YUV_YCbCr | YUV_YC, 2, 1 } },
	{ "YVYU",	true,  .yuv = { 1, YUV_YCrCb | YUV_YC, 2, 1 } },
	{ "NV12M",	true,  .yuv = { 2, YUV_YCbCr, 2, 2 } },
	{ "NV21M",	true,  .yuv = { 2, YUV_YCrCb, 2, 2 } },
	{ "NV16M",	true,  .yuv = { 2, YUV_YCbCr, 2, 1 } },
	{ "NV61M",	true,  .yuv = { 2, YUV_YCrCb, 2, 1 } },
	{ "YUV420M",	true,  .yuv = { 3, YUV_YCbCr, 2, 2 } },
	{ "YVU420M",	true,  .yuv = { 3, YUV_YCrCb, 2, 2 } },
	{ "YUV422M",	true,  .yuv = { 3, YUV_YCbCr, 2, 1 } },
	{ "YVU422M",	true,  .yuv = { 3, YUV_YCrCb, 2, 1 } },
	{ "YUV444M",	true,  .yuv = { 3, YUV_YCbCr, 1, 1 } },
	{ "YVU444M",	true,  .yuv = { 3, YUV_YCrCb, 1, 1 } },
	{ "YUV24",	true,  .yuv = { 1, YUV_YCbCr | YUV_YC, 1, 1 } },
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

	if (format->is_yuv)
		image->size = image->width * image->height
			     * (8 + 2 * 8 / format->yuv.xsub / format->yuv.ysub)
			     / 8;
	else
		image->size = image->width * image->height
			     * format->rgb.bpp / 8;

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
			o_c[2*x + u_offset] = (idata[3*x + 1] + idata[3*x + 4]) / 2;
			o_c[2*x + v_offset] = (idata[3*x + 2] + idata[3*x + 5]) / 2;
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
		if (xsub == 1) {
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
			colorspace_rgb2ycbcr(matrix, params->quantization, idata, odata);
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

	if (input->format->is_yuv)
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

	if (input->format->is_yuv)
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

static void histogram_compute(const struct image *image, void *histo)
{
	const uint8_t *data = image->data;
	uint8_t comp_min[3] = { 255, 255, 255 };
	uint8_t comp_max[3] = { 0, 0, 0 };
	uint32_t comp_sums[3] = { 0, 0, 0 };
	uint32_t comp_bins[3][64];
	unsigned int comp_map[3];
	unsigned int x, y;
	unsigned int i, j;

	if (image->format->is_yuv)
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

static int histogram(const struct image *image, const char *filename)
{
	uint8_t data[3*4 + 3*4 + 3*64*4];
	int ret;
	int fd;

	histogram_compute(image, data);

	fd = open(filename, O_WRONLY | O_CREAT,
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		printf("Unable to open histogram file %s: %s (%d)\n", filename,
		       strerror(errno), errno);
		return -errno;
	}

	ret = file_write(fd, data, sizeof(data));
	if (ret < 0)
		printf("Unable to write histogram: %s (%d)\n",
		       strerror(-ret), ret);
	else
		ftruncate(fd, sizeof(data));

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
	if (options->process_yuv) {
		struct image *yuv;

		yuv = image_new(format_by_name("YUV24"), input->width,
				input->height);
		if (!yuv) {
			ret = -ENOMEM;
			goto done;
		}

		image_colorspace_rgb_to_yuv(input, yuv, &options->params);
		image_delete(input);
		input = yuv;
	}

	/* Scale */
	if (options->output_width && options->output_height) {
		output_width = options->output_width;
		output_height = options->output_height;
	} else {
		output_width = input->width;
		output_height = input->height;
	}

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
		ret = histogram(input, options->histo_filename);
		if (ret)
			goto done;
	}

	/* Format the output */
	if (input->format->is_yuv && !options->output_format->is_yuv) {
		printf("RGB output with YUV processing not supported\n");
		ret = -EINVAL;
		goto done;
	}

	if (!input->format->is_yuv && options->output_format->is_yuv) {
		const struct format_info *format = format_by_name("YUV24");
		struct image *converted;

		converted = image_new(format, input->width, input->height);
		if (!converted) {
			ret = -ENOMEM;
			goto done;
		}

		image_colorspace_rgb_to_yuv(input, converted, &options->params);
		image_delete(input);
		input = converted;
	}

	output = image_new(options->output_format, input->width, input->height);
	if (!output) {
		ret = -ENOMEM;
		goto done;
	}

	if (output->format->is_yuv) {
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
	} else {
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
	printf("-a, --alpha value	Set the alpha value. Valid syntaxes are floating\n");
	printf("			point values ([0.0 - 1.0]), fixed point values ([0-255])\n");
	printf("			or percentages ([0%% - 100%%]). Defaults to 1.0\n");
	printf("-c, --compose n		Compose n copies of the image offset by (50,50) over a black background\n");
	printf("-e, --encoding enc	Set the YCbCr encoding method. Valid values are\n");
	printf("			BT.601, REC.709, BT.2020 and SMPTE240M\n");
	printf("-f, --format format	Set the output image format\n");
	printf("			Defaults to RGB24 if not specified\n");
	printf("			Use -f help to list the supported formats\n");
	printf("-h, --help		Show this help screen\n");
	printf("-H, --histogram file	Compute histogram on the output image and store it to file\n");
	printf("-l, --lut file		Apply 1D Look Up Table from file\n");
	printf("-L, --clu file		Apply 3D Look Up Table from file\n");
	printf("-o, --output file	Store the output image to file\n");
	printf("-q, --quantization q	Set the quantization method. Valid values are\n");
	printf("			limited or full\n");
	printf("-s, --size WxH		Set the output image size\n");
	printf("			Defaults to the input size if not specified\n");
	printf("-y, --yuv		Perform all processing in YUV space\n");
}

static void list_formats(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(format_info); i++)
		printf("%s\n", format_info[i].name);
}

static struct option opts[] = {
	{"alpha", 1, 0, 'a'},
	{"clu", 1, 0, 'L'},
	{"compose", 1, 0, 'c'},
	{"encoding", 1, 0, 'e'},
	{"format", 1, 0, 'f'},
	{"help", 0, 0, 'h'},
	{"histogram", 1, 0, 'H'},
	{"lut", 1, 0, 'l'},
	{"output", 1, 0, 'o'},
	{"quantization", 1, 0, 'q'},
	{"size", 1, 0, 's'},
	{"yuv", 0, 0, 'y'},
	{0, 0, 0, 0}
};

static int parse_args(struct options *options, int argc, char *argv[])
{
	char *endptr;
	int c;

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	memset(options, 0, sizeof(*options));
	options->output_format = format_by_name("RGB24");
	options->params.alpha = 255;
	options->params.encoding = V4L2_YCBCR_ENC_601;
	options->params.quantization = V4L2_QUANTIZATION_LIM_RANGE;

	opterr = 0;
	while ((c = getopt_long(argc, argv, "a:c:e:f:hH:l:L:o:q:s:y", opts, NULL)) != -1) {

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

		case 'y':
			options->process_yuv = true;
			break;

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
