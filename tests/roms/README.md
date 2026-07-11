# ZEEBO ROMS - Referência Externa

O projeto zeebo_libretro agora usa ROMs da pasta externa:
```
C:\Users\Lucas\Downloads\zeebo-romset-and-devtools
```

## Como testar com RetroArch:

1. **Copiar DLL:**
```powershell
Copy-Item x64/Release/zeebo_libretro.dll `
  -Destination "C:\Program Files (x86)\Steam\steamapps\common\RetroArch\cores\"
```

2. **Carregar jogo:**
   - Load Core → Zeebo
   - Load Content → Selecione em Downloads/zeebo-romset-and-devtools

## Jogos Prioridade 🎯

- **276212** - Pac-Mania (CreateInstance crash)
- **277229** - Zeebo Family Pack (rasterizador)
- **274754** - Double Dragon (raw MOD)
- **279382** - Zeeboids (CLSID?)

## 68 Jogos Disponíveis

Ver lista completa em Downloads/zeebo-romset-and-devtools/readme.nfo
