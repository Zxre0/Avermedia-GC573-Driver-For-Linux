// SPDX-License-Identifier: GPL-2.0-only
/* Original bounded identification transaction; no HDMI configuration writes. */
#include <linux/errno.h>
#include "gc573_i2c.h"

static int gc573_i2c_command(const struct gc573_i2c_io *io,
			    unsigned char command, int check_ack,
			    struct gc573_i2c_result *result)
{
	int ret;

	io->write(io->ctx, GC573_I2C_COMMAND, command);
	/* Command includes IACK: require a new completion, not just idle TIP. */
	ret = io->wait(io->ctx, 0x03, 0x01, &result->status);
	if (ret)
		return ret;
	if (result->status & 0x20)
		return -EAGAIN;
	if (check_ack && (result->status & 0x80))
		return -ENXIO;
	return 0;
}

static int gc573_i2c_send(const struct gc573_i2c_io *io,
			 unsigned char byte, unsigned char command,
			 struct gc573_i2c_result *result)
{
	io->write(io->ctx, GC573_I2C_DATA, byte);
	return gc573_i2c_command(io, command, 1, result);
}

int gc573_i2c_identify(const struct gc573_i2c_io *io,
		     struct gc573_i2c_result *result)
{
	unsigned char prelow, prehigh, stop_status;
	unsigned int i;
	int ret;

	*result = (struct gc573_i2c_result) { 0 };
	result->initial_control = io->read(io->ctx, GC573_I2C_CONTROL);
	result->initial_status = io->read(io->ctx, GC573_I2C_COMMAND);
	/* Reserved control bits suggest an incompatible/unavailable register. */
	if (result->initial_control & 0x3f)
		return -ENODEV;
	/* Only accept an idle controller with interrupts disabled. */
	if (result->initial_control & 0x40)
		return -EBUSY;
	if (result->initial_status & 0x42)
		return -EBUSY;
	prelow = io->read(io->ctx, GC573_I2C_PRELOW);
	prehigh = io->read(io->ctx, GC573_I2C_PREHIGH);
	io->write(io->ctx, GC573_I2C_CONTROL, 0);
	/* Prescaler observed for this byte controller in the vendor reference. */
	io->write(io->ctx, GC573_I2C_PRELOW, 0x3d);
	io->write(io->ctx, GC573_I2C_PREHIGH, 0);
	io->write(io->ctx, GC573_I2C_CONTROL, 0x80);
	if (io->read(io->ctx, GC573_I2C_CONTROL) != 0x80) {
		ret = -EIO;
		goto restore;
	}
	/* Receiver address 0x48 (7-bit), register pointer 0x00, repeated START. */
	ret = gc573_i2c_send(io, 0x90, 0x91, result);
	if (ret)
		goto stop;
	ret = gc573_i2c_send(io, 0x00, 0x11, result);
	if (ret)
		goto stop;
	ret = gc573_i2c_send(io, 0x91, 0x91, result);
	if (ret)
		goto stop;
	for (i = 0; i < sizeof(result->id); i++) {
		ret = gc573_i2c_command(io, i == sizeof(result->id) - 1 ?
				       0x29 : 0x21, 0, result);
		if (ret)
			goto stop;
		result->id[i] = io->read(io->ctx, GC573_I2C_DATA);
		result->bytes_read++;
	}
stop:
	io->write(io->ctx, GC573_I2C_COMMAND, 0x41);
	result->stop_error = io->wait(io->ctx, 0x42, 0, &stop_status);
	if (!ret)
		ret = result->stop_error;
restore:
	/* Restore the prescaler/control even after NACK or a timed-out command. */
	io->write(io->ctx, GC573_I2C_CONTROL, 0);
	io->write(io->ctx, GC573_I2C_PRELOW, prelow);
	io->write(io->ctx, GC573_I2C_PREHIGH, prehigh);
	io->write(io->ctx, GC573_I2C_CONTROL, result->initial_control);
	/* Flush posted restoration writes and check the readable control byte. */
	if (io->read(io->ctx, GC573_I2C_CONTROL) != result->initial_control && !ret)
		ret = -EIO;
	return ret;
}
