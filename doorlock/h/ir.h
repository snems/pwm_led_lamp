#ifndef IR_H
#define IR_H
#include <stdint.h>
#include <stdbool.h>

typedef void (ir_command_callback_t)(void *context, uint16_t address, uint8_t command, bool repeat);
void ir_initialize(void);
void ir_set_callback(ir_command_callback_t *callback, void *context);

#endif //IR_H
