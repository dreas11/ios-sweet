import pathlib
import subprocess
import tempfile
import unittest
from test_gpu_arc import added_file

ROOT = pathlib.Path(__file__).resolve().parents[1]


class GMUBranchTests(unittest.TestCase):
    def test_reset_guards_and_clock_rollback(self):
        patch = (ROOT / 'xnu/patches/0089-sweet-gpu-reset-held-gmu-clock.patch').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            (d / 'sweet_gpu_gmu_branch.h').write_text(added_file(patch, 'sweet_gpu_gmu_branch.h'))
            binary = d / 'gmu'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', tmp,
                            str(ROOT / 'tests/gpu_gmu_branch_test.c'), '-o', str(binary)],
                           check=True, capture_output=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('GMU reset-held branch tests PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
