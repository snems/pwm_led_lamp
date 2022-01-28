#include <hal.h>
#include "ir.h"
#include "config.h"

#define IR_NUMBER_OF_BITS_PER_COMMAND   96
#define IR_BITMAP_RECEIVED_SIZE         12
#define IR_MEASURES_PER_BIT   3u   /** Measures per bit, depends on tick time. Maximum 32. */

#define IR_REPEAT_TIMEOUT                                       120u /** How long we will waiting for repeat. */
#define IR_TIMER_10_MSEC                                      40000u /** 10 milliseconds. */
#define IR_SYNC_LEADING_PULSE_MAX_TICKS                       40000u /** Maximum timer ticks of first part of synchronisation. */
#define IR_SYNC_LEADING_PULSE_MIN_TICKS                       32000u /** Minimum timer ticks of first part of synchronisation. */
#define IR_SYNC_SPACE_LEADING_SPACE_MAX_TICKS                 20000u /** Maximum timer ticks of second part of synchronisation. */
#define IR_SYNC_SPACE_LEADING_SPACE_MIN_TICKS                 16000u /** Minimum timer ticks of second part of synchronisation. */

#define IR_REPEAT_PAUSE_MIN_TIME                                 90u /** Time between repeat codes. */
#define IR_REPEAT_LEADING_PULSE_MAX_TICKS                     40000u /** Maximum timer ticks of first part of repeating. */
#define IR_REPEAT_LEADING_PULSE_MIN_TICKS                     32000u /** Minimum timer ticks of first part of repeating. */
#define IR_REPEAT_SPACE_LEADING_SPACE_MAX_TICKS               10000u /** Maximum timer ticks of second part of repeating. */
#define IR_REPEAT_SPACE_LEADING_SPACE_MIN_TICKS                8000u /** Minimum timer ticks of second part of repeating. */



static struct
{
	GPTDriver                 *gpt;                      /** Timer driver context. */
	GPTConfig                 gpt_config;                /** Timer configuration. */
	struct
	{
		ir_command_callback_t  *callback;                                           /** Callback for received commands. */
		void                                *callback_context;                      /** Context for callback. */
		uint8_t                             received_bits[IR_BITMAP_RECEIVED_SIZE]; /** Array for measurement results. */
		uint16_t                            last_address;                           /** Last received address. */
		uint8_t                             last_command;                           /** Last received command. */
		bool                                command_received;                       /** True if we received command. */
	}decoder;
	struct
	{
		uint32_t              ticks_measurement_shift;        /** Time from signal change to measurement. */
		uint32_t              ticks_per_measurement;          /** Tick from one measurement to next one. */
		volatile bool         rearm_timer;                    /** Flag means that measurements synchronizing with signal change. */
		uint8_t               bits_received;                  /** Number of received bits. */
		uint8_t               bit_measure_num;                /** Current number of measure, per bit. */
		uint32_t              bit_measures;                   /** Measures per bit. */
		uint32_t              time_since_last_command_msec;   /** Time since last command received, milliseconds. */
	}measurements;
	enum
	{
		IR_SYNC_WAIT_SIGNAL_RISE = 0,      /** Waiting for first rise signal. */
		IR_SYNC_WAIT_SIGNAL_FALL,          /** Signal risen and now waiting for falling it. */
		IR_SYNC_WAIT_SIGNAL_RISE_END,      /** Signal risen and fallen and now waiting for second rise. */
		IR_SYNC_DONE,                      /** Signal risen second time. It's mean synchro impulse was and now data is receiving. */
	}sync_state;
	enum
	{
		IR_REPEAT_WAIT_SIGNAL_RISE = 0,      /** Waiting for first rise signal. */
		IR_REPEAT_WAIT_SIGNAL_FALL,          /** Signal risen and now waiting for falling it. */
		IR_REPEAT_WAIT_SIGNAL_RISE_END,      /** Signal risen and fallen and now waiting for second rise. */
		IR_REPEAT_FAULT,                     /** Repeat fault. */
	}repeat_state;
	enum
	{
		IR_STATE_SYNCHRONIZATION = 0,      /** Wait for start. */
		IR_STATE_RECEIVING_COMMAND,        /** Command receiving in progress. */
		IR_STATE_WAIT_REPEAT,              /** Waiting for repeat command. */
	}state;


}ir_context;


static void ir_pad_interrupt (void*context);
static void ir_timer_callback(GPTDriver *gptp);
static void ir_synchronize_receiving (void);
static void ir_receive_command(void);

static bool ir_bit_value_get(uint8_t bit_number)
{
	if (bit_number >= IR_NUMBER_OF_BITS_PER_COMMAND)
	{
		return false;
	}
	return (ir_context.decoder.received_bits[bit_number / 8] & (1 << (bit_number % 8))) != 0;
}

static void ir_bit_value_set(uint8_t bit_number, bool bit_value)
{
	if (bit_value)
	{
		(ir_context.decoder.received_bits[bit_number / 8] |= (1 << (bit_number % 8)));
	}
	else
	{
		(ir_context.decoder.received_bits[bit_number / 8] &= ~(1 << (bit_number % 8)));
	}
}

static bool ir_bit_decode(uint8_t from_bit_number, bool *value)
{
	const bool bit_1 = ir_bit_value_get(from_bit_number);
	const bool bit_2 = ir_bit_value_get(from_bit_number + 1);
	if (!bit_1 || bit_2)
	{
		/* Bit 1 must be "1" and bit 2 must be "0". */
		return false;
	}

	const bool bit_3 = ir_bit_value_get( from_bit_number + 2);
	if (bit_3)
	{
		/* if bit 3 is "1" - it is next value and our value is logic zero. */
		*value = false;
		return true;
	}
	/* if bit 3 and 4 is "0" - our value is logic one. */
	const bool bit_4 = ir_bit_value_get(from_bit_number + 3);
	if (bit_4)
	{
		return false;
	}
	*value = true;
	return true;
}

static bool ir_byte_decode(uint8_t *from_bit_number, uint8_t *value)
{
	uint8_t i;
	bool bit_conversion_result;
	bool bit_value;
	uint8_t result = 0;

	for (i = 0; i < 8; i++)
	{
		bit_conversion_result = ir_bit_decode(*from_bit_number, &bit_value);
		if (!bit_conversion_result)
		{
			return false;
		}

		result >>= 1;
		if (bit_value)
		{
			result |= 0x80;
			*from_bit_number += 4;
		}
		else
		{
			*from_bit_number += 2;
		}
	}
	*value = result;
	return true;
}

static bool ir_word_decode(uint8_t *from_bit_number, uint16_t *value)
{
	union
	{
		struct
		{
			uint8_t b1;
			uint8_t b2;
		};
		uint16_t w;
	}result;

	if (!ir_byte_decode(from_bit_number, &result.b1))
	{
		return false;
	}

	if (!ir_byte_decode(from_bit_number, &result.b2))
	{
		return false;
	}

	*value = result.w;

	return true;
}

static void ir_decode_command(void)
{
	uint8_t bit_index = 0;
	uint16_t address = 0;
	uint8_t command = 0;
	uint8_t i_command = 0;

	ir_word_decode(&bit_index, &address);
	ir_byte_decode(&bit_index, &command);
	ir_byte_decode(&bit_index, &i_command);

	if ((command + i_command) != 0xff)
	{
		return;
	}

	if (ir_context.decoder.callback)
	{
		ir_context.decoder.command_received = true;
		ir_context.decoder.last_address = address;
		ir_context.decoder.last_command = command;
		ir_context.decoder.callback(ir_context.decoder.callback_context, address, command, false);
	}
}

static void ir_push_signal_value(bool value, uint8_t bit_number)
{
	ir_bit_value_set(bit_number, value);
	if (bit_number == IR_NUMBER_OF_BITS_PER_COMMAND - 1)
	{
		/* Last bit received. */
		ir_decode_command();
	}
}

static bool ir_pad_value(void)
{
#if IR_PIN_INVERTED == TRUE
	return palReadPad(IR_PORT, IR_PIN) == PAL_LOW;
#elif
	return palReadPad(IR_PORT, IR_PIN) == PAL_HIGH;
#endif
}

static void ir_reset_state(void)
{
	gptStopTimerI(ir_context.gpt);
	ir_context.decoder.command_received = false;
	ir_context.decoder.last_address = 0;
	ir_context.decoder.last_command = 0;
	ir_context.measurements.bit_measures = 0;
	ir_context.measurements.bit_measure_num = 0;
	ir_context.measurements.bits_received = 0;
	ir_context.measurements.time_since_last_command_msec = 0;
	ir_context.state = IR_STATE_SYNCHRONIZATION;
	ir_context.sync_state = IR_SYNC_WAIT_SIGNAL_RISE;
	ir_context.repeat_state = IR_REPEAT_WAIT_SIGNAL_RISE;
}

static bool ir_waiting_timeout(void)
{
	return ir_context.measurements.time_since_last_command_msec > IR_REPEAT_TIMEOUT;
}

static void ir_synchronization(void)
{
	switch (ir_context.sync_state)
	{
		case IR_SYNC_WAIT_SIGNAL_RISE:
			if (ir_pad_value())
			{
				gptStopTimerI(ir_context.gpt);
				gptStartOneShot(ir_context.gpt, IR_SYNC_LEADING_PULSE_MAX_TICKS);
				ir_context.sync_state = IR_SYNC_WAIT_SIGNAL_FALL;
			}
			return;
		case IR_SYNC_WAIT_SIGNAL_FALL:
			if (!ir_pad_value())
			{
				uint32_t impulse_ticks = gptGetCounterX(ir_context.gpt);
				if (impulse_ticks > IR_SYNC_LEADING_PULSE_MIN_TICKS)
				{
					/* First impulse is 16 normal impulses. Each impulse measures 3 times with time shift 1/2 measure time. */
					ir_context.measurements.ticks_per_measurement = impulse_ticks / 16 / 3;
					ir_context.measurements.ticks_measurement_shift = ir_context.measurements.ticks_per_measurement / 2;
					/* Now, measure space. */
					ir_context.sync_state = IR_SYNC_WAIT_SIGNAL_RISE_END;
					gptStopTimerI(ir_context.gpt);
					gptStartOneShot(ir_context.gpt, IR_SYNC_SPACE_LEADING_SPACE_MAX_TICKS);
				}
				else
				{
					ir_reset_state();
				}
			}
			return;
		case IR_SYNC_WAIT_SIGNAL_RISE_END:
			if (ir_pad_value())
			{
				uint32_t space_time = gptGetCounterX(ir_context.gpt);
				if (space_time > IR_SYNC_SPACE_LEADING_SPACE_MIN_TICKS)
				{
					ir_context.sync_state = IR_SYNC_DONE;
					ir_context.state = IR_STATE_RECEIVING_COMMAND;
					ir_synchronize_receiving();
				}
				else
				{
					ir_reset_state();
				}
			}
			return;
		case IR_SYNC_DONE:
			return;
	}
}

static void ir_synchronize_receiving (void)
{
	gptStopTimerI(ir_context.gpt);
	gptStartOneShotI(ir_context.gpt, ir_context.measurements.ticks_measurement_shift);
	ir_context.measurements.rearm_timer = true;
}

static void ir_receiving(void)
{
	if (ir_waiting_timeout())
	{
		ir_reset_state();
	}

	switch (ir_context.repeat_state)
	{
		case IR_REPEAT_WAIT_SIGNAL_RISE:
			if (ir_pad_value())
			{
				if (ir_context.measurements.time_since_last_command_msec < IR_REPEAT_PAUSE_MIN_TIME)
				{
					ir_context.repeat_state = IR_REPEAT_WAIT_SIGNAL_RISE_END;
					gptStopTimerI(ir_context.gpt);
					gptStartOneShot(ir_context.gpt, IR_REPEAT_SPACE_LEADING_SPACE_MAX_TICKS);
				}
				gptStopTimerI(ir_context.gpt);
				gptStartOneShot(ir_context.gpt, IR_REPEAT_LEADING_PULSE_MAX_TICKS);
				ir_context.repeat_state = IR_REPEAT_WAIT_SIGNAL_FALL;
			}
			return;
		case IR_REPEAT_WAIT_SIGNAL_FALL:
			if (!ir_pad_value())
			{
				uint32_t impulse_ticks = gptGetCounterX(ir_context.gpt);
				if (impulse_ticks > IR_REPEAT_LEADING_PULSE_MIN_TICKS)
				{
					ir_context.repeat_state = IR_REPEAT_WAIT_SIGNAL_RISE_END;
					gptStopTimerI(ir_context.gpt);
					gptStartOneShot(ir_context.gpt, IR_REPEAT_SPACE_LEADING_SPACE_MAX_TICKS);
				}
				else
				{
					gptStopTimerI(ir_context.gpt);
					gptStartContinuousI(ir_context.gpt, IR_TIMER_10_MSEC);
					ir_context.repeat_state = IR_REPEAT_FAULT;
				}
			}
			return;
		case IR_REPEAT_WAIT_SIGNAL_RISE_END:
			if (ir_pad_value())
			{
				uint32_t space_time = gptGetCounterX(ir_context.gpt);
				if (space_time > IR_REPEAT_SPACE_LEADING_SPACE_MIN_TICKS)
				{
					if (ir_context.decoder.callback)
					{
						ir_context.decoder.callback(ir_context.decoder.callback_context,
						                                         ir_context.decoder.last_address,
						                                         ir_context.decoder.last_command,
						                                         true);
						ir_context.measurements.time_since_last_command_msec = 0;
						ir_context.repeat_state = IR_REPEAT_WAIT_SIGNAL_RISE;
						gptStopTimerI(ir_context.gpt);
						gptStartContinuousI(ir_context.gpt, IR_TIMER_10_MSEC);
					}
				}
				else
				{
					gptStopTimerI(ir_context.gpt);
					gptStartContinuousI(ir_context.gpt, IR_TIMER_10_MSEC);
					ir_context.repeat_state = IR_REPEAT_FAULT;
				}
			}
			return;
		case IR_REPEAT_FAULT:
			/* Do not do anything, just waiting for timeout. */
			return;
	}
}

static void ir_pad_interrupt (void*context)
{
	(void)context;

	switch (ir_context.state)
	{
		case IR_STATE_SYNCHRONIZATION:
			ir_synchronization();
			break;
		case IR_STATE_RECEIVING_COMMAND:
			if (ir_pad_value())
			{
				ir_synchronize_receiving();
			}
			break;
		case IR_STATE_WAIT_REPEAT:
			ir_receiving();
			break;
	}

}

static void ir_receive_command (void)
{
	ir_context.measurements.bit_measures <<= 1;

	if (ir_pad_value())
	{
		ir_context.measurements.bit_measures |= 1;
	}
	ir_context.measurements.bit_measure_num++;

	if (ir_context.measurements.bit_measure_num == IR_MEASURES_PER_BIT)
	{
		/* All measures done, calculate effective value and push bit to higher level. */
		uint8_t one_values = 0;
		while(ir_context.measurements.bit_measures)
		{
			if (ir_context.measurements.bit_measures & 1)
			{
				one_values++;
			}
			ir_context.measurements.bit_measures >>= 1;
		}
		bool effective_value = one_values == (IR_MEASURES_PER_BIT);

		ir_push_signal_value(effective_value, ir_context.measurements.bits_received);

		ir_context.measurements.bit_measures = 0;
		ir_context.measurements.bit_measure_num = 0;
		ir_context.measurements.bits_received++;

	}
}

static void ir_timer_callback(GPTDriver *gptp)
{
	(void)gptp;
	if (ir_context.measurements.rearm_timer)
	{
		ir_context.measurements.rearm_timer = false;
		gptStopTimerI(ir_context.gpt);
		gptStartContinuousI(ir_context.gpt, ir_context.measurements.ticks_per_measurement);
	}
	switch (ir_context.state)
	{
		case IR_STATE_SYNCHRONIZATION:
			break;
		case IR_STATE_RECEIVING_COMMAND:
			if (ir_context.measurements.bits_received == IR_NUMBER_OF_BITS_PER_COMMAND)
			{
				if (!ir_context.decoder.command_received)
				{
					ir_reset_state();
				}
				else
				{
					ir_context.state = IR_STATE_WAIT_REPEAT;
					gptStopTimerI(ir_context.gpt);
					gptStartContinuousI(ir_context.gpt, IR_TIMER_10_MSEC);
					/* Command longer than repeat, so correct start time. */
					ir_context.measurements.time_since_last_command_msec = 50;
				}
				break;
			}
			ir_receive_command();
			break;
		case IR_STATE_WAIT_REPEAT:
			ir_context.measurements.time_since_last_command_msec += 10;
			if (ir_waiting_timeout())
			{
				ir_reset_state();
			}
			break;
	}
}

void ir_initialize(void)
{
	/* Setup infrared receiver pin. */
	palSetPadMode(IR_PORT, IR_PIN, PAL_MODE_INPUT_PULLUP);
	palSetPadCallback(IR_PORT, IR_PIN, ir_pad_interrupt, NULL);
	palEnablePadEvent(IR_PORT, IR_PIN, PAL_EVENT_MODE_FALLING_EDGE | PAL_EVENT_MODE_RISING_EDGE);

	/* Setup timers. */
	{
		ir_context.gpt = &GPTD1;
		/* 4MhZ 0.25 usec per tick. */
		ir_context.gpt_config.frequency = 4000000;
		ir_context.gpt_config.callback = ir_timer_callback;
		gptStart(ir_context.gpt, &ir_context.gpt_config);
	}
}

void ir_set_callback(ir_command_callback_t *callback, void *context)
{
	ir_context.decoder.callback = callback;
	ir_context.decoder.callback_context = context;
}
