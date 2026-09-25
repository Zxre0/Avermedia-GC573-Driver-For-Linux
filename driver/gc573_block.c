// SPDX-License-Identifier: GPL-2.0-only
/* Original transaction for the GC573 block I2C controller.
 * Offsets and commands checked against the official GC573 Windows driver.
 * Error status bits and hardware abort behavior remain unverified.
 */
#include <linux/errno.h>
#include "gc573_block.h"

int gc573_block_prepare_gpio(const struct gc573_block_io *io,
			     struct gc573_gpio_result *result)
{
	*result = (struct gc573_gpio_result) { 0 };
	result->fpga_id = io->read(io->ctx, 0);
	result->board_id = io->read(io->ctx, GC573_BOARD_ID);
	result->before = io->read(io->ctx, GC573_GPIO);
	result->status_before = io->read(io->ctx, GC573_BLOCK_STATUS);
	result->irq_enable = io->read(io->ctx, GC573_IRQ_ENABLE);
	result->irq_before = io->read(io->ctx, GC573_IRQ_STATUS);
	/* Limit this initialization experiment to the observed board/image. */
	if (result->fpga_id != 0x20201015 || result->board_id != 0x57300102 ||
	    result->before == 0xeeeeeeee || result->before == 0xffffffff)
		return -ENODEV;
	/* 0x08 is observed, not an established busy/error interpretation. */
	if ((result->status_before != 0 && result->status_before != 0x08) ||
	    result->irq_enable || result->irq_before)
		return -EBUSY;
	if (!(result->before & GC573_GPIO_PREPARE)) {
		io->write(io->ctx, GC573_GPIO, result->before | GC573_GPIO_PREPARE);
		result->changed = 1;
	}
	/* Flush the posted write before the vendor's 100 ms settling delay. */
	result->after = io->read(io->ctx, GC573_GPIO);
	io->sleep_ms(io->ctx, 100);
	result->after = io->read(io->ctx, GC573_GPIO);
	result->status_100ms = io->read(io->ctx, GC573_BLOCK_STATUS);
	result->irq_after = io->read(io->ctx, GC573_IRQ_STATUS);
	result->samples = 1;
	if (result->after == 0xeeeeeeee || result->after == 0xffffffff ||
	    !(result->after & GC573_GPIO_PREPARE))
		return -EIO;
	/* Observe only: no fresh command, FIFO read, reset or IRQ ACK. */
	io->sleep_ms(io->ctx, 1900);
	result->status_2000ms = io->read(io->ctx, GC573_BLOCK_STATUS);
	result->irq_after = io->read(io->ctx, GC573_IRQ_STATUS);
	result->samples = 2;
	/* Leave the initialization bit set, as the Windows driver does. */
	return 0;
}

/* Return 1 for a fresh read completion, 0 to keep waiting, or an error. */
static int gc573_block_observe_mask(unsigned int status, unsigned int *saw_clear,
				    unsigned int mask)
{
	if (status == 0xeeeeeeee || status == 0xffffffff)
		return -ENODEV;
	if (!(status & mask)) {
		*saw_clear = 1;
		return 0;
	}
	return *saw_clear ? 1 : 0;
}

int gc573_block_observe(unsigned int status, unsigned int *saw_clear)
{
	return gc573_block_observe_mask(status, saw_clear, GC573_BLOCK_READ_DONE);
}

int gc573_block_observe_write(unsigned int status, unsigned int *saw_clear)
{
	return gc573_block_observe_mask(status, saw_clear, GC573_BLOCK_WRITE_DONE);
}

static int gc573_block_identify_state(const struct gc573_block_io *io,
				      struct gc573_block_result *result,
				      int after_gpio_setup,
				      unsigned int subaddr, unsigned int length,
				      unsigned int address)
{
	unsigned int i;
	int ret;

	*result = (struct gc573_block_result) { 0 };
	if (!length || length > sizeof(result->data) || subaddr > 0xff)
		return -EINVAL;
	if ((address != 0x91 && address != 0xa9 && address != 0x59 &&
	     address != 0x97 && address != 0x71 && address != 0x69 &&
	     address != 0x6b && address != 0x6d && address != 0x6f && address != 0xd9) ||
	    length > 256 - subaddr)
		return -EINVAL;
	result->initial_status = io->read(io->ctx, GC573_BLOCK_STATUS);
	result->initial_divider = io->read(io->ctx, GC573_BLOCK_DIVIDER);
	result->irq_enable = io->read(io->ctx, GC573_IRQ_ENABLE);
	if (result->initial_status == 0xeeeeeeee ||
	    result->initial_status == 0xffffffff ||
	    result->initial_divider == 0xeeeeeeee ||
	    result->initial_divider == 0xffffffff)
		return -ENODEV;
	/* Permit zero or the observed old read result, but not unknown states. */
	if ((result->initial_status != 0 &&
	     result->initial_status != GC573_BLOCK_WRITE_DONE &&
	     result->initial_status != GC573_BLOCK_READ_DONE &&
	     !(after_gpio_setup && result->initial_status == 0x08)) ||
	    result->irq_enable & GC573_IRQ_I2C)
		return -EBUSY;

	io->write(io->ctx, GC573_BLOCK_DIVIDER, GC573_BLOCK_TEST_DIVIDER);
	io->write(io->ctx, GC573_BLOCK_ADDRESS, address);
	/* Width registers encode byte count minus one. */
	io->write(io->ctx, GC573_BLOCK_SUBADDR_WIDTH, 0);
	io->write(io->ctx, GC573_BLOCK_SUBADDR, subaddr);
	io->write(io->ctx, GC573_BLOCK_FIFO_WIDTH, 0);
	io->write(io->ctx, GC573_BLOCK_LENGTH, length);
	io->write(io->ctx, GC573_BLOCK_COMMAND, 0x10);
	/* Command 0x10 need not clear an old completion on this FPGA. */
	result->status = io->read(io->ctx, GC573_BLOCK_STATUS);
	result->prepared_status = result->status;
	if (result->status != 0 && result->status != GC573_BLOCK_WRITE_DONE &&
	    result->status != GC573_BLOCK_READ_DONE &&
	    !(after_gpio_setup && result->status == 0x08)) {
		ret = -EIO;
		goto cleanup;
	}
	/* If still set, require clear then set after START before trusting RX. */
	result->completion_armed = !(result->status & GC573_BLOCK_READ_DONE);
	io->write(io->ctx, GC573_BLOCK_COMMAND, 0x08);
	result->started = 1;
	ret = io->wait(io->ctx, &result->status, &result->completion_armed);
	result->irq_status = io->read(io->ctx, GC573_IRQ_STATUS);
	/* Match the vendor's post-read command before accessing the RX FIFO. */
	io->write(io->ctx, GC573_BLOCK_COMMAND, 0x10);
	if (!ret) {
		if (!result->completion_armed ||
		    !(result->status & GC573_BLOCK_READ_DONE) ||
		    result->status == 0xeeeeeeee ||
		    result->status == 0xffffffff) {
			ret = -EIO;
		} else {
			for (i = 0; i < length; i++) {
				result->data[i] = io->read(io->ctx, GC573_BLOCK_RX);
				result->bytes_read++;
			}
		}
	}
	/* Write only this controller's pending global interrupt bit. */
	if (result->irq_status & GC573_IRQ_I2C)
		io->write(io->ctx, GC573_IRQ_STATUS, GC573_IRQ_I2C);
cleanup:
	io->write(io->ctx, GC573_BLOCK_DIVIDER, result->initial_divider);
	result->cleanup_status = io->read(io->ctx, GC573_BLOCK_STATUS);
	return ret;
}

int gc573_block_identify(const struct gc573_block_io *io,
			struct gc573_block_result *result)
{
	return gc573_block_identify_state(io, result, 0, 0, 4, 0x91);
}

int gc573_block_read_registers(const struct gc573_block_io *io,
			       struct gc573_block_result *result,
			       unsigned int subaddr, unsigned int length)
{
	return gc573_block_identify_state(io, result, 0, subaddr, length, 0x91);
}

/* Fixed EDID SRAM endpoint selected by receiver bank-zero register 0x4b. */
int gc573_block_read_edid(const struct gc573_block_io *io,
			  struct gc573_block_result *result,
			  unsigned int subaddr, unsigned int length)
{
	return gc573_block_identify_state(io, result, 0, subaddr, length, 0xa9);
}

int gc573_splitter_read(const struct gc573_block_io *io,
		       struct gc573_block_result *result, unsigned int reg)
{
	return gc573_block_identify_state(io, result, 0, reg, 1, 0x59);
}

/* Fixed TX-common timer registers mapped by splitter bank-zero 0xf1=0x97. */
int gc573_splitter_timer_read(const struct gc573_block_io *io,
			     struct gc573_block_result *result, unsigned int reg)
{
	*result = (struct gc573_block_result) { 0 };
	if (reg < 0x11 || reg > 0x13)
		return -EINVAL;
	return gc573_block_identify_state(io, result, 0, reg, 1, 0x97);
}

/* Fixed continuation of Windows 0x14004fa12..0x14004fa62. */
int gc573_splitter_map_read(const struct gc573_block_io *io,
			   struct gc573_block_result *result, unsigned int reg)
{
	*result = (struct gc573_block_result) { 0 };
	if (reg != 0x50 && (reg < 0x2c || reg > 0x2f))
		return -EINVAL;
	return gc573_block_identify_state(io, result, 0, reg, 1, 0x97);
}

/* RX endpoint is mapped by splitter register 0xf0=0x71. No RX writes yet. */
int gc573_splitter_rx_read(const struct gc573_block_io *io,
			  struct gc573_block_result *result, unsigned int reg,
			  unsigned int length)
{
	*result = (struct gc573_block_result) { 0 };
	if (!((reg == 0x0f && length == 1) || (reg == 0 && length == 4) ||
	      (reg == 0x22 && length == 3) || (reg == 0xc5 && length == 1)))
		return -EINVAL;
	return gc573_block_identify_state(io, result, 0, reg, length, 0x71);
}

/* Windows splitter ID path: 0x14004f494 -> 0x1400597e8 -> 0x1400462fc.
 * Fixed address 0x58 (8-bit write form), controller zero, block backend.
 * Read bank first instead of the vendor's bank-select write. No reset here.
 */
static int gc573_splitter_identify_state(const struct gc573_block_io *io,
					struct gc573_splitter_result *result,
					int after_startup)
{
	unsigned int i;
	int ret;

	*result = (struct gc573_splitter_result) { 0 };
	if (io->read(io->ctx, 0) != 0x20201015 ||
	    io->read(io->ctx, GC573_BOARD_ID) != 0x57300102)
		return -ENODEV;
	result->gpio = io->read(io->ctx, GC573_GPIO);
	if (result->gpio == 0xeeeeeeee || result->gpio == 0xffffffff ||
	    (result->gpio & 0x108) != 0x108)
		return -ENODEV;
	if (io->read(io->ctx, GC573_IRQ_ENABLE) ||
	    io->read(io->ctx, GC573_IRQ_STATUS))
		return -EBUSY;
	result->last_subaddr = 0x0f;
	result->transactions++;
	ret = gc573_block_identify_state(io, &result->last, after_startup, 0x0f, 1, 0x59);
	if (ret)
		return ret;
	result->bank = result->last.data[0];
	result->bank_valid = 1;
	if (result->bank & 1)
		return -EOPNOTSUPP;
	result->last_subaddr = 0;
	result->transactions++;
	ret = gc573_block_identify_state(io, &result->last, 0, 0, 4, 0x59);
	if (ret)
		return ret;
	for (i = 0; i < sizeof(result->id); i++)
		result->id[i] = result->last.data[i];
	result->id_valid = 1;
	/* Both identities are explicitly compared by the Windows driver. */
	result->id_matches = result->id[0] == 0x54 && result->id[1] == 0x49 &&
		(result->id[2] == 0x64 || result->id[2] == 0x63) &&
		result->id[3] == 0x66;
	return result->id_matches ? 0 : -ENODEV;
}

int gc573_splitter_identify(const struct gc573_block_io *io,
			   struct gc573_splitter_result *result)
{
	return gc573_splitter_identify_state(io, result, 0);
}

/* GPIO-only prefix of Windows 0x14005a470, followed by bounded ID reads.
 * No vendor timer/state machine, splitter register writes or GPIO retries.
 */
int gc573_splitter_startup(const struct gc573_block_io *io,
			   struct gc573_splitter_start_result *startup,
			   struct gc573_splitter_result *result)
{
	static const unsigned char masks[] = { 0x10, 0x40, 0x02, 0x20, 0x20, 0x20 };
	static const unsigned int values[] = { 0x10, 0x40, 0, 0x20, 0, 0x20 };
	static const unsigned int delays[] = { 0, 2, 2, 5, 10, 5 };
	unsigned int expected, observed, divider, i;

	*startup = (struct gc573_splitter_start_result) { 0 };
	*result = (struct gc573_splitter_result) { 0 };
	if (!io->sleep_ms)
		return -EINVAL;
	if (io->read(io->ctx, 0) != 0x20201015 ||
	    io->read(io->ctx, GC573_BOARD_ID) != 0x57300102)
		return -ENODEV;
	startup->gpio_before = io->read(io->ctx, GC573_GPIO);
	startup->gpio_after = startup->gpio_before;
	startup->status_before = io->read(io->ctx, GC573_BLOCK_STATUS);
	divider = io->read(io->ctx, GC573_BLOCK_DIVIDER);
	/* Observed prepared board, either HPD state. If GPIO 5 is already high,
	 * continue with reads only; bit 10 changed asynchronously after startup.
	 * Its electrical meaning is unknown. No GPIO write targets that bit.
	 */
	if (((startup->gpio_before & ~4U) != 0x1f958 &&
	     (startup->gpio_before & ~0x404U) != 0x1f978) ||
	    divider == 0xeeeeeeee || divider == 0xffffffff)
		return -ENODEV;
	if (io->read(io->ctx, GC573_IRQ_ENABLE) ||
	    io->read(io->ctx, GC573_IRQ_STATUS) ||
	    (startup->status_before != 0 && startup->status_before != 1 &&
	     startup->status_before != 4 && startup->status_before != 8))
		return -EBUSY;
	startup->preflight_complete = 1;
	expected = startup->gpio_before;
	if (expected & 0x20) {
		startup->resumed = 1;
		goto settle;
	}
	/* Vendor's pre-start callback returns a 200 ms settling delay. */
	io->sleep_ms(io->ctx, 200);
	for (i = 0; i < sizeof(masks); i++) {
		observed = io->read(io->ctx, GC573_GPIO);
		startup->gpio_after = observed;
		if (observed != expected)
			return -EIO;
		expected = (observed & ~masks[i]) | values[i];
		io->write(io->ctx, GC573_GPIO, expected);
		startup->writes_started++;
		observed = io->read(io->ctx, GC573_GPIO);
		startup->gpio_after = observed;
		startup->gpio_steps[i] = observed;
		if (observed != expected)
			return -EIO;
		startup->steps_completed++;
		if (delays[i])
			io->sleep_ms(io->ctx, delays[i]);
	}
settle:
	/* Extra settling before our immediate read; Windows defers to a timer.
	 * Allow only the observed asynchronous bit-10 change during this wait.
	 * The GPIO pulse is not repeated when continuing from the released state.
	 */
	io->sleep_ms(io->ctx, 100);
	startup->gpio_after = io->read(io->ctx, GC573_GPIO);
	startup->gpio_settle_changed = startup->gpio_after ^ expected;
	startup->status_after = io->read(io->ctx, GC573_BLOCK_STATUS);
	startup->status_after_valid = 1;
	if (startup->gpio_settle_changed & ~0x400U)
		return -EIO;
	startup->complete = 1;
	/* This explicit startup/continuation permits one fresh request from
	 * observed timeout status 8. The second read uses the normal gate.
	 */
	return gc573_splitter_identify_state(io, result, 1);
}

static int gc573_receiver_gpio(const struct gc573_block_io *io,
			       struct gc573_receiver_result *result,
			       unsigned int mask, unsigned int value)
{
	unsigned int before = io->read(io->ctx, GC573_GPIO);

	if (before == 0xeeeeeeee || before == 0xffffffff)
		return -ENODEV;
	io->write(io->ctx, GC573_GPIO, (before & ~mask) | value);
	result->gpio_after = io->read(io->ctx, GC573_GPIO);
	if (result->gpio_after == 0xeeeeeeee || result->gpio_after == 0xffffffff ||
	    (result->gpio_after & mask) != value)
		return -EIO;
	result->steps++;
	return 0;
}

/* Private driver interface; there is no arbitrary userspace write parameter. */
static int gc573_block_write_address(const struct gc573_block_io *io,
				     struct gc573_block_result *result,
				     unsigned int subaddr, unsigned int value,
				     unsigned int address)
{
	int ret;

	*result = (struct gc573_block_result) { 0 };
	if (subaddr > 0xff || value > 0xff || !io->wait_write ||
	    (address != 0x90 && address != 0x58 && address != 0x96 && address != 0x70 &&
	     address != 0x68 && address != 0x6a && address != 0x6c && address != 0x6e && address != 0xd8))
		return -EINVAL;
	result->initial_status = io->read(io->ctx, GC573_BLOCK_STATUS);
	result->initial_divider = io->read(io->ctx, GC573_BLOCK_DIVIDER);
	result->irq_enable = io->read(io->ctx, GC573_IRQ_ENABLE);
	/* Audio/video IRQs are independent; leave them enabled and unacknowledged. */
	if ((result->irq_enable & ~0x22U) || result->initial_status != GC573_BLOCK_READ_DONE)
		return -EBUSY;
	if (result->initial_divider == 0xeeeeeeee ||
	    result->initial_divider == 0xffffffff)
		return -ENODEV;
	io->write(io->ctx, GC573_BLOCK_DIVIDER, GC573_BLOCK_TEST_DIVIDER);
	io->write(io->ctx, GC573_BLOCK_ADDRESS, address);
	io->write(io->ctx, GC573_BLOCK_SUBADDR_WIDTH, 0);
	io->write(io->ctx, GC573_BLOCK_SUBADDR, subaddr);
	io->write(io->ctx, GC573_BLOCK_COMMAND, 0x10);
	io->write(io->ctx, GC573_BLOCK_FIFO_WIDTH, 0);
	io->write(io->ctx, GC573_BLOCK_LENGTH, 1);
	result->prepared_status = io->read(io->ctx, GC573_BLOCK_STATUS);
	result->status = result->prepared_status;
	if (result->status != 0 && result->status != GC573_BLOCK_READ_DONE &&
	    result->status != GC573_BLOCK_WRITE_DONE) {
		ret = -EIO;
		goto restore;
	}
	result->completion_armed = !(result->status & GC573_BLOCK_WRITE_DONE);
	io->write(io->ctx, GC573_BLOCK_TX, value);
	io->write(io->ctx, GC573_BLOCK_COMMAND, 4);
	result->started = 1;
	ret = io->wait_write(io->ctx, &result->status, &result->completion_armed);
	result->irq_status = io->read(io->ctx, GC573_IRQ_STATUS);
	if (!ret && (!result->completion_armed ||
		     result->status != GC573_BLOCK_WRITE_DONE))
		ret = -EIO;
	if (result->irq_status & GC573_IRQ_I2C)
		io->write(io->ctx, GC573_IRQ_STATUS, GC573_IRQ_I2C);
restore:
	io->write(io->ctx, GC573_BLOCK_DIVIDER, result->initial_divider);
	result->cleanup_status = io->read(io->ctx, GC573_BLOCK_STATUS);
	return ret;
}

int gc573_block_write_byte(const struct gc573_block_io *io,
			   struct gc573_block_result *result,
			   unsigned int subaddr, unsigned int value)
{
	return gc573_block_write_address(io, result, subaddr, value, 0x90);
}

int gc573_splitter_write(const struct gc573_block_io *io,
			struct gc573_block_result *result, unsigned int reg,
			unsigned int value)
{
	return gc573_block_write_address(io, result, reg, value, 0x58);
}

int gc573_splitter_timer_write(const struct gc573_block_io *io,
			      struct gc573_block_result *result, unsigned int reg,
			      unsigned int value)
{
	*result = (struct gc573_block_result) { 0 };
	if (reg < 0x11 || reg > 0x13)
		return -EINVAL;
	return gc573_block_write_address(io, result, reg, value, 0x96);
}

int gc573_splitter_map_write(const struct gc573_block_io *io,
			    struct gc573_block_result *result, unsigned int reg,
			    unsigned int value)
{
	*result = (struct gc573_block_result) { 0 };
	if (reg != 0x50 && (reg < 0x2c || reg > 0x2f))
		return -EINVAL;
	return gc573_block_write_address(io, result, reg, value, 0x96);
}

static int splitter_rx_control_register(unsigned int reg)
{
	return reg == 0x08 || reg == 0x0f || (reg >= 0x22 && reg <= 0x2a) ||
	       (reg >= 0x3a && reg <= 0x3c) || reg == 0x48 ||
	       (reg >= 0xa0 && reg <= 0xa2) || reg == 0xa7 || reg == 0xce;
}

int gc573_splitter_rx_control_read(const struct gc573_block_io *io,
				  struct gc573_block_result *result, unsigned int reg)
{
	*result = (struct gc573_block_result) { 0 };
	if (!splitter_rx_control_register(reg) && reg != 0x59 && reg != 0x5a)
		return -EINVAL;
	return gc573_block_identify_state(io, result, 0, reg, 1, 0x71);
}

int gc573_splitter_rx_control_write(const struct gc573_block_io *io,
				   struct gc573_block_result *result, unsigned int reg,
				   unsigned int value)
{
	*result = (struct gc573_block_result) { 0 };
	if (!splitter_rx_control_register(reg))
		return -EINVAL;
	return gc573_block_write_address(io, result, reg, value, 0x70);
}

/* Only offsets used by the post-CAOF setup at Windows 0x14004e284. */
static int splitter_rx_setup_register(unsigned int reg)
{
	return reg == 0x0f || reg == 0x23 || (reg >= 0x26 && reg <= 0x2a) ||
	       reg == 0x3b || reg == 0x3c || (reg >= 0x42 && reg <= 0x47) ||
	       reg == 0x49 || reg == 0x53 || reg == 0x56 || reg == 0x57 ||
	       reg == 0xa7 || reg == 0xa8 || reg == 0xce || reg == 0xe3 || reg == 0xf0;
}

int gc573_splitter_rx_setup_read(const struct gc573_block_io *io,
				struct gc573_block_result *result, unsigned int reg)
{
	*result = (struct gc573_block_result) { 0 };
	if (!splitter_rx_setup_register(reg))
		return -EINVAL;
	return gc573_block_identify_state(io, result, 0, reg, 1, 0x71);
}

int gc573_splitter_rx_setup_write(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int reg,
				 unsigned int value)
{
	*result = (struct gc573_block_result) { 0 };
	if (!splitter_rx_setup_register(reg))
		return -EINVAL;
	return gc573_block_write_address(io, result, reg, value, 0x70);
}

static int splitter_rx_finish_register(unsigned int reg)
{
	return reg == 0x0f || reg == 0x20 || reg == 0x21 ||
	       (reg >= 0x26 && reg <= 0x29) || (reg >= 0x53 && reg <= 0x55) ||
	       reg == 0x57 || reg == 0xab || reg == 0xac || reg == 0xc5;
}

int gc573_splitter_rx_finish_read(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int reg)
{
	*result = (struct gc573_block_result) { 0 };
	if (!splitter_rx_finish_register(reg) && reg != 0x13 && reg != 0x19)
		return -EINVAL;
	return gc573_block_identify_state(io, result, 0, reg, 1, 0x71);
}

int gc573_splitter_rx_finish_write(const struct gc573_block_io *io,
				  struct gc573_block_result *result, unsigned int reg,
				  unsigned int value)
{
	*result = (struct gc573_block_result) { 0 };
	if (!splitter_rx_finish_register(reg))
		return -EINVAL;
	return gc573_block_write_address(io, result, reg, value, 0x70);
}

int gc573_splitter_tx_reset_read(const struct gc573_block_io *io,
				struct gc573_block_result *result)
{
	return gc573_block_identify_state(io, result, 0, 0x20, 1, 0x97);
}

int gc573_splitter_tx_reset_write(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int value)
{
	*result = (struct gc573_block_result) { 0 };
	if (value != 0 && value != 2)
		return -EINVAL;
	return gc573_block_write_address(io, result, 0x20, value, 0x96);
}

int gc573_splitter_tx_port_read(const struct gc573_block_io *io,
			       struct gc573_block_result *result, unsigned int port,
			       unsigned int reg)
{
	*result = (struct gc573_block_result) { 0 };
	if (port > 3 || (reg != 0x03 && reg != 0x01 && reg != 0x84 &&
			reg != 0x86 && reg != 0x88))
		return -EINVAL;
	return gc573_block_identify_state(io, result, 0, reg, 1, 0x69 + port * 2);
}

static int splitter_tx_control_register(unsigned int port, unsigned int reg)
{
	if (port > 3)
		return 0;
	if ((!port && reg == 0x03) || reg == 0x41 || reg == 0xc1)
		return 1;
	if ((port == 1 || port == 2) &&
	    (reg == 0x02 || reg == 0x08 || reg == 0x34 || reg == 0x3a || reg == 0x41 ||
	     reg == 0x8a || reg == 0x8b || reg == 0x93 || reg == 0xc0 || reg == 0xc1 ||
	     reg == 0xc3))
		return 1;
	return reg == 0x01 || (reg >= 0x18 && reg <= 0x1c) || reg == 0x35 ||
	       reg == 0x84 || reg == 0x86 || reg == 0x88 || reg == 0x94;
}

int gc573_splitter_tx_control_read(const struct gc573_block_io *io,
				   struct gc573_block_result *result, unsigned int port,
				   unsigned int reg)
{
	*result = (struct gc573_block_result) { 0 };
	if (!splitter_tx_control_register(port, reg))
		return -EINVAL;
	return gc573_block_identify_state(io, result, 0, reg, 1, 0x69 + port * 2);
}

int gc573_splitter_tx_control_write(const struct gc573_block_io *io,
				    struct gc573_block_result *result, unsigned int port,
				    unsigned int reg, unsigned int value)
{
	*result = (struct gc573_block_result) { 0 };
	if (!splitter_tx_control_register(port, reg))
		return -EINVAL;
	return gc573_block_write_address(io, result, reg, value, 0x68 + port * 2);
}

int gc573_splitter_tx_route_read(const struct gc573_block_io *io,
				struct gc573_block_result *result)
{
	return gc573_block_identify_state(io, result, 0, 0x15, 1, 0x97);
}

int gc573_splitter_rx_event_read(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int reg)
{
	*result = (struct gc573_block_result) { 0 };
	if (!((reg >= 5 && reg <= 9) || (reg >= 0x10 && reg <= 0x15) ||
	      (reg >= 0x19 && reg <= 0x1b) || reg == 0x1d || reg == 0x26 ||
	      reg == 0x55 || reg == 0xc5))
		return -EINVAL;
	return gc573_block_identify_state(io, result, 0, reg, 1, 0x71);
}

int gc573_splitter_tx_route_write(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int value)
{
	*result = (struct gc573_block_result) { 0 };
	if (!(value & 8))
		return -EINVAL;
	return gc573_block_write_address(io, result, 0x15, value, 0x96);
}

int gc573_receiver_write_test(const struct gc573_block_io *io,
			      struct gc573_signal_result *signal,
			      struct gc573_write_test_result *result)
{
	int ret;

	*result = (struct gc573_write_test_result) { 0 };
	ret = gc573_receiver_status(io, signal);
	if (ret)
		return ret;
	/* Full bank byte must already equal the intended write value. */
	if (signal->bank != 0 || signal->controls[0].data[0] != 0xb1)
		return -ENODEV;
	if (!io->wait_write)
		return -EINVAL;
	ret = gc573_block_write_byte(io, &result->write, 0x0f, 0);
	if (ret)
		return ret;
	ret = gc573_block_identify_state(io, &result->verify, 0, 0x0f, 1, 0x91);
	if (ret)
		return ret;
	if (result->verify.data[0] != 0)
		return -EIO;
	result->bank_verified = 1;
	return 0;
}

int gc573_receiver_identify(const struct gc573_block_io *io,
			    struct gc573_receiver_result *receiver,
			    struct gc573_block_result *block)
{
	unsigned int i;
	int ret;

	*receiver = (struct gc573_receiver_result) { 0 };
	*block = (struct gc573_block_result) { 0 };
	if (io->read(io->ctx, 0) != 0x20201015 ||
	    io->read(io->ctx, GC573_BOARD_ID) != 0x57300102)
		return -ENODEV;
	receiver->gpio_before = io->read(io->ctx, GC573_GPIO);
	receiver->status_before = io->read(io->ctx, GC573_BLOCK_STATUS);
	if (io->read(io->ctx, GC573_IRQ_ENABLE) ||
	    io->read(io->ctx, GC573_IRQ_STATUS) ||
	    (receiver->status_before != 0 && receiver->status_before != 4 &&
	     receiver->status_before != 8))
		return -EBUSY;
	/* Match the observed board state, including GPIO 4/6 already high. */
	if (receiver->gpio_before == 0xeeeeeeee ||
	    receiver->gpio_before == 0xffffffff ||
	    (receiver->gpio_before & 0x50) != 0x50)
		return -ENODEV;
	ret = gc573_receiver_gpio(io, receiver, GC573_GPIO_PREPARE,
				 GC573_GPIO_PREPARE);
	if (ret)
		return ret;
	io->sleep_ms(io->ctx, 100);
	/* Windows receiver startup: GPIO 8 high, low, high; 2 ms per step. */
	for (i = 0; i < 3; i++) {
		ret = gc573_receiver_gpio(io, receiver, GC573_GPIO_RECEIVER,
					 i == 1 ? 0 : GC573_GPIO_RECEIVER);
		if (ret)
			return ret;
		io->sleep_ms(io->ctx, 2);
	}
	io->sleep_ms(io->ctx, 100);
	receiver->status_after = io->read(io->ctx, GC573_BLOCK_STATUS);
	receiver->setup_complete = 1;
	/* The vendor can issue another request after a failed read. The meaning
	 * of 0x08 is unresolved: permit it only in this explicit startup test.
	 */
	return gc573_block_identify_state(io, block, 1, 0, 4, 0x91);
}

int gc573_receiver_status(const struct gc573_block_io *io,
			  struct gc573_signal_result *result)
{
	/* Fixed bank/identity/status/control reads; no interrupt or FIFO regs. */
	static const unsigned char registers[] = {
		0x0f, 0x00, 0x13, 0x16, 0x19,
		0x04, 0x20, 0x25, 0x2a, 0x2d, 0x32, 0x35,
	};
	static const unsigned char lengths[] = { 1, 4, 1, 1, 1, 1, 4, 3, 2, 3, 1, 1 };
	static const unsigned char expected_id[] = { 0x54, 0x49, 0x05, 0x68 };
	unsigned int gpio, i, j;
	int ret;

	*result = (struct gc573_signal_result) { 0 };
	if (io->read(io->ctx, 0) != 0x20201015 ||
	    io->read(io->ctx, GC573_BOARD_ID) != 0x57300102)
		return -ENODEV;
	gpio = io->read(io->ctx, GC573_GPIO);
	if (gpio == 0xeeeeeeee || gpio == 0xffffffff ||
	    (gpio & 0x108) != 0x108)
		return -ENODEV;
	if (io->read(io->ctx, GC573_IRQ_ENABLE))
		return -EBUSY;
	for (i = 0; i < sizeof(registers); i++) {
		result->last_subaddr = registers[i];
		result->transactions++;
		ret = gc573_block_identify_state(io, &result->last, 0,
						registers[i], lengths[i], 0x91);
		if (ret)
			return ret;
		result->valid |= 1U << i;
		switch (i) {
		case 0:
			result->bank = result->last.data[0];
			/* Do not change receiver configuration to select a bank. */
			if (result->bank & 7)
				return -EOPNOTSUPP;
			break;
		case 1:
			for (j = 0; j < sizeof(result->id); j++)
				result->id[j] = result->last.data[j];
			for (j = 0; j < sizeof(result->id); j++)
				if (result->id[j] != expected_id[j])
					return -ENODEV;
			break;
		case 2:
			result->port0 = result->last.data[0];
			break;
		case 3:
			result->port1 = result->last.data[0];
			break;
		case 4:
			result->sync = result->last.data[0];
			break;
		default:
			result->controls[i - 5].subaddr = registers[i];
			result->controls[i - 5].length = lengths[i];
			for (j = 0; j < lengths[i]; j++)
				result->controls[i - 5].data[j] = result->last.data[j];
			break;
		}
	}
	return 0;
}

/* Splitter EDID mapping from official 0x1400498dc; SRAM is read only. */
int gc573_splitter_edid_control_read(const struct gc573_block_io *io,
				    struct gc573_block_result *r, unsigned int reg)
{
	*r = (struct gc573_block_result) { 0 };
	if (reg != 0x34 && reg != 0x4b && (reg < 0xc5 || reg > 0xca))
		return -EINVAL;
	return gc573_block_identify_state(io, r, 0, reg, 1, 0x71);
}

int gc573_splitter_edid_map(const struct gc573_block_io *io,
			    struct gc573_block_result *r)
{
	return gc573_block_write_address(io, r, 0x4b, 0xd9, 0x70);
}

int gc573_splitter_edid_memory_read(const struct gc573_block_io *io,
				   struct gc573_block_result *r, unsigned int offset)
{
	*r = (struct gc573_block_result) { 0 };
	if (offset > 252 || (offset & 3))
		return -EINVAL;
	return gc573_block_identify_state(io, r, 0, offset, 4, 0xd9);
}

int gc573_splitter_edid_control_write(const struct gc573_block_io *io,
				     struct gc573_block_result *r, unsigned int reg,
				     unsigned int value)
{
	*r = (struct gc573_block_result) { 0 };
	if (reg != 0x34 && (reg < 0xc5 || reg > 0xca))
		return -EINVAL;
	return gc573_block_write_address(io, r, reg, value, 0x70);
}

/* Port 1/2 clock/rate controls from 4d874. */
int gc573_splitter_video_tx_read(const struct gc573_block_io *io,
				struct gc573_block_result *r, unsigned int port, unsigned int reg)
{
	*r = (struct gc573_block_result) { 0 };
	if (port != 1 && port != 2)
		return -EINVAL;
	if (reg != 1 && reg != 3 && reg != 6 && reg != 7 && (reg < 0x10 || reg > 0x15) &&
	    reg != 0x18 && reg != 0x1a && reg != 0x3a && (reg < 0x83 || reg > 0x8b) && reg != 0x91 &&
	    reg != 0x94 && reg != 0xaf && (reg < 0xc0 || reg > 0xc3))
		return -EINVAL;
	return gc573_block_identify_state(io, r, 0, reg, 1, 0x69 + 2 * port);
}

int gc573_splitter_video_tx_write(const struct gc573_block_io *io,
				 struct gc573_block_result *r, unsigned int port, unsigned int reg,
				 unsigned int value)
{
	*r = (struct gc573_block_result) { 0 };
	if (port != 1 && port != 2)
		return -EINVAL;
	if (reg != 1 && reg != 7 && (reg < 0x10 || reg > 0x15) && reg != 0x18 &&
	    reg != 0x1a && reg != 0x3a && reg != 0x83 && reg != 0x84 && reg != 0x85 && (reg < 0x87 || reg > 0x8b) &&
	    reg != 0x91 && reg != 0x94 && reg != 0xaf && (reg < 0xc0 || reg > 0xc3))
		return -EINVAL;
	return gc573_block_write_address(io, r, reg, value, 0x68 + 2 * port);
}

int gc573_splitter_video_rx_read(const struct gc573_block_io *io,
				struct gc573_block_result *r, unsigned int reg)
{
	*r = (struct gc573_block_result) { 0 };
	if (reg != 0x98 && reg != 0x0f && reg != 0x15 && reg != 0xcf && reg != 0x13 &&
		(reg < 0x9b || reg > 0xaa))
		return -EINVAL;
	return gc573_block_identify_state(io, r, 0, reg, 1, 0x71);
}

/* External TX2 DDC engine only; EDID reads and HDMI 2.0 SCDC only. */
int gc573_splitter_ddc_read(const struct gc573_block_io *io,
                           struct gc573_block_result *r, unsigned int reg)
{
	*r = (struct gc573_block_result) { 0 };
	if (reg != 3 && reg != 0x19 && reg != 0x1d && (reg < 0x28 || reg > 0x30))
		return -EINVAL;
	return gc573_block_identify_state(io, r, 0, reg, 1, 0x6d);
}
int gc573_splitter_ddc_write(const struct gc573_block_io *io,
							struct gc573_block_result *r, unsigned int reg, unsigned int value)
{
	*r = (struct gc573_block_result) { 0 };
	if (reg != 0x19 && reg != 0x1d && (reg < 0x28 || reg > 0x2e) && reg != 0x30)
		return -EINVAL;
	if (value > 255 || (reg == 0x29 && value != 0xa0 && value != 0xa8) ||
		(reg == 0x2e && value != 9 && value != 3 && value != 15 && value != 0 && value != 1))
		return -EINVAL;
	return gc573_block_write_address(io, r, reg, value, 0x6c);
}

int gc573_splitter_edid_memory_write(const struct gc573_block_io *io,
                                     struct gc573_block_result *r, unsigned int offset, unsigned int value)
{
	*r = (struct gc573_block_result) { 0 };
	if (offset > 255 || value > 255) return -EINVAL;
	return gc573_block_write_address(io,r,offset,value,0xd8);
}

int gc573_splitter_passthrough_rx_read(const struct gc573_block_io *io,
                                      struct gc573_block_result *r, unsigned int reg)
{
	*r=(struct gc573_block_result){0};
	if(reg!=15 && reg!=0x15 && reg!=0x98 && (reg<0x9b || reg>0xaa) &&
       reg!=0xab && reg!=0xac && reg!=0x26 && reg!=0x55 && reg!=0x34 &&
       (reg<0xc5 || reg>0xca)) return -EINVAL;
	return gc573_block_identify_state(io,r,0,reg,1,0x71);
}
int gc573_splitter_passthrough_rx_write(const struct gc573_block_io *io,
                                       struct gc573_block_result *r, unsigned int reg, unsigned int value)
{
	*r=(struct gc573_block_result){0};
	if(reg!=15 && reg!=0xab && reg!=0xac && reg!=0x26 && reg!=0x55 && reg!=0x34 &&
       (reg<0xc5 || reg>0xca)) return -EINVAL;
	if(value>255 || (reg==15 && value!=0 && value!=2 && value!=3)) return -EINVAL;
	return gc573_block_write_address(io,r,reg,value,0x70);
}
