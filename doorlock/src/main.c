#include <hal.h>
#include <hal_pal.h>
#include "ch.h"
#include "usbcfg.h"
#include "chprintf.h"

#define INFRARED_RECEIVER_PORT  GPIOA
#define INFRARED_RECEIVER_PIN   0U

struct context
{
	BaseSequentialStream * chp; /* For print to serial port. */
	time_usecs_t usecs;
	bool print;
};

static void interrupt (void*context)
{
	struct context *ctx = (struct context*)context;
	static bool first_event = true;
	static systime_t event_time_previous = 0;
	static systime_t event_rising_previous;
	systime_t event_time_current = 0;
	systime_t event_rising_current;

	event_rising_current = palReadPad(INFRARED_RECEIVER_PORT, INFRARED_RECEIVER_PIN) == 1;
	event_time_current = chVTGetSystemTime();

	/* Skip first event, because not have previous state. */
	if (first_event)
	{
		first_event = false;
		event_rising_previous = event_rising_current;
		event_time_previous = event_rising_current;
		return;
	}

	sysinterval_t delta = chTimeDiffX(event_time_previous, event_time_current);
	time_usecs_t usecs = chTimeI2US(delta);
	if (usecs > 5000)
	{
		ctx->usecs = usecs;
		ctx->print = true;
	}
	event_rising_previous = event_rising_current;
	event_time_previous = event_time_current;
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

	/* Setup infrared receiver. */
	palSetPadMode(GPIOA, 0U, PAL_MODE_INPUT_PULLUP);
	palSetPadCallback(GPIOA, 0u, interrupt, &context);
	palEnablePadEvent(GPIOA, 0u, PAL_EVENT_MODE_FALLING_EDGE|PAL_EVENT_MODE_RISING_EDGE);

	/* Initialize and start serial over USB dirver. */
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

	chprintf(context.chp,"start\n\r");

	while (true)
	{
		if (context.print)
		{
			chprintf(context.chp,"event : %d\n\r", context.usecs);
			context.print = false;
		}
		chThdSleepMilliseconds(500);
	}
}
