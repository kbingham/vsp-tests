#!/usr/bin/python

import matplotlib.pyplot as plt
import struct
import sys


def usage(argv0):
	print 'Usage: %s <histo.bin>' % argv0


def main(argv):

	if len(argv) != 2:
		usage(argv[0])
		return 1

	data = file(argv[1], 'rb').read()

	if len(data) == (2 + 64) * 4 or len(data) == (2 + 256) * 4:
		maxmin = struct.unpack('<I', data[0:4])
		sums = struct.unpack('<I', data[4:8])
		histo = struct.unpack('<%uI' % (len(data[8:]) / 4), data[8:])

		num_bins = len(histo);

		title = 'min %u max %u sum %u' % \
			((maxmin[0] >>  0) & 0xff, (maxmin[0] >> 16) & 0xff, sums[0])

		print 'pixels (%u)' % sum(histo)

		plt.figure(figsize=(10, 10))
		plt.xlim([0, num_bins])
		plt.ylim([0, max(histo)])
		plt.bar(range(num_bins), histo, color='r');

	elif len(data) == (6 + 64 * 3) * 4:
		maxmin = struct.unpack('<III', data[0:12])
		sums = struct.unpack('<III', data[12:24])
		histo = [struct.unpack('<I', data[24+i*4:24+i*4+4])[0] for i in xrange(len(data[24:])/4)]

		num_bins = len(histo) / 3;

		histo_r = histo[0:num_bins]
		histo_g = histo[num_bins:2*num_bins]
		histo_b = histo[num_bins*2:]

		title = 'RGB min (%u,%u,%u) max (%u,%u,%u) sum (%u,%u,%u)' % \
			((maxmin[0] >>  0) & 0xff, (maxmin[1] >>  0) & 0xff, (maxmin[2] >>  0) & 0xff,
			 (maxmin[0] >> 16) & 0xff, (maxmin[1] >> 16) & 0xff, (maxmin[2] >> 16) & 0xff,
			 sums[0], sums[1], sums[2])

		print 'pixels RGB (%u,%u,%u)' % (sum(histo_r), sum(histo_g), sum(histo_b))

		plt.figure(figsize=(10, 20))
		plt.subplot(3, 1, 1)
		plt.xlim([0, num_bins])
		plt.ylim([0, max(histo_r)])
		plt.bar(range(num_bins), histo_r, color='r');
		plt.subplot(3, 1, 2)
		plt.xlim([0, num_bins])
		plt.ylim([0, max(histo_g)])
		plt.bar(range(num_bins), histo_g, color='g');
		plt.subplot(3, 1, 3)
		plt.xlim([0, num_bins])
		plt.ylim([0, max(histo_b)])
		plt.bar(range(num_bins), histo_b, color='b');

	else:
		print 'Invalid histogram length %u' % len(data)
		return 1

	plt.suptitle(title)
	plt.savefig(argv[1].replace('bin', 'png'), dpi=72)

        return 0


if __name__ == '__main__':
        sys.exit(main(sys.argv))

