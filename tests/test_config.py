"""RF config validation and precedence without opening SDR hardware."""
from pathlib import Path
import subprocess
import tempfile
import unittest

EXE = Path(__file__).resolve().parents[1] / 'build/std-t98.exe'


class RfConfig(unittest.TestCase):
    def test_config_and_cli_precedence(self):
        with tempfile.TemporaryDirectory(prefix='t98-config-') as directory:
            tmp = Path(directory)
            config = tmp / '\u8a2d\u5b9a.conf'
            config.write_text('# Calibration\ngain = 0\nppm = 0\nfreq-error = -330 # Hz\nsample-rate = 1200000\npfb = 64\n', encoding='utf-8-sig')
            iq = tmp / 'zero.cf32'
            iq.write_bytes(bytes(8192 * 8))
            common = ['--input', str(iq), '--center-freq', '350956250', '--channel', '2']
            cases = [(['--config', str(config)], b'residual -330.0 Hz', b'polyphase 64'),
                     (['--freq-error', '-200', '--pfb', '48', '--config', str(config)], b'residual -200.0 Hz', b'polyphase 48'),
                     (['--config', str(config), '--freq-error', '-100'], b'residual -100.0 Hz', b'polyphase 64')]
            for i, (args, correction, pfb) in enumerate(cases):
                p = subprocess.run([str(EXE), *common, *args, '-o', str(tmp / f'output{i}')], capture_output=True)
                self.assertEqual(p.returncode, 0, p.stderr)
                self.assertIn(correction, p.stderr)
                self.assertIn(pfb, p.stderr)
                self.assertIn(b'CH 2 (351.20625 MHz)', p.stderr)

    def test_invalid_config_fails_before_opening_radio(self):
        with tempfile.TemporaryDirectory(prefix='t98-config-') as directory:
            config = Path(directory) / 'bad.conf'
            for text, message in [('gain=\n', b'expected key = value'),
                                  ('gain=0\ngain=10\n', b'duplicate setting'),
                                  ('overwrite=true\n', b'unknown setting'),
                                  ('freq-error=nan\n', b'Invalid value'),
                                  ('gain=100\n', b'--gain must be')]:
                config.write_text(text, encoding='utf-8')
                p = subprocess.run([str(EXE), '--config', str(config)], capture_output=True)
                self.assertNotEqual(p.returncode, 0)
                self.assertIn(message, p.stderr)
            p = subprocess.run([str(EXE), '--config', str(config.with_name('missing.conf'))], capture_output=True)
            self.assertIn(b'Cannot open RF config', p.stderr)


if __name__ == '__main__':
    unittest.main()
