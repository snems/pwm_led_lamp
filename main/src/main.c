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
			if (context.cmd_command == 6)
			{
				pwm_set(100);
			}
			else if (context.cmd_command == 7)
			{
				pwm_set(10000);
			}
		}
		chThdSleepMilliseconds(500);
	}
}