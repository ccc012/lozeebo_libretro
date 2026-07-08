#include "brew/BrewShellClasses.h"

bool is_generic_core_stub_clsid(uint32_t clsId) {
    switch (clsId) {
        case 0x01001019:
        case 0x0100101a:
        case 0x01001015:
        case 0x01001027:
        case 0x01001028:
        case 0x01001035:
        case 0x01001036:
        case 0x01001037:
        case 0x01001038:
        case 0x01001039:
        case 0x0100103a:
        case 0x0100103b:
        case 0x0100103c:
        case 0x01001043:
        case 0x01001044:
        case 0x01001047:
        case 0x01001048:
        case 0x01001049:
        case 0x0100104a:
        case 0x0100104b:
        case 0x0100104c:
        case 0x0100104d:
        case 0x0100104e:
        case 0x0100104f: // AEECLSID_APPHISTORY — dedicated BrewAppHistory stub
            return false;
        default:
            return (clsId >= 0x01001024 && clsId <= 0x01001055) || clsId == 0x01001057;
    }
}

bool is_hash_clsid(uint32_t clsId) {
    switch (clsId) {
        case 0x01001015:
        case 0x01001027:
        case 0x01001028:
        case 0x01001039:
        case 0x0100103a:
        case 0x0100103b:
        case 0x01001049:
        case 0x0100104a:
        case 0x0100104b:
        case 0x0100104c:
        case 0x0100104d:
        case 0x0100104e:
            return true;
        default:
            return false;
    }
}

bool is_known_applet_clsid(uint32_t clsId) {
    switch (clsId) {
        case 0x01070798:
        case 0x01077cf4:
        case 0x01072195:
        case 0x0102f789:
        case 0x01087a3c:
        case 0x01087c1c:
        case 0x01087b72:
        case 0x0103d666:
        case 0x01009ff2:
        case 0x0108ff07:
        case 0x00c9b04b:
        case 0xbf2e2021:
            return true;
        default:
            return false;
    }
}

bool is_font_clsid(uint32_t clsId) {
    switch (clsId) {
        case 0x01012786:
        case 0x01012787:
        case 0x01012788:
        case 0x0101402c:
        case 0x0101402d:
        case 0x0101402e:
        case 0x01001022:
        case 0x0100a001:
        case 0x0100a002:
        case 0x0100a003:
        case 0x0100a004:
        case 0x0100a005:
        case 0x0100a006:
        case 0x0100a007:
        case 0x0100a008:
        case 0x0100a009:
        case 0x0100a00a:
        case 0x0100a100:
        case 0x0102f679:
        case 0x0102f67a:
        case 0x0102f67b:
        case 0x0102f67c:
        case 0x0102f67d:
        case 0x0102f67e:
        case 0x0102f67f:
        case 0x0102f680:
        case 0x0102f681:
        case 0x01030852:
        case 0x01030853:
            return true;
        default:
            return false;
    }
}
