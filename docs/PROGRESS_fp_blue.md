# Progresso: Correção da película azul sobre personagens no Zeebo Family Pack

- **Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\progress-fp-blue`
- **Branch:** `progress/fp-blue`
- **Data:** 2026-11-08

## 1. Causa Comprovada

No Zeebo Family Pack, o menu inicial/roda desenha primeiro os fundos azuis configurando cor primária azul `glColor4x(0, 0x75C2, 0xA3D7, 0x10000)` (r=0.00, g=0.46, b=0.64, a=1.00).

Para desenhar os avatares/personagens texturizados na roda (canto inferior esquerdo e centro), o jogo invoca `glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE)` (0x2300, 0x2200, 0x1E01) — detectado 3.400 vezes nos logs de frame da roda. Na especificação OpenGL ES 1.1 (Tabela 3.8), o modo `GL_REPLACE` substitui a cor do fragmento pelos canais da textura ($C_v = C_t$), ignorando a cor primária corrente de vértice/glColor ($C_f$).

Antes da correção, `src/gpu/egl_gl.c` tratava `GLFN_TexEnvx` e `GLFN_TexEnvxv` como no-op e o rasterizador (`raster_triangle`) aplicava incondicionalmente modulação multiplicativa ($C_v = C_f \times C_t$). Como consequência matemática, as cores da textura dos personagens eram multiplicadas pela cor azul do fundo anterior, tingindo os avatares de ciano/verde escuro cadavérico.

## 2. Correção Implementada

- Armazenamento e chaveamento de `g_tex_env_mode` em `egl_gl.c` (`GL_MODULATE=0x2100` padrão, `GL_REPLACE=0x1E01`, `GL_ADD=0x0104`, `GL_DECAL=0x2101`).
- Suporte a `GLFN_TexEnvx` e `GLFN_TexEnvxv` para `GL_TEXTURE_ENV` + `GL_TEXTURE_ENV_MODE`.
- Preservação de parâmetros inteiros/enums em `zgl_igles_dispatch_gl` para `GLFN_TexEnvx`.
- Detecção de canal alfa (`has_alpha`) por textura para cumprir a especificação OpenGL ES 1.1 em `GL_REPLACE` (textura com alfa substitui alfa; textura RGB pura preserva alfa primário).
- Microteste determinístico red/green em `tests/gles_composition.c`.

## 3. Comprovação Red / Green

### Microteste Unitário
- Comando: `build-progress\Release\gles_composition.exe`
  - **Red (antes):** `FP REPLACE ignores blue current color: got 001d14 expected 804020`
  - **Green (depois):** `GLES composition: OK` (valida tanto `GL_REPLACE` isolando tint azul quanto `GL_MODULATE` com blend alfa 50%).

### Portões de Compatibilidade e Não-Regressão
- Double Dragon (`capability_gate.py --filter "Double Dragon"`): `PASS (1)` (transparência preservada).
- Ovos (`capability_gate.py --filter "Ovos"`): `PASS (1)`.
- Áudio ADPCM (`audio_adpcm_rate.exe`): `PASS`.
- Transições de Input (`input_transitions.exe`): `PASS`.

## 4. Evidências Visuais Reais (Antes vs Depois)

- **Cenário:** Zeebo Family Pack aos 900 frames, roda inicial com 3 personagens no canto inferior esquerdo.
- **Antes (Blue/Cadavérico):** `artifacts/fp-blue/before900.png` (PPM SHA256: `DC43FEA183E9930E6C6C24F1012680CBCC9CBD754CEFDA39BE98A31F258947C0`).
  - Personagens afogados em tom ciano/azul escuro.
- **Depois (Correto):** `artifacts/fp-blue/after900.png` (PPM SHA256: `F7202F41BA860A0E8ABFE83977B0D85C91F72E23209B79CB0D584752ECF352D6`).
  - Tons de pele naturais, cabelos ruivos/castanhos, camisetas listradas laranja/amarela com cores originais.

*Nota de baseline:* O arquivo `tools/fp-gate/baseline.json` NÃO foi alterado unilateralmente (permaneceu com hash original `DC43...`), ficando pendente aceitação do novo hash `F720...` pelo master após revisão.

## 5. Arquivos Modificados / Adicionados

- `src/gpu/egl_gl.c`: implementação de `g_tex_env_mode`, dispatch de `TexEnv`, e composição no rasterizador.
- `CMakeLists.txt`: adição do target `gles_composition`.
- `tests/gles_composition.c`: microteste unitário de composição GLES 1.x.
