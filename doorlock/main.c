#include "ch.h"
#include "hal.h"
#include "usbcfg.h"
#include "chprintf.h"

#define LED_PIN			13u

/*
 * Blinker thread.
 */
static THD_WORKING_AREA(area_led_thread, 128);
static THD_FUNCTION(led_thread, arg) 
{
	(void)arg;

	chRegSetThreadName("blinker");
	palSetPadMode(GPIOC, LED_PIN, PAL_MODE_OUTPUT_PUSHPULL);
	while (true) 
	{
		palSetPad(GPIOC, 13U);
		chThdSleepMilliseconds(950);
		palClearPad(GPIOC, 13U);
		chThdSleepMilliseconds(50);
	}
}

int main(void) 
{
	halInit();     /* Initialize hardware. */
	chSysInit();   /* Initialize OS. */

	/* Initialize and start serial ove USB dirver. */
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
	BaseSequentialStream * chp = (BaseSequentialStream *)&SDU1;

	int i = 0;
	while (true) 
	{
		uint8_t buf = 0;
		chprintf(chp,"workling : %d \n\r", i++);
		streamRead(chp, &buf, 1); 
		streamWrite(chp, &buf, 1); 
		chThdSleepMilliseconds(500);
	}
}
