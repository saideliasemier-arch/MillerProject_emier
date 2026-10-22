#include "jtag.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "pico/time.h"

#define JTAG_MAX_FREQUENCY_KHZ 1000u

static const uint32_t tdi_mask = 1u << JTAG_PIN_TDI;
static const uint32_t tdo_mask = 1u << JTAG_PIN_TDO;
static const uint32_t tck_mask = 1u << JTAG_PIN_TCK;
static const uint32_t tms_mask = 1u << JTAG_PIN_TMS;
static const uint32_t srst_mask = 1u << JTAG_PIN_SRST;
static const uint32_t trst_mask = 1u << JTAG_PIN_TRST;
static const uint32_t led_mask = 1u << JTAG_PIN_LED;
static uint32_t half_period_cycles = 1u;

static void write_pin(uint32_t mask, bool high)
{
    if (high) {
        sio_hw->gpio_set = mask;
    } else {
        sio_hw->gpio_clr = mask;
    }
}

static bool jtag_cycle(bool tms, bool tdi)
{
    sio_hw->gpio_clr = tck_mask;
    sio_hw->gpio_clr = (tms ? 0u : tms_mask) | (tdi ? 0u : tdi_mask);
    sio_hw->gpio_set = (tms ? tms_mask : 0u) | (tdi ? tdi_mask : 0u);
    busy_wait_at_least_cycles(half_period_cycles);

    sio_hw->gpio_set = tck_mask;
    busy_wait_at_least_cycles(half_period_cycles);
    bool tdo = (sio_hw->gpio_in & tdo_mask) != 0;

    sio_hw->gpio_clr = tck_mask;
    return tdo;
}

void jtag_set_frequency(uint32_t frequency_khz)
{
    if (frequency_khz == 0u) {
        frequency_khz = 1u;
    }
    if (frequency_khz > JTAG_MAX_FREQUENCY_KHZ) {
        frequency_khz = JTAG_MAX_FREQUENCY_KHZ;
    }

    uint32_t sys_clock_khz = clock_get_hz(clk_sys) / 1000u;
    uint32_t cycles_per_period = sys_clock_khz / frequency_khz;
    half_period_cycles = cycles_per_period / 2u;
    if (half_period_cycles == 0u) {
        half_period_cycles = 1u;
    }
}

void jtag_init(void)
{
    const uint pins[] = {
        JTAG_PIN_TDI, JTAG_PIN_TDO, JTAG_PIN_TCK, JTAG_PIN_TMS,
        JTAG_PIN_SRST, JTAG_PIN_TRST, JTAG_PIN_LED
    };

    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        gpio_init(pins[i]);
        gpio_set_function(pins[i], GPIO_FUNC_SIO);
    }

    gpio_set_dir(JTAG_PIN_TDI, GPIO_OUT);
    gpio_set_dir(JTAG_PIN_TDO, GPIO_IN);
    gpio_set_dir(JTAG_PIN_TCK, GPIO_OUT);
    gpio_set_dir(JTAG_PIN_TMS, GPIO_OUT);
    gpio_set_dir(JTAG_PIN_SRST, GPIO_IN);
    gpio_pull_up(JTAG_PIN_SRST);
    gpio_set_dir(JTAG_PIN_TRST, GPIO_OUT);
    gpio_set_dir(JTAG_PIN_LED, GPIO_OUT);

    sio_hw->gpio_clr = tdi_mask | tck_mask | tms_mask | trst_mask;
    sio_hw->gpio_set = led_mask;
    jtag_set_frequency(JTAG_MAX_FREQUENCY_KHZ);
}

void jtag_set_signals(uint8_t mask, uint8_t values)
{
    if ((mask & DIRTYJTAG_SIG_TCK) != 0u) {
        write_pin(tck_mask, (values & DIRTYJTAG_SIG_TCK) != 0u);
    }
    if ((mask & DIRTYJTAG_SIG_TDI) != 0u) {
        write_pin(tdi_mask, (values & DIRTYJTAG_SIG_TDI) != 0u);
    }
    if ((mask & DIRTYJTAG_SIG_TMS) != 0u) {
        write_pin(tms_mask, (values & DIRTYJTAG_SIG_TMS) != 0u);
    }
    if ((mask & DIRTYJTAG_SIG_TRST) != 0u) {
        write_pin(trst_mask, (values & DIRTYJTAG_SIG_TRST) != 0u);
    }
    if ((mask & DIRTYJTAG_SIG_SRST) != 0u) {
        bool released = (values & DIRTYJTAG_SIG_SRST) != 0u;
        if (released) {
            gpio_set_dir(JTAG_PIN_SRST, GPIO_IN);
        } else {
            sio_hw->gpio_clr = srst_mask;
            gpio_set_dir(JTAG_PIN_SRST, GPIO_OUT);
        }
    }
}

uint8_t jtag_get_signals(void)
{
    return (sio_hw->gpio_in & tdo_mask) != 0u ? DIRTYJTAG_SIG_TDO : 0u;
}

void jtag_shift_bits(const uint8_t *tdi, uint8_t *tdo, uint32_t bit_count)
{
    if (tdo != NULL) {
        for (uint32_t i = 0; i < (bit_count + 7u) / 8u; ++i) {
            tdo[i] = 0u;
        }
    }

    for (uint32_t bit = 0; bit < bit_count; ++bit) {
        bool tdi_value = (tdi[bit / 8u] & (uint8_t)(0x80u >> (bit % 8u))) != 0u;
        bool tdo_value = jtag_cycle(false, tdi_value);
        if (tdo != NULL && tdo_value) {
            tdo[bit / 8u] |= (uint8_t)(0x80u >> (bit % 8u));
        }
    }
}

uint8_t jtag_clock(uint8_t signals, uint8_t cycles)
{
    bool tms = (signals & DIRTYJTAG_SIG_TMS) != 0u;
    bool tdi = (signals & DIRTYJTAG_SIG_TDI) != 0u;
    bool tdo = false;

    for (uint16_t cycle = 0; cycle < cycles; ++cycle) {
        tdo = jtag_cycle(tms, tdi);
    }
    return tdo ? 0xffu : 0u;
}

void jtag_led_set(bool on)
{
    /* The MillerV1 JTAG LED is active low. */
    write_pin(led_mask, !on);
}