"""Runnable real-helper probe: python tests/hle_probe_cost.py [build-dir]. No ROM required."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
build = root / (sys.argv[1] if len(sys.argv) > 1 else 'build-progress') / 'hle-probe'
build.mkdir(parents=True, exist_ok=True)
source = (root / 'src/cpu/cpu.c').read_text()
helpers = source[source.index('static int try_hle_empty_count(uint32_t instr) {'):source.index('#if defined(_MSC_VER)\n#pragma optimize("", on)')]
prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REG_PC 15
#define REG_LR 14
#define CPSR_Z (1u<<30)
#define CPSR_C (1u<<29)
#define CPSR_N (1u<<31)
#define CPSR_V (1u<<28)
#define LOGI(...) ((void)0)
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while(0)
static struct { uint32_t r[16], cpsr; uint64_t executed; } g_cpu;
static uint32_t mem[256], reads, writes;
static uint32_t zmem_read32(uint32_t a) { reads++; return mem[(a/4)&255]; }
static void zmem_write32(uint32_t a,uint32_t v) { writes++; mem[(a/4)&255]=v; }
static void zcpu_write_pc(uint32_t a) { g_cpu.r[REG_PC]=a; }
static int zbrew_ifile_block_write(uint32_t a,uint32_t n) { return 0; }
static void *zmem_host_ptr(uint32_t a,uint32_t n) { return 0; }
'''
suffix = r'''
int main(void) {
    unsigned i;
    const uint32_t unrelated[] = {0xE1A00000u,0xE2801001u,0xE5900000u,0xEAFFFFFEu,0xE12FFF1Eu};
    /* Unrelated real instruction mix: no speculative guest-memory reads. */
    for (i=0;i<sizeof(unrelated)/sizeof(unrelated[0]);i++) {
        reads=0; memset(&g_cpu,0,sizeof(g_cpu)); g_cpu.r[REG_PC]=64;
        CHECK(!try_hle_empty_count(unrelated[i]));
        CHECK(!try_hle_word_fill(unrelated[i]));
        CHECK(reads==0); CHECK(g_cpu.r[REG_PC]==64); CHECK(g_cpu.executed==0);
    }
    /* Both recognized count loops, every entry offset, exact output. */
    for (i=0;i<6;i++) {
        unsigned lt=i/3, offset=i%3;
        memset(&g_cpu,0,sizeof(g_cpu)); memset(mem,0,sizeof(mem));
        mem[16]=lt?0xE1500005u:0xE1530002u;
        mem[17]=lt?0xB2800001u:0x82822001u;
        mem[18]=lt?0xBAFFFFFCu:0x8AFFFFFCu;
        g_cpu.r[REG_PC]=64+4*offset; g_cpu.cpsr=CPSR_N|CPSR_V;
        g_cpu.r[lt?0:2]=4; g_cpu.r[lt?5:3]=12;
        CHECK(try_hle_empty_count(mem[16+offset]));
        CHECK(g_cpu.r[lt?0:2]==12); CHECK(g_cpu.r[REG_PC]==76);
        CHECK(g_cpu.cpsr==(CPSR_Z|CPSR_C)); CHECK(g_cpu.executed==27);
    }
    /* Word fill, entry LDR/STR, same memory and architectural state. */
    for (i=0;i<2;i++) {
        memset(&g_cpu,0,sizeof(g_cpu)); memset(mem,0,sizeof(mem)); writes=0;
        mem[16]=0xE5942004u; mem[17]=0xE7821100u; mem[18]=0xE2800001u;
        mem[19]=0xE3500004u; mem[20]=0xBAFFFFFAu; mem[33]=256;
        g_cpu.r[REG_PC]=64+4*i; g_cpu.r[4]=128; g_cpu.r[0]=1; g_cpu.r[1]=0x12345678;
        CHECK(try_hle_word_fill(mem[16+i])); CHECK(writes==3);
        CHECK(mem[65]==0x12345678 && mem[66]==mem[65] && mem[67]==mem[65]);
        CHECK(g_cpu.r[0]==4 && g_cpu.r[2]==256 && g_cpu.r[REG_PC]==84);
        CHECK(g_cpu.cpsr==(CPSR_Z|CPSR_C)); CHECK(g_cpu.executed==20);
    }
    puts("PASS: unmatched probes=0 guest reads; count/fill entry outputs preserved");
    return 0;
}
'''
(build / 'probe.c').write_text(prefix + helpers + suffix)
(build / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.10)\nproject(hle_probe C)\nadd_executable(hle_probe probe.c)\n')
subprocess.run(['cmake', '-S', str(build), '-B', str(build / 'out')], check=True, stdout=subprocess.DEVNULL)
subprocess.run(['cmake', '--build', str(build / 'out'), '--config', 'Release', '--parallel', '1'], check=True, stdout=subprocess.DEVNULL)
subprocess.run([str(build / 'out/Release/hle_probe.exe')], check=True)
