"""Runnable red/green comparator fixtures, no emulator or ROM required."""
from copy import deepcopy
from pathlib import Path
from tempfile import TemporaryDirectory
from capability_gate import compare, screen

base = dict(frames=180, audio_nonzero=42, screen='nonuniform',
            ppm_sha256='image', errors=['historical error'], exit_code=0, timed_out=False)
assert compare(base, deepcopy(base))[0] == 'PASS'
for patch in [dict(screen='uniform', ppm_sha256='uniform'),
              dict(errors=['historical error', 'new fault']),
              dict(audio_nonzero=0), dict(timed_out=True),
              dict(frames=179), dict(exit_code=1), dict(screen='absent', ppm_sha256=None)]:
    assert compare(base, dict(base, **patch))[0] == 'REGRESSION', patch
assert compare(base, {'missing': True})[0] == 'INCONCLUSIVE'
assert compare(base, dict(base, errors=[]))[0] == 'PASS'  # Improvement is not a regression.
assert compare(dict(base, snapshot_deterministic=False), dict(base, ppm_sha256='random-seed-image'))[0] == 'PASS'
assert compare(dict(base, snapshot_deterministic=False), dict(base, screen='uniform', ppm_sha256='uniform'))[0] == 'REGRESSION'
with TemporaryDirectory() as directory:
    ppm = Path(directory) / 'frame.ppm'
    assert screen(ppm) == 'absent'
    for pixels, expected in [(b'\0'*6, 'black'), (b'\xff\0\0'*2, 'uniform'), (b'\xff\0\0\0\xff\0', 'nonuniform')]:
        ppm.write_bytes(b'P6\n2 1\n255\n' + pixels)
        assert screen(ppm) == expected
    ppm.write_bytes(b'P6\n2 1\n255\n\0')
    try:
        screen(ppm)
    except ValueError:
        pass
    else:
        raise AssertionError('truncated image accepted')
print('PASS comparator: unchanged, uniform, new fault, audio loss, missing, timeout, incomplete frames, exit, improvement, PPM validation')
