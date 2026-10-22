import unittest
from unittest.mock import Mock

from dirtyjtag_remote_bitbang import (
    DirtyJtag,
    SIG_TCK,
    SIG_TDI,
    SIG_TMS,
    SIG_TRST,
    SIG_SRST,
    clock_command,
    release_reset_command,
    signal_command,
)


class RemoteBitbangTranslationTests(unittest.TestCase):
    def test_translates_all_jtag_output_bits(self) -> None:
        self.assertEqual(
            signal_command(0b111),
            bytes((0x04, SIG_TCK | SIG_TMS | SIG_TDI, SIG_TCK | SIG_TMS | SIG_TDI)),
        )

    def test_translates_each_jtag_output_bit_independently(self) -> None:
        mask = SIG_TCK | SIG_TMS | SIG_TDI
        self.assertEqual(signal_command(0b100), bytes((0x04, mask, SIG_TCK)))
        self.assertEqual(signal_command(0b010), bytes((0x04, mask, SIG_TMS)))
        self.assertEqual(signal_command(0b001), bytes((0x04, mask, SIG_TDI)))
        self.assertEqual(signal_command(0b000), bytes((0x04, mask, 0)))

    def test_keeps_active_low_reset_lines_released(self) -> None:
        mask = SIG_TRST | SIG_SRST
        self.assertEqual(
            release_reset_command(),
            bytes((0x04, mask, mask)),
        )

    def test_clocks_one_cycle_with_expected_tms_and_tdi(self) -> None:
        self.assertEqual(clock_command(0b010), bytes((0x86, SIG_TMS, 1, 0)))
        self.assertEqual(clock_command(0b001), bytes((0x86, SIG_TDI, 1, 0)))
        self.assertEqual(
            clock_command(0b011),
            bytes((0x86, SIG_TMS | SIG_TDI, 1, 0)),
        )

    def test_clock_read_suppresses_the_duplicate_tck_rise(self) -> None:
        adapter = DirtyJtag.__new__(DirtyJtag)
        adapter.pending = bytearray()
        adapter.remote_state = 0
        adapter.suppress_next_rise = False
        adapter._request = Mock(return_value=b"\xff")

        adapter.queue_signal(0)
        self.assertTrue(adapter.read_tdo())
        adapter.queue_signal(0b100)

        adapter._request.assert_called_once_with(
            signal_command(0) + clock_command(0)
        )
        self.assertFalse(adapter.pending)
        self.assertFalse(adapter.suppress_next_rise)


if __name__ == "__main__":
    unittest.main()
