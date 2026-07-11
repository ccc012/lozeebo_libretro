# STATUS R5-D — GLES/Family Pack: pipeline de transformação

## Resumo (8 linhas)

- **Bloqueada pela Tarefa A** (signals): CPU do Family Pack ainda para logo após o boot
  (`instrucoes=729568`, `halted=1`, zero chamadas GL), então não há dados reais de
  `Orthox`/`VertexPointer` do jogo pra depurar nesta rodada.
- Sem ficar parado: escrevi um **harness isolado** (`tests/test_gles_transform.c`) que
  chama `zgl_handle()` direto, sem CPU/jogo, reproduzindo a sequência
  `LoadIdentity → Orthox → VertexPointer → TexCoordPointer → DrawArrays(TRIANGLE_FAN)`.
- **Resultado**: a matemática de transformação (`transform_vertex`, `mtx_mul`, viewport)
  está **correta** — com dados consistentes (vértices e `Orthox` no mesmo espaço, ex.
  pixels 0-640/0-480), o quad mapeia exatamente para a tela e o rasterizador pinta os
  pixels certos (confirmado por dump de pixels). Não encontrei bug de código nessa parte.
- Adicionei log permanente (`glOrthox(l=... r=... b=... t=... n=... f=...)` e
  `glViewport(...)`) em `egl_gl.c` — hoje esses valores decodificados não apareciam em
  lugar nenhum do log, só os 2 primeiros args brutos da primeira chamada.
- **Regressão**: nula. Rebuild + smoke test nos 4 Tier 1 batem exatamente com a
  baseline (mesmos `instrucoes=`/`fb[0]=` de antes da mudança — só adicionei logging).
- Compilei com `cl.exe` direto (MSVC via `vcvars64.bat`), não toquei no `.vcxproj`.

## O que fiz

### 1. Harness de teste isolado do rasterizador

`tests/test_gles_transform.c` (novo arquivo, dentro do meu escopo `src/gpu/egl_gl.c`
+ infraestrutura de teste). Compila linkando só os módulos necessários (CPU, memória,
framebuffer, `egl_gl.c`, log/trace) sem `brew.c`/`boot.c`/loader — três funções que
`egl_gl.c` importa de `brew.c` (`zbrew_stack_arg`, `zbrew_make_interface`,
`zbrew_mark_frame`) ganharam stubs mínimos no próprio arquivo de teste.

Como compilar (documentado no cabeçalho do arquivo):

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cl /nologo /Isrc /Isrc\core /DZEEBO_CORE_BUILD=1 ^
  src\cpu\cpu.c src\cpu\decode.c src\cpu\execute_arm.c src\cpu\execute_thumb.c src\cpu\flags.c ^
  src\memory\memory.c src\memory\heap.c src\gpu\framebuffer.c src\gpu\egl_gl.c ^
  src\debug\log.c src\debug\trace.c tests\test_gles_transform.c ^
  /Fe:tests\test_gles_transform.exe /Fo:tests\obj\"
tests\test_gles_transform.exe
```

### 2. Duas execuções, dois resultados diferentes (evidência do que É e do que NÃO é bug)

**Primeira tentativa** (dados inconsistentes de propósito — vértices em NDC `-1..1`
combinados com `Orthox(0,640,480,0,...)` em espaço de pixel):

```
v[0]: screen=(-1.0,-1.0) uv=(0.00,0.00)
v[1]: screen=(1.0,-1.0) uv=(0.00,0.00)
...
pixel nao-zero em (0,0) = 0xFFFFFFFF
pixel nao-zero em (1,0) = 0xFF000000   <- so 2 pixels tocados, quad "sumiu"
```

Isso reproduz exatamente o sintoma relatado na sessão 2026-07-10 ("vértices computam
mas coordenadas de tela ficam fora da área visível"). Rastreei a matemática à mão
(clip[0] = pj[0]*(-1) + pj[12] = -0.003125 + (-1) ≈ -1.003 → sx ≈ -1.0) e bate
exatamente com o log. **Mas isso é esperado**: eu misturei dois espaços de coordenada
incompatíveis de propósito para ver se o pipeline reproduzia o sintoma — não é uma
falha de `transform_vertex`, é a matemática correta de projeção ortográfica aplicada a
uma entrada que não faz sentido junto (vértice `-1` em unidade de pixel, quando a
Ortho espera unidade de pixel `0..640`).

**Segunda tentativa** (vértices em pixel `(100,100)-(540,380)`, mesmo espaço do
`Orthox(0,640,480,0,...)` — convenção normal de UI 2D):

```
v[0]: screen=(100.0,100.0) uv=(0.00,0.00)
v[1]: screen=(540.0,100.0) uv=(0.00,0.00)
v[2]: screen=(540.0,380.0) uv=(0.00,0.00)
v[3]: screen=(100.0,380.0) uv=(0.00,0.00)
fb[0]=0xFF000000 (canto superior esquerdo, fora do quad - clear color correto)
fb[centro]=0xFFFFFFFF (320,240, dentro do quad - branco correto)
```

Passagem perfeita: as coordenadas de tela batem exatamente com as coordenadas de
pixel de entrada, e o rasterizador pinta a área certa. **Conclusão: `mtx_mul`
(column-major), `transform_vertex` (multiplicação MV→clip→NDC→tela, sinal do Y) e o
viewport default (`0,0,640,480`) estão corretos.** Não há bug de transformação
matricial no código atual.

### 3. Por que não fui além: falta dado real do jogo

Sem o `Orthox`/`VertexPointer` REAL do Family Pack (CPU para antes de qualquer
chamada GL, ver regressão da Tarefa A), não dá pra saber se o bug de 2026-07-10 era
(a) um `Orthox`/vértices em espaços diferentes de verdade no código do jogo
(plausível — muitos jogos 2D BREW usam vértices em unidades "de tela" tipo `Q16` que
podem não bater com o que o `Orthox` do jogo declara), ou (b) já foi corrigido por
outra mudança nas sessões seguintes (a correção de `decode_vertex_ptr` do
`STATUS_B_RENDER_FAMILYPACK.md`, que resolveu o ponteiro `0x00000003` inválido, pode
ter sido a causa real do "fora de tela" — ponteiro errado lendo lixo de memória como
coordenada, não bug de matriz).

Por isso adicionei os logs de `glOrthox`/`glViewport` com valores decodificados
completos (antes só apareciam os 2 primeiros args brutos da primeira chamada de cada
função, via o log genérico em `zgl_dispatch`) — assim que a Tarefa A destravar a CPU,
o próximo passo é rodar o smoke test e olhar essas duas linhas de log direto.

### 4. Regressão (4 jogos Tier 1, antes/depois da minha mudança)

Idêntico em ambos os casos (só logging foi adicionado, nenhuma mudança de lógica):

| Jogo | instrucoes (frame 121) | fb[0] | halted |
|---|---|---|---|
| Double Dragon | 17671 | 0xFFFFFFFF | 1 |
| Pac-Mania | 145686 | 0xFF000000 | 1 |
| Zeeboids | 544678 | 0xFF000000 | 1 |
| Family Pack | 729568 | 0xFF000000 | 1 |

Batem com os números da baseline documentados no prompt da rodada (Family Pack:
729568 citado literalmente no `docs/PROGRESS.md`).

## Diff aplicado

- `src/gpu/egl_gl.c`: 2 linhas de `LOGI` novas (`glOrthox`/`glViewport` com valores
  decodificados), sem mudança de lógica/matemática.
- `tests/test_gles_transform.c`: novo arquivo, harness de teste standalone (não
  integrado ao `CMakeLists.txt`/`.sln` ainda — compilação manual documentada no
  cabeçalho; deixo essa integração para quem pegar isso depois, já que mexer no
  `.vcxproj`/`CMakeLists.txt` tem mais risco de conflito com outras tarefas rodando
  em paralelo no mesmo período).

## O que falta (para quem pegar depois de mim ou eu mesmo depois da Tarefa A)

1. Assim que `rodada5/signals` (Tarefa A) tiver um commit, `git cherry-pick`/merge pro
   meu worktree e rodar o smoke test do Family Pack de novo — as duas novas linhas de
   log (`glOrthox`/`glViewport`) vão mostrar os valores reais que o jogo usa.
2. Comparar esses valores reais contra os dados reais de `glVertexPointer` (já
   logados hoje, ver `STATUS_B_RENDER_FAMILYPACK.md`) — se estiverem em espaços
   incompatíveis (ex. `Orthox` em `-1..1` mas vértices em pixel, ou vice-versa), é
   bug real no jogo/nossa decodificação de fixed-point, e o harness que deixei serve
   pra reproduzir e testar a correção sem depender do jogo rodando.
3. Se os valores baterem (mesma convenção), o quad já deve renderizar corretamente
   com o código atual — nesse caso o próximo problema não é mais transformação, é
   textura/conteúdo (ver seção sobre `data.vfs` não encontrado no meu teste local,
   que é sobre caminho de arquivo, fora do meu escopo).
