/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_BLOCK_H
#define GC573_BLOCK_H

#define GC573_BLOCK_DIVIDER 0x180
#define GC573_BLOCK_TEST_DIVIDER 0x4e2
#define GC573_BLOCK_ADDRESS 0x184
#define GC573_BLOCK_SUBADDR_WIDTH 0x188
#define GC573_BLOCK_FIFO_WIDTH 0x18c
#define GC573_BLOCK_SUBADDR 0x190
#define GC573_BLOCK_LENGTH 0x194
#define GC573_BLOCK_RX 0x19c
#define GC573_BLOCK_TX 0x198
#define GC573_BLOCK_COMMAND 0x1a0
#define GC573_BLOCK_STATUS 0x1a4
#define GC573_BLOCK_READ_DONE 0x04
#define GC573_BLOCK_WRITE_DONE 0x01
#define GC573_IRQ_STATUS 0x10
#define GC573_IRQ_ENABLE 0x1c
#define GC573_IRQ_I2C 0x800
#define GC573_GPIO 0x40
#define GC573_GPIO_PREPARE 0x08
#define GC573_GPIO_RECEIVER 0x100
#define GC573_BOARD_ID 0x64

struct gc573_block_io {
	void *ctx;
	unsigned int (*read)(void *ctx, unsigned int offset);
	void (*write)(void *ctx, unsigned int offset, unsigned int value);
	int (*wait)(void *ctx, unsigned int *status, unsigned int *saw_clear);
	void (*sleep_ms)(void *ctx, unsigned int milliseconds);
	int (*wait_write)(void *ctx, unsigned int *status, unsigned int *saw_clear);
	unsigned long (*time_ms)(void *ctx);
	/* Optional runtime gate: no capture/audio accesses until HDMI setup ends. */
	int (*ready)(void *ctx);
	void (*control_lock)(void *ctx);
	void (*control_unlock)(void *ctx);
	unsigned int owned_irq_mask; /* Runtime video/audio IRQs, never I2C. */
	/* Issue READ START and sample status before CPU scheduling can hide busy. */
	unsigned int (*start_read)(void *ctx);
};

struct gc573_gpio_result {
	unsigned int fpga_id;
	unsigned int board_id;
	unsigned int before;
	unsigned int after;
	unsigned int changed;
	unsigned int status_before;
	unsigned int status_100ms;
	unsigned int status_2000ms;
	unsigned int irq_enable;
	unsigned int irq_before;
	unsigned int irq_after;
	unsigned int samples;
};

struct gc573_receiver_result {
	unsigned int gpio_before;
	unsigned int gpio_after;
	unsigned int status_before;
	unsigned int status_after;
	unsigned int steps;
	unsigned int setup_complete;
};

struct gc573_block_result {
	unsigned int initial_status;
	unsigned int initial_divider;
	unsigned int prepared_status;
	unsigned int start_status;
	unsigned int completion_armed;
	unsigned int started;
	unsigned int irq_enable;
	unsigned int status;
	unsigned int irq_status;
	unsigned int cleanup_status;
	unsigned int bytes_read;
	unsigned char data[4];
};

#define GC573_CONTROL_READS 7

struct gc573_control_sample {
	unsigned char subaddr;
	unsigned char length;
	unsigned char data[4];
};

struct gc573_signal_result {
	unsigned int valid;
	unsigned int transactions;
	unsigned int last_subaddr;
	unsigned char bank;
	unsigned char id[4];
	unsigned char port0;
	unsigned char port1;
	unsigned char sync;
	struct gc573_control_sample controls[GC573_CONTROL_READS];
	struct gc573_block_result last;
};

struct gc573_receiver_video_result {
	unsigned int phase, transactions, writes_started, last_reg, bank, bank_verified;
	unsigned int timing_samples, width, height, htotal, vtotal, interlaced, complete;
	unsigned int expected, observed, pixel_min_khz, pixel_max_khz, steps_verified, output_enabled;
	unsigned int clock_samples, counts[5], reference_half_khz, measurement_restored;
	unsigned char timing[2][19], extra[4], avi[5], output[7];
	struct gc573_block_result last;
};
int gc573_receiver_video(const struct gc573_block_io *io,
			 struct gc573_signal_result *signal,
			 struct gc573_receiver_video_result *r, unsigned int enable);
int gc573_receiver_video_retryable(const struct gc573_receiver_video_result *r, int error);

struct gc573_write_test_result {
	struct gc573_block_result write;
	struct gc573_block_result verify;
	unsigned int bank_verified;
};

struct gc573_splitter_result {
	unsigned int gpio, transactions, last_subaddr, bank_valid, id_valid;
	unsigned int id_matches;
	unsigned char bank, id[4];
	struct gc573_block_result last;
};

#define GC573_SPLITTER_LINK_REGS 18
extern const unsigned char gc573_splitter_link_regs[GC573_SPLITTER_LINK_REGS];

struct gc573_splitter_link_result {
	unsigned int phase, transactions, last_address, last_reg;
	unsigned int mapping_verified, rx_identity_verified, rx_valid, tx_valid;
	unsigned int irq_valid, banks_verified, complete;
	unsigned char irq_before, irq_after, rx[GC573_SPLITTER_LINK_REGS], tx[4];
	int prerequisite_error;
	struct gc573_block_result last;
};

int gc573_splitter_link_status(const struct gc573_block_io *io,
			       struct gc573_splitter_result *identity,
			       struct gc573_splitter_link_result *result);
int gc573_splitter_rx_event_read(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int reg);

#define GC573_SPLITTER_EDID_REGS 8
extern const unsigned char gc573_splitter_edid_regs[GC573_SPLITTER_EDID_REGS];
struct gc573_splitter_edid_result {
	unsigned int phase, transactions, writes_started, control_valid;
	unsigned int mapping_verified, bytes_read, last_reg, complete, header_matches;
	unsigned char control[GC573_SPLITTER_EDID_REGS], mapping_after;
	unsigned char data[256], checksum[2];
	int prerequisite_error;
	struct gc573_block_result last;
};
int gc573_splitter_edid_read(const struct gc573_block_io *io,
			    struct gc573_splitter_result *identity,
			    struct gc573_splitter_link_result *link,
			    struct gc573_splitter_edid_result *result);
int gc573_splitter_edid_control_read(const struct gc573_block_io *io,
				    struct gc573_block_result *r, unsigned int reg);
int gc573_splitter_edid_map(const struct gc573_block_io *io,
			    struct gc573_block_result *r);
int gc573_splitter_edid_memory_read(const struct gc573_block_io *io,
				   struct gc573_block_result *r, unsigned int offset);

struct gc573_splitter_video_result {
	unsigned int phase, transactions, writes_started, steps_verified;
	unsigned int last_address, last_reg, expected, observed, reference_ticks, reference_khz;
	unsigned int initial_count, exponent, samples, counts[10], depth;
	unsigned int pixel_khz, link_khz, rate_valid, complete;
	unsigned int irq_valid, analog_complete, output_setup_complete;
	unsigned char irq_before[6];
	unsigned int bank, bank_verified, avi_color, rx_cf, rx13, format_valid;
	unsigned int tx_status, output_enabled, waiting_link;
	int prerequisite_error;
	struct gc573_block_result last;
};
int gc573_splitter_video_rate(unsigned int reference_khz, unsigned int sum,
			       unsigned int exponent, unsigned int depth,
			       unsigned int *pixel_khz, unsigned int *link_khz);
int gc573_splitter_video_clock(const struct gc573_block_io *io,
			       struct gc573_splitter_result *identity,
			       struct gc573_splitter_link_result *link,
			       struct gc573_splitter_video_result *r, unsigned int configure, unsigned int port);
int gc573_splitter_video_tx_read(const struct gc573_block_io *io,
				struct gc573_block_result *r, unsigned int port, unsigned int reg);
int gc573_splitter_video_tx_write(const struct gc573_block_io *io,
				 struct gc573_block_result *r, unsigned int port, unsigned int reg,
				 unsigned int value);
int gc573_splitter_video_rx_read(const struct gc573_block_io *io,
				struct gc573_block_result *r, unsigned int reg);

/* Startup snapshot, not a live monitor of the external display. */
struct gc573_passthrough_result {
	unsigned int phase, sink_present, tx_status, preserved, enabled;
};
int gc573_splitter_passthrough(const struct gc573_block_io *io,
			      struct gc573_passthrough_result *r);

#define GC573_SPLITTER_HPD_SAMPLES 20
struct gc573_splitter_hpd_result {
	unsigned int phase, transactions, writes_started, steps_completed, steps_verified;
	unsigned int last_address, last_reg, expected, observed, bank, bank_verified;
	unsigned int sink_mask, ab_valid, setup_complete, samples, lock_seen;
	unsigned int edid_configured, c1_readback, c1_valid;
	unsigned char edid_checksum[2];
	unsigned char ab_before, poll[GC573_SPLITTER_HPD_SAMPLES][2];
	int prerequisite_error;
	struct gc573_block_result last;
};

int gc573_splitter_input_hpd(const struct gc573_block_io *io,
			    struct gc573_splitter_result *identity,
			    struct gc573_splitter_link_result *link,
			    struct gc573_splitter_hpd_result *result);

int gc573_splitter_edid_enable(const struct gc573_block_io *io,
			       struct gc573_splitter_result *identity,
			       struct gc573_splitter_link_result *link,
			       struct gc573_splitter_edid_result *edid,
			       struct gc573_splitter_hpd_result *result);
int gc573_splitter_edid_control_write(const struct gc573_block_io *io,
				     struct gc573_block_result *r, unsigned int reg,
				     unsigned int value);

int gc573_splitter_port_activate(const struct gc573_block_io *io,
				 struct gc573_splitter_result *identity,
				 struct gc573_splitter_link_result *link,
				 struct gc573_splitter_hpd_result *result, unsigned int port);

int gc573_splitter_identify(const struct gc573_block_io *io,
			   struct gc573_splitter_result *result);

struct gc573_splitter_start_result {
	unsigned int gpio_before, gpio_after, status_before, status_after;
	unsigned int preflight_complete, writes_started, steps_completed, complete;
	unsigned int resumed, gpio_settle_changed, status_after_valid;
	unsigned int gpio_steps[6];
};

int gc573_splitter_startup(const struct gc573_block_io *io,
			   struct gc573_splitter_start_result *startup,
			   struct gc573_splitter_result *result);

int gc573_splitter_read(const struct gc573_block_io *io,
		       struct gc573_block_result *result, unsigned int reg);
int gc573_splitter_write(const struct gc573_block_io *io,
			struct gc573_block_result *result, unsigned int reg,
			unsigned int value);

struct gc573_splitter_prepare_result {
	unsigned int phase, preflight_complete, transactions, writes_started;
	unsigned int bank, bank_verified, last_reg, expected, observed;
	unsigned int engine_valid, engine_before, engine_status, recovery_used;
	unsigned int poll_samples, ready, complete;
	int prerequisite_error;
	struct gc573_block_result last;
};

int gc573_splitter_prepare(const struct gc573_block_io *io,
			  struct gc573_splitter_result *identity,
			  struct gc573_splitter_prepare_result *result);

struct gc573_splitter_clock_result {
	struct gc573_splitter_prepare_result ops;
	unsigned int data_valid, words[4], selector_base, raw, khz, valid;
	unsigned int cleanup_complete;
	int measurement_error;
};

int gc573_splitter_clock_decode(unsigned int low, unsigned int high,
			       unsigned int *raw, unsigned int *khz);
int gc573_splitter_clock(const struct gc573_block_io *io,
			struct gc573_splitter_result *identity,
			struct gc573_splitter_clock_result *result);

int gc573_splitter_timer_read(const struct gc573_block_io *io,
			     struct gc573_block_result *result, unsigned int reg);
int gc573_splitter_timer_write(const struct gc573_block_io *io,
			      struct gc573_block_result *result, unsigned int reg,
			      unsigned int value);

struct gc573_splitter_timing_values {
	unsigned int ticks;
	unsigned char bytes[5];
};

struct gc573_splitter_timing_result {
	unsigned int phase, transactions, writes_started, steps_verified, complete;
	unsigned int last_address, last_reg, expected, observed, mapping_verified;
	unsigned int before_valid, after_valid, bank_verified;
	unsigned char before[5], after[5];
	int clock_error;
	struct gc573_splitter_timing_values values;
	struct gc573_block_result last;
};

int gc573_splitter_timing_compute(unsigned int khz,
				 struct gc573_splitter_timing_values *values);
int gc573_splitter_timing(const struct gc573_block_io *io,
			 struct gc573_splitter_result *identity,
			 struct gc573_splitter_clock_result *clock,
			 struct gc573_splitter_timing_result *result);

int gc573_splitter_map_read(const struct gc573_block_io *io,
			   struct gc573_block_result *result, unsigned int reg);
int gc573_splitter_map_write(const struct gc573_block_io *io,
			    struct gc573_block_result *result, unsigned int reg,
			    unsigned int value);
int gc573_splitter_rx_read(const struct gc573_block_io *io,
			  struct gc573_block_result *result, unsigned int reg,
			  unsigned int length);

struct gc573_splitter_map_result {
	unsigned int phase, transactions, writes_started, steps_verified, complete;
	unsigned int last_address, last_reg, expected, observed, tx_mapping_verified;
	unsigned int rx_mapping_verified, before_valid, after_valid, bank_verified;
	unsigned int rx_valid, rx_bank_verified;
	unsigned char before[5], after[5], rx_bank, rx_id[4], rx_reset[3], rx_c5;
	int timing_error;
	struct gc573_block_result last;
};

int gc573_splitter_map(const struct gc573_block_io *io,
		      struct gc573_splitter_result *identity,
		      struct gc573_splitter_clock_result *clock,
		      struct gc573_splitter_timing_result *timing,
		      struct gc573_splitter_map_result *result);

int gc573_splitter_rx_control_read(const struct gc573_block_io *io,
				  struct gc573_block_result *result, unsigned int reg);
int gc573_splitter_rx_control_write(const struct gc573_block_io *io,
				   struct gc573_block_result *result, unsigned int reg,
				   unsigned int value);

struct gc573_splitter_cal_result {
	unsigned int phase, transactions, writes_started, steps_completed;
	unsigned int preflight_complete, reset_complete, bank, bank_verified;
	unsigned int last_reg, last_value, initial_flags, flags, flags_valid;
	unsigned int poll_samples, completion_seen, values_valid, cleanup_complete;
	unsigned int complete;
	unsigned char values[3];
	int prerequisite_error, poll_error;
	struct gc573_block_result last;
};

int gc573_splitter_calibrate(const struct gc573_block_io *io,
			    struct gc573_splitter_result *identity,
			    struct gc573_splitter_clock_result *clock,
			    struct gc573_splitter_timing_result *timing,
			    struct gc573_splitter_map_result *map,
			    struct gc573_splitter_cal_result *result);

int gc573_splitter_rx_setup_read(const struct gc573_block_io *io,
				struct gc573_block_result *result, unsigned int reg);
int gc573_splitter_rx_setup_write(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int reg,
				 unsigned int value);

struct gc573_splitter_setup_result {
	unsigned int phase, transactions, writes_started, steps_completed;
	unsigned int steps_verified, last_step, bank, bank_verified, complete;
	unsigned int last_reg, expected, observed;
	int prerequisite_error;
	struct gc573_block_result last;
};

int gc573_splitter_setup(const struct gc573_block_io *io,
			struct gc573_splitter_result *identity,
			struct gc573_splitter_clock_result *clock,
			struct gc573_splitter_timing_result *timing,
			struct gc573_splitter_map_result *map,
			struct gc573_splitter_cal_result *cal,
			struct gc573_splitter_setup_result *result);

int gc573_splitter_rx_finish_read(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int reg);
int gc573_splitter_rx_finish_write(const struct gc573_block_io *io,
				  struct gc573_block_result *result, unsigned int reg,
				  unsigned int value);

struct gc573_splitter_finish_result {
	unsigned int phase, transactions, writes_started, steps_completed, steps_verified;
	unsigned int bank, bank_verified, control_bank_verified, last_address, last_reg;
	unsigned int expected, observed, ab_before, ab_valid, ab_ca_branch;
	unsigned int status_valid, complete;
	unsigned char status[2];
	int prerequisite_error;
	struct gc573_block_result last;
};

int gc573_splitter_finish(const struct gc573_block_io *io,
			 struct gc573_splitter_result *identity,
			 struct gc573_splitter_clock_result *clock,
			 struct gc573_splitter_timing_result *timing,
			 struct gc573_splitter_map_result *map,
			 struct gc573_splitter_cal_result *cal,
			 struct gc573_splitter_setup_result *setup,
			 struct gc573_splitter_finish_result *result);

int gc573_splitter_tx_reset_read(const struct gc573_block_io *io,
				struct gc573_block_result *result);
int gc573_splitter_tx_reset_write(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int value);
int gc573_splitter_tx_port_read(const struct gc573_block_io *io,
			       struct gc573_block_result *result, unsigned int port,
			       unsigned int reg);

struct gc573_splitter_tx_result {
	unsigned int phase, transactions, writes_started, steps_verified;
	unsigned int last_address, last_reg, expected, observed, common_mapping_verified;
	unsigned int mapping_valid, port_valid[4], bank_verified, control_bank_verified;
	unsigned int reset_complete, complete, status_valid;
	unsigned char mapping[4], ports[4][5], status[2];
	int prerequisite_error;
	struct gc573_block_result last;
};

int gc573_splitter_tx_prepare(const struct gc573_block_io *io,
			     struct gc573_splitter_result *identity,
			     struct gc573_splitter_clock_result *clock,
			     struct gc573_splitter_timing_result *timing,
			     struct gc573_splitter_map_result *map,
			     struct gc573_splitter_cal_result *cal,
			     struct gc573_splitter_setup_result *setup,
			     struct gc573_splitter_finish_result *finish,
			     struct gc573_splitter_tx_result *result);

int gc573_splitter_tx_control_read(const struct gc573_block_io *io,
				   struct gc573_block_result *result, unsigned int port,
				   unsigned int reg);
int gc573_splitter_tx_control_write(const struct gc573_block_io *io,
				    struct gc573_block_result *result, unsigned int port,
				    unsigned int reg, unsigned int value);

struct gc573_splitter_ports_result {
	unsigned int phase, transactions, writes_started, steps_completed, steps_verified;
	unsigned int port, last_address, last_reg, expected, observed, mapping_verified;
	unsigned int ports_complete, no_sink_mask, sink_valid[4], snapshot_valid[4];
	unsigned int port_count, common_enabled, tail_complete;
	unsigned int bank_verified, control_bank_verified, status_valid, complete;
	unsigned char sink[4][2], snapshot[4][5], status[2];
	int prerequisite_error;
	struct gc573_block_result last;
};

int gc573_splitter_tx_ports(const struct gc573_block_io *io,
			   struct gc573_splitter_result *identity,
			   struct gc573_splitter_clock_result *clock,
			   struct gc573_splitter_timing_result *timing,
			   struct gc573_splitter_map_result *map,
			   struct gc573_splitter_cal_result *cal,
			   struct gc573_splitter_setup_result *setup,
			   struct gc573_splitter_finish_result *finish,
			   struct gc573_splitter_tx_result *tx,
			   struct gc573_splitter_ports_result *result, unsigned int extent);

int gc573_splitter_tx_route_read(const struct gc573_block_io *io,
				struct gc573_block_result *result);
int gc573_splitter_tx_route_write(const struct gc573_block_io *io,
				 struct gc573_block_result *result, unsigned int value);

int gc573_block_identify(const struct gc573_block_io *io,
			struct gc573_block_result *result);
int gc573_block_observe(unsigned int status, unsigned int *saw_clear);
int gc573_block_observe_write(unsigned int status, unsigned int *saw_clear);
int gc573_block_prepare_gpio(const struct gc573_block_io *io,
			     struct gc573_gpio_result *result);
int gc573_receiver_identify(const struct gc573_block_io *io,
			    struct gc573_receiver_result *receiver,
			    struct gc573_block_result *block);
int gc573_receiver_status(const struct gc573_block_io *io,
			  struct gc573_signal_result *result);
int gc573_receiver_write_test(const struct gc573_block_io *io,
			      struct gc573_signal_result *signal,
			      struct gc573_write_test_result *result);
int gc573_block_read_registers(const struct gc573_block_io *io,
			       struct gc573_block_result *result,
			       unsigned int subaddr, unsigned int length);
int gc573_block_write_byte(const struct gc573_block_io *io,
			   struct gc573_block_result *result,
			   unsigned int subaddr, unsigned int value);
int gc573_block_read_edid(const struct gc573_block_io *io,
			  struct gc573_block_result *result,
			  unsigned int subaddr, unsigned int length);

struct gc573_init_result {
	unsigned int steps_completed;
	unsigned int writes_started;
	unsigned int last_step;
	unsigned int last_bank;
	unsigned int last_reg;
	unsigned int last_value;
	unsigned int bank_verified;
	unsigned int table_complete;
	unsigned int preflight_complete;
	unsigned int post_attempted;
	int post_error;
	struct gc573_block_result last;
};

int gc573_receiver_init(const struct gc573_block_io *io,
			struct gc573_signal_result *signal,
			struct gc573_init_result *result);

struct gc573_cal_result {
	unsigned int phase, steps, writes_started, bank, bank_verified;
	unsigned int last_reg, last_value, preflight_complete;
	unsigned int poll_samples, completion_seen, cleanup_complete, post_attempted;
	unsigned int flags_valid, results_valid;
	unsigned char initial_flags[2], flags[2], values[2][3];
	int poll_error, post_error;
	struct gc573_block_result last;
};

int gc573_receiver_calibrate(const struct gc573_block_io *io,
			     struct gc573_signal_result *signal,
			     struct gc573_cal_result *result);

struct gc573_clock_result {
	unsigned int phase, bank, bank_verified, writes_started, last_reg;
	unsigned int preflight_complete, recovery_used, ready_samples, ready;
	unsigned int selector_base, data_valid, raw_value, khz, clock_valid;
	unsigned int cleanup_complete, post_attempted;
	unsigned char engine_status, output_mode, data[4][2];
	int measurement_error, post_error;
	struct gc573_block_result last;
};

int gc573_receiver_clock(const struct gc573_block_io *io,
			 struct gc573_signal_result *signal,
			 struct gc573_clock_result *result);

struct gc573_timing_values {
	unsigned int half_khz, adjusted_khz;
	unsigned char reg91, reg92, regfd, reg45, reg44, reg46, reg47;
};

struct gc573_timing_result {
	unsigned int phase, bank, bank_verified, writes_started, steps_verified;
	unsigned int last_reg, expected, observed, complete, post_attempted;
	int clock_error, post_error;
	struct gc573_timing_values values;
	struct gc573_block_result last;
};

int gc573_timing_compute(unsigned int khz, struct gc573_timing_values *values);
int gc573_receiver_timing(const struct gc573_block_io *io,
			  struct gc573_signal_result *signal,
			  struct gc573_clock_result *clock,
			  struct gc573_timing_result *result);

struct gc573_edid_result {
	unsigned int phase, mapping_written, mapping_verified, bytes_read, complete;
	unsigned int last_offset, header_matches, post_attempted;
	unsigned char mapping_before, mapping_after, checksum[2], data[256];
	int prerequisite_error, post_error;
	struct gc573_block_result last;
};

int gc573_receiver_edid_read(const struct gc573_block_io *io,
			     struct gc573_signal_result *signal,
			     struct gc573_clock_result *clock,
			     struct gc573_timing_result *timing,
			     struct gc573_edid_result *result);

struct gc573_ddc_plan {
	unsigned char base_checksum, extension_checksum[2], physical_offset;
};

struct gc573_ddc_result {
	unsigned int phase, bank, bank_verified, writes_started, steps_completed;
	unsigned int last_reg, expected, observed, gpio, before_valid, after_valid;
	unsigned int complete, post_attempted;
	unsigned char before[2][6], after[2][6];
	int prerequisite_error, post_error;
	struct gc573_ddc_plan plan;
	struct gc573_block_result last;
};

int gc573_ddc_plan(const unsigned char data[256], struct gc573_ddc_plan *plan);
int gc573_receiver_ddc(const struct gc573_block_io *io,
		       struct gc573_signal_result *signal,
		       struct gc573_clock_result *clock,
		       struct gc573_timing_result *timing,
		       struct gc573_edid_result *edid,
		       struct gc573_ddc_result *result);

struct gc573_input_result {
	unsigned int phase, bank, bank_verified, writes_started, steps_completed;
	unsigned int last_reg, expected, observed, setup_complete, hpd_written;
	unsigned int gpio_before, gpio_after, hpd_verified, samples, lock_seen;
	unsigned int post_attempted;
	unsigned char poll[20][2];
	int prerequisite_error, post_error;
	struct gc573_block_result last;
};

int gc573_receiver_input(const struct gc573_block_io *io,
			 struct gc573_signal_result *signal,
			 struct gc573_clock_result *clock,
			 struct gc573_timing_result *timing,
			 struct gc573_edid_result *edid,
			 struct gc573_ddc_result *ddc,
			 struct gc573_input_result *result);
int gc573_splitter_ddc_read(const struct gc573_block_io *io,
	struct gc573_block_result *r, unsigned int reg);
int gc573_splitter_ddc_write(const struct gc573_block_io *io,
	struct gc573_block_result *r, unsigned int reg, unsigned int value);

int gc573_splitter_edid_memory_write(const struct gc573_block_io *io,
    struct gc573_block_result *r, unsigned int offset, unsigned int value);
int gc573_splitter_passthrough_rx_read(const struct gc573_block_io *io,
    struct gc573_block_result *r, unsigned int reg);
int gc573_splitter_passthrough_rx_write(const struct gc573_block_io *io,
    struct gc573_block_result *r, unsigned int reg, unsigned int value);
int gc573_splitter_port_ddc_read(const struct gc573_block_io *io,
    struct gc573_block_result *r, unsigned int port, unsigned int reg);
int gc573_splitter_port_ddc_write(const struct gc573_block_io *io,
    struct gc573_block_result *r, unsigned int port, unsigned int reg, unsigned int value);
#endif
