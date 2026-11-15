"""Compile the actual patch additions against a fake RPMh controller.

No device, proprietary firmware or extracted build tree is required.
"""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def added_file(patch, name):
    lines = patch.splitlines(keepends=True)
    start = lines.index('+++ b/bsd/dev/' + name + '\n') + 1
    result = []
    for line in lines[start:]:
        if line.startswith(('--- ', 'diff ')):
            break
        if line.startswith('+'):
            result.append(line[1:])
    return ''.join(result)


class GPUArcTests(unittest.TestCase):
    def test_policy_and_controller_failures(self):
        patch = (ROOT / 'xnu/patches/0083-sweet-gpu-rpmh-arc-discovery.patch').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp) / 'bsd/dev'
            directory.mkdir(parents=True)
            for name in ('sweet_arc.h', 'sweet_rpmh_arc.inc'):
                (directory / name).write_text(added_file(patch, name))
            subprocess.run(['git', 'apply', str(ROOT / 'xnu/patches/0084-sweet-rpmh-arc-completion-latch.patch')],
                           cwd=tmp, check=True, capture_output=True)
            subprocess.run(['git', 'apply', '--include=bsd/dev/sweet_rpmh_arc.inc', str(ROOT / 'xnu/patches/0086-sweet-gpu-cx-domain-cycle.patch')], cwd=tmp, check=True, capture_output=True)
            binary = directory / 'test-arc'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(directory), str(ROOT / 'tests/gpu_arc_test.c'),
                            '-o', str(binary)], check=True, capture_output=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('ARC policy and transaction tests PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
