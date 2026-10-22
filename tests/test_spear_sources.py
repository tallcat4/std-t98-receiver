"""Guard the imported RF/DSP code against accidental local simplification.

The manifest records LF-normalized source hashes from the stated SPEAR commit.
The two receiver files have recorded single-channel adaptations. FIR's platform
guard is normalized back; remaining files must match the original SPEAR hashes.
"""
import hashlib
import json
from pathlib import Path
import unittest


class SpearSources(unittest.TestCase):
    def test_imported_frontend_matches_spear(self):
        root = Path(__file__).resolve().parents[1]
        manifest = json.loads((root/'tests/spear_sources.json').read_text())
        self.assertEqual(manifest['commit'], 'd1611fd408939ad238372d58828639e8fde28400')
        for relative, expected in manifest['sha256'].items():
            with self.subTest(source=relative):
                code = (root/'vendor/spear'/relative).read_text(encoding='utf-8')
                if relative == 'dsp/src/fir.cpp':
                    code = code.replace(
                        '#if defined(_WIN32) || !defined(__GNUC__) || !defined(__x86_64__) || defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)',
                        '#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)')
                expected = manifest.get('single_channel_adaptations', {}).get(relative, expected)
                self.assertEqual(hashlib.sha256(code.encode('utf-8')).hexdigest(), expected)


if __name__ == '__main__':
    unittest.main()
