#ifndef PWM_H
#define PWM_H

#include <stdint.h>

typedef void (pwm_callback_t)(void *context, bool rising);

void pwm_corrected_set(uint8_t value);
void pwm_set(uint16_t value);
void pwm_set_callback(pwm_callback_t *callback, void *context);
void pwm_initialize(void);

#endif //PWM_H
