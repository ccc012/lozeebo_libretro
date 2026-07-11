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
    ZT_FILE_STUB     = 0x78, /* Truncate/GetInfoEx/SetCacheSize/Map */

    /* ISound (vtable) */
    ZT_SND_ADDREF    = 0x80,
    ZT_SND_RELEASE   = 0x81,
    ZT_SND_PLAY      = 0x82, /* R1=addr R2=bytes R3=rate [SP]=fmt [SP+4]=loop */
    ZT_SND_STOP      = 0x83, /* R1=voice                          */
    ZT_SND_SETVOLUME = 0x84, /* R1=voice R2=vol                   */

    /* ---- Suporte a modulos BREW reais ---- */

    /* AEEHelperFuncs: slot N da tabela -> trap 0x100+N (117 slots) */
    ZT_HELPER_BASE   = 0x100,
    ZT_HELPER_END    = 0x1FF,

    /* IShell real (ordem AEEShell.h): slot N -> trap 0x200+N (128) */
    ZT_ISHELL_BASE   = 0x200,
    ZT_ISHELL_END    = 0x27F,

    /* Retorno de guest call (maquina de estados do boot) */
    ZT_GUEST_RETURN  = 0x290,

    /* Interfaces stub logadas (IDisplay real etc): 64 slots gener. */
    ZT_STUB_BASE     = 0x300,
    ZT_STUB_END      = 0x3FF,

    /* EGL interface (para jogos 3D como Zeebo Family Pack) */
    ZT_EGL_BASE      = 0x400,
    ZT_EGL_END       = 0x4FF,

    /* GL interface (OpenGL ES para jogos 3D) */
    ZT_GL_BASE       = 0x500,
    ZT_GL_END        = 0x5FF,

    ZT_MAX = 0x600
};

/* ---- Class IDs (convencao; valores reais viram da doc BREW) ---- */
#define AEECLSID_SHELL    0x01001000u
#define AEECLSID_DISPLAY  0x01001001u
#define AEECLSID_GRAPHICS 0x01001002u
#define AEECLSID_BITMAP   0x01001003u
#define AEECLSID_FILEMGR  0x01001004u
#define AEECLSID_SOUND    0x01001005u

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
void zbrew_handle_bitmap(uint32_t id);
void zbrew_handle_file(uint32_t id);
uint32_t zbrew_create_filemgr(void);
uint32_t zbrew_make_idisplay_real(void);
void zbrew_handle_sound(uint32_t id);

/* Diretorio base para IFile (setado pelo loader com a pasta da ROM) */
void zbrew_set_file_base(const char *dir);

/* Le o argumento N passado na stack (0 = primeiro arg apos R3) */
uint32_t zbrew_stack_arg(int n);

/* ---- helpers.c: AEEHelperFuncs para modulos BREW reais ---- */
uint32_t zbrew_build_helper_table(void);   /* aloca e preenche; ret. endereco */
void     zbrew_handle_helper(uint32_t id); /* dispatch trap 0x100+ */

/* ---- boot.c: ciclo de vida do applet BREW real ---- */
/* Inicia o boot: chama AEEMod_Load do modulo carregado.
 * entry: endereco de entrada; clsid: applet a criar (0 = desconhecido) */
void zboot_start(uint32_t entry, uint32_t applet_clsid);
uint32_t zboot_get_applet_object(void);
/* Trap ZT_GUEST_RETURN: avanca a maquina de estados */
void zboot_on_guest_return(void);
/* Processa timers expirados e executa callbacks */
void zboot_process_timers(void);
/* Chamado por frame: dispara timers vencidos (IShell_SetTimer) */
void zboot_tick(uint32_t elapsed_ms);
/* Dispatch dos traps do IShell real (0x200+) */
void zbrew_handle_ishell_real(uint32_t id);
/* Dispatch dos stubs logados (0x300+) */
void zbrew_handle_stub(uint32_t id);
/* Endereco do objeto IShell real (vtable ordem AEEShell.h) */
uint32_t zboot_shell_obj(void);
/* Estado atual do boot em texto (debug/log) */
const char *zboot_state_name(void);
/* Serializacao do estado do boot para savestate */
size_t zboot_serialize(void *dst, size_t max);
bool   zboot_unserialize(const void *src, size_t len);

#endif /* ZEEBO_BREW_H */
