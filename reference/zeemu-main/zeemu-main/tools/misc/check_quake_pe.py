import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'roms/274802/mod/274802/quake.mod'

with open(path, 'rb') as f:
    data = f.read()

print(f'File size: {len(data)} (0x{len(data):X})')

# Check BREW header
if data[8:12] == b'BREW':
    print('BREW signature at 0x8')
    # Header at offset 0
    code_size = struct.unpack_from('<I', data, 0x1C)[0]
    data_size = struct.unpack_from('<I', data, 0x20)[0]
    bss_size = struct.unpack_from('<I', data, 0x24)[0]
    header_size = struct.unpack_from('<I', data, 0x10)[0]
    print(f'  code_size=0x{code_size:X} ({code_size})')
    print(f'  data_size=0x{data_size:X} ({data_size})')
    print(f'  bss_size=0x{bss_size:X} ({bss_size})')
    print(f'  header_size=0x{header_size:X}')
    data_va = 0x40 + code_size
    print(f'  data_va=0x{data_va:X}')
    bss_va = data_va + data_size
    print(f'  bss_va=0x{bss_va:X}')
    bss_end = bss_va + bss_size
    print(f'  bss_end=0x{bss_end:X}')
    total = bss_end
    print(f'  total virtual size=0x{total:X}')
elif data[0:4] == b'BREW':
    print('BREW signature at 0x0')
    code_size = struct.unpack_from('<I', data, 0x1C)[0]
    data_size = struct.unpack_from('<I', data, 0x20)[0]
    bss_size = struct.unpack_from('<I', data, 0x24)[0]
    header_size = struct.unpack_from('<I', data, 0x10)[0]
    print(f'  code_size=0x{code_size:X} ({code_size})')
    print(f'  data_size=0x{data_size:X} ({data_size})')
    print(f'  bss_size=0x{bss_size:X} ({bss_size})')
    print(f'  header_size=0x{header_size:X}')
    data_va = header_size + code_size
    print(f'  data_va=0x{data_va:X}')
    bss_va = data_va + data_size
    print(f'  bss_va=0x{bss_va:X}')
    bss_end = bss_va + bss_size
    print(f'  bss_end=0x{bss_end:X}')
    total = bss_end
    print(f'  total virtual size=0x{total:X}')

# Check PE signature
pe_off = struct.unpack_from('<I', data, 0x3C)[0]
print(f'\nPE offset from 0x3C: 0x{pe_off:X}')
if pe_off < len(data) and data[pe_off:pe_off+4] == b'PE\x00\x00':
    print('PE signature found!')
    machine = struct.unpack_from('<H', data, pe_off+4)[0]
    n_secs = struct.unpack_from('<H', data, pe_off+6)[0]
    opt_hdr_size = struct.unpack_from('<H', data, pe_off+20)[0]
    print(f'Machine: 0x{machine:X}, Sections: {n_secs}, OptHdrSize: {opt_hdr_size}')
    sec_table_off = pe_off + 24 + opt_hdr_size
    max_va = 0
    for i in range(min(n_secs, 20)):
        off = sec_table_off + i * 40
        name = data[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        virt_sz = struct.unpack_from('<I', data, off+8)[0]
        virt_addr = struct.unpack_from('<I', data, off+12)[0]
        file_sz = struct.unpack_from('<I', data, off+16)[0]
        file_pos = struct.unpack_from('<I', data, off+20)[0]
        end = virt_addr + virt_sz
        if end > max_va:
            max_va = end
        print(f'  Section {i}: "{name}" VA=0x{virt_addr:08X} VSZ=0x{virt_sz:08X} FSZ=0x{file_sz:08X} FPOS=0x{file_pos:08X} END=0x{end:08X}')
    print(f'\n  Max VA end: 0x{max_va:X}')
    if 0x64C800 < max_va:
        print(f'  0x64C800 IS within PE sections!')
    else:
        print(f'  0x64C800 is OUTSIDE PE sections')
else:
    print('No PE signature')

# Check if 0x64C800 is within the file
if 0x64C800 < len(data):
    print(f'\nAddress 0x64C800 IS within file!')
    print(f'  Bytes: {data[0x64C800:0x64C800+32].hex()}')
else:
    print(f'\nAddress 0x64C800 (0x{0x64C800:X}) is OUTSIDE file (size=0x{len(data):X})')
