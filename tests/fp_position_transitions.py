"""Real FP callback proof, not framebuffer hashes. Run from repository root:
python tests/fp_position_transitions.py build-input/Release/lozeebo_libretro.dll ROM
Guest addresses below belong to the supplied Brazil FP module only.
"""
import ctypes as c
import struct
import sys

core = c.CDLL(sys.argv[1])
ENV = c.CFUNCTYPE(c.c_bool, c.c_uint, c.c_void_p)
VIDEO = c.CFUNCTYPE(None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t)
AUDIO = c.CFUNCTYPE(None, c.c_int16, c.c_int16)
BATCH = c.CFUNCTYPE(c.c_size_t, c.c_void_p, c.c_size_t)
POLL = c.CFUNCTYPE(None)
INPUT = c.CFUNCTYPE(c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint)
frame = 0
left = {300, 301, 330, 331, 370, 371}
right = {350, 351}
callbacks = {
    "environment": ENV(lambda cmd, data: cmd == 10),  # XRGB8888
    "video_refresh": VIDEO(lambda *args: None),
    "audio_sample": AUDIO(lambda *args: None),
    "audio_sample_batch": BATCH(lambda data, count: count),
    "input_poll": POLL(lambda: None),
    "input_state": INPUT(lambda port, device, index, key:
                         int((key == 6 and frame in left) or
                             (key == 7 and frame in right))),
}
for name, callback in callbacks.items():
    getattr(core, "retro_set_" + name)(callback)

class Game(c.Structure):
    _fields_ = [("path", c.c_char_p), ("data", c.c_void_p),
                ("size", c.c_size_t), ("meta", c.c_char_p)]

class CPU(c.Structure):
    _fields_ = [("r", c.c_uint32 * 16), ("cpsr", c.c_uint32),
                ("executed", c.c_uint64), ("halted", c.c_bool)]

class Header(c.Structure):
    _fields_ = [("magic", c.c_uint32), ("version", c.c_uint32), ("cpu", CPU),
                ("sizes", c.c_uint32 * 5), ("voices", c.c_uint32 * 16)]

core.retro_serialize_size.restype = c.c_size_t
core.retro_serialize.argtypes = [c.c_void_p, c.c_size_t]
core.retro_serialize.restype = c.c_bool
core.retro_init()
game = Game(sys.argv[2].encode(), None, 0, None)
try:
    assert core.retro_load_game(c.byref(game)), "ROM load failed"
    state = c.create_string_buffer(core.retro_serialize_size())
    for frame in range(400):
        core.retro_run()
        if frame not in {299, 300, 302, 330, 332, 350, 352, 370, 372}:
            continue
        assert core.retro_serialize(state, len(state)), "serialize failed"
        header = Header.from_buffer(state)
        assert header.magic == 0x5A535441 and header.version == 1
        # FP 0x5690 calls 0x6a84; signed axes stored in user+0x154.
        offset = c.sizeof(Header) + header.sizes[0] + 0x121C0 + 0x154
        x, y = struct.unpack_from("<hh", state, offset)
        expected = -32768 if frame in left else 32766 if frame in right else 0
        print(f"frame={frame} guest_x={x} guest_y={y} expected_x={expected}")
        assert (x, y) == (expected, 0), "FP guest axis did not receive transition"
    print("FP held/neutral/same/opposite/previous: OK")
finally:
    core.retro_unload_game()
    core.retro_deinit()
