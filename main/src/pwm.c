#include <hal.h>
#include "pwm.h"
#include "config.h"

static struct
{
	PWMDriver *driver;
	PWMConfig config;
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

void pwm_initialize(void)
{
	pwm_context.driver = &PWMD3;
	pwm_context.config.callback = NULL;
	pwm_context.config.frequency = 4000000; //* 4 MHz. */
	pwm_context.config.period = 10000;      //* 200 Hz PWM frequency. */
	pwm_context.active_level = true;
#if PWM_INVERTED == TRUE
	pwm_context.active_level = !pwm_context.active_level;
#endif
	pwm_context.config.channels[0].mode = (PWM_INVERTED == TRUE) ? PWM_OUTPUT_ACTIVE_LOW : PWM_OUTPUT_ACTIVE_HIGH;
	pwm_context.config.channels[0].callback = NULL;
	pwm_context.config.channels[1].mode = PWM_OUTPUT_DISABLED;
	pwm_context.config.channels[1].callback = NULL;
	pwm_context.config.channels[2].mode = PWM_OUTPUT_DISABLED;
	pwm_context.config.channels[2].callback = NULL;
	pwm_context.config.channels[3].mode = PWM_OUTPUT_DISABLED;
	pwm_context.config.channels[3].callback = NULL;

	pwmStart(pwm_context.driver, &pwm_context.config);
	pwmEnableChannel(pwm_context.driver, 0, 0);
}

