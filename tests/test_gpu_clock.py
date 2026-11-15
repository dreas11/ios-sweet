import pathlib
import subprocess
import tempfile
import unittest
from test_gpu_arc import added_file
ROOT = pathlib.Path(__file__).resolve().parents[1]
class GPUClockTests(unittest.TestCase):
    def test_root_clock_and_failure_restore(self):
        patch = (ROOT / 'xnu/patches/0087-sweet-gpu-gmu-root-clock-cycle.patch').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            (d / 'sweet_gpu_clock.h').write_text(added_file(patch, 'sweet_gpu_clock.h'))
            binary = d / 'power'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',tmp,
                            str(ROOT / 'tests/gpu_clock_test.c'),'-o',str(binary)],check=True,capture_output=True)
            r = subprocess.run([str(binary)],check=True,capture_output=True,text=True)
            self.assertIn('GPU root clock tests PASS',r.stdout)
if __name__ == '__main__': unittest.main()
