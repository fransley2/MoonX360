import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from input_poc import InputModel, NEUTRAL


class InputTests(unittest.TestCase):
    def test_disconnect_and_watchdog_release_everything(self):
        model = InputModel()
        model.connect("s1", 1000)
        self.assertTrue(model.apply("s1", 1, 1000, 1001, "state",
                                    buttons=["A", "GUIDE"], keys=["ENTER"], lt=255))
        self.assertTrue(model.tick(1502))
        self.assertEqual(model.state, NEUTRAL)
        self.assertFalse(model.apply("s1", 2, 1502, 1502, "press", button="B"))

    def test_sequence_takeover_and_stale_packet(self):
        model = InputModel()
        model.connect("s1", 0)
        self.assertTrue(model.apply("s1", 1, 10, 11, "press", button="A"))
        self.assertFalse(model.apply("s1", 1, 11, 12, "press", button="B"))
        model.takeover()
        self.assertEqual(model.state, NEUTRAL)
        self.assertFalse(model.apply("s1", 2, 12, 13, "press", button="B"))
        model.connect("s2", 14)
        self.assertFalse(model.apply("s1", 3, 15, 15, "press", button="B"))
        self.assertTrue(model.apply("s2", 1, 15, 15, "press", button="B"))

    def test_heartbeat_does_not_change_state_and_release_all_does(self):
        model = InputModel()
        model.connect("s", 0)
        model.apply("s", 1, 0, 0, "press", button="X")
        self.assertTrue(model.apply("s", 2, 100, 100, "heartbeat"))
        self.assertEqual(model.state.buttons, frozenset(("X",)))
        self.assertTrue(model.apply("s", 3, 101, 101, "release_all"))
        self.assertEqual(model.state, NEUTRAL)


if __name__ == "__main__":
    unittest.main()
