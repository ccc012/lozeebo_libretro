/* aee_ids.h - Class IDs e tabelas reais do BREW (AEE)
 *
 * Valores confirmados por interoperabilidade (referencia: zeemu, GPL-3.0,
 * github.com/mrpostiga/zeemu, e documentacao publica do BREW SDK).
 */
#ifndef ZEEBO_AEE_IDS_H
#define ZEEBO_AEE_IDS_H

#include <stdint.h>

/* ---- Class IDs de servicos (AEEClassIDs.h) ---- */
#define AEECLSID_DISPLAY_REAL   0x01001001u
#define AEECLSID_DISPLAY1_REAL  0x010127D4u
#define AEECLSID_HEAP_REAL      0x01001002u
#define AEECLSID_FILEMGR_REAL   0x01001003u
#define AEECLSID_DBMGR_REAL     0x01001008u
#define AEECLSID_THREAD_REAL    0x01001017u
#define AEECLSID_MEMCACHE1_REAL 0x0106E415u
#define AEECLSID_RANDOM_REAL    0x0100103Cu
#define AEECLSID_SOUNDPLAYER_R  0x01002000u
#define AEECLSID_GRAPHICS_REAL  0x01002001u
#define AEECLSID_GRAPHICS_BREW  0x0102FB92u
#define AEECLSID_PNGDECODER_R   0x01030766u /* PNGDecoderBREW: IImageDecoder+IForceFeed */
#define AEECLSID_JPEGDECODER_R  0x0102FD92u /* decoder irmao (stb_image; PopCap/Zuma) */
#define AEEIID_IIMAGEDECODER    0x01026E20u
#define AEEIID_IFORCEFEED       0x0101EB0Bu
#define AEECLSID_APPLETCTL_REAL 0x01001058u
#define AEECLSID_SIGNALCBFACT_R 0x01041207u
#define AEECLSID_MEDIA_REAL     0x01005500u
#define AEECLSID_MEDIAMIDI_REAL 0x01005501u
#define AEECLSID_MEDIAMP3_REAL  0x01005502u
#define AEECLSID_MEDIAQCP_REAL  0x01005503u
#define AEECLSID_MEDIAPMD_REAL  0x01005504u
#define AEECLSID_MEDIAMPEG4_REAL 0x01005507u
#define AEECLSID_MEDIAMMF_REAL  0x01005508u
#define AEECLSID_MEDIAADPCM_REAL 0x0100550Au
#define AEECLSID_MEDIAAAC_REAL  0x0100550Bu
#define AEECLSID_MEDIAUTIL_REAL 0x0100550Du
#define AEECLSID_MEDIAAMR_REAL  0x0100550Eu
#define AEECLSID_MEDIASAF_REAL  0x01005510u
#define AEECLSID_MEDIAPCM_REAL  0x01005511u
#define AEECLSID_MEDIAXMF_REAL  0x01005512u
#define AEECLSID_MEDIADLS_REAL  0x01005513u
#define AEECLSID_MEDIASVG_REAL  0x01005514u
#define AEECLSID_MEDIAWMA_RAW   0x0108D5F5u
#define AEECLSID_GL_REAL        0x01014BC3u
#define AEECLSID_EGL_REAL       0x01014BC4u
#define AEECLSID_QEGL_REAL      0x0103D8ECu
#define AEECLSID_HID_REAL       0x0106C411u
#define AEECLSID_I3D_REAL       0x01013A83u
#define AEECLSID_I3DUTIL_REAL   0x0101132Fu
#define AEECLSID_I3DMODEL_REAL  0x010113F6u
#define AEECLSID_TEXTCTL_REAL   0x01003209u
#define AEECLSID_MENUCTL_REAL   0x01003100u
#define AEECLSID_LISTCTL_REAL   0x01003102u
#define AEECLSID_ICONVIEWCTL_R  0x01003103u

/* ---- Class IDs de fontes (AEECLSID_FONT_*, AEEFont.h) ---- */
#define AEECLSID_FONT_FIXED4X6   0x0100A001u
#define AEECLSID_FONT_BASIC6     0x0100A002u
#define AEECLSID_FONT_BASIC9     0x0100A003u
#define AEECLSID_FONT_BASIC10    0x0100A004u
#define AEECLSID_FONT_BASIC11    0x0100A005u
#define AEECLSID_FONT_BASIC11B   0x0100A006u
#define AEECLSID_FONT_BASIC14    0x0100A007u
#define AEECLSID_FONT_BASIC12    0x0100A008u
#define AEECLSID_FONT_BASIC12B   0x0100A009u
#define AEECLSID_FONT_BASIC15    0x0100A00Au
#define AEECLSID_BITFONTFOUNDRY  0x0100A100u
#define AEECLSID_FONT            0x01001022u
#define AEECLSID_FONTSYS_NORMAL  0x01012786u
#define AEECLSID_FONTSYS_LARGE   0x01012787u
#define AEECLSID_FONTSYS_BOLD    0x01012788u
#define AEECLSID_FONTSYS_ITALIC  0x0101402Cu
#define AEECLSID_FONTSYS_BOLDITALIC 0x0101402Du
#define AEECLSID_FONTSYS_LARGEITALLIC 0x0101402Eu
#define AEECLSID_FONT_STANDARD11 0x0102F679u
#define AEECLSID_FONT_STANDARD11B 0x0102F67Au
#define AEECLSID_FONT_STANDARD18 0x0102F67Bu
#define AEECLSID_FONT_STANDARD18B 0x0102F67Cu
#define AEECLSID_FONT_STANDARD23 0x0102F67Du
#define AEECLSID_FONT_STANDARD23B 0x0102F67Eu
#define AEECLSID_FONT_STANDARD26 0x0102F67Fu
#define AEECLSID_FONT_STANDARD26B 0x0102F680u
#define AEECLSID_FONT_STANDARD36 0x0102F681u
#define AEECLSID_FONT_STANDARD15 0x01030852u
#define AEECLSID_FONT_STANDARD15B 0x01030853u

/* ---- Applets conhecidos (CLSID -> jogo) ---- */
typedef struct {
    uint32_t clsid;
    const char *name;
} zaee_applet_t;

static const zaee_applet_t ZAEE_KNOWN_APPLETS[] = {
    { 0x010903C6u, "Zeebo Family Pack" },
    { 0x01087B72u, "Pac-Mania" },
    { 0x0102F789u, "Double Dragon" },
    { 0x01087C1Cu, "Quake II" },
    { 0x01087A3Cu, "Quake" },
    { 0x01070798u, "Z-Wheel" },
    { 0x01072195u, "Zeebo App" },
    { 0x0108FF1Au, "Zeeboids" },
    { 0x01081970u, "Action Hero 3D" },
    { 0x010A2337u, "Alice no Pais das Maravilhas" },
    { 0x0108E356u, "Alien Breaker Deluxe" },
    { 0x01073825u, "Alpine Racer" },
    { 0x0109DA8Eu, "Armageddon Squadron" },
    { 0x0109EC1Bu, "Bad Dudes" },
    { 0x0108C1E1u, "Bejeweled Twist" },
    { 0x010924DEu, "Caveman Ninja" },
    { 0x01081984u, "Crash Bandicoot" },
    { 0x010924DFu, "Dark Seal" },
    { 0x010940DAu, "Disney All Star Cards" },
    { 0x01087AE8u, "FIFA 09" },
    { 0x0108FAB8u, "Galaxy on Fire" },
    { 0x0109EC1Cu, "Heavy Barrel" },
    { 0x010978A2u, "Heavy Weapon" },
    { 0x0109DA8Fu, "Iron Sight" },
    { 0x010924DDu, "Karnov" },
    { 0x010924E0u, "Magical Drop III" },
    { 0x0108C0BCu, "Need For Speed" },
    { 0x01099CD6u, "Peggle" },
    { 0x010A1241u, "Powerboat Challenge" },
    { 0x0108D48Eu, "Prey Evil" },
    { 0x0109DA90u, "Raging Thunder" },
    { 0x0108FAB9u, "Rally Master Pro" },
    { 0x0109DA91u, "Reckless Racing" },
    { 0x0108AF6Cu, "Resident Evil 4" },
    { 0x01087B73u, "Ridge Racer" },
    { 0x010924E1u, "Spin Master" },
    { 0x010924E2u, "Street Hoop" },
    { 0x010924E3u, "Super BurgerTime" },
    { 0x0108D1B7u, "Tekken 2" },
    { 0x010A6D13u, "Tork and Kral" },
    { 0x01095146u, "Toy Raid" },
    { 0x01087A49u, "Treino Cerebral" },
    { 0x010A2335u, "Turma da Monica" },
    { 0x010413C3u, "Ultimate Chess 3D" },
    { 0x010963A5u, "Um Jogo de Ovos" },
    { 0x010924E4u, "Wizard Fire" },
    { 0x0109400Bu, "Zeebo Channels" },
    { 0x0109E4E0u, "Zeebo Clube" },
    { 0x0108FF07u, "Zeebo Extreme Baja" },
    { 0x0108FF13u, "Zeebo Extreme Boia Cross" },
    { 0x0108FF06u, "Zeebo Extreme Corrida Aerea" },
    { 0x0108FF14u, "Zeebo Extreme Jetboard" },
    { 0x0108C0BBu, "Zeebo Extreme Rolima" },
    { 0x0108FF19u, "Zeebo F.C. Foot Camp" },
    { 0x0108FF17u, "Zeebo F.C. Super League" },
    { 0x0108FF18u, "Zeebo Sports Peteca" },
    { 0x0108FF16u, "Zeebo Sports Queimada" },
    { 0x0108EFF9u, "Zeebo Sports Tenis" },
    { 0x0108FF15u, "Zeebo Sports Volei" },
    { 0xBF2E2021u, "Zenonia" },
    { 0x0109AF55u, "Zuma" },
};
#define ZAEE_KNOWN_APPLET_COUNT \
    (sizeof(ZAEE_KNOWN_APPLETS) / sizeof(ZAEE_KNOWN_APPLETS[0]))

/* ---- Nomes dos slots da AEEHelperFuncs (offset = indice * 4) ----
 * Tabela de 117 funcoes que modulos BREW usam via GET_HELPER()
 * (ponteiro gravado em AEEMod_Load - 4). */
static const char *const ZAEE_HELPER_NAMES[] = {
    /* 0x000 */ "memmove", "memset", "strcpy", "strcat",
    /* 0x010 */ "strcmp", "strlen", "strchr", "strrchr",
    /* 0x020 */ "sprintf", "wstrcpy", "wstrcat", "wstrcmp",
    /* 0x030 */ "wstrlen", "wstrchr", "wstrrchr", "wsprintf",
    /* 0x040 */ "strtowstr", "wstrtostr", "wstrtofloat", "floattowstr",
    /* 0x050 */ "utf8towstr", "wstrtoutf8", "wstrlower", "wstrupper",
    /* 0x060 */ "chartype", "SetupNativeImage", "malloc", "free",
    /* 0x070 */ "wstrdup", "realloc", "wwritelongex", "wstrsize",
    /* 0x080 */ "wstrncopyn", "OEMStrLen", "OEMStrSize", "GetAEEVersion",
    /* 0x090 */ "atoi", "f_op", "f_cmp", "dbgprintf",
    /* 0x0A0 */ "wstrcompress", "LocalTimeOffset", "GetRand", "GetTimeMS",
    /* 0x0B0 */ "GetUpTimeMS", "GetSeconds", "GetJulianDate", "sysfree",
    /* 0x0C0 */ "GetAppInstance", "strtoul", "strncpy", "strncmp",
    /* 0x0D0 */ "stricmp", "strnicmp", "strstr", "memcmp",
    /* 0x0E0 */ "memchr", "strexpand", "stristr", "memstr",
    /* 0x0F0 */ "wstrncmp", "strdup", "strbegins", "strends",
    /* 0x100 */ "strchrend", "strchrsend", "memrchr", "memchrend",
    /* 0x110 */ "memrchrbegin", "strlower", "strupper", "wstricmp",
    /* 0x120 */ "wstrnicmp", "inet_aton", "inet_ntoa", "swapl",
    /* 0x130 */ "swaps", "GetFSFree", "GetRAMFree", "vsprintf",
    /* 0x140 */ "vsnprintf", "snprintf", "JulianToSeconds", "strlcpy",
    /* 0x150 */ "strlcat", "wstrlcpy", "wstrlcat", "setstaticptr",
    /* 0x160 */ "f_assignstr", "f_assignint", "wwritelong", "dbgheapmark",
    /* 0x170 */ "lockmem", "unlockmem", "dumpheap", "strtod",
    /* 0x180 */ "f_calc", "sleep", "getlasterror", "wgs84_to_degrees",
    /* 0x190 */ "dbgevent", "IsBadPtr", "basename", "makepath",
    /* 0x1A0 */ "splitpath", "stribegins", "GetUTCSeconds", "f_toint",
    /* 0x1B0 */ "f_get", "qsort", "trunc", "utrunc",
    /* 0x1C0 */ "err_realloc", "err_strdup", "inet_pton", "inet_ntop",
    /* 0x1D0 */ "GetALSContext",
};
#define ZAEE_HELPER_COUNT \
    (sizeof(ZAEE_HELPER_NAMES) / sizeof(ZAEE_HELPER_NAMES[0]))

/* ---- Nomes dos metodos do IShell (ordem real do AEEShell.h) ---- */
static const char *const ZAEE_ISHELL_NAMES[] = {
    "AddRef", "Release", "CreateInstance", "QueryClass",
    "GetDeviceInfo", "StartApplet", "CloseApplet", "CanStartApplet",
    "ActiveApplet", "EnumAppletInit", "EnumNextApplet", "SetTimer",
    "CancelTimer", "GetTimerExpiration", "CreateDialog", "GetActiveDialog",
    "EndDialog", "LoadResString", "LoadResData", "LoadResObject",
    "FreeResData", "SendEvent", "Beep", "GetPrefs",
    "SetPrefs", "GetItemStyle", "Prompt", "MessageBox",
    "MessageBoxText", "SetAlarm", "CancelAlarm", "AlarmsActive",
    "GetHandler", "RegisterHandler", "RegisterNotify", "Notify",
    "Resume", "ForceExit", "GetPosition", "CheckPrivLevel",
    "IsValidResource", "LoadResDataEx", "RegisterSystemCallback", "DetectType",
    "GetDeviceInfoEx", "GetClassItemID", "Obsolete", "GetProperty",
    "SetProperty", "RegisterEvent", "Reset", "AppIsInGroup",
    "GetUpTimeMS",
};
#define ZAEE_ISHELL_NAMED_COUNT \
    (sizeof(ZAEE_ISHELL_NAMES) / sizeof(ZAEE_ISHELL_NAMES[0]))

#endif /* ZEEBO_AEE_IDS_H */
