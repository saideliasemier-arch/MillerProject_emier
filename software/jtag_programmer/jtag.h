#ifndef MILLER_JTAG_H
#define MILLER_JTAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* MillerV1 RP2040 GPIO mapping from the KiCad schematic. */
#define JTAG_PIN_TDI 16u
#define JTAG_PIN_TDO 17u
#define JTAG_PIN_TCK 18u
#define JTAG_PIN_TMS 19u
#define JTAG_PIN_SRST 20u
#define JTAG_PIN_TRST 21u
#define JTAG_PIN_LED 25u

#define DIRTYJTAG_SIG_TCK  (1u << 1)
#define DIRTYJTAG_SIG_TDI  (1u << 2)
#define DIRTYJTAG_SIG_TDO  (1u << 3)
#define DIRTYJTAG_SIG_TMS  (1u << 4)
#define DIRTYJTAG_SIG_TRST (1u << 5)
#define DIRTYJTAG_SIG_SRST (1u << 6)

void jtag_init(void);
void jtag_set_frequency(uint32_t frequency_khz);
void jtag_set_signals(uint8_t mask, uint8_t values);
uint8_t jtag_get_signals(void);
void jtag_shift_bits(const uint8_t *tdi, uint8_t *tdo, uint32_t bit_count);
uint8_t jtag_clock(uint8_t signals, uint8_t cycles);
void jtag_led_set(bool on);

#endif