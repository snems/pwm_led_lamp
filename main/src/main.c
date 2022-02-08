#include <hal.h>
#include <string.h>
#include "ch.h"
#include "usbcfg.h"
#include "chprintf.h"
#include "ir.h"
#include "pwm.h"
#include "config.h"

struct context
{
	BaseSequentialStream * chp; /* For print to serial port. */
	uint8_t brightness_value;
	bool brightness_on;
	bool was_command;
	uint16_t cmd_address;
	uint8_t cmd_command;
	uint8_t cmd_repeat;
};

#define REMOTE_1_ADDRESS          0x7f00
#define REMOTE_1_COMMAND_OFF      0x53
#define REMOTE_1_COMMAND_ON       0x52
#define REMOTE_1_COMMAND_PLUS     0x51
#define REMOTE_1_COMMAND_MINUS    0x50

#define REMOTE_2_ADDRESS          0xff00
#define REMOTE_2_COMMAND_OFF      0x06
#define REMOTE_2_COMMAND_ON       0x07
#define REMOTE_2_COMMAND_PLUS     0x05
#define REMOTE_2_COMMAND_MINUS    0x04

static void remote_command(void *context, uint16_t address, uint8_t command, bool repeat)
{
	struct context *ctx = (struct context*)context;
	enum
	{
		REMOTE_CMD_NONE = 0,
		REMOTE_CMD_OFF,
		REMOTE_CMD_ON,
		REMOTE_CMD_PLUS,
		REMOTE_CMD_MINUS,
	} remote_command = REMOTE_CMD_NONE;


	ctx->cmd_repeat = repeat;
	ctx->cmd_command = command;
	ctx->cmd_address = address;
	ctx->was_command = true;

	if (repeat) { return; }

	if (address == REMOTE_1_ADDRESS)
	{
		switch (command)
		{
			case REMOTE_1_COMMAND_ON: remote_command = REMOTE_CMD_ON; break;
			case REMOTE_1_COMMAND_OFF: remote_command = REMOTE_CMD_OFF; break;
			case REMOTE_1_COMMAND_PLUS: remote_command = REMOTE_CMD_PLUS; break;
			case REMOTE_1_COMMAND_MINUS: remote_command = REMOTE_CMD_MINUS; break;
			default: break;
		}
	}
	else if (address == REMOTE_2_ADDRESS)
	{
		switch (command)
		{
			case REMOTE_2_COMMAND_ON: remote_command = REMOTE_CMD_ON; break;
			case REMOTE_2_COMMAND_OFF: remote_command = REMOTE_CMD_OFF; break;
			case REMOTE_2_COMMAND_PLUS: remote_command = REMOTE_CMD_PLUS; break;
			case REMOTE_2_COMMAND_MINUS: remote_command = REMOTE_CMD_MINUS; break;
			default: break;
		}
	}

	switch (remote_command)
	{
		default:
		case REMOTE_CMD_NONE:
			break;
		case REMOTE_CMD_OFF:
			ctx->brightness_on = false;
			break;
		case REMOTE_CMD_ON:
			ctx->brightness_on = true;
			break;
		case REMOTE_CMD_PLUS:
			if (ctx->brightness_on)
			{
				uint8_t tmp = ctx->brightness_value;
				tmp += 10;
				if (tmp > 100) { tmp = 100; }
				ctx->brightness_value = tmp;
			}
			break;
		case REMOTE_CMD_MINUS:
			if (ctx->brightness_on)
			{
				uint8_t tmp = ctx->brightness_value;
				if (tmp < 10) { tmp = 0; }
				else { tmp -= 10; }
				ctx->brightness_value = tmp;
			}
			break;
	}
}

/*
 * Blinker thread.
 */
static THD_WORKING_AREA(area_led_thread, 128);
static THD_FUNCTION(led_thread, arg) 
{
	(void)arg;
	chRegSetThreadName("blinker");
	palSetPadMode(GPIOC, GPIOC_BOARD_LED, PAL_MODE_OUTPUT_PUSHPULL);
	while (true) 
	{
		palSetPad(GPIOC, GPIOC_BOARD_LED);
		chThdSleepMilliseconds(950);
		palClearPad(GPIOC, GPIOC_BOARD_LED);
		chThdSleepMilliseconds(50);
	}
}

static THD_WORKING_AREA(area_pwm_thread, 128);
static THD_FUNCTION(pwm_thread, arg)
{
	const struct context *c = (const struct context *)arg;
	uint16_t pwm_value = 0; /* Value to pwm set. */
	chRegSetThreadName("pwm_smooth");

	while (true)
	{
		uint16_t pwm_expected_value = 0; /* Value to pwm set. */
		if (c->brightness_on)
		{
			pwm_expected_value = c->brightness_value; /* Value to pwm set. */
			pwm_expected_value *= c->brightness_value; /* Value to pwm set. */
		}
		if (pwm_value > pwm_expected_value)
		{
			if (pwm_value >= 10)
			{
				pwm_value-=10;
			}
			else
			{
				pwm_value = 0;
			}
			pwm_set(pwm_value);
			chThdSleepMilliseconds(1);
		}
		else if (pwm_value < pwm_expected_value)
		{
			pwm_value+=10;
			if (pwm_value > 10000)
			{
				pwm_value = 10000;
			}
			pwm_set(pwm_value);
			chThdSleepMilliseconds(1);
		}
		else
		{
			chThdSleepMilliseconds(10);
		}
	}
}

int main(void) 
{
	struct context context = {};
	halInit();     /* Initialize hardware. */
	chSysInit();   /* Initialize OS. */


	/* Initialize and start serial over USB driver. */
	sduObjectInit(&SDU1);
	sduStart(&SDU1, &serusbcfg);

	/* Activate bus. */
	usbDisconnectBus(serusbcfg.usbp);
	chThdSleepMilliseconds(1500);
	usbStart(serusbcfg.usbp, &usbcfg);
	usbConnectBus(serusbcfg.usbp);


	/* Create threads. */
	chThdCreateStatic(area_led_thread, 
	                  sizeof(area_led_thread), 
	                  NORMALPRIO+1, 
	                  led_thread, 
	                  NULL);

	chThdCreateStatic(area_pwm_thread,
	                  sizeof(area_pwm_thread),
	                  NORMALPRIO+1,
	                  pwm_thread,
	                  &context);


	/* Set base stream to USB-serial */
	context.chp = (BaseSequentialStream *)&SDU1;

	/* Initialize infrared receiver. */
	ir_initialize();
	ir_set_callback(remote_command, &context);

	/* Initialize PWM controller. */
	pwm_initialize();
	palSetPadMode(PWM_PORT, PWM_PIN, PAL_MODE_STM32_ALTERNATE_PUSHPULL);
	context.brightness_value = 50;

	while (true)
	{
		if (context.was_command)
		{
			chprintf(context.chp,"address 0x%04X, command 0x%02X, repeat %d\n\r", context.cmd_address, context.cmd_command, context.cmd_repeat);
			context.was_command = false;
			context.cmd_repeat = 0;
		}
		else
		{
			chThdSleepMilliseconds(100);
		}
	}
}
