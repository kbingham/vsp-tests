#!/usr/bin/python

import math
import sys

clu_configs = (
	('zero', {
		'scale': 0.0,
		'a': 0.0,
		'freq': 1.0,
		'weights': (1.0, 1.0, 1.0)
	} ),
	('identity', {
		'scale': 1.0,
		'a': 0.0,
		'freq': 1.0,
		'weights': (1.0, 1.0, 1.0)
	} ),
	# Keep the three weights different to generate an anisothropic
	# look up table.
	('wave', {
		'scale': 1.0,
		'a': 0.1,
		'freq': 3.0,
		'weights': (1.0, 2.0, 3.0)
	} ),
)

lut_configs = (
	('zero',	0.0, 1.0, 1.0, 1.0),
	('identity',	1.0, 1.0, 1.0, 1.0),
	('gamma',	1.0, 0.5, 1.0, 2.0),
)

def clu_value(x, y, z, scale, a, freq, weights):
	x = x / 16.
	y = y / 16.
	z = z / 16.

	dist = math.sqrt((x*x*weights[0] + y*y*weights[1] + z*z*weights[2]) / 3. / sum(weights))
	offset = a * math.sin(dist * freq * 2 * math.pi)

	x = max(0, min(255, int((x*scale + offset) * 256)))
	y = max(0, min(255, int((y*scale + offset) * 256)))
	z = max(0, min(255, int((z*scale + offset) * 256)))

	return (z, y, x, 0)

def generate_clu(config):
	clu = []

	for z in xrange(17):
		for y in xrange(17):
			for x in xrange(17):
				clu.extend(clu_value(x, y, z, **config[1]))

	file('clu-%s.bin' % config[0], 'wb').write(''.join([chr(c) for c in clu]))


def gamma(vin, gamma, scale):
	return int(255 * scale * math.pow(vin / 255., gamma))

def generate_lut(config):
	lut = []
	for i in xrange(256):
		lut.extend([gamma(i, g, config[1]) for g in config[2:]])
		lut.append(0)

	file('lut-%s.bin' % config[0], 'wb').write(''.join([chr(c) for c in lut]))


def main(argv):
	for config in clu_configs:
		generate_clu(config)

	for config in lut_configs:
		generate_lut(config)

	return 0

if __name__ == '__main__':
	sys.exit(main(sys.argv))
