"""Cross-repository Windows check. Requires all three sibling checkouts built.

Run: python std-t98-receiver/tests/test_family.py from the workspace.
Only generated test data is used; no RTL-SDR is opened.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time
import wave
import numpy as np

RECEIVER = Path(__file__).resolve().parents[1]
WORKSPACE = RECEIVER.parent
sys.path[:0] = [str(WORKSPACE/'std-t98-cracker'), str(WORKSPACE/'std-t98-vocode')]
from std_t98_cracker.burst import read_bursts, read_llr, pack49, unpack49, write_bursts
from std_t98_cracker.fec import FecDecoder
from std_t98_cracker.sequence import Sequence
from std_t98_cracker.cli import main as cracker
from std_t98_vocode import inspect_file, records, convert, load_codec
from descramble import descramble_burst, generate_pn_sequence_196


def run(*args, code=0, env=None):
    p = subprocess.run([str(x) for x in args], capture_output=True, env=env)
    assert p.returncode == code, (p.returncode, p.stdout, p.stderr)
    return p


def main():
    exe = RECEIVER/'build/std-t98.exe'
    native = RECEIVER/'build/t98-tests.exe'
    compiler = Path(os.environ.get('CC', r'C:\Strawberry\c\bin\gcc.exe'))
    with tempfile.TemporaryDirectory(prefix='t98-family-') as directory:
        tmp = Path(directory)
        # Independent NumPy FFT oracle for the FFTW -> portable IFFT migration.
        run(native, '--rx-taps', tmp/'rx.f32')
        nfft, ntaps, fs, baud, alpha = 8192, 520, 62500, 2400, .2
        f = np.fft.fftshift(np.fft.fftfreq(nfft, d=1/fs))
        af = np.abs(f)
        f0, f1 = (1-alpha)*baud/2, (1+alpha)*baud/2
        spectrum = np.zeros(nfft)
        spectrum[af < f0] = 1
        transition = (af >= f0) & (af <= f1)
        spectrum[transition] = np.cos((2*np.pi*af[transition]/baud-np.pi*(1-alpha))/(4*alpha))
        spectrum /= np.maximum(np.abs(np.sinc(f/baud)), 1e-8)
        impulse = np.fft.fftshift(np.fft.ifft(np.fft.ifftshift(spectrum)).real)
        taps = impulse[nfft//2-ntaps//2:nfft//2+ntaps//2] * np.kaiser(ntaps, 8)
        taps /= taps.sum()
        np.testing.assert_allclose(np.fromfile(tmp/'rx.f32', dtype='<f4'), taps.astype(np.float32), rtol=1e-6, atol=1e-9)
        base = tmp/'fixture'
        run(native, '--fixture', base)
        bits, payload = read_bursts(tmp/'fixture.burst')
        assert bits == 72 and payload.shape == (10, 4, 9)
        assert payload[0].tobytes() == bytes(range(36))
        assert inspect_file(tmp/'fixture.burst')['frames'] == 40
        assert inspect_file(tmp/'fixture.ambe', 'ambe')['frames'] == 40
        llr = read_llr(tmp/'fixture.llr', 10)
        assert np.array_equal(llr > 0, np.unpackbits(payload, axis=-1) > 0)
        codec = load_codec()
        decoder = FecDecoder()
        # Compare independently implemented FEC and bit-order mappings.
        random = np.random.default_rng(98).integers(0, 256, (100, 4, 9), dtype=np.uint8)
        recovered, valid, errors = decoder.fec(random)
        reference = np.array([[codec.fec_demod(frame.tobytes()) for frame in b] for b in random])
        np.testing.assert_array_equal(recovered, reference)
        convert(tmp/'fixture.burst', tmp/'fec.burst', 'fec')
        np.testing.assert_array_equal(read_bursts(tmp/'fec.burst')[1], pack49(decoder.fec(payload)[0]))
        convert(tmp/'fixture.burst', tmp/'raw.wav')
        # Check PN and ThumbDV against the original vocode implementation.
        plain = np.tile(np.array([list(bytes.fromhex(s)) for s in (
            'deba0301a6e600', '9f9ac301a64500', 'de9cc909b6e600', 'df9ac201a64c00')], dtype=np.uint8)[None], (10, 1, 1))
        encrypted = pack49(unpack49(plain) ^ Sequence().thumbdv(12345)[None])
        old = descramble_burst([bytes(f) for f in encrypted[0]], generate_pn_sequence_196(12345))
        assert b''.join(old) == plain[0].tobytes()
        write_bursts(tmp/'secret.burst', encrypted)
        assert cracker(['crack', str(tmp/'secret.burst'), '--method', 'linear']) == 0
        assert cracker(['descramble', str(tmp/'secret.burst'), '--key', '12345', '-o', str(tmp/'clear')]) == 0
        np.testing.assert_array_equal(read_bursts(tmp/'clear.burst')[1], plain)
        convert(tmp/'clear.burst', tmp/'clear.wav')
        for file in ('raw.wav', 'clear.wav'):
            with wave.open(str(tmp/file), 'rb') as wav:
                assert (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes()) == (8000, 1, 2, 6400)
        # Japanese paths exercise the wide Windows command line and filesystem.
        unicode_dir = tmp/'\u65e5\u672c\u8a9e'
        unicode_dir.mkdir()
        iq = unicode_dir/'\u5165\u529b.cf32'
        iq.write_bytes(bytes(1200000*8))
        output = unicode_dir/'\u53d7\u4fe1'
        arguments = [exe, '--input', iq, '--sample-rate', '1200000', '--center-freq', '350950000', '-o', output]
        start = time.perf_counter()
        run(*arguments)
        print(f'1 s IQ replay: {time.perf_counter()-start:.3f} s elapsed')
        assert Path(str(output)+'.burst').stat().st_size == 0
        run(*arguments, code=1)  # no overwrite
        iq.write_bytes(b'bad')
        run(*arguments, '--overwrite', code=1)
        # Test actual dynamic library ABI, options, USB failure propagation.
        dll = tmp/'fake_rtlsdr.dll'
        run(compiler, '-shared', '-static', RECEIVER/'tests/fake_rtlsdr.c', '-o', dll)
        assert b'TEST DOUBLE' in run(exe, '--rtl-library', dll, '--list-devices').stdout
        run(exe, '--rtl-library', dll, '--seconds', '.1', '--gain', '19', '--ppm', '2', '-o', tmp/'radio')
        fail_env = dict(os.environ, T98_TEST_USB_FAILURE='1')
        run(exe, '--rtl-library', dll, '--seconds', '.1', '-o', tmp/'failed', code=1, env=fail_env)
        print('PASS: receiver files -> both readers -> FEC/explicit key -> real vocoder WAV; RTL ABI, Unicode and failure paths')


if __name__ == '__main__':
    main()
