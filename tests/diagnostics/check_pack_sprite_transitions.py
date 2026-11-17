#!/usr/bin/env python3
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from pack_sprite_transitions import changed_words, verify


def fixture(dictionary_index=0):
    header_bytes = 40
    palette = bytes((5, 0, 0, 0))
    state = struct.pack(">HH6H", 1, 16, 1, 2, 3, 4, 5, 6)
    states = state + state
    packet_offset = header_bytes + len(palette) + len(states)
    dictionary_offset = packet_offset + 24
    body_offset = dictionary_offset + 4
    body = struct.pack(">4BH", 2, 3, 1, 0, dictionary_index)
    total = body_offset + len(body)
    header = struct.pack(">4sHHHHIIIIIII", b"TP1\0", 1, 1, 2, 1, 1,
                         0x031401D0,
                         header_bytes + len(palette), packet_offset,
                         dictionary_offset, body_offset, total)
    descriptor = struct.pack(">HHIIIHHI", 0, 1, body_offset, len(body), 10,
                             1, 1, 7)
    return header + palette + states + descriptor + b"\x01\x02\x03\x04" + body


class TransitionPackTests(unittest.TestCase):
    def test_minimal_packet_round_trips(self):
        verify(fixture())

    def test_dictionary_index_is_bounded(self):
        with self.assertRaisesRegex(ValueError, "dictionary index"):
            verify(fixture(1))

    def test_vacated_target_byte_restores_lower_sprite(self):
        old = {(4, 7): b"\x11\x12\x13\x14"}
        new = {(4, 7): b"\x11\0\x13\x14"}
        lower = {(4, 7): b"\0\x25\0\0"}
        self.assertEqual(changed_words(old, new, lower),
                         [(4, 7, b"\x11\x25\x13\x14")])


if __name__ == "__main__":
    unittest.main(verbosity=2)
