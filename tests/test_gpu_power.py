import pathlib
import subprocess
import tempfile
import unittest
from test_gpu_arc import added_file
ROOT = pathlib.Path(__file__).resolve().parents[1]
class GPUPowerTests(unittest.TestCase):
    def test_domain_cycle_and_failure_restore(self):
        patch = (ROOT / 'xnu/patches/0086-sweet-gpu-cx-domain-cycle.patch').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp) / "bsd/dev"
            d.mkdir(parents=True)
            (d / 'sweet_gpu_power.h').write_text(added_file(patch, 'sweet_gpu_power.h'))
            subprocess.run(['git','apply','--include=bsd/dev/sweet_gpu_power.h',str(ROOT / 'xnu/patches/0087-sweet-gpu-gmu-root-clock-cycle.patch')],cwd=tmp,check=True,capture_output=True)
            binary = d / 'power'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(d),
                            str(ROOT / 'tests/gpu_power_test.c'),'-o',str(binary)],check=True,capture_output=True)
            r = subprocess.run([str(binary)],check=True,capture_output=True,text=True)
            self.assertIn('GPU CX cycle tests PASS',r.stdout)
if __name__ == '__main__': unittest.main()
