"""Reject mismatched experimental library modes and stale pixel-kernel sources."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


class VmxBuildContract(unittest.TestCase):
    def check_rejected(self,mode,manifest,reason):
        spec=importlib.util.spec_from_file_location('vmx_build',Path(__file__).parents[1]/'build.py')
        build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder);(output/'ffmpeg').mkdir()
            (output/'ffmpeg/build-manifest.json').write_text(json.dumps(manifest))
            build.OUT=str(output);build.VMX_AB_EXPERIMENT=mode
            with patch.object(build,'target_app',side_effect=AssertionError('must reject before compile')), \
                 patch.object(build,'run',side_effect=AssertionError('must reject before link')):
                with self.assertRaisesRegex(SystemExit,reason):build.target_xex()

    def test_mode_mismatch(self):
        self.check_rejected('1',{'vmx_ab_experiment':0},'VMX A/B mode changed')
        self.check_rejected('0',{'vmx_ab_experiment':1},'VMX A/B mode changed')

    def test_unverified_source(self):
        self.check_rejected('1',{'vmx_ab_experiment':1,'vmx_ab_inputs':{}},'Unverified VMX A/B source')


if __name__=='__main__':unittest.main()
