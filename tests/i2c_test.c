// SPDX-License-Identifier: GPL-2.0-only
/* Host-side fault injection against the same transaction code as the module. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_i2c.h"

struct fake {
	unsigned char regs[5];
	unsigned char commands[16];
	unsigned char sent[8];
	unsigned int command_count, sent_count, writes, waits, reads;
	unsigned int fail_at;
	int wait_error, stop_error;
	unsigned char fail_status;
	int reject_enable;
};

static unsigned char fake_read(void *ctx, unsigned int reg)
{
	struct fake *f = ctx;
	static const unsigned char bytes[] = { 0x54, 0x49, 0x05, 0x68 };

	if (reg == GC573_I2C_DATA) {
		assert(f->reads < sizeof(bytes));
		return bytes[f->reads++];
	}
	if (reg == GC573_I2C_CONTROL && f->reject_enable && f->writes)
		return 0;
	return f->regs[reg];
}

static void fake_write(void *ctx, unsigned int reg, unsigned char value)
{
	struct fake *f = ctx;

	f->writes++;
	if (reg == GC573_I2C_COMMAND) {
		assert(f->command_count < sizeof(f->commands));
		f->commands[f->command_count++] = value;
	} else {
		f->regs[reg] = value;
	}
	if (reg == GC573_I2C_DATA) {
		assert(f->sent_count < sizeof(f->sent));
		f->sent[f->sent_count++] = value;
	}
}

static int fake_wait(void *ctx, unsigned char mask, unsigned char value,
		     unsigned char *status)
{
	struct fake *f = ctx;

	f->waits++;
	if (f->commands[f->command_count - 1] == 0x41) {
		assert(mask == 0x42 && value == 0);
		*status = f->stop_error ? 0x40 : 0;
		return f->stop_error;
	}
	assert(mask == 3 && value == 1);
	*status = f->waits == f->fail_at ? f->fail_status : 1;
	return f->waits == f->fail_at ? f->wait_error : 0;
}

static int run(struct fake *f, struct gc573_i2c_result *result)
{
	const struct gc573_i2c_io io = {
		.ctx = f, .read = fake_read, .write = fake_write, .wait = fake_wait,
	};
	int ret;

	f->regs[0] = 0x55;
	f->regs[1] = 0x02;
	ret = gc573_i2c_identify(&io, result);
	assert(f->regs[0] == 0x55 && f->regs[1] == 0x02);
	assert(f->regs[2] == result->initial_control);
	return ret;
}

int main(void)
{
	static const unsigned char expected[] = {
		0x91, 0x11, 0x91, 0x21, 0x21, 0x21, 0x29, 0x41,
	};
	struct gc573_i2c_result result;
	struct fake f = { .regs = { 0, 0, 0x80, 0, 0 } };
	unsigned int i;

	assert(run(&f, &result) == 0);
	assert(result.bytes_read == 4 && result.id[3] == 0x68);
	assert(f.command_count == sizeof(expected));
	assert(!memcmp(f.commands, expected, sizeof(expected)));
	assert(f.sent_count == 3);
	assert(f.sent[0] == 0x90 && f.sent[1] == 0 && f.sent[2] == 0x91);
	/* Every stage timeout must stop, restore, and preserve the error. */
	for (i = 1; i <= 7; i++) {
		f = (struct fake) { .fail_at = i, .wait_error = -ETIMEDOUT };
		assert(run(&f, &result) == -ETIMEDOUT);
		assert(f.commands[f.command_count - 1] == 0x41);
		assert(result.bytes_read == (i > 3 ? i - 4 : 0));
	}
	for (i = 1; i <= 3; i++) {
		f = (struct fake) { .fail_at = i, .fail_status = 0x81 };
		assert(run(&f, &result) == -ENXIO);
		assert(!result.bytes_read);
		assert(f.commands[f.command_count - 1] == 0x41);
	}
	f = (struct fake) { .fail_at = 1, .fail_status = 0x21 };
	assert(run(&f, &result) == -EAGAIN);
	f = (struct fake) { .stop_error = -ETIMEDOUT };
	assert(run(&f, &result) == -ETIMEDOUT);
	assert(result.stop_error == -ETIMEDOUT);
	f = (struct fake) { .regs = { 0, 0, 0x40, 0, 0 } };
	assert(run(&f, &result) == -EBUSY && !f.writes);
	f = (struct fake) { .regs = { 0, 0, 0, 0, 0x40 } };
	assert(run(&f, &result) == -EBUSY && !f.writes);
	f = (struct fake) { .reject_enable = 1 };
	assert(run(&f, &result) == -EIO && !f.command_count);
	/* Actual GC573 observation: 0xee is not a valid control register. */
	f = (struct fake) { .regs = { 0, 0, 0xee, 0, 0xee } };
	assert(run(&f, &result) == -ENODEV && !f.writes);
	puts("PASS: success sequence, seven timeouts, three NACKs, arbitration loss,");
	puts("      STOP timeout, busy/IRQ refusal, enable failure, state restoration");
	return 0;
}
