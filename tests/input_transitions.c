/* Exercises the actual boot/HID seam, including guest-visible event state. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/brew/boot.c"

static void keys(uint32_t mask) {
    zinput_update(!!(mask & ZKEY_UP), !!(mask & ZKEY_DOWN),
                  !!(mask & ZKEY_LEFT), !!(mask & ZKEY_RIGHT),
                  !!(mask & ZKEY_A), !!(mask & ZKEY_B),
                  !!(mask & ZKEY_C), !!(mask & ZKEY_MENU), !!(mask & ZKEY_BACK));
    zboot_tick(16);
}

static void expect_event(uint32_t uid, bool down) {
    uint32_t id, actual_uid;
    bool actual_down;
    assert(hid_evt_pop(&id, &actual_uid, &actual_down));
    assert(actual_uid == uid && actual_down == down);
}

int main(int argc, char **argv) {
    assert(zmem_init());
    zheap_reset();
    zboot_start(0x1000, 0x0102F789u);
    g_state = BOOT_RUNNING;
    if (argc > 1 && !strcmp(argv[1], "position")) {
        uint32_t signal = make_signal_object(0x1000, 0x2000);
        zboot_signal_register(&g_hid_position_signal, signal);
        keys(ZKEY_LEFT);
        assert(g_state == BOOT_SIGNAL_CALL);
        g_signal_queue_count = 0;
        g_state = BOOT_RUNNING;
        keys(0);
        assert(g_hid_axis_lx == 0x8000u);
        assert(g_state == BOOT_SIGNAL_CALL); /* neutral must reach guest */
        g_state = BOOT_RUNNING;
        keys(ZKEY_LEFT);
        assert(g_state == BOOT_SIGNAL_CALL);
    } else if (argc > 1 && !strcmp(argv[1], "abi")) {
        uint32_t obj = zheap_alloc(16), pos = zheap_alloc(100), sentinel = zheap_alloc(4);
        zmem_write32(obj + 4, ZCLSID_HID_DEVICE);
        zmem_write32(sentinel, 0xDEADBEEFu);
        g_cpu.r[0] = obj; g_cpu.r[1] = pos; g_cpu.r[2] = sentinel;
        zbrew_handle_stub(ZT_STUB_BASE + 10);
        assert(zmem_read32(sentinel) == 0xDEADBEEFu);
        assert(zmem_read32(pos + 4) == 0x8000u);
    } else if (argc > 1 && !strcmp(argv[1], "busy-position")) {
        uint32_t signal = make_signal_object(0x1000, 0x2000);
        zboot_signal_register(&g_hid_position_signal, signal);
        g_state = BOOT_SIGNAL_CALL;
        keys(ZKEY_LEFT); keys(0); keys(ZKEY_LEFT);
        assert(g_signal_queue_count == 3);
        const uint32_t samples[] = {0, 0x8000u, 0};
        for (unsigned n = 0; n < 3; n++) {
            g_state = BOOT_RUNNING;
            assert(zboot_signal_drain_one());
            assert(g_hid_axis_lx == samples[n]);
        }
    } else if (argc > 1 && !strcmp(argv[1], "busy")) {
        g_state = BOOT_SIGNAL_CALL;
        keys(ZKEY_LEFT);
        keys(0);
        keys(ZKEY_LEFT);
        g_state = BOOT_RUNNING;
        keys(ZKEY_LEFT);
        expect_event(0x0106C3FFu, true);
        expect_event(0x0106C3FFu, false);
        expect_event(0x0106C3FFu, true);
    } else {
        keys(ZKEY_A);
        expect_event(0x0106C40Au, true);
        keys(ZKEY_B); /* release A and press B in one poll */
        uint32_t mask = ZKEY_A;
        while (g_hid_evt_count) {
            uint32_t id, uid; bool down;
            assert(hid_evt_pop(&id, &uid, &down));
            if (uid == 0x0106C40Au && !down) mask &= ~ZKEY_A;
            if (uid == 0x0106C40Bu && down) mask |= ZKEY_B;
        }
        assert(mask == ZKEY_B); /* guest must not retain held A */
    }
    puts("input transitions: OK");
    zmem_shutdown();
    return 0;
}
