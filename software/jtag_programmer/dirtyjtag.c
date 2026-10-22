#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jtag.h"
#include "pico/stdlib.h"
#include "tusb.h"

enum {
    CMD_STOP = 0x00,
    CMD_INFO = 0x01,
    CMD_FREQ = 0x02,
    CMD_XFER = 0x03,
    CMD_SETSIG = 0x04,
    CMD_GETSIG = 0x05,
    CMD_CLK = 0x06,
    CMD_SETVOLTAGE = 0x07,
    CMD_GOTOBOOTLOADER = 0x08,
    CMD_NO_READ = 0x80,
    CMD_EXTEND_LENGTH = 0x40,
    CMD_READOUT = 0x80,
    CMD_MAX_XFER_BITS = 496,
    USB_PACKET_SIZE = 64
};

static uint8_t rx_buffer[USB_PACKET_SIZE];
static uint8_t tx_buffer[USB_PACKET_SIZE];
static uint8_t xfer_buffer[62];

static bool append_response(size_t *response_length, const uint8_t *data, size_t length)
{
    if (length > sizeof(tx_buffer) - *response_length) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        tx_buffer[*response_length + i] = data[i];
    }
    *response_length += length;
    return true;
}

static size_t process_commands(const uint8_t *commands, size_t command_length)
{
    size_t input_offset = 0;
    size_t response_length = 0;

    while (input_offset < command_length) {
        uint8_t command = commands[input_offset++];
        uint8_t opcode = command & 0x0fu;
        if (opcode == CMD_STOP) {
            break;
        }

        switch (opcode) {
        case CMD_INFO: {
            static const uint8_t info[10] = {'D', 'J', 'T', 'A', 'G', '2', '\n', 0, 0, 0};
            if (!append_response(&response_length, info, sizeof(info))) {
                return response_length;
            }
            break;
        }
        case CMD_FREQ: {
            if (command_length - input_offset < 2u) {
                return response_length;
            }
            uint32_t frequency_khz = ((uint32_t)commands[input_offset] << 8)
                                   | commands[input_offset + 1u];
            input_offset += 2u;
            jtag_set_frequency(frequency_khz);
            break;
        }
        case CMD_XFER: {
            if (input_offset >= command_length) {
                return response_length;
            }
            uint32_t bit_count = commands[input_offset++];
            if ((command & CMD_EXTEND_LENGTH) != 0u) {
                bit_count += 256u;
            }
            if (bit_count > CMD_MAX_XFER_BITS) {
                return response_length;
            }
            size_t byte_count = (bit_count + 7u) / 8u;
            if (byte_count > command_length - input_offset) {
                return response_length;
            }
            const uint8_t *tdi = &commands[input_offset];
            input_offset += byte_count;
            bool no_read = (command & CMD_NO_READ) != 0u;
            if (!no_read && byte_count > sizeof(tx_buffer) - response_length) {
                return response_length;
            }
            jtag_shift_bits(tdi, no_read ? NULL : xfer_buffer, bit_count);
            if (!no_read && !append_response(&response_length, xfer_buffer, byte_count)) {
                return response_length;
            }
            break;
        }
        case CMD_SETSIG: {
            if (command_length - input_offset < 2u) {
                return response_length;
            }
            jtag_set_signals(commands[input_offset], commands[input_offset + 1u]);
            input_offset += 2u;
            break;
        }
        case CMD_GETSIG: {
            uint8_t signal_state = jtag_get_signals();
            if (!append_response(&response_length, &signal_state, 1u)) {
                return response_length;
            }
            break;
        }
        case CMD_CLK: {
            if (command_length - input_offset < 2u) {
                return response_length;
            }
            uint8_t signals = commands[input_offset++];
            uint8_t cycles = commands[input_offset++];
            uint8_t readout = jtag_clock(signals, cycles);
            if ((command & CMD_READOUT) != 0u
                && !append_response(&response_length, &readout, 1u)) {
                return response_length;
            }
            break;
        }
        case CMD_SETVOLTAGE:
            if (input_offset >= command_length) {
                return response_length;
            }
            ++input_offset;
            break;
        case CMD_GOTOBOOTLOADER:
            break;
        default:
            return response_length;
        }
    }

    return response_length;
}

int main(void)
{
    jtag_init();
    tusb_init();

    while (true) {
        tud_task();
        if (!tud_vendor_available()) {
            tight_loop_contents();
            continue;
        }

        uint32_t received = tud_vendor_read(rx_buffer, sizeof(rx_buffer));
        size_t response_length = process_commands(rx_buffer, received);
        if (response_length != 0u) {
            tud_vendor_write(tx_buffer, (uint32_t)response_length);
            tud_vendor_flush();
        }
    }
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                const tusb_control_request_t *request)
{
    (void)rhport;
    (void)request;
    return stage != CONTROL_STAGE_SETUP;
}