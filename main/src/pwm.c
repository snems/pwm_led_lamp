#include <hal.h>
#include "pwm.h"
#include "config.h"

static struct
{
	PWMDriver *driver;
	PWMConfig config;
	pwm_callback_t *callback;
	void *callback_context;
	bool active_level;
}pwm_context;

void pwm_set(uint16_t value)
{
	if (value > 10000) { value = 10000; }
	else if (value < 10) { value = 10; } /** Couldn't use very low values, there is to slow interrupts. */
	pwmEnableChannel(pwm_context.driver, 0, PWM_PERCENTAGE_TO_WIDTH(pwm_context.driver, value));
}

void pwm_corrected_set(uint8_t value)
{
	uint16_t value_to_set;
	if (value > 100) { value = 100; }

	value_to_set = value;
	value_to_set *= value_to_set;

	if (value_to_set < 10) { value_to_set = 10; } /** Couldn't use very low values, there is to slow interrupts. */

	pwmEnableChannel(pwm_context.driver, 0, PWM_PERCENTAGE_TO_WIDTH(pwm_context.driver, value_to_set));
}

static void pwm_routine_periodic(PWMDriver *pwm_driver)
{
	(void)pwm_driver;

	if (pwm_context.callback)
	{
		pwm_context.callback(pwm_context.callback_context, pwm_context.active_level);
	}
}

static void pwm_routine_channel(PWMDriver *pwm_driver)
{
	(void)pwm_driver;

	if (pwm_context.callback)
	{
		pwm_context.callback(pwm_context.callback_context, !pwm_context.active_level);
	}
}

void pwm_set_callback(pwm_callback_t *callback, void *context)
{
	pwm_context.callback = callback;
	pwm_context.callback_context = context;
}

void pwm_initialize(void)
{
	pwm_context.driver = &PWMD3;
	pwm_context.config.callback = pwm_routine_periodic;
	pwm_context.config.frequency = 4000000; //* 4 MHz. */
	pwm_context.config.period = 10000;      //* 200 Hz PWM frequency. */
	pwm_context.active_level = true;
#if PWM_INVERTED == TRUE
	pwm_context.active_level = !pwm_context.active_level;
#endif
	pwm_context.config.channels[0].mode = PWM_OUTPUT_ACTIVE_HIGH;
	pwm_context.config.channels[0].callback = pwm_routine_channel;
	pwm_context.config.channels[1].mode = PWM_OUTPUT_DISABLED;
	pwm_context.config.channels[1].callback = NULL;
	pwm_context.config.channels[2].mode = PWM_OUTPUT_DISABLED;
	pwm_context.config.channels[2].callback = NULL;
	pwm_context.config.channels[3].mode = PWM_OUTPUT_DISABLED;
	pwm_context.config.channels[3].callback = NULL;

	pwmStart(pwm_context.driver, &pwm_context.config);
	pwmEnablePeriodicNotification(pwm_context.driver);
	pwmEnableChannel(pwm_context.driver, 0, 0);
	pwmEnableChannelNotification(pwm_context.driver, 0);
}

