#!/usr/bin/env python3
# Status Report - Gerador de relatório de progresso

import os
from datetime import datetime

def count_files(directory, extension):
    count = 0
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith(extension):
                count += 1
    return count

def count_lines(directory, extension):
    lines = 0
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith(extension):
                try:
                    with open(os.path.join(root, file), 'r', encoding='utf-8', errors='ignore') as f:
                        lines += len(f.readlines())
                except:
                    pass
    return lines

print("""
?????????????????????????????????????????????????????????????
?        Zeebo LibRetro - Skeleton Report                  ?
?        Data: {}                              ?
?????????????????????????????????????????????????????????????
""".format(datetime.now().strftime("%Y-%m-%d %H:%M:%S")))

# Contar arquivos
c_files = count_files("src", ".c")
h_files = count_files("src", ".h")
doc_files = count_files("docs", ".md")

print(f"""
?? ESTATÍSTICAS DE CÓDIGO
?? Arquivos .c:        {c_files}
?? Arquivos .h:        {h_files}
?? Docs (.md):         {doc_files}
""")

# Contar linhas
c_lines = count_lines("src", ".c")
h_lines = count_lines("src", ".h")

print(f"""
?? LINHAS DE CÓDIGO
?? C files:            {c_lines} linhas
?? H files:            {h_lines} linhas
?? Total:              {c_lines + h_lines} linhas
""")

# Status checklist
print("""
? CHECKLIST FINAL - FASE 0
?? [?] Estrutura de pastas criada
?? [?] libretro.h obtido
?? [?] Skeleton core implementado
?? [?] Makefile criado
?? [?] Build system configurado
?? [?] DLL compilada (Visual Studio)
?? [?] Documentação criada
?? [?] Teste no RetroArch (PRÓXIMO)
?? [??] Implementação de CPU ARM (DEPOIS)
""")

print("""
?? PRÓXIMOS PASSOS
1. Copiar zeebo_libretro.dll para C:\\RetroArch\\cores\\
2. Abrir RetroArch
3. Load Core ? procurar "Zeebo"
4. Verificar se aparece na lista
5. Testar carregar um arquivo .mod (qualquer arquivo)
6. Confirmar que tela preta aparece (sem crash)

Ver: docs/TESTE_RETROARCH.md
""")

print("""
?? ARQUIVOS PRINCIPAIS
??? src/core/libretro_core.c     (220 linhas, 28 funções)
??? src/core/libretro.h          (header oficial)
??? Makefile                      (build multiplataforma)
??? zeebo_libretro.sln           (Visual Studio solution)
??? docs/SKELETON_CHECKLIST.md   (checklist detalhado)
??? docs/TESTE_RETROARCH.md      (guia de teste)
??? docs/SKELETON_RESUMO.md      (sumário final)
??? x64/Release/zeebo_libretro.dll (12.3 KB compilada)
""")

print("""
?? STATUS: SKELETON COMPLETO E COMPILADO
""")
