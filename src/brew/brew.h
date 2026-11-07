/* brew.h - Sistema HLE das APIs BREW
 *
 * Mecanismo (docs 05_EMULACAO_APIS_BREW.md): enderecos magicos.
 * Cada API tem um endereco na regiao 0xF0000000+. As vtables das
 * interfaces (criadas na memoria emulada) apontam para esses enderecos.
 * Quando o PC entra na regiao, o trap identifica a API pelo endereco,
 * le os argumentos de R0-R3 (+stack), emula, poe o retorno em R0 e
 * volta com PC = LR.
 *
 * NOTA: os layouts de vtable abaixo sao a convencao deste emulador.
 * Compatibilidade com MODs reais requer reverse engineering do
 * AEEShell real (fase de RE com Ghidra - ver docs).
 */
#ifndef ZEEBO_BREW_H
#define ZEEBO_BREW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- Enderecos magicos: 0xF0000000 + id*4 ---- */
#define ZTRAP_BASE   0xF0000000u
#define ZTRAP_ADDR(id) (ZTRAP_BASE + ((uint32_t)(id) << 2))
#define ZTRAP_ID(addr) (((addr) - ZTRAP_BASE) >> 2)

enum ztrap_id {
    /* Sistema */
    ZT_RETURN_APPLET = 0x00,  /* jogo retornou do entry point -> halt */

    /* Helpers globais BREW (chamados por stubs, nao por vtable) */
    ZT_MALLOC        = 0x08,  /* R0=size -> R0=addr             */
    ZT_FREE          = 0x09,  /* R0=addr                        */
    ZT_REALLOC       = 0x0A,  /* R0=addr R1=size -> R0=addr     */
    ZT_MEMSET        = 0x0B,  /* R0=dst R1=valor R2=len         */
    ZT_MEMCPY        = 0x0C,  /* R0=dst R1=src R2=len           */
    ZT_LOG_STR       = 0x0D,  /* R0=ptr string (DBGPRINTF)      */
    ZT_GET_TIME_MS   = 0x0E,  /* -> R0=ms desde o boot          */
    ZT_GET_KEYS      = 0x0F,  /* -> R0=bitmask de teclas        */

    /* Traps de teste (ROM de teste / diagnostico) */
    ZT_TEST_FILL     = 0x10,  /* R0=cor XRGB: preenche a tela   */
    ZT_TEST_RECT     = 0x11,  /* R0=x R1=y R2=w R3=h (cor atual)*/
    ZT_TEST_SETCOLOR = 0x12,  /* R0=cor XRGB                    */

    /* IShell (vtable) */
    ZT_SHELL_ADDREF         = 0x20,
    ZT_SHELL_RELEASE        = 0x21,
    ZT_SHELL_CREATEINSTANCE = 0x22, /* R0=this R1=clsid R2=ptr_out */
    ZT_SHELL_GETDEVICEINFO  = 0x23, /* R0=this R1=ptr AEEDeviceInfo */
    ZT_SHELL_SETTIMER       = 0x24, /* logado, nao implementado    */
    ZT_SHELL_GETUPTIMEMS    = 0x25,

    /* IDisplay (vtable) */
    ZT_DISP_ADDREF      = 0x30,
    ZT_DISP_RELEASE     = 0x31,
    ZT_DISP_UPDATE      = 0x32,  /* marca frame pronto            */
    ZT_DISP_CLEARSCREEN = 0x33,
    ZT_DISP_SETCOLOR    = 0x34,  /* R1=cor XRGB                   */
    ZT_DISP_FILLRECT    = 0x35,  /* R1=x R2=y R3=w [SP]=h         */
    ZT_DISP_DRAWRECT    = 0x36,  /* R1=x R2=y R3=w [SP]=h         */
    ZT_DISP_DRAWLINE    = 0x37,  /* R1=x0 R2=y0 R3=x1 [SP]=y1     */
    ZT_DISP_DRAWPIXEL   = 0x38,  /* R1=x R2=y                     */
    ZT_DISP_BITBLT      = 0x39,  /* R1=src R2=w R3=h [SP]=x [SP+4]=y */

    /* IBitmap (vtable) */
    ZT_BMP_ADDREF  = 0x50,
    ZT_BMP_RELEASE = 0x51,
    ZT_BMP_GETINFO = 0x52,  /* R0=this R1=ptr_info(w,h)          */
    ZT_BMP_BLT     = 0x53,  /* R0=this R1=x R2=y                 */

    /* IFileMgr (vtable, ordem AEEFile.h) */
    ZT_FMGR_ADDREF   = 0x60,
    ZT_FMGR_RELEASE  = 0x61,
    ZT_FMGR_OPENFILE = 0x62, /* R0=this R1=nome R2=modo OFM -> R0=IFile ou 0 */
    ZT_FMGR_TEST     = 0x63, /* R0=this R1=nome -> R0=0 se existe */
    ZT_FMGR_REMOVE   = 0x64,
    ZT_FMGR_GETINFO  = 0x65, /* R0=this R1=nome R2=FileInfo*      */
    ZT_FMGR_MKDIR    = 0x66, /* R0=this R1=nome                   */
    ZT_FMGR_STUB     = 0x67, /* slots reais sem implementacao: loga e falha */

    /* IFile (vtable, ordem AEEFile.h: herda IAStream) */
    ZT_FILE_ADDREF   = 0x70,
    ZT_FILE_RELEASE  = 0x71, /* fecha o arquivo                   */
    ZT_FILE_READ     = 0x72, /* R0=this R1=buf R2=len -> R0=lidos */
    ZT_FILE_WRITE    = 0x73,
    ZT_FILE_SEEK     = 0x74, /* R0=this R1=tipo(0=ini 1=fim 2=atual) R2=offset */
    ZT_FILE_READABLE = 0x75, /* R0=this R1=AEECallback* (ignorado) */
    ZT_FILE_CANCEL   = 0x76,
    ZT_FILE_GETINFO  = 0x77, /* R0=this R1=FileInfo*              */
    ZT_FILE_STUB     = 0x78, /* Truncate/GetInfoEx                */
    ZT_UNZIP_READ    = 0x79,
    ZT_UNZIP_SETSTREAM = 0x7A,
    ZT_FILE_SETCACHE = 0x7B, /* SetCacheSize: SUCCESS (zeemu)     */
    ZT_FILE_MAP      = 0x7C, /* Map(pBuf, size, offset) -> ptr    */

    /* ISound (vtable, ordem AEEISound.h confirmada no zeemu BrewSound.cpp) */
    ZT_SND_ADDREF      = 0x80,
    ZT_SND_RELEASE     = 0x81,
    ZT_SND_REGISTERNOTIFY = 0x82,
    ZT_SND_SET        = 0x83,
    ZT_SND_GET        = 0x84,
    ZT_SND_SETDEVICE  = 0x85,
    ZT_SND_PLAYTONE   = 0x86,
    ZT_SND_PLAYTONELIST = 0x87,
    ZT_SND_PLAYFREQTONE = 0x88,
    ZT_SND_STOPTONE   = 0x89,
    ZT_SND_VIBRATE    = 0x8A,
    ZT_SND_STOPVIBRATE = 0x8B,
    ZT_SND_SETVOLUME  = 0x8C,
    ZT_SND_GETVOLUME  = 0x8D,
    ZT_SND_GETRESOURCECTL = 0x8E,

    /* IFileMgr: slots de vtable sem implementacao, com ID unico por slot
     * para o log indicar qual metodo real foi chamado (6/+8..+20). Evita
     * colidir com ZT_FILE_* (0x70-0x78) e ZT_SND_* (0x80-0x84). */
    ZT_FMGR_STUB2    = 0x90, /* +0 RmDir +1 GetFreeSpace +2 GetLastError
                              * +3 EnumInit +4 EnumNext +5 Rename
                              * +6 EnumNextEx +7 SetDescription +8 GetInfoEx
                              * +9 Use +10 GetFileUseInfo +11 ResolvePath
                              * +12 CheckPathAccess +13 GetFreeSpaceEx */

    /* ---- Suporte a modulos BREW reais ---- */

    /* AEEHelperFuncs: slot N da tabela -> trap 0x100+N (117 slots) */
    ZT_HELPER_BASE   = 0x100,
    ZT_HELPER_END    = 0x1FF,

    /* IShell real (ordem AEEShell.h): slot N -> trap 0x200+N (128) */
    ZT_ISHELL_BASE   = 0x200,
    ZT_ISHELL_END    = 0x27F,

    /* Retorno de guest call (maquina de estados do boot) */
    ZT_GUEST_RETURN  = 0x290,
    /* AEECallback real devolvido por IThread_GetResumeCBK. */
    ZT_THREAD_RESUME_CALLBACK = 0x291,

    /* Interfaces stub logadas (IDisplay real etc): 64 slots gener. */
    ZT_STUB_BASE     = 0x300,
    ZT_STUB_END      = 0x3FF,

    /* EGL interface (para jogos 3D como Zeebo Family Pack) */
    ZT_EGL_BASE      = 0x400,
    ZT_EGL_END       = 0x4FF,

    /* GL interface (OpenGL ES para jogos 3D) */
    ZT_GL_BASE       = 0x500,
    ZT_GL_END        = 0x5FF,

    /* IGLES (QGL_INTF 0x0103D8EA / QGL_SUB 0x0103D8DD): vtable de 160
     * slots na ordem glES_name_for_index do qualcomm. Diferente da IGL
     * alfabetica: as funcoes gl recebem this em R0 e os argumentos GL
     * comecam em R1 (confirmado nos logs do Pac-Mania). Slot N -> 0x700+N */
    ZT_IGLES_BASE    = 0x700,
    ZT_IGLES_END     = 0x79F,

    /* IFont real (ordem AEEFont.h): slot 0=AddRef 1=Release 2=QueryInterface
     * 3=DrawText 4=GetFontMetrics 5=MeasureText -> trap 0x600+N */
    ZT_FONT_BASE     = 0x600,
    ZT_FONT_END      = 0x605,

    /* ISignal / ISignalCtl (AEESignal.h): slot 0=AddRef 1=Release
     * 2=QueryInterface 3=Set 4=Detach 5=Enable -> trap 0x6A0+N.
     * Objeto: +0 vtable, +4 callback PFNNOTIFY, +8 pUser, +12 detached,
     * +16 pendente (Set sem callback valido; Enable re-dispara). */
    ZT_SIGNAL_BASE   = 0x6A0,
    ZT_SIGNAL_END    = 0x6AF,

    /* IMedia real (ordem AEEIMedia.h confirmada no zeemu BrewMediaPCM.cpp):
     * 0=AddRef 1=Release 2=QueryInterface 3=RegisterNotify
     * 4=SetMediaParm 5=GetMediaParm 6=Play 7=Record 8=Stop 9=Seek
     * 10=Pause 11=Resume 12=GetTotalTime 13=GetState
     * SetMediaData NAO e um slot: vai por SetMediaParm(MM_PARM_MEDIA_DATA=1).
     * Slot N -> trap 0x800+N. */
    ZT_MEDIA_BASE    = 0x800,
    ZT_MEDIA_ADDREF  = 0x800,
    ZT_MEDIA_RELEASE = 0x801,
    ZT_MEDIA_QUERYINTERFACE = 0x802,
    ZT_MEDIA_REGISTERNOTIFY = 0x803,
    ZT_MEDIA_SETMEDIADATA   = 0x804, /* SetMediaParm */
    ZT_MEDIA_GETMEDIADATA   = 0x805, /* GetMediaParm */
    ZT_MEDIA_PLAY    = 0x806,
    ZT_MEDIA_RECORD  = 0x807,
    ZT_MEDIA_STOP    = 0x808,
    ZT_MEDIA_SEEK    = 0x809,
    ZT_MEDIA_PAUSE   = 0x80A,
    ZT_MEDIA_RESUME  = 0x80B,
    ZT_MEDIA_GETTOTALTIME = 0x80C,
    ZT_MEDIA_GETSTATE = 0x80D,
    ZT_MEDIA_END      = 0x81F,

    /* IMediaUtil (AEEMediaUtil.h) -> trap 0x860+N */
    ZT_MEDIAUTIL_BASE = 0x860,
    ZT_MEDIAUTIL_ADDREF = 0x860,
    ZT_MEDIAUTIL_RELEASE = 0x861,
    ZT_MEDIAUTIL_CREATEMEDIA = 0x862,
    ZT_MEDIAUTIL_DETECTTYPE = 0x863,
    ZT_MEDIAUTIL_END  = 0x86F,

    /* ISource (AEEISource.h) -> trap 0x870+N */
    ZT_SOURCE_BASE    = 0x870,
    ZT_SOURCE_ADDREF  = 0x870,
    ZT_SOURCE_RELEASE = 0x871,
    ZT_SOURCE_READ    = 0x872,
    ZT_SOURCE_SEEK    = 0x873,
    ZT_SOURCE_GETLENGTH = 0x874,
    ZT_SOURCE_END     = 0x87F,

    /* IImageDecoder (AEEIImageDecoder.h) + IForceFeed:
     * decoder: 0=AddRef 1=Release 2=QI 3=GetBitmap 4=GetRop
     * forcefeed: 0=AddRef 1=Release 2=QI 3=Write 4=Reset
     * (ZT_IMAGE_BASE 0x400 colide com EGL — nao reutilizar.) */
    ZT_IMGDEC_BASE    = 0x880,
    ZT_IMGDEC_END     = 0x88F,
    ZT_FORCEFEED_BASE = 0x890,
    ZT_FORCEFEED_END  = 0x894,

    /* IImage — FORA de ZT_EGL (0x400). Antes colidia e o dispatch ia pro EGL.
     * 0=AddRef 1=Release 2=Draw 3=DrawFrame 4=GetInfo 5=SetParm
     * 6=Start 7=Stop 8=SetStream 9=HandleEvent 10=Notify */
    ZT_IMAGE_BASE    = 0x8A0,
    ZT_IMAGE_END     = 0x8AA,

    /* IMemAStream (AEE.h): 0=AddRef 1=Release 2=Readable 3=Read 4=Cancel
     * 5=Set 6=SetEx. CLSID 0x0100100C. */
    ZT_MEMASTREAM_BASE = 0x8B0,
    ZT_MEMASTREAM_END  = 0x8B6,

    ZT_MAX = 0x8C0
};

/* ---- Class IDs (convencao; valores reais viram da AEE) ---- */
#define AEECLSID_SHELL    0x01001000u
#define AEECLSID_DISPLAY  0x01001001u
#define AEECLSID_GRAPHICS 0x01001002u
#define AEECLSID_BITMAP   0x01001003u
#define AEECLSID_FILEMGR  0x01001004u
#define AEECLSID_SOUND    0x01001005u

/* Class IDs de midia (AEEMediaClassIDs.h / aee_ids.h) */
#define AEECLSID_MEDIAQM     0x01005500u
#define AEECLSID_MEDIAMIDI   0x01005501u
#define AEECLSID_MEDIAMP3    0x01005502u
#define AEECLSID_MEDIAQCP    0x01005503u
#define AEECLSID_MEDIAPMD    0x01005504u
#define AEECLSID_MEDIAMPEG4  0x01005507u
#define AEECLSID_MEDIAMMF    0x01005508u
#define AEECLSID_MEDIAADPCM  0x0100550Au
#define AEECLSID_MEDIAAAC    0x0100550Bu
#define AEECLSID_MEDIAUTIL   0x0100550Du
#define AEECLSID_MEDIAAMR    0x0100550Eu
#define AEECLSID_MEDIASAF    0x01005510u
#define AEECLSID_MEDIAPCM    0x01005511u
#define AEECLSID_MEDIAXMF    0x01005512u
#define AEECLSID_MEDIADLS    0x01005513u
#define AEECLSID_MEDIASVG    0x01005514u
#define AEECLSID_MEDIAWMA    0x0108D5F5u

/* Modos MMD (AEEMediaData) */
#define AEEMEDIA_MMD_FILE       2u
#define AEEMEDIA_MMD_BUFFER     3u
#define AEEMEDIA_MMD_MDATA      4u
#define AEEMEDIA_MMD_VARIANT    5u
#define AEEMEDIA_MMD_STREAM     6u

/* Codigos de notificacao IMedia_RegisterNotify (AEEMedia.h) */
#define AEEMEDIA_NOTIFY_PAUSE    1u
#define AEEMEDIA_NOTIFY_ABORTED  2u
#define AEEMEDIA_NOTIFY_FIM      3u     /* MM_STATE_DONE */
#define AEEMEDIA_NOTIFY_UNSUPPORTED 4u
#define AEEMEDIA_NOTIFY_LOCKED   5u
#define AEEMEDIA_NOTIFY_MEDIA_RESIZE 6u

/* Codigos de erro BREW */
#define ZBREW_SUCCESS   0
#define ZBREW_ECLASSNOTSUPPORT 12
#define ZBREW_EFAILED   1

bool zbrew_init(void);           /* cria IShell + registra trap handler */
void zbrew_reset(void);
void zbrew_shutdown(void);

/* Endereco emulado do IShell (passado em R0 para o entry do jogo) */
uint32_t zbrew_shell_ptr(void);

/* True quando o jogo chamou IDisplay_Update desde o ultimo frame */
bool zbrew_frame_ready(void);
void zbrew_clear_frame_flag(void);
void zbrew_mark_frame(void);

/* Milissegundos desde o boot (incrementado pelo retro_run) */
void zbrew_tick_ms(uint32_t ms);

/* Cria um objeto de interface na memoria emulada:
 * [ptr_vtable][dados...]; a vtable e preenchida com os traps dados.
 * Retorna endereco emulado do objeto (0 se falhou). */
uint32_t zbrew_make_interface(const uint32_t *trap_ids, int n_methods,
                              uint32_t data_words);

/* Handlers implementados nos modulos i*.c (chamados pelo dispatch) */
void zbrew_handle_shell(uint32_t id);
void zbrew_handle_memory(uint32_t id);
void zbrew_handle_display(uint32_t id);
void zbrew_handle_idisplay_real(uint32_t id);
void zbrew_handle_bitmap(uint32_t id);
void zbrew_handle_file(uint32_t id);
uint32_t zbrew_create_filemgr(void);
uint32_t zbrew_create_file_object(int slot);
int      zbrew_ifile_is_live(uint32_t obj);
int      zbrew_ifile_block_write(uint32_t addr, uint32_t len);
uint32_t zbrew_ifile_force_vtbl_read(uint32_t addr);
void     zbrew_ifile_release_addr(uint32_t obj);
void     zbrew_ifile_reset(void);
/* relative=1: vtbl entries = trap-vtbl (RE4 ldr+add). 0: absolute (FIFA BLX). */
uint32_t zbrew_make_idisplay_real(int relative);
void zbrew_handle_sound(uint32_t id);
void zbrew_handle_media(uint32_t id);
void zbrew_media_tick(void);
void zbrew_handle_source(uint32_t id);
void zbrew_handle_media_util(uint32_t id);

/* Cache de interfaces criadas por CreateInstance (clsid -> obj), usado
 * pelos handlers de midia/source para mapear obj de volta ao estado. */
void     createinstance_cache_put(uint32_t clsid, uint32_t obj);
uint32_t createinstance_cache_get(uint32_t clsid);

/* Diretorio base para IFile (setado pelo loader com a pasta da ROM) */
void zbrew_set_file_base(const char *dir);

/* Pasta do .mod (app root BREW). fs:/~ resolve aqui, nao na raiz do pacote. */
void        zbrew_set_mod_dir(const char *path);
const char *zbrew_get_mod_dir(void);

/* Le um arquivo inteiro por nome, tentando o host (pasta da ROM) e o
 * container VFS FUFS (data.vfs). Devolve 1 e preenche *out (malloc) /
 * *out_size em sucesso. Usado pelo IMedia para carregar midia por nome. */
int  zbrew_read_asset(const char *name, uint8_t **out, uint32_t *out_size);

/* Le o argumento N passado na stack (0 = primeiro arg apos R3) */
uint32_t zbrew_stack_arg(int n);

/* IGLES: desloca os reads de stack em +1 slot (this ocupa R0) */
void zbrew_stack_arg_shift(bool on);

/* ---- ifont.c: fontes AEEFont (IFont real) ---- */
/* True se o CLSID eh de fonte conhecida do sistema */
bool zbrew_is_font_clsid(uint32_t clsid);
/* Cria (cachando por CLSID) o objeto IFont real; devolve endereco ou 0 */
uint32_t zbrew_font_create(uint32_t clsid);
/* True se obj eh uma fonte criada aqui; preenche metricas AEEFontMetrics */
bool zbrew_font_get_metrics(uint32_t obj, uint16_t *ascent, uint16_t *descent,
                            uint16_t *leading, uint16_t *max_char_width);
/* Dispatch dos traps do IFont (0x600+) */
void zbrew_handle_font(uint32_t id);

/* ---- helpers.c: AEEHelperFuncs para modulos BREW reais ---- */
uint32_t zbrew_build_helper_table(void);   /* aloca e preenche; ret. endereco */
void     zbrew_handle_helper(uint32_t id); /* dispatch trap 0x100+ */

/* ---- boot.c: ciclo de vida do applet BREW real ---- */
/* Inicia o boot: chama AEEMod_Load do modulo carregado.
 * entry: endereco de entrada; clsid: applet a criar (0 = desconhecido) */
void zboot_start(uint32_t entry, uint32_t applet_clsid);
uint32_t zboot_get_applet_object(void);
uint32_t zboot_get_applet_clsid(void);
/* Trap ZT_GUEST_RETURN: avanca a maquina de estados */
void zboot_on_guest_return(void);
/* Callback real de resume: R0 = objeto IThreadCtl. */
void zboot_thread_resume_callback(void);
/* IThread_Exit/Suspend: handler ja redirecionou PC; trap_dispatch nao BX LR. */
void zbrew_trap_skip_return(void);
/* Processa timers expirados e executa callbacks */
void zboot_process_timers(void);
/* Chamado por frame: dispara timers vencidos (IShell_SetTimer) */
void zboot_tick(uint32_t elapsed_ms);
/* Dispatch dos traps do IShell real (0x200+) */
void zbrew_handle_ishell_real(uint32_t id);
/* Dispatch dos stubs logados (0x300+) */
void zbrew_handle_stub(uint32_t id);
/* Dispatch do ISignal/ISignalCtl real (0x6A0+): Set/Detach/Enable */
void zbrew_handle_signal(uint32_t id);
/* Enfileira um callback assincrono do guest (R0=pUser R1=arg1 R2=arg2).
 * Usado pelo IMedia para PFNMEDIANOTIFY e por outras APIs do SDK. */
void zbrew_queue_signal_callback(uint32_t callback, uint32_t user,
                                 uint32_t arg1, uint32_t arg2);
/* IDIB com vtbl stub DIBM (mesmo layout GetDeviceBitmap/CreateDIBitmap).
 * depth=16 -> RGB565 pitch=w*2; depth=32 -> XRGB8888 pitch=w*4. */
uint32_t zbrew_create_device_bitmap(int width, int height, int depth);
/* Device bitmap da tela (VRAM). Um so para IDisplay e DISPLAY1/EGL. */
uint32_t zbrew_device_bitmap(void);
/* Endereco do objeto IShell real (vtable ordem AEEShell.h) */
uint32_t zboot_shell_obj(void);
/* Estado atual do boot em texto (debug/log) */
const char *zboot_state_name(void);
int zboot_state(void);
/* Valores de zboot_state() usados fora de boot.c (ver enum no boot.c).
 * BOOT_RUNNING=4, BOOT_TIMER_CALL=5, BOOT_SIGNAL_CALL=6. */
#define ZBOOT_RUNNING       4
#define ZBOOT_TIMER_CALL    5
#define ZBOOT_SIGNAL_CALL   6
/* Thread guest: true enquanto o corpo IThread roda (BOOT_SIGNAL_CALL). */
bool zboot_in_thread_guest(void);
/* Se a slice da thread esgotou, grava contexto e volta a RUNNING.
 * Chamar apos zcpu_run; depois zboot_process_timers para bombear applet. */
void zboot_thread_slice_yield_if_needed(void);
#define ZBOOT_THREAD_SLICE_INSTR 50000u
/* Serializacao do estado do boot para savestate */
size_t zboot_serialize(void *dst, size_t max);
bool   zboot_unserialize(const void *src, size_t len);

#endif /* ZEEBO_BREW_H */
