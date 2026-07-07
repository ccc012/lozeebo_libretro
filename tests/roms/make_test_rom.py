#!/usr/bin/env python3
"""Gera test_draw.mod - ROM de teste com codigo ARM montado a mao.

O programa:
  1. Preenche a tela de azul   (trap ZT_TEST_FILL,     0xF0000040)
  2. Seta a cor vermelha       (trap ZT_TEST_SETCOLOR, 0xF0000048)
  3. Desenha retangulo 480x360 (trap ZT_TEST_RECT,     0xF0000044)
  4. Loop infinito

Se a tela do RetroArch mostrar azul com retangulo vermelho central,
o pipeline CPU -> trap HLE -> framebuffer -> video esta funcionando.
"""
import struct
import os

CODE = [
    0xE3A000FF,  # 1000: MOV R0,#0xFF        ; azul (XRGB 0x0000FF)
    0xE59F4034,  # 1004: LDR R4,[PC,#0x34]   ; literal TEST_FILL em 0x1040
    0xE1A0E00F,  # 1008: MOV LR,PC           ; LR = 0x1010
    0xE12FFF14,  # 100C: BX  R4              ; chama TEST_FILL
    0xE3A008FF,  # 1010: MOV R0,#0xFF0000    ; vermelho
    0xE59F402C,  # 1014: LDR R4,[PC,#0x2C]   ; literal TEST_SETCOLOR em 0x1048
    0xE1A0E00F,  # 1018: MOV LR,PC
    0xE12FFF14,  # 101C: BX  R4
    0xE3A00050,  # 1020: MOV R0,#80          ; x
    0xE3A0103C,  # 1024: MOV R1,#60          ; y
    0xE3A02E1E,  # 1028: MOV R2,#480         ; w (0x1E ror 28)
    0xE3A03F5A,  # 102C: MOV R3,#360         ; h (0x5A ror 30)
    0xE59F400C,  # 1030: LDR R4,[PC,#0x0C]   ; literal TEST_RECT em 0x1044
    0xE1A0E00F,  # 1034: MOV LR,PC
    0xE12FFF14,  # 1038: BX  R4
    0xEAFFFFFE,  # 103C: B   .               ; loop infinito
    0xF0000040,  # 1040: .word TEST_FILL     (id 0x10 * 4)
    0xF0000044,  # 1044: .word TEST_RECT     (id 0x11 * 4)
    0xF0000048,  # 1048: .word TEST_SETCOLOR (id 0x12 * 4)
]

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_draw.mod")
with open(out, "wb") as f:
    for word in CODE:
        f.write(struct.pack("<I", word))

print(f"OK: {out} ({len(CODE)*4} bytes)")
