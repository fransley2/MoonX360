import sys
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
import diagnose


class ResumeTests(unittest.TestCase):
    def test_resume_only_if_capture_stopped_a_running_debugger(self):
        report = {"commands": {"getexecstate": {"message": " start"}},
                  "exec_state_after_screenshot": {"message": " stop"}}
        with patch.object(diagnose, "request", side_effect=[
                {"code": 200, "message": " OK"}, {"code": 200, "message": " start"}]) as send:
            diagnose.resume_if_capture_stopped("console", report)
        self.assertEqual([call.args[1] for call in send.call_args_list],
                         ["go", "getexecstate"])
        self.assertEqual(report["exec_state_after_resume"]["message"].strip(), "start")

        stopped = {"commands": {"getexecstate": {"message": " stop"}},
                   "exec_state_after_screenshot": {"message": " stop"}}
        with patch.object(diagnose, "request") as send:
            diagnose.resume_if_capture_stopped("console", stopped)
        send.assert_not_called()


if __name__ == "__main__":
    unittest.main()
