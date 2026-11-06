# RE4 - Analise do render_fn (applet+0x6C)

**ROM:** tests/roms/re4.mod (493152 bytes; codigo ARM inicia no offset de arquivo 0x200, ou seja, VA 0x000000 = arquivo 0x200)
**Ponto de entrada do loop:** 0x2F440 (chamado pelo emulador como render_fn apos cada timer)
**Arquivo de disassembly de referencia:** disasm.py / disasm_re4.py (capstone, modo ARM)

---

## 1. Resumo do achado (resposta direta)

O `render_fn` que fica em `applet+0x6C` **NAO desenha nada**. Ele e uma funcao de predicado/validacao de estado que retorna 0 ou 1 e retorna imediatamente, sem emitir qualquer comando de video.

Consequentemente:
- Nao chama `eglSwapBuffers`
- Nao chama `glClear`
- Nao chama `glDrawArrays` / `glDrawElements`
- Nao chama `glTexImage2D`
- Nao chama `glBindTexture`

Por isso a tela fica preta: o emulador chama `0x2F440` esperando que ele renderize um frame, mas `render_fn` so checa um estado interno e retorna. A renderizacao de verdade (se houver) deve sair por outro caminho (interface IDisplay via `applet+0x3C`, vtable no offset 0x9C), que o emulador nao esta chamando.

---

## 2. Formato do disassembly (disasm.py)

O arquivo `disasm.py` le o ROM e mapeia VA -> offset de arquivo:

```c
#define OFFSET 0x200
u32(va) = ler 4 bytes little-endian em arquivo[0x200 + va]
```

Todas as VA abaixo seguem essa convencao. Opcodes sao mostrados como palavra de 32 bits (little-endian, ARM).

---

## 3. Regiao 0x2F400 .. 0x2F500 — o chamador do render_fn

Esta regiao contem a funcao que o emulador tem chamado de `render_fn`. O trecho critico fica em `0x2F40C .. 0x2F458`:

```
0x2F40C: E92D4010   push {r4, lr}
0x2F410: E1B04000   movs r4, r0        ; r4 = applet (argumento); flags atualizados
0x2F414: 03A0000E   moveq r0, #0xe
0x2F418: 0A00000D   beq #0x2f454       ; se applet == 0, retorna 0
0x2F41C: E5940010   ldr r0, [r4, #0x10]; r0 = *(applet + 0x10)
0x2F420: E3500000   cmp r0, #0
0x2F424: 0A000002   beq #0x2f434       ; se campo == 0, vai para indirecao de literal
0x2F428: EBFF942A   bl #0x144d8        ; chama funcao auxiliar (0x144d8)
0x2F42C: E3A00000   mov r0, #0
0x2F430: E5840010   str r0, [r4, #0x10]; *(applet + 0x10) = 0
0x2F434: E59F0020   ldr r0, [pc, #0x20]; carrega literal em 0x2F45C (0xFFFD0BC0)
0x2F438: E08F0000   add r0, pc, r0     ; computacao PIC (pc-relative)
0x2F43C: E5100004   ldr r0, [r0, #-4]  ; indirecao dupla: obtem ponteiro do applet
0x2F440: E28FE008   add lr, pc, #8     ; lr = endereco de retorno (0x2F450)
0x2F444: E590106C   ldr r1, [r0, #0x6c]; r1 = render_fn = *(applet + 0x6C)
0x2F448: E1A00004   mov r0, r4         ; r0 = applet (primeiro argumento)
0x2F44C: E12FFF11   bx r1              ; BLX-equivalente: chama render_fn(applet)
0x2F450: E3A00000   mov r0, #0         ; descarta valor de retorno, retorna 0
0x2F454: E8BD4010   pop {r4, lr}
0x2F458: E12FFF1E   bx lr
```

### O que cada instrucao faz (0x2F440 .. 0x2F44C)

| Endereco | Opcode | Instrucao | Significado |
|----------|--------|-----------|-------------|
| 0x2F440 | E28FE008 | `add lr, pc, #8` | lr = endereco de volta = 0x2F450 (pc atual + 8 + 8) |
| 0x2F444 | E590106C | `ldr r1, [r0, #0x6c]` | **r1 = render_fn = ponteiro armazenado em applet+0x6C** |
| 0x2F448 | E1A00004 | `mov r0, r4` | r0 = applet (passado como primeiro argumento) |
| 0x2F44C | E12FFF11 | `bx r1` | Desvia para render_fn (bit 0 de r1 = 0 => modo ARM) |
| 0x2F450 | E3A00000 | `mov r0, #0` | Forca retorno 0 (ignora o que render_fn devolveu) |

Apenas uma chamada de desvio indireto existe nesta regiao (`bx r1` em 0x2F44C); a unica `bl` direta eh `bl #0x144d8` em 0x2F428 (funcao auxiliar, na e render).

---

## 4. Estrutura do applet: +0x68 = DISPLAY, +0x6C = render_fn

O construtor do applet fica em `0x17484`. Ele monta uma vtable completa (offsets 0x00 .. 0x74) carregando ponteiros de funcao da literal pool (padrao `ldr r0,[pc,#imm] ; add r0,pc,r0 ; str r0,[r4,#off]`).

Resolucao das duas entradas relevantes da literal pool:

| Campo | Endereco da store | Ponteiro resolvido | Funcao |
|-------|-------------------|--------------------|--------|
| applet+0x68 | 0x175CC (`str r0,[r4,#0x68]`) | **0x00033680** | DISPLAY |
| applet+0x6C | 0x175D8 (`str r0,[r4,#0x6c]`) | **0x00033318** | render_fn |

Exemplo da resolucao para render_fn (+0x6C):
```
0x175D0: E59F00C8   ldr r0, [pc, #0xc8]   ; carrega literal em 0x176A0 = 0x0001BD3C
0x175D4: E08F0000   add r0, pc, r0        ; r0 = 0x175DC + 0x0001BD3C = 0x00033318
0x175D8: E584006C   str r0, [r4, #0x6c]   ; applet+0x6C = 0x33318  (render_fn)
```

E para DISPLAY (+0x68):
```
0x175C4: E59F00D0   ldr r0, [pc, #0xd0]   ; literal em 0x1769C = 0x0001C0B0
0x175C8: E08F0000   add r0, pc, r0        ; r0 = 0x175D0 + 0x0001C0B0 = 0x00033680
0x175CC: E5840068   str r0, [r4, #0x68]   ; applet+0x68 = 0x33680  (DISPLAY)
```

Outros pares `str [r4,#0x68]` / `str [r4,#0x6c]` com o mesmo valor aparecem em `0x15FCC/0x15FD0`, `0x175CC/0x175D8`, `0x247E0/0x247E8`, `0x6C498/0x6C49C` — confirmando que +0x68 e +0x6C sao preenchidos juntos no init do applet.

**Nota:** tambem existem stores que ZERAM esses campos (ex. `0x15FBC: mov r0,#0` seguida de `0x15FD0: str r0,[r4,#0x6c]`), ou seja, render_fn pode ser resetado a 0 em alguns caminhos de limpeza.

---

## 5. Funcao 0x4420 (HandleEvent "verdadeira")

Trecho relevante de `0x4420 .. 0x44B4`:

```
0x4420: E92D41FC   push {r2,r3,r4,r5,r6,r7,r8,lr}
0x4424: E590503C   ldr r5, [r0, #0x3c]     ; r5 = applet->0x3C  (objeto IDisplay / interface com vtable)
0x4428: E1A04000   mov r4, r0              ; r4 = applet
0x442C: E5900040   ldr r0, [r0, #0x40]     ; r0 = applet->0x40
0x4430: E1A08003   mov r8, r3
0x4434: E1A07002   mov r7, r2
0x4438: E1A06001   mov r6, r1              ; r6 = evento
0x443C: EB00AC07   bl #0x2f460             ; chamada de interface (vtable[8]) em *(applet->0x40 + 0x10)
0x4440: E3550000   cmp r5, #0
0x4444: 01A02007   moveq r2, r7
0x4448: 01A01006   moveq r1, r6
0x444C: 01A00004   moveq r0, r4
0x4450: 028DD008   addeq sp, sp, #8
0x4454: E1A03008   mov r3, r8
0x4458: 08BD41F0   popeq {r4,r5,r6,r7,r8,lr}
0x445C: 0AFFFFE3   beq #0x43f0
0x4460: E58D3000   str r3, [sp]
0x4464: E1A03007   mov r3, r7
0x4468: E1A02006   mov r2, r6
0x446C: E1A01004   mov r1, r4
0x4470: E1A00005   mov r0, r5              ; r0 = applet->0x3C
0x4474: EB003BCD   bl #0x133b0             ; chamada de interface no objeto IDisplay (applet->0x3C)
0x4478: E3560001   cmp r6, #1              ; evento == 1 (render/draw)?
0x447C: 1A00000B   bne #0x44b0             ; se nao, pula o render
0x4480: E5940040   ldr r0, [r4, #0x40]     ; r0 = applet->0x40
0x4484: EB00ABE0   bl #0x2f40c             ; <<< CHAMA O CHAMADOR DO render_fn (0x2F40C) >>>
0x4488: E3A00001   mov r0, #1
0x448C: E585002C   str r0, [r5, #0x2c]     ; applet->0x3C->0x2C = 1
0x4490: E5951000   ldr r1, [r5]            ; r1 = vtable de applet->0x3C
0x4494: E1A00005   mov r0, r5
0x4498: E5912000   ldr r2, [r1]            ; r2 = vtable[0] (offset-base vtable, estilo BREW/AEE)
0x449C: E28FE008   add lr, pc, #8
0x44A0: E0822001   add r2, r2, r1          ; r2 = vtable[0] + base_vtable  (endereco real da funcao)
0x44A4: E3A01001   mov r1, #1
0x44A8: E12FFF12   bx r2                   ; chama metodo da vtable IDisplay
0x44AC: E3A00001   mov r0, #1
0x44B0: E8BD41FC   pop {r2,r3,r4,r5,r6,r7,r8,lr}
0x44B4: E12FFF1E   bx lr
```

### Chamadas IDisplay que o jogo faz

1. **`applet->0x3C`** e um objeto com vtable (carregado em `r5` em 0x4424). Chamadas via interface:
   - `0x4474: bl #0x133b0` — chama um metodo da interface em `applet->0x3C` (com argumentos `r0=r5, r1=applet, r2=evento, r3=...`).
   - `0x4490 .. 0x44A8` — chamada via vtable offset-base: `r1 = *r5` (base da vtable), `r2 = [r1]` (vtable[0]), `r2 = r2 + r1`, `bx r2`. Este e o padrao BREW/AEE onde entradas da vtable sao offsets relativos à base.

2. **`applet->0x68` (DISPLAY = 0x33680)** e chamado com `r0 = 0x14` em `0x2F4B4: bx r1` (funcao em 0x2F49C), mas 0x33680 e um predicado (veja secao 7), entao nao produz desenho.

3. O chamador do render_fn (`0x2F40C`, invocado em `0x4484`) dispara `render_fn` em `applet->0x40+0x6C`.

---

## 6. render_fn internamente — funcao 0x33318

Este e o ponteiro armazenado em `applet+0x6C` (resolucao da literal pool no construtor 0x17484). Disassembly completo:

```
0x33318: E92D4008   push {r3, lr}
0x3331C: E5900078   ldr r0, [r0, #0x78]     ; r0 = *(applet + 0x78)
0x33320: E2800F85   add r0, r0, #0x214      ; r0 += 0x214
0x33324: E1D000F8   ldrsh r0, [r0, #8]      ; r0 = *(signed short *)(r0 + 8)
0x33328: E3500FFA   cmp r0, #0x3e8          ; compara r0 com 1000 (0x3E8)
0x3332C: BA000003   blt #0x33340            ; se r0 < 1000, desvia para retorno 0
0x33330: EBFF79BE   bl #0x11a30             ; inicializacao unica (guarda por flag)
0x33334: EBFF5128   bl #0x77dc              ; ldr r0,[r0,#0x34]; bx lr  (le um campo)
0x33338: E3500001   cmp r0, #1
0x3333C: 0A000000   beq #0x33344            ; se resultado == 1, vai para retorno 1
0x33340: E3A00000   mov r0, #0              ; retorno 0
0x33344: E8BD4008   pop {r3, lr}
0x33348: E12FFF1E   bx lr
```

### O que cada instrucao faz

| Endereco | Opcode | Instrucao | O que faz |
|----------|--------|-----------|-----------|
| 0x33318 | E92D4008 | `push {r3, lr}` | Salva r3 e lr na pilha |
| 0x3331C | E5900078 | `ldr r0, [r0, #0x78]` | r0 = ponteiro armazenado em applet+0x78 |
| 0x33320 | E2800F85 | `add r0, r0, #0x214` | r0 += 0x214 (deslocamento dentro da estrutura) |
| 0x33324 | E1D000F8 | `ldrsh r0, [r0, #8]` | r0 = short com sinal em (r0 + 8) |
| 0x33328 | E3500FFA | `cmp r0, #0x3e8` | Compara r0 com 1000 (0x3E8) |
| 0x3332C | BA000003 | `blt #0x33340` | Se r0 < 1000, pula para 0x33340 (retorna 0) |
| 0x33330 | EBFF79BE | `bl #0x11a30` | Chama inicializacao unica (0x11a30) |
| 0x33334 | EBFF5128 | `bl #0x77dc` | Chama 0x77dc (retorna `*(r0+0x34)`) |
| 0x33338 | E3500001 | `cmp r0, #1` | Compara resultado com 1 |
| 0x3333C | 0A000000 | `beq #0x33344` | Se == 1, vai para 0x33344 (retorna 1) |
| 0x33340 | E3A00000 | `mov r0, #0` | Retorna 0 |
| 0x33344 | E8BD4008 | `pop {r3, lr}` | Restaura r3 e lr |
| 0x33348 | E12FFF1E | `bx lr` | Retorna |

### Funcoes chamadas por render_fn

- **0x11a30** (`0x33330`): inicializacao preguiçosa unica (guarda por flag em `0x11A3C`). Carrega ponteiros da literal pool (`0x7786C`, `0x77874`), chama `bl #0x55724` e `blx #0x490` (veneer/stub). Retorna um ponteiro em r0. **Nao e eglSwapBuffers nem qualquer comando de desenho.**
- **0x77dc** (`0x33334`): `0x77DC: E5900034 ldr r0,[r0,#0x34]; E12FFF1E bx lr` — simplesmente le e retorna o campo `*(r0 + 0x34)`. **Nao desenha.**

### Conclusao sobre render_fn

`render_fn` (0x33318) e um **predicato de estado**: verifica se um contador/campo (short em `applet+0x78+0x214+8`) atingiu 1000, roda uma inicializacao unica e le um campo, depois retorna 0 ou 1.

**Ele NAO:**
- chama `eglSwapBuffers`
- chama `glClear`
- chama `glDrawArrays` / `glDrawElements`
- chama `glTexImage2D`
- chama `glBindTexture`
- desenha qualquer primitiva

Ele apenas **retorna imediatamente** (apos a verificacao de estado). Por isso, quando o emulador chama `0x2F440` (que por sua vez chama `render_fn`), nada e desenhado no framebuffer -> **tela preta**.

---

## 7. DISPLAY (applet+0x68) — funcao 0x33680

Para completude, o campo `applet+0x68` (DISPLAY) aponta para 0x33680, que **tambem e um predicado** (retorna 0/1), nao uma funcao de desenho:

```
0x33680: E92D4010   push {r4, lr}
0x33684: E1A04000   mov r4, r0
0x33688: E1D008D4   ldrsb r0, [r0, #0x84]   ; r0 = *(signed byte*)(applet+0x84)
0x3368C: E3500000   cmp r0, #0
0x33690: BA00001A   blt #0x33700            ; se <0, retorna 0
0x33694: E59400A4   ldr r0, [r4, #0xa4]     ; r0 = *(applet+0xA4)
0x33698: E3500000   cmp r0, #0
0x3369C: 0A000017   beq #0x33700            ; se ==0, retorna 0
0x336A0: E5D40084   ldrb r0, [r4, #0x84]
0x336A4: E3500000   cmp r0, #0
0x336A8: 1A000004   bne #0x336c0
0x336AC: EBFF78DF   bl #0x11a30
0x336B0: EBFF7C67   bl #0x12854
0x336B4: E1D419D4   ldrsb r1, [r4, #0x94]
0x336B8: E1500001   cmp r0, r1
0x336BC: 0A00000C   beq #0x336f4            ; se igual, retorna 1
0x336C0: E5D40084   ldrb r0, [r4, #0x84]
0x336C4: E3500001   cmp r0, #1
0x336C8: 1A000004   bne #0x336e0
0x336CC: EBFF78D7   bl #0x11a30
0x336D0: EBFF7C51   bl #0x1281c
0x336D4: E1D419D4   ldrsb r1, [r4, #0x94]
0x336D8: E1500001   cmp r0, r1
0x336DC: 0A000004   beq #0x336f4            ; se igual, retorna 1
0x336E0: E1D408D4   ldrsb r0, [r4, #0x84]
0x336E4: E3500008   cmp r0, #8
0x336E8: BA000004   blt #0x33700            ; se <8, retorna 0
0x336EC: E3500016   cmp r0, #0x16           ; 22
0x336F0: CA000002   bgt #0x33700            ; se >22, retorna 0
0x336F4: E3A00001   mov r0, #1              ; retorna 1
0x336F8: E8BD4010   pop {r4, lr}
0x336FC: E12FFF1E   bx lr
0x33700: E3A00000   mov r0, #0              ; retorna 0
```

Tambem nao desenha: checa campos de estado (`0x84`, `0xA4`, `0x94`) e retorna booleano.

---

## 8. Chamadas EGL/GL no render_fn

**Nenhuma.** Dentro de `render_fn` (0x33318) e de suas chamadas diretas (0x11a30, 0x77dc) nao existem instrucoes `bl`/`blx` para funcoes de EGL ou OpenGL ES. As strings de EGL/GL presentes no ROM (ex. `eglSwapBuffers`, `glClear`, `glDrawArrays`, `glTexImage2D`, `glBindTexture`, `EGL_EXT_swap_control`, etc., localizadas em torno de VA 0x72F78 .. 0x74814) sao **dados de string de uma biblioteca** (referencias a `GLES_ext.c`), nao chamadas de codigo diretamente pelo render_fn.

As chamadas de graficos do RE4 sao feitas, portanto, por interfaces BREW (IDisplay / EGL / GL via `AEECLSID_EGL_REAL` / `AEECLSID_GL_REAL`), nao por chamadas diretas nativas no `render_fn`.

---

## 9. Conclusão — por que a tela fica preta

1. O loop do jogo chama `0x2F440` (render caller) apos cada timer.
2. `0x2F440` faz `ldr r1,[r0,#0x6C]; bx r1`, ou seja, chama o ponteiro em `applet+0x6C`.
3. `applet+0x6C` = **0x33318** (definido pelo construtor em 0x17484).
4. 0x33318 (`render_fn`) **nao desenha**: e um predicado que retorna 0 ou 1 apos checar estado, chamando apenas helpers de inicializacao (0x11a30) e leitura de campo (0x77dc).
5. Não há `eglSwapBuffers`, `glClear`, `glDrawArrays`, `glDrawElements`, `glTexImage2D` nem `glBindTexture` em `render_fn`.
6. Como o emulador descarta o retorno (`mov r0,#0` em 0x2F450) e render_fn nao produz efeitos colaterais de video, nada e escrito no framebuffer -> **tela preta**.

**Recomendação:** a renderizacao de verdade provavelmente deve disparar pela interface IDisplay (`applet+0x3C`, vtable no offset 0x9C, chamada em `0x4498 .. 0x44A8`) ou por outra funcao de desenho que ainda nao esta sendo invocada. O `render_fn` em `applet+0x6C` nao e a funcao que desenha o frame.

---

**Evidencias geradas:** `disasm_re4.py`, `disasm_re4_out.txt`, `disasm_region.py` (0x15FCC, 0x175CC), `disasm_renderfn.py` (0x33318), resolucao da literal pool do construtor 0x17484.
