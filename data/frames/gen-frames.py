#!/usr/bin/python

import copy
import glob
import gzip
import itertools
import operator
import re
import struct
import sys

class FormatRGB:
	def __init__(self, name, mapping):
		self.name = name
		self.mapping = copy.copy(mapping)
		self.depth = sum(v[1] for v in mapping.values())

		if self.mapping.has_key('a'):
			self.alpha_bits = self.mapping['a'][1]
		elif self.mapping.has_key('x'):
			self.alpha_bits = self.mapping['x'][1]
		else:
			self.alpha_bits = 0

	def alpha(self, alpha):
		if self.mapping.has_key('a'):
			return (alpha >> (8 - self.mapping['a'][1])) << self.mapping['a'][0]
		elif self.mapping.has_key('x'):
			return (alpha >> (8 - self.mapping['x'][1])) << self.mapping['x'][0]
		else:
			return 0

	def generate_8(self, width, height, rgb):
		output = []
		for i in xrange(width * height):
			r, g, b = rgb[i]
			pixel = ((r >> (8 - self.mapping['r'][1])) << self.mapping['r'][0]) \
			      | ((g >> (8 - self.mapping['g'][1])) << self.mapping['g'][0]) \
			      | ((b >> (8 - self.mapping['b'][1])) << self.mapping['b'][0])
			output.append(struct.pack('B', pixel))
		return ''.join(output)

	def generate_16(self, width, height, rgb, alpha):
		output = []
		for i in xrange(width * height):
			r, g, b = rgb[i]
			pixel = ((r >> (8 - self.mapping['r'][1])) << self.mapping['r'][0]) \
			      | ((g >> (8 - self.mapping['g'][1])) << self.mapping['g'][0]) \
			      | ((b >> (8 - self.mapping['b'][1])) << self.mapping['b'][0]) \
			      | self.alpha(alpha)
			output.append(struct.pack('<H', pixel))
		return ''.join(output)

	def generate_24(self, width, height, rgb):
		output = []
		for i in xrange(width * height):
			r, g, b = rgb[i]
			pixel = ((r >> (8 - self.mapping['r'][1])) << self.mapping['r'][0]) \
			      | ((g >> (8 - self.mapping['g'][1])) << self.mapping['g'][0]) \
			      | ((b >> (8 - self.mapping['b'][1])) << self.mapping['b'][0])
			output.append(struct.pack('<I', pixel)[0:3])
		return ''.join(output)

	def generate_32(self, width, height, rgb, alpha):
		output = []
		for i in xrange(width * height):
			r, g, b = rgb[i]
			pixel = ((r >> (8 - self.mapping['r'][1])) << self.mapping['r'][0]) \
			      | ((g >> (8 - self.mapping['g'][1])) << self.mapping['g'][0]) \
			      | ((b >> (8 - self.mapping['b'][1])) << self.mapping['b'][0]) \
			      | self.alpha(alpha)
			output.append(struct.pack('<I', pixel))
		return ''.join(output)

	def convert(self, width, height, rgb, alpha):
		if self.depth == 8:
			return self.generate_8(width, height, rgb)
		elif self.depth == 16:
			return self.generate_16(width, height, rgb, alpha)
		elif self.depth == 24:
			return self.generate_24(width, height, rgb)
		elif self.depth == 32:
			return self.generate_32(width, height, rgb, alpha)
		else:
			raise RuntimeError, 'Invalid depth %s' % self.depth

	def bin(self, bins, val):
		bins[val >> 2] += 1

	def histogram(self, width, height, rgb):
		rgb_min = [255, 255, 255]
		rgb_max = [0, 0, 0]
		rgb_sum = [0, 0, 0]
		rgb_bins = [[0] * 64, [0] * 64, [0] * 64]

		for i in xrange(width * height):
			pixel = rgb[i]

			rgb_min = map(min, pixel, rgb_min)
			rgb_max = map(max, pixel, rgb_max)
			rgb_sum = map(operator.add, pixel, rgb_sum)

			map(self.bin, rgb_bins, pixel)

		output = []
		for i in xrange(len(rgb_min)):
			output.append(struct.pack('BBBB', rgb_min[i], 0, rgb_max[i], 0))
		output.append(struct.pack('<3I', *rgb_sum))
		for i in xrange(len(rgb_bins)):
			output.append(struct.pack('<64I', *rgb_bins[i]))

		return ''.join(output)

	def compose(self, ninputs, width, height, rgb):
		output = [(0, 0, 0)] * (width * height)
		offset = 50

		for input in xrange(ninputs):
			length = width - offset
			for y in xrange(height - offset):
				dst_offset = (y + offset) * width + offset
				src_offset = y * width
				output[dst_offset:dst_offset+length] = rgb[src_offset:src_offset+length]
			offset += 50

		return ''.join(chr(d) for d in list(itertools.chain.from_iterable(output)))


class FormatYUVPacked:
	def __init__(self, name, mapping):
		self.name = name
		self.mapping = copy.copy(mapping)

	def convert(self, width, height, yuv):
		output = []
		for i in xrange(width * height / 2):
			pixel = yuv[i*4:(i+1)*4]
			pixel = (pixel[self.mapping[0]], pixel[self.mapping[1]],
				 pixel[self.mapping[2]], pixel[self.mapping[3]])
			output.extend(pixel)
		return ''.join(output)

	def bin(self, bins, val):
		bins[val >> 2] += 1

	def histogram(self, width, height, yuv):
		vyu_min = [255, 255, 255]
		vyu_max = [0, 0, 0]
		vyu_sum = [0, 0, 0]
		vyu_bins = [[0] * 64, [0] * 64, [0] * 64]

		for y in xrange(height):
			for x in xrange(width / 2):
				offset = y * width * 2 + x * 4
				u0 = ord(yuv[offset])
				y0 = ord(yuv[offset+1])
				v0 = ord(yuv[offset+2])
				y1 = ord(yuv[offset+3])

				if x != width / 2 - 1:
					u2 = ord(yuv[offset+4])
					v2 = ord(yuv[offset+6])
					u1 = (u0 + u2) / 2
					v1 = (v0 + v2) / 2
				else:
					u1 = u0
					v1 = v1

				for vyu in ((v0, y0, u0), (v1, y1, u1)):
					vyu_min = map(min, vyu, vyu_min)
					vyu_max = map(max, vyu, vyu_max)
					vyu_sum = map(operator.add, vyu, vyu_sum)

					map(self.bin, vyu_bins, vyu)

		output = []
		for i in xrange(len(vyu_min)):
			output.append(struct.pack('BBBB', vyu_min[i], 0, vyu_max[i], 0))
		output.append(struct.pack('<3I', *vyu_sum))
		for i in xrange(len(vyu_bins)):
			output.append(struct.pack('<64I', *vyu_bins[i]))

		return ''.join(output)

	def compose(self, ninputs, width, height, yuv):
		output = ['\0'] * (width * height * 2)
		offset = 50

		for input in xrange(ninputs):
			length = (width - offset) * 2
			for y in xrange(height - offset):
				dst_offset = ((y + offset) * width + offset) * 2
				src_offset = y * width * 2
				output[dst_offset:dst_offset+length] = yuv[src_offset:src_offset+length]
			offset += 50

		return ''.join(output)


class FormatNV:
	def __init__(self, name, hsub, vsub, mapping):
		self.name = name
		self.hsub = hsub
		self.vsub = vsub
		self.mapping = copy.copy(mapping)

	def convert(self, width, height, yuv):
		output = []

		for i in xrange(width * height):
			output.append(yuv[2*i+1])

		for y in xrange(height / self.vsub):
			for x in xrange(width / 2):
				offset = (y * self.vsub * width * 2) + x * 4
				uv = (yuv[offset], yuv[offset+2])
				uv = (uv[self.mapping[0]], uv[self.mapping[1]])
				output.extend(uv)

		return ''.join(output)


class FormatYUVPlanar:
	def __init__(self, name, hsub, vsub, mapping):
		self.name = name
		self.hsub = hsub
		self.vsub = vsub
		self.mapping = copy.copy(mapping)

	def convert(self, width, height, yuv):
		output = []

		for i in xrange(width * height):
			output.append(yuv[2*i+1])

		for y in xrange(height / self.vsub):
			for x in xrange(width / 2):
				offset = (y * self.vsub * width * 2) + x * 4
				u = yuv[offset + self.mapping[0] * 2]
				output.append(u)

		for y in xrange(height / self.vsub):
			for x in xrange(width / 2):
				offset = (y * self.vsub * width * 2) + x * 4
				v = yuv[offset + self.mapping[1] * 2]
				output.append(v)

		return ''.join(output)


formats_rgb = {
	'rgb332': FormatRGB('rgb332',  {'r': (5, 3), 'g': (2, 3), 'b': (0, 2)}),
	'rgb565': FormatRGB('rgb565',  {'r': (11, 5), 'g': (5, 6), 'b': (0, 5)}),
	'bgr24':  FormatRGB('bgr24',   {'r': (16, 8), 'g': (8, 8),  'b': (0, 8)}),
	'rgb24':  FormatRGB('rgb24',   {'r': (0, 8),  'g': (8, 8),  'b': (16, 8)}),
}

formats_argb = {
	'argb555': FormatRGB('argb555', {'a': (15, 1), 'r': (10, 5), 'g': (5, 5), 'b': (0, 5)}),
	'xrgb555': FormatRGB('xrgb555', {'x': (15, 1), 'r': (10, 5), 'g': (5, 5), 'b': (0, 5)}),
	'abgr32':  FormatRGB('abgr32',  {'a': (24, 8), 'r': (16, 8), 'g': (8, 8),  'b': (0, 8)}),
	'argb32':  FormatRGB('argb32',  {'a': (0, 8),  'r': (8, 8),  'g': (16, 8), 'b': (24, 8)}),
	'xbgr32':  FormatRGB('xbgr32',  {'x': (24, 8), 'r': (16, 8), 'g': (8, 8),  'b': (0, 8)}),
	'xrgb32':  FormatRGB('xrgb32',  {'x': (0, 8),  'r': (8, 8),  'g': (16, 8), 'b': (24, 8)}),
}

formats_yuv = {
	'uyvy':    FormatYUVPacked('uyvy', (0, 1, 2, 3)),
	'vyuy':    FormatYUVPacked('vyuy', (2, 1, 0, 3)),
	'yuyv':    FormatYUVPacked('yuyv', (1, 0, 3, 2)),
	'yvyu':    FormatYUVPacked('yvyu', (1, 2, 3, 0)),
	'nv12m':   FormatNV('nv12m', 2, 2, (0, 1)),
	'nv21m':   FormatNV('nv21m', 2, 2, (1, 0)),
	'nv16m':   FormatNV('nv16m', 2, 1, (0, 1)),
	'nv61m':   FormatNV('nv61m', 2, 1, (1, 0)),
	'yuv420m': FormatYUVPlanar('yuv420m', 2, 2, (0, 1)),
}

resolutions = ((640, 480), (1024, 768))

def main(argv):
	re_fname = re.compile('frame-([a-z]*)-([0-9]*)x([0-9]*).([a-z]*).gz')

	for fname in glob.glob('*.gz'):
		match = re_fname.match(fname)
		if not match:
			continue

		typ = match.group(1)
		res = (int(match.group(2)), int(match.group(3)))
		fmt = match.group(4)

		if fmt == 'rgb':
			rgb = gzip.open(fname, 'rb').read()
			rgb = [struct.unpack('BBB', rgb[i*3:(i+1)*3]) for i in xrange(len(rgb) / 3)]

			for format in formats_rgb.values():
				bin_fname = 'frame-%s-%s-%ux%u.bin' % (typ, format.name, res[0], res[1])
				print 'Generating %s' % bin_fname
				bin = format.convert(res[0], res[1], rgb, 0)
				file(bin_fname, 'wb').write(bin)

			for format in formats_argb.values():
				for alpha in (0, 100, 200, 255):
					if format.alpha_bits == 1 and alpha not in (0, 255):
						continue
					bin_fname = 'frame-%s-%s-%ux%u-alpha%u.bin' % (typ, format.name, res[0], res[1], alpha)
					print 'Generating %s' % bin_fname
					bin = format.convert(res[0], res[1], rgb, alpha)
					file(bin_fname, 'wb').write(bin)

			format = formats_rgb['rgb24']

			bin_fname = 'histo-%s-%s-%ux%u.bin' % (typ, format.name, res[0], res[1])
			print 'Generating %s' % bin_fname
			bin = format.histogram(res[0], res[1], rgb)
			file(bin_fname, 'wb').write(bin)

			if typ == 'reference' and res[0] == 1024 and res[1] == 768:
				for ninputs in xrange(1, 6):
					bin_fname = 'frame-composed-%u-%s-%ux%u.bin' % (ninputs, format.name, res[0], res[1])
					print 'Generating %s' % bin_fname
					bin = format.compose(ninputs, res[0], res[1], rgb)
					file(bin_fname, 'wb').write(bin)

		elif fmt == 'yuv':
			yuv = gzip.open(fname, 'rb').read()

			for format in formats_yuv.values():
				bin_fname = 'frame-%s-%s-%ux%u.bin' % (typ, format.name, res[0], res[1])
				print 'Generating %s' % bin_fname
				bin = format.convert(res[0], res[1], yuv)
				file(bin_fname, 'wb').write(bin)

			format = formats_yuv['uyvy']
			bin_fname = 'histo-%s-%s-%ux%u.bin' % (typ, format.name, res[0], res[1])
			print 'Generating %s' % bin_fname
			bin = format.histogram(res[0], res[1], yuv)
			file(bin_fname, 'wb').write(bin)

			if typ == 'reference' and res[0] == 1024 and res[1] == 768:
				for ninputs in xrange(1, 6):
					bin_fname = 'frame-composed-%u-%s-%ux%u.bin' % (ninputs, format.name, res[0], res[1])
					print 'Generating %s' % bin_fname
					bin = format.compose(ninputs, res[0], res[1], yuv)
					file(bin_fname, 'wb').write(bin)


if __name__ == '__main__':
	sys.exit(main(sys.argv))
