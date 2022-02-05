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
	uint8_t pwm_value;
	bool was_command;
	uint16_t cmd_address;
	uint8_t cmd_command;
	uint8_t cmd_repeat;
};

static void remote_command(void *context, uint16_t address, uint8_t command, bool repeat)
{
	struct context *ctx = (struct context*)context;
	ctx->cmd_address = address;
	ctx->cmd_command = command;
	ctx->was_command = true;
	if (repeat)
	{
		ctx->cmd_repeat++;
	}
}


static void pwm_callback(void *context, bool rising)
{
	(void)context;
	if (rising)
	{
		palSetPad(PWM_PORT, PWM_PIN);
	}
	else
	{
		palClearPad(PWM_PORT, PWM_PIN);
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
		uint16_t pwm_expected_value = c->pwm_value; /* Value to pwm set. */
		pwm_expected_value *= c->pwm_value; /* Value to pwm set. */
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
			chThdSleepMilliseconds(100);
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
	pwm_set_callback(pwm_callback, &context);
	palSetPadMode(PWM_PORT, PWM_PIN, PAL_MODE_OUTPUT_PUSHPULL);
	pwm_set(5000);

	while (true)
	{

		if (context.was_command)
		{
			chprintf(context.chp,"address 0x%04X, command 0x%02X, repeat %d\n\r", context.cmd_address, context.cmd_command, context.cmd_repeat);
			context.was_command = false;
			context.cmd_repeat = 0;
			if (context.cmd_command == 6 || context.cmd_command == 0x53)
			{
				context.pwm_value = 0;
			}
			else if (context.cmd_command == 7 || context.cmd_command == 0x52)
			{
				context.pwm_value = 100;
			}
			else if (context.cmd_command == 5 || context.cmd_command == 0x51)
			{
				uint8_t tmp = context.pwm_value;
				tmp += 5;
				if (tmp > 100) { tmp = 100; }
				context.pwm_value = tmp;
				chThdSleepMilliseconds(5);
			}
			else if (context.cmd_command == 4 || context.cmd_command == 0x50)
			{
				uint8_t tmp = context.pwm_value;
				if (tmp < 5) { tmp = 0; }
				else { tmp -= 5; }
				context.pwm_value = tmp;
				chThdSleepMilliseconds(5);
			}
			else
			{
				chThdSleepMilliseconds(100);
			}
		}
		else
		{
			chThdSleepMilliseconds(100);
		}
	}
}
