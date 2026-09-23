/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_I2C_H
#define GC573_I2C_H

/* Byte register indices, at four-byte spacing relative to BAR0 + 0x120. */
#define GC573_I2C_PRELOW 0
#define GC573_I2C_PREHIGH 1
#define GC573_I2C_CONTROL 2
#define GC573_I2C_DATA 3
#define GC573_I2C_COMMAND 4

struct gc573_i2c_io {
	void *ctx;
	unsigned char (*read)(void *ctx, unsigned int reg);
	void (*write)(void *ctx, unsigned int reg, unsigned char value);
	int (*wait)(void *ctx, unsigned char mask, unsigned char value,
		    unsigned char *status);
};

struct gc573_i2c_result {
	unsigned char initial_control;
	unsigned char initial_status;
	unsigned char status;
	unsigned char id[4];
	unsigned int bytes_read;
	int stop_error;
};

int gc573_i2c_identify(const struct gc573_i2c_io *io,
		     struct gc573_i2c_result *result);
#endif
