> **Catalogo historico anterior a limpeza.** Pastas removidas nao devem ser restauradas automaticamente. Estrutura vigente: [LIMPEZA_FINAL.md](LIMPEZA_FINAL.md).

# Catálogo de encerramento — 2026-11-12

[Registro final](ENCERRAMENTO.md). Todos os documentos anteriores são históricos; datas e provas têm escopos diferentes.

## Estrutura preservada

| Pasta | Finalidade |
|---|---|
| `src/audio` | Decodificação PCM/ADPCM/MP3/MIDI, mixer e dependências com avisos próprios |
| `src/brew` | Boot, interfaces e serviços BREW/AEE HLE |
| `src/core` | Integração libretro |
| `src/cpu` | Interpretação ARM/Thumb e flags |
| `src/debug` | Logs, tracing, disassembly e desempenho |
| `src/gpu` | Framebuffer, desenho e EGL/GLES |
| `src/input` | Input e transições |
| `src/loader` | MOD/MIF/BAR, ZIP, pacotes completos, PAKZ local |
| `src/memory` | Mapa de memória e heap |
| `tests` | Smoke host e contratos; fixtures em `roms` têm restrições |
| `tools` | Runners, baselines e provas focais; alguns exigem ROMs externas |
| `docs` | Contratos, diagnósticos, progresso e pesquisa |
| `agent-coord` | Registro histórico das frentes/handoffs; não operação ativa |
| `reference` | Material externo heterogêneo; não presumir licença GPL do projeto |
| `artifacts` | Evidências mantidas, inclusive capturas DD versionadas |
| `dist/info` | Metadados libretro preservados |
| `zeebo_libretro` | Projeto Visual Studio legado, não equivalente ao CMake atual |
| `.github` | Templates GitHub existentes |

Builds antigos: quarentena externa. Build limpo de verificação: `build-archive-check`, ignorado. Repositórios aninhados/gitlinks mantidos intactos.

## Ordem de leitura

1. `ENCERRAMENTO.md`: decisão, estado, testes, recuperação e riscos.
2. `../tools/REGRESSION.md`: comandos e limites da suíte.
3. `CONTENT_PACKAGES.md`, `INPUT_TRANSITIONS.md`: contratos recentes.
4. `PROGRESS_dd_gameplay.md`, handoffs FP: provas específicas, não catálogo completo jogável.
5. `INDEX.md`: mapa temático histórico; `THIRD_PARTY.md`: notas de licença com ressalva atual.

## Arquivos por área

Listagem abaixo inclui arquivos próprios, documentação e ferramentas; evidências binárias, árvores de terceiros, bancos e builds constam do inventário JSON externo, sem reproduzir milhares de itens aqui.

### Raiz

- [`.gitattributes`](../.gitattributes)
- [`.gitignore`](../.gitignore)
- [`ANALYSIS_FINDINGS.md`](historico/raiz/ANALYSIS_FINDINGS.md)
- [`analyze_clsids.ps1`](../tools/legado/scripts/analyze_clsids.ps1)
- [`BLOCKERS_ANALYSIS.md`](historico/raiz/BLOCKERS_ANALYSIS.md)
- [`BRIEFING_3_PESSOAS.md`](historico/raiz/BRIEFING_3_PESSOAS.md)
- [`build_safe.ps1`](../tools/legado/scripts/build_safe.ps1)
- [`CMakeLists.txt`](../CMakeLists.txt)
- [`CMakePresets.json`](../CMakePresets.json)
- [`COLLABORATION.md`](historico/raiz/COLLABORATION.md)
- [`CppProperties.json`](../tools/legado/build/CppProperties.json)
- [`DISTRIBUICAO_3_IAS.md`](historico/raiz/DISTRIBUICAO_3_IAS.md)
- [`FIXES_TO_IMPLEMENT.md`](historico/raiz/FIXES_TO_IMPLEMENT.md)
- [`IMPLEMENTATION_LOG.md`](historico/raiz/IMPLEMENTATION_LOG.md)
- [`LICENSE`](../LICENSE)
- [`Makefile`](../tools/legado/build/Makefile)
- [`PROMPT_PARA_3_PESSOAS.txt`](historico/raiz/PROMPT_PARA_3_PESSOAS.txt)
- [`PROMPT_RODADA_2_3IAS.md`](historico/raiz/PROMPT_RODADA_2_3IAS.md)
- [`PROMPT_RODADA_3_ONBOARDING.md`](historico/raiz/PROMPT_RODADA_3_ONBOARDING.md)
- [`PROMPT_RODADA_5.md`](historico/raiz/PROMPT_RODADA_5.md)
- [`PROMPT_RODADA_6.md`](historico/raiz/PROMPT_RODADA_6.md)
- [`PROMPT_RODADA_7.md`](historico/raiz/PROMPT_RODADA_7.md)
- [`PROMPT_UNICO_3_PESSOAS.md`](historico/raiz/PROMPT_UNICO_3_PESSOAS.md)
- [`quick_build.ps1`](../tools/legado/scripts/quick_build.ps1)
- [`README.md`](../README.md)
- [`RENDERING_ANALYSIS.md`](historico/raiz/RENDERING_ANALYSIS.md)
- [`RODADA_5_CONSOLIDADO.md`](historico/raiz/RODADA_5_CONSOLIDADO.md)
- [`smoke_audio_summary.txt`](../smoke_audio_summary.txt)
- [`STATUS_A_EVENTO_DOUBLE_DRAGON.md`](historico/raiz/STATUS_A_EVENTO_DOUBLE_DRAGON.md)
- [`STATUS_B_RENDER_FAMILYPACK.md`](historico/raiz/STATUS_B_RENDER_FAMILYPACK.md)
- [`STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md`](historico/raiz/STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md)
- [`STATUS_DE_BAD_DUDES.md`](historico/raiz/STATUS_DE_BAD_DUDES.md)
- [`STATUS_R5_A_SIGNALS.md`](historico/raiz/STATUS_R5_A_SIGNALS.md)
- [`STATUS_R5_B_IDISPLAY.md`](historico/raiz/STATUS_R5_B_IDISPLAY.md)
- [`STATUS_R5_C_PACMANIA_DRAW.md`](historico/raiz/STATUS_R5_C_PACMANIA_DRAW.md)
- [`STATUS_R5_D_GLES_FP.md`](historico/raiz/STATUS_R5_D_GLES_FP.md)
- [`STATUS_R5_E_INPUT.md`](historico/raiz/STATUS_R5_E_INPUT.md)
- [`sweep_68_games.ps1`](../tools/legado/scripts/sweep_68_games.ps1)
- [`sweep_result.csv`](../preservacao/levantamento/sweep_result_raiz.csv)
- [`test_before_build.ps1`](../tools/legado/scripts/test_before_build.ps1)
- [`TESTS_MOD_ZIP.md`](historico/raiz/TESTS_MOD_ZIP.md)
- [`WORK_COMPLETE.md`](historico/raiz/WORK_COMPLETE.md)
- [`WORKFLOW_DESENVOLVIMENTO.md`](historico/raiz/WORKFLOW_DESENVOLVIMENTO.md)
- [`zeebo_libretro.sln`](../tools/legado/build/zeebo_libretro.sln)

### Código

- [`src/audio/adpcm.c`](../src/audio/adpcm.c)
- [`src/audio/adpcm.h`](../src/audio/adpcm.h)
- [`src/audio/audio.c`](../src/audio/audio.c)
- [`src/audio/audio.h`](../src/audio/audio.h)
- [`src/audio/audio_internal.h`](../src/audio/audio_internal.h)
- [`src/audio/extern/drmp3/dr_mp3.h`](../src/audio/extern/drmp3/dr_mp3.h)
- [`src/audio/extern/tinysoundfont/tml.h`](../src/audio/extern/tinysoundfont/tml.h)
- [`src/audio/extern/tinysoundfont/tsf.h`](../src/audio/extern/tinysoundfont/tsf.h)
- [`src/audio/midi.c`](../src/audio/midi.c)
- [`src/audio/midi.h`](../src/audio/midi.h)
- [`src/audio/miniz.c`](../src/audio/miniz.c)
- [`src/audio/miniz.h`](../src/audio/miniz.h)
- [`src/audio/miniz_common.h`](../src/audio/miniz_common.h)
- [`src/audio/miniz_export.h`](../src/audio/miniz_export.h)
- [`src/audio/miniz_tdef.c`](../src/audio/miniz_tdef.c)
- [`src/audio/miniz_tdef.h`](../src/audio/miniz_tdef.h)
- [`src/audio/miniz_tinfl.c`](../src/audio/miniz_tinfl.c)
- [`src/audio/miniz_tinfl.h`](../src/audio/miniz_tinfl.h)
- [`src/audio/miniz_zip.c`](../src/audio/miniz_zip.c)
- [`src/audio/miniz_zip.h`](../src/audio/miniz_zip.h)
- [`src/audio/mixer.c`](../src/audio/mixer.c)
- [`src/audio/mp3.c`](../src/audio/mp3.c)
- [`src/audio/mp3.h`](../src/audio/mp3.h)
- [`src/audio/pcm.c`](../src/audio/pcm.c)
- [`src/brew/aee_ids.h`](../src/brew/aee_ids.h)
- [`src/brew/boot.c`](../src/brew/boot.c)
- [`src/brew/brew.c`](../src/brew/brew.c)
- [`src/brew/brew.h`](../src/brew/brew.h)
- [`src/brew/helpers.c`](../src/brew/helpers.c)
- [`src/brew/ibitmap.c`](../src/brew/ibitmap.c)
- [`src/brew/idisplay.c`](../src/brew/idisplay.c)
- [`src/brew/idisplay_real.c`](../src/brew/idisplay_real.c)
- [`src/brew/ifile.c`](../src/brew/ifile.c)
- [`src/brew/ifont.c`](../src/brew/ifont.c)
- [`src/brew/iimage.c`](../src/brew/iimage.c)
- [`src/brew/iimage.h`](../src/brew/iimage.h)
- [`src/brew/imedia.c`](../src/brew/imedia.c)
- [`src/brew/imedia.h`](../src/brew/imedia.h)
- [`src/brew/imedia_util.c`](../src/brew/imedia_util.c)
- [`src/brew/imemory.c`](../src/brew/imemory.c)
- [`src/brew/ishell.c`](../src/brew/ishell.c)
- [`src/brew/isound.c`](../src/brew/isound.c)
- [`src/brew/isource.c`](../src/brew/isource.c)
- [`src/core/libretro.h`](../src/core/libretro.h)
- [`src/core/libretro_core.c`](../src/core/libretro_core.c)
- [`src/core/libretro_core.h`](../src/core/libretro_core.h)
- [`src/cpu/cpu.c`](../src/cpu/cpu.c)
- [`src/cpu/cpu.h`](../src/cpu/cpu.h)
- [`src/cpu/decode.c`](../src/cpu/decode.c)
- [`src/cpu/decode.h`](../src/cpu/decode.h)
- [`src/cpu/execute_arm.c`](../src/cpu/execute_arm.c)
- [`src/cpu/execute_thumb.c`](../src/cpu/execute_thumb.c)
- [`src/cpu/flags.c`](../src/cpu/flags.c)
- [`src/debug/disasm.c`](../src/debug/disasm.c)
- [`src/debug/log.c`](../src/debug/log.c)
- [`src/debug/log.h`](../src/debug/log.h)
- [`src/debug/perf.c`](../src/debug/perf.c)
- [`src/debug/perf.h`](../src/debug/perf.h)
- [`src/debug/trace.c`](../src/debug/trace.c)
- [`src/gpu/draw.c`](../src/gpu/draw.c)
- [`src/gpu/egl_gl.c`](../src/gpu/egl_gl.c)
- [`src/gpu/egl_gl.h`](../src/gpu/egl_gl.h)
- [`src/gpu/framebuffer.c`](../src/gpu/framebuffer.c)
- [`src/gpu/framebuffer.h`](../src/gpu/framebuffer.h)
- [`src/input/input.c`](../src/input/input.c)
- [`src/input/input.h`](../src/input/input.h)
- [`src/loader/bar_parser.c`](../src/loader/bar_parser.c)
- [`src/loader/bar_parser.h`](../src/loader/bar_parser.h)
- [`src/loader/content_package.c`](../src/loader/content_package.c)
- [`src/loader/content_package.h`](../src/loader/content_package.h)
- [`src/loader/mif_parser.c`](../src/loader/mif_parser.c)
- [`src/loader/mod_loader.c`](../src/loader/mod_loader.c)
- [`src/loader/mod_loader.h`](../src/loader/mod_loader.h)
- [`src/loader/pakz.c`](../src/loader/pakz.c)
- [`src/loader/pakz.h`](../src/loader/pakz.h)
- [`src/loader/zip_extract.c`](../src/loader/zip_extract.c)
- [`src/memory/heap.c`](../src/memory/heap.c)
- [`src/memory/memory.c`](../src/memory/memory.c)
- [`src/memory/memory.h`](../src/memory/memory.h)

### Testes

- [`tests/audio_adpcm_rate.c`](../tests/audio_adpcm_rate.c)
- [`tests/check-content-frontend.py`](../tests/check-content-frontend.py)
- [`tests/check-content-packages.py`](../tests/check-content-packages.py)
- [`tests/content_package_check.c`](../tests/content_package_check.c)
- [`tests/content_run7_check.c`](../tests/content_run7_check.c)
- [`tests/fp_position_transitions.py`](../tests/fp_position_transitions.py)
- [`tests/gles_composition.c`](../tests/gles_composition.c)
- [`tests/hle_probe_cost.py`](../tests/hle_probe_cost.py)
- [`tests/input_transitions.c`](../tests/input_transitions.c)
- [`tests/libretro_smoke.c`](../tests/libretro_smoke.c)
- [`tests/pakz_reader.c`](../tests/pakz_reader.c)
- [`tests/roms/.gitkeep`](../tests/roms/.gitkeep)
- [`tests/roms/README.md`](../tests/roms/README.md)
- [`tests/test_cpu.c`](../tests/test_cpu.c)
- [`tests/test_gles_transform.c`](../tests/test_gles_transform.c)
- [`tests/test_memory.c`](../tests/test_memory.c)

### Ferramentas

- [`tools/capability-gate/.gitignore`](../tools/capability-gate/.gitignore)
- [`tools/capability-gate/capability_gate.py`](../tools/capability-gate/capability_gate.py)
- [`tools/capability-gate/evidence180.json`](../tools/capability-gate/evidence180.json)
- [`tools/capability-gate/README.md`](../tools/capability-gate/README.md)
- [`tools/capability-gate/test_comparator.py`](../tools/capability-gate/test_comparator.py)
- [`tools/dd-gate/check-directions.ps1`](../tools/dd-gate/check-directions.ps1)
- [`tools/dd-gate/gameplay.py`](../tools/dd-gate/gameplay.py)
- [`tools/dd-gate/run.ps1`](../tools/dd-gate/run.ps1)
- [`tools/fp-final-playable-proof/run.ps1`](../tools/fp-final-playable-proof/run.ps1)
- [`tools/fp-gate/baseline.json`](../tools/fp-gate/baseline.json)
- [`tools/fp-gate/check-content-path.ps1`](../tools/fp-gate/check-content-path.ps1)
- [`tools/fp-gate/run.ps1`](../tools/fp-gate/run.ps1)
- [`tools/fp-minigame-proof-v2/probe.ps1`](../tools/fp-minigame-proof-v2/probe.ps1)
- [`tools/fp-visual-nav/.gitattributes`](../tools/fp-visual-nav/.gitattributes)
- [`tools/fp-visual-nav/.gitignore`](../tools/fp-visual-nav/.gitignore)
- [`tools/fp-visual-nav/classify_ppm.py`](../tools/fp-visual-nav/classify_ppm.py)
- [`tools/fp-visual-nav/evidence/classify_dense300.json`](../tools/fp-visual-nav/evidence/classify_dense300.json)
- [`tools/fp-visual-nav/evidence/classify_nav1800.json`](../tools/fp-visual-nav/evidence/classify_nav1800.json)
- [`tools/fp-visual-nav/evidence/summaries.txt`](../tools/fp-visual-nav/evidence/summaries.txt)
- [`tools/fp-visual-nav/export_keys.py`](../tools/fp-visual-nav/export_keys.py)
- [`tools/fp-visual-nav/ppm_info.py`](../tools/fp-visual-nav/ppm_info.py)
- [`tools/fp-visual-nav/ppm_to_png.py`](../tools/fp-visual-nav/ppm_to_png.py)
- [`tools/fp-visual-nav/run_nav.ps1`](../tools/fp-visual-nav/run_nav.ps1)
- [`tools/ovos-gate/run.ps1`](../tools/ovos-gate/run.ps1)
- [`tools/REGRESSION.md`](../tools/REGRESSION.md)
- [`tools/regression.py`](../tools/regression.py)
- [`tools/smoke-all.ps1`](../tools/smoke-all.ps1)
- [`tools/summarize-survey.py`](../tools/summarize-survey.py)

### Documentação

- [`docs/AUDIO_REGRESSION_69_20261106.md`](AUDIO_REGRESSION_69_20261106.md)
- [`docs/AUDIO_REGRESSION_CANDIDATES_20261106.md`](AUDIO_REGRESSION_CANDIDATES_20261106.md)
- [`docs/CONTENT_PACKAGES.md`](CONTENT_PACKAGES.md)
- [`docs/DDRAGON_VIDEO_FIX.md`](DDRAGON_VIDEO_FIX.md)
- [`docs/DDRENDER_2D.md`](DDRENDER_2D.md)
- [`docs/EGL_AUTO_SWAP.md`](EGL_AUTO_SWAP.md)
- [`docs/ENCERRAMENTO.md`](ENCERRAMENTO.md)
- [`docs/FAMILYPACK_BLACKSCREEN_20261106.md`](FAMILYPACK_BLACKSCREEN_20261106.md)
- [`docs/FAMILYPACK_REGRESSION.md`](FAMILYPACK_REGRESSION.md)
- [`docs/INDEX.md`](INDEX.md)
- [`docs/INFUSE_REVERSE_ENGINEERING.md`](INFUSE_REVERSE_ENGINEERING.md)
- [`docs/INFUSE_RTTI.md`](INFUSE_RTTI.md)
- [`docs/INPUT_TRANSITIONS.md`](INPUT_TRANSITIONS.md)
- [`docs/NEXT_COMPAT_CLUSTERS.md`](NEXT_COMPAT_CLUSTERS.md)
- [`docs/PACMANIA_AUDIO.md`](PACMANIA_AUDIO.md)
- [`docs/PACMANIA_AUDIO2.md`](PACMANIA_AUDIO2.md)
- [`docs/PLANNING_ARCHIVE.md`](PLANNING_ARCHIVE.md)
- [`docs/PLZ_RESEARCH.md`](PLZ_RESEARCH.md)
- [`docs/PORTABILITY.md`](PORTABILITY.md)
- [`docs/PROGRESS.md`](PROGRESS.md)
- [`docs/PROGRESS_dd_gameplay.md`](PROGRESS_dd_gameplay.md)
- [`docs/PROGRESS_fp_blue.md`](PROGRESS_fp_blue.md)
- [`docs/RE4_RENDER_FN.md`](RE4_RENDER_FN.md)
- [`docs/RE_INFUSE_NOTES.md`](RE_INFUSE_NOTES.md)
- [`docs/REA_INFUSE_DISPLAY_GL.md`](REA_INFUSE_DISPLAY_GL.md)
- [`docs/REA_INFUSE_DISPLAY_RUNTIME_CONTRACT.md`](REA_INFUSE_DISPLAY_RUNTIME_CONTRACT.md)
- [`docs/REGRESSION_060942_FIX.md`](REGRESSION_060942_FIX.md)
- [`docs/REGRESSION_172840_TRIAGE.md`](REGRESSION_172840_TRIAGE.md)
- [`docs/REPO_AUDIT_20261106.md`](REPO_AUDIT_20261106.md)
- [`docs/RETOMADA_REA_INFUSE.md`](RETOMADA_REA_INFUSE.md)
- [`docs/ROADMAP_50_IDEIAS.md`](ROADMAP_50_IDEIAS.md)
- [`docs/SUPBTIME_RENDER.md`](SUPBTIME_RENDER.md)
- [`docs/SUPBTIME_VIDEO.md`](SUPBTIME_VIDEO.md)
- [`docs/TESTING.md`](TESTING.md)
- [`docs/THIRD_PARTY.md`](THIRD_PARTY.md)
- [`docs/TRIAGE_ALL_GAMES.md`](TRIAGE_ALL_GAMES.md)
- [`docs/ZEEBOIDS_DIAGNOSIS.md`](ZEEBOIDS_DIAGNOSIS.md)

### Coordenação

- [`agent-coord/AGENT_PROMPT.md`](historico/agent-coord/AGENT_PROMPT.md)
- [`agent-coord/DIAGNOSTICO_20261106.md`](historico/agent-coord/DIAGNOSTICO_20261106.md)
- [`agent-coord/fronts/alice-play.md`](historico/agent-coord/fronts/alice-play.md)
- [`agent-coord/fronts/dd-playable.md`](historico/agent-coord/fronts/dd-playable.md)
- [`agent-coord/fronts/de-load.md`](historico/agent-coord/fronts/de-load.md)
- [`agent-coord/fronts/fp-input.md`](historico/agent-coord/fronts/fp-input.md)
- [`agent-coord/fronts/fp-playable.md`](historico/agent-coord/fronts/fp-playable.md)
- [`agent-coord/fronts/ovos-playable.md`](historico/agent-coord/fronts/ovos-playable.md)
- [`agent-coord/fronts/ovos-scene.md`](historico/agent-coord/fronts/ovos-scene.md)
- [`agent-coord/fronts/re4-scene.md`](historico/agent-coord/fronts/re4-scene.md)
- [`agent-coord/fronts/rea-runtime.md`](historico/agent-coord/fronts/rea-runtime.md)
- [`agent-coord/fronts/repo-hygiene.md`](historico/agent-coord/fronts/repo-hygiene.md)
- [`agent-coord/fronts/zuma-stable.md`](historico/agent-coord/fronts/zuma-stable.md)
- [`agent-coord/HANDOFF_TEMPLATE.md`](historico/agent-coord/HANDOFF_TEMPLATE.md)
- [`agent-coord/handoffs/alice-play-20261106.md`](historico/agent-coord/handoffs/alice-play-20261106.md)
- [`agent-coord/handoffs/core-options-20261107.md`](historico/agent-coord/handoffs/core-options-20261107.md)
- [`agent-coord/handoffs/dd-playable-20261107.md`](historico/agent-coord/handoffs/dd-playable-20261107.md)
- [`agent-coord/handoffs/dd-playable-final-20261107.md`](historico/agent-coord/handoffs/dd-playable-final-20261107.md)
- [`agent-coord/handoffs/de-load-20261106.md`](historico/agent-coord/handoffs/de-load-20261106.md)
- [`agent-coord/handoffs/fp-audio-local-20261106.md`](historico/agent-coord/handoffs/fp-audio-local-20261106.md)
- [`agent-coord/handoffs/fp-final-playable-proof-20261107.md`](historico/agent-coord/handoffs/fp-final-playable-proof-20261107.md)
- [`agent-coord/handoffs/fp-flow-local-20261106.md`](historico/agent-coord/handoffs/fp-flow-local-20261106.md)
- [`agent-coord/handoffs/fp-gameplay-local-20261106.md`](historico/agent-coord/handoffs/fp-gameplay-local-20261106.md)
- [`agent-coord/handoffs/fp-gate-20261107.md`](historico/agent-coord/handoffs/fp-gate-20261107.md)
- [`agent-coord/handoffs/fp-input-local-20261106.md`](historico/agent-coord/handoffs/fp-input-local-20261106.md)
- [`agent-coord/handoffs/fp-minigame-proof-20261107.md`](historico/agent-coord/handoffs/fp-minigame-proof-20261107.md)
- [`agent-coord/handoffs/fp-minigame-proof-v2-20261107.md`](historico/agent-coord/handoffs/fp-minigame-proof-v2-20261107.md)
- [`agent-coord/handoffs/fp-performance-cpu-v2-20261107.md`](historico/agent-coord/handoffs/fp-performance-cpu-v2-20261107.md)
- [`agent-coord/handoffs/fp-performance-cpu-v3-20261107.md`](historico/agent-coord/handoffs/fp-performance-cpu-v3-20261107.md)
- [`agent-coord/handoffs/fp-signal-local-20261106.md`](historico/agent-coord/handoffs/fp-signal-local-20261106.md)
- [`agent-coord/handoffs/fp-video-local-20261106.md`](historico/agent-coord/handoffs/fp-video-local-20261106.md)
- [`agent-coord/handoffs/fp-visual-nav-local-20261106.md`](historico/agent-coord/handoffs/fp-visual-nav-local-20261106.md)
- [`agent-coord/handoffs/ovos-playable-20261107.md`](historico/agent-coord/handoffs/ovos-playable-20261107.md)
- [`agent-coord/handoffs/ovos-playable-20261107b.md`](historico/agent-coord/handoffs/ovos-playable-20261107b.md)
- [`agent-coord/handoffs/ovos-playable-20261107c.md`](historico/agent-coord/handoffs/ovos-playable-20261107c.md)
- [`agent-coord/handoffs/ovos-playable-gate-20261107.md`](historico/agent-coord/handoffs/ovos-playable-gate-20261107.md)
- [`agent-coord/handoffs/re4-scene-20261106.md`](historico/agent-coord/handoffs/re4-scene-20261106.md)
- [`agent-coord/handoffs/rea-runtime-20261106.md`](historico/agent-coord/handoffs/rea-runtime-20261106.md)
- [`agent-coord/handoffs/zuma-stable-20261106.md`](historico/agent-coord/handoffs/zuma-stable-20261106.md)
- [`agent-coord/INTEGRATION.md`](historico/agent-coord/INTEGRATION.md)
- [`agent-coord/README.md`](historico/agent-coord/README.md)
- [`agent-coord/REGISTRY.md`](historico/agent-coord/REGISTRY.md)
- [`agent-coord/RULES.md`](historico/agent-coord/RULES.md)
- [`agent-coord/scripts/spawn-front.ps1`](historico/agent-coord/scripts/spawn-front.ps1)

### Metadados e projetos

- [`.github/ISSUE_TEMPLATE/bug_report.yml`](../.github/ISSUE_TEMPLATE/bug_report.yml)
- [`.github/ISSUE_TEMPLATE/config.yml`](../.github/ISSUE_TEMPLATE/config.yml)
- [`.github/PULL_REQUEST_TEMPLATE.md`](../.github/PULL_REQUEST_TEMPLATE.md)
- [`dist/info/zeebo_libretro.info`](../dist/info/zeebo_libretro.info)
- [`zeebo_libretro/zeebo_libretro.vcxproj`](../tools/legado/build/zeebo_libretro/zeebo_libretro.vcxproj)
- [`zeebo_libretro/zeebo_libretro.vcxproj.filters`](../tools/legado/build/zeebo_libretro/zeebo_libretro.vcxproj.filters)
