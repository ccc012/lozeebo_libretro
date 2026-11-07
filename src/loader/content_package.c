/* Whole-package preparation. 7-Zip only writes stdout: guest paths never reach
 * its filesystem extractor. Cache publication is one directory rename. */
#include "content_package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

static int extension(const char *p, const char *ext) {
    const char *s = p ? strrchr(p, '.') : NULL;
    if (!s) return 0;
    while (*s && *ext && tolower((unsigned char)*s) == *ext) { ++s; ++ext; }
    return !*s && !*ext;
}
bool zcontent_is_package(const char *p) { return extension(p, ".7z") || extension(p, ".zip"); }

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <wchar.h>

#define MAX_ENTRIES 8192
#define MAX_FILE (512ULL * 1024 * 1024)
#define MAX_TOTAL (2ULL * 1024 * 1024 * 1024)
#define MAX_LIST (8 * 1024 * 1024)
#define PATH_CAP 1024
/* ponytail: Windows desktop + installed 7-Zip; use an in-process decoder when
 * supporting other platforms. No PATH lookup or shell execution. */
static const wchar_t seven[] = L"C:\\ProgramData\\chocolatey\\bin\\7z.exe";
typedef struct { char name[512]; uint64_t size; int dir; } Entry;

static int wide(const char *s, wchar_t *w, int n) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) > 0;
}
static int utf8(const wchar_t *s, char *u, int n) {
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, u, n, NULL, NULL) > 0;
}
static int join(wchar_t *out, const wchar_t *root, const char *name) {
    wchar_t w[512];
    if (!wide(name, w, 512) || wcslen(root) + wcslen(w) + 2 >= PATH_CAP) return 0;
    swprintf(out, PATH_CAP, L"%ls\\%ls", root, w);
    return 1;
}
/* Reject Win32 aliases, ADS, device names and all path ambiguity. */
static int safe_name(char *s) {
    char *p = s, *start = s;
    if (!*s || strlen(s) >= 500 || *s == '/' || *s == '\\') return 0;
    for (;;) {
        unsigned char c = (unsigned char)*p;
        if (c && (c < 32 || strchr(":\"<>|?*", c))) return 0;
        if (c == '/' || c == '\\' || !c) {
            size_t n = (size_t)(p - start), k;
            char base[16];
            if (!n || start[n-1] == '.' || start[n-1] == ' ') return 0;
            for (k = 0; k < n && k < sizeof(base)-1 && start[k] != '.'; ++k)
                base[k] = (char)toupper((unsigned char)start[k]);
            base[k] = 0;
            if (!strcmp(base,"CON") || !strcmp(base,"PRN") || !strcmp(base,"AUX") ||
                !strcmp(base,"NUL") || !strcmp(base,"CONIN$") || !strcmp(base,"CONOUT$") ||
                (strlen(base)==4 && (!memcmp(base,"COM",3) || !memcmp(base,"LPT",3)) &&
                 base[3]>='0' && base[3]<='9')) return 0;
            if (!c) break;
            *p = '\\'; start = p+1;
        }
        ++p;
    }
    return 1;
}
static int plain_dir(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}
/* Check every ancestor; never traverse cache reparse points. */
static int parents(wchar_t *path, int create) {
    wchar_t *p;
    for (p=path+3; *p; ++p) if (*p == '\\' || *p == '/') {
        wchar_t c=*p; *p=0;
        if (create) CreateDirectoryW(path,NULL);
        if (!plain_dir(path)) { *p=c; return 0; }
        *p=c;
    }
    return 1;
}
static HANDLE open_read(const wchar_t *p) {
    DWORD a;
    wchar_t copy[PATH_CAP];
    if (wcslen(p)>=PATH_CAP) return INVALID_HANDLE_VALUE;
    wcscpy(copy,p);
    if (!parents(copy,0)) return INVALID_HANDLE_VALUE;
    a=GetFileAttributesW(p);
    if (a==INVALID_FILE_ATTRIBUTES || (a & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) return INVALID_HANDLE_VALUE;
    return CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
}
static int hash_handle(HANDLE f, const void *prefix, ULONG prefix_size, unsigned char out[32]) {
    BCRYPT_ALG_HANDLE alg=NULL;
    BCRYPT_HASH_HANDLE h=NULL;
    unsigned char buf[65536]; DWORD n; int ok=0;
    if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0) goto end;
    if (BCryptCreateHash(alg,&h,NULL,0,NULL,0,0)<0) goto end;
    if (prefix_size && BCryptHashData(h,(PUCHAR)prefix,prefix_size,0)<0) goto end;
    for (;;) {
        if (!ReadFile(f,buf,sizeof(buf),&n,NULL)) goto end;
        if (!n) break;
        if (BCryptHashData(h,buf,n,0)<0) goto end;
    }
    ok=BCryptFinishHash(h,out,32,0)>=0;
end:
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg,0);
    return ok;
}
static int hash_file(const wchar_t *p, unsigned char out[32], uint64_t expected) {
    HANDLE f=open_read(p); LARGE_INTEGER sz; int ok;
    if (f==INVALID_HANDLE_VALUE) return 0;
    ok=GetFileSizeEx(f,&sz) && (uint64_t)sz.QuadPart==expected && hash_handle(f,NULL,0,out);
    CloseHandle(f); return ok;
}
/* Win32 argv quoting, including backslashes before closing quotes. */
static int quote(wchar_t *cmd, size_t cap, const wchar_t *arg) {
    size_t n=wcslen(cmd), slash=0;
    if (n+3>=cap) return 0;
    cmd[n++]=L' '; cmd[n++]=L'"';
    for (;;) {
        if (*arg==L'\\') { ++slash; ++arg; continue; }
        if (*arg==L'"' || !*arg) slash*=2;
        while (slash) { if(n+2>=cap)return 0; cmd[n++]=L'\\'; --slash; }
        if (!*arg) break;
        if (*arg==L'"') { if(n+2>=cap)return 0; cmd[n++]=L'\\'; }
        if(n+2>=cap)return 0;
        cmd[n++]=*arg++;
    }
    cmd[n++]=L'"'; cmd[n]=0; return 1;
}
/* Bounded pipe, no guest executable, no shell, no filesystem output from 7z.
 * A watchdog terminates hung/encrypted archives after 120 seconds. */
static int run7(const wchar_t *source, const char *member, HANDLE dest,
                char **listing, uint64_t limit) {
    wchar_t cmd[8192]=L"", wm[512];
    HANDLE rd=NULL,wr=NULL,nul=INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    STARTUPINFOW si; PROCESS_INFORMATION pi; DWORD code=1,n,wrote;
    unsigned char buf[65536]; uint64_t total=0; ULONGLONG start=GetTickCount64(); int ok=0;
    memset(&si,0,sizeof(si)); memset(&pi,0,sizeof(pi)); si.cb=sizeof(si);
    if (!quote(cmd,8192,seven) || !quote(cmd,8192,member ? L"x" : L"l") ||
        !quote(cmd,8192,L"-sccUTF-8") || !quote(cmd,8192,L"-bd") ||
        !quote(cmd,8192,L"-y") || !quote(cmd,8192,L"-pLOZeebo-no-password")) goto end;
    if (member) {
        if (!quote(cmd,8192,L"-so") || !quote(cmd,8192,L"-spd")) goto end;
    } else if (!quote(cmd,8192,L"-slt") || !quote(cmd,8192,L"-ba")) goto end;
    if (!quote(cmd,8192,L"--") || !quote(cmd,8192,source)) goto end;
    if (member && (!wide(member,wm,512) || !quote(cmd,8192,wm))) goto end;
    if (!CreatePipe(&rd,&wr,&sa,0) || !SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0)) goto end;
    nul=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);
    if (nul==INVALID_HANDLE_VALUE) goto end;
    si.dwFlags=STARTF_USESTDHANDLES; si.hStdOutput=wr; si.hStdError=nul; si.hStdInput=nul;
    if (!CreateProcessW(seven,cmd,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) goto end;
    CloseHandle(wr); wr=NULL;
    if (listing) { *listing=(char *)malloc((size_t)limit+1); if (!*listing) goto end; }
    for (;;) {
        DWORD avail=0;
        if (!PeekNamedPipe(rd,NULL,0,NULL,&avail,NULL)) break;
        if (!avail) {
            if (WaitForSingleObject(pi.hProcess,10)==WAIT_OBJECT_0) {
                if (!PeekNamedPipe(rd,NULL,0,NULL,&avail,NULL) || !avail) break;
            }
            if (GetTickCount64()-start>120000) goto end;
            continue;
        }
        if (!ReadFile(rd,buf,avail<sizeof(buf)?avail:sizeof(buf),&n,NULL)) goto end;
        if (total+n>limit) goto end;
        if (listing) memcpy(*listing+(size_t)total,buf,n);
        else if (!WriteFile(dest,buf,n,&wrote,NULL) || wrote!=n) goto end;
        total+=n;
    }
    if (WaitForSingleObject(pi.hProcess,1000)!=WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess,&code) || code) goto end;
    if (listing) (*listing)[total]=0;
    ok=listing || total==limit;
end:
    if (pi.hProcess) {
        if (WaitForSingleObject(pi.hProcess,0)!=WAIT_OBJECT_0) { TerminateProcess(pi.hProcess,1); WaitForSingleObject(pi.hProcess,5000); }
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    if(rd)CloseHandle(rd); if(wr)CloseHandle(wr); if(nul!=INVALID_HANDLE_VALUE)CloseHandle(nul);
    if (!ok && listing) { free(*listing); *listing=NULL; }
    return ok;
}
static int compare_entry(const void *a,const void *b) { return _stricmp(((const Entry *)a)->name,((const Entry *)b)->name); }
/* Parse -slt records strictly enough to reject links/aliases before extraction. */
static int parse(char *text, Entry *entries, unsigned *count) {
    char *line=text; Entry *e=NULL; uint64_t total=0; unsigned i;
    *count=0;
    while (*line) {
        char *next=strchr(line,'\n'); char *end;
        if (next) *next++=0;
        end=line+strlen(line); if(end>line && end[-1]=='\r')end[-1]=0;
        if (!strncmp(line,"Path = ",7)) {
            if (*count==MAX_ENTRIES || strlen(line+7)>=sizeof(entries[0].name)) return 0;
            e=&entries[(*count)++]; memset(e,0,sizeof(*e));
            strcpy(e->name,line+7); if(!safe_name(e->name))return 0;
            e->size=UINT64_MAX;
        } else if (e && !strncmp(line,"Size = ",7)) {
            char *tail; e->size=strtoull(line+7,&tail,10);
            if(tail==line+7 || *tail || e->size>MAX_FILE)return 0;
        } else if (e && !strncmp(line,"Attributes = ",13)) {
            const char *a=line+13;
            e->dir=*a=='D';
            /* Unix symbolic links and unusual Windows attributes are rejected. */
            if (strchr(a,'l') || strchr(a,'L') || strchr(a,'R') || strstr(a,"0x")) return 0;
        } else if (strstr(line,"Symbolic Link =") || strstr(line,"Hard Link =") ||
                   !strcmp(line,"Encrypted = +") || !strcmp(line,"Anti = +") ||
                   !strcmp(line,"Alternate Stream = +")) return 0;
        if (!next) break; line=next;
    }
    if (!*count)return 0;
    qsort(entries,*count,sizeof(*entries),compare_entry);
    for(i=0;i<*count;++i) {
        if(entries[i].size==UINT64_MAX)return 0;
        total+=entries[i].size; if(total>MAX_TOTAL)return 0;
        if(i && !_stricmp(entries[i-1].name,entries[i].name))return 0;
        if(i && !entries[i-1].dir) {
            size_t n=strlen(entries[i-1].name);
            if(!_strnicmp(entries[i-1].name,entries[i].name,n) && entries[i].name[n]=='\\')return 0;
        }
    }
    return 1;
}
/* Remove only our private staging tree; never follow directory junctions. */
static void discard(const wchar_t *root) {
    wchar_t glob[PATH_CAP],p[PATH_CAP]; WIN32_FIND_DATAW d; HANDLE f;
    if(!plain_dir(root))return;
    swprintf(glob,PATH_CAP,L"%ls\\*",root); f=FindFirstFileW(glob,&d);
    if(f!=INVALID_HANDLE_VALUE) {
        do {
            if(!wcscmp(d.cFileName,L".") || !wcscmp(d.cFileName,L".."))continue;
            if(wcslen(root)+wcslen(d.cFileName)+2>=PATH_CAP)continue;
            swprintf(p,PATH_CAP,L"%ls\\%ls",root,d.cFileName);
            if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
                if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))discard(p);
            } else DeleteFileW(p);
        }while(FindNextFileW(f,&d)); FindClose(f);
    }
    RemoveDirectoryW(root);
}
static int manifest(const wchar_t *root, Entry *e,unsigned count,int write) {
    wchar_t path[PATH_CAP]; HANDLE f; DWORD n; unsigned i; unsigned char digest[32],stored[32]; int ok=0;
    if(!join(path,root,".lozeebo-sha256"))return 0;
    f=write?CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL):open_read(path);
    if(f==INVALID_HANDLE_VALUE)return 0;
    for(i=0;i<count;++i) if(!e[i].dir) {
        if(!join(path,root,e[i].name) || !hash_file(path,digest,e[i].size))goto end;
        if(write) { if(!WriteFile(f,digest,32,&n,NULL) || n!=32)goto end; }
        else if(!ReadFile(f,stored,32,&n,NULL) || n!=32 || memcmp(stored,digest,32))goto end;
    }
    if(write) { if(!FlushFileBuffers(f))goto end; }
    else if(!ReadFile(f,stored,1,&n,NULL) || n)goto end;
    ok=1;
end: CloseHandle(f); return ok;
}

bool zcontent_prepare(const char *source,const char *member,char *out,size_t out_size,char *error,size_t error_size) {
    wchar_t input[PATH_CAP],absolute[PATH_CAP],temp[PATH_CAP],root[PATH_CAP],cache[PATH_CAP],stage[PATH_CAP],path[PATH_CAP];
    unsigned char digest[32]; char hex[65],chosen[512]="",requested[512]="",*listing=NULL;
    Entry *entries=NULL; HANDLE source_file=INVALID_HANDLE_VALUE,f=INVALID_HANDLE_VALUE; LARGE_INTEGER size;
    unsigned count=0,i,mods=0; int ok=0,staged=0; const char *why="Package preparation failed";
#define FAIL(msg) do { why=(msg); goto end; } while(0)
    if(out_size)out[0]=0;
    if(!source || !out_size || !wide(source,input,PATH_CAP))FAIL("Invalid package path");
    { DWORD n=GetFullPathNameW(input,PATH_CAP,absolute,NULL);
      if(!n || n>=PATH_CAP)FAIL("Package path too long"); }
    if(wcslen(absolute)>700)FAIL("Package path too long");
    source_file=open_read(absolute);
    if(source_file==INVALID_HANDLE_VALUE || !GetFileSizeEx(source_file,&size) || size.QuadPart<=0 || (uint64_t)size.QuadPart>MAX_TOTAL)FAIL("Cannot read package or package exceeds 2 GiB");
    /* Keep source locked against writes/deletion until publication. */
    if(!hash_handle(source_file,absolute,(ULONG)((wcslen(absolute)+1)*sizeof(wchar_t)),digest))FAIL("Package SHA256 failed");
    for(i=0;i<32;++i)sprintf(hex+i*2,"%02x",digest[i]);
    if(!GetTempPathW(PATH_CAP,temp) || wcslen(temp)>700)FAIL("Temporary directory unavailable");
    swprintf(root,PATH_CAP,L"%lsLOZeebo-content-v1",temp);
    if(!parents(root,0))FAIL("Unsafe cache parent directory");
    CreateDirectoryW(root,NULL); if(!plain_dir(root))FAIL("Unsafe cache directory");
    if(!join(cache,root,hex))FAIL("Cache path too long");
    entries=(Entry *)calloc(MAX_ENTRIES,sizeof(*entries)); if(!entries)FAIL("Out of memory");
    if(!run7(absolute,NULL,INVALID_HANDLE_VALUE,&listing,MAX_LIST))FAIL("7-Zip unavailable, damaged/encrypted package, or listing limit exceeded");
    if(!parse(listing,entries,&count))FAIL("Unsafe package path/link, duplicate member, or package limits exceeded");
    if(member && *member) {
        if(strlen(member)>=sizeof(requested))FAIL("Archive member path too long");
        strcpy(requested,member); if(!safe_name(requested) || !extension(requested,".mod"))FAIL("Select a MOD member, not a MIF");
    }
    for(i=0;i<count;++i) {
        if(!_stricmp(entries[i].name,".lozeebo-sha256"))FAIL("Reserved cache member name");
        if(!entries[i].dir && extension(entries[i].name,".mod") && (!*requested || !_stricmp(entries[i].name,requested))) {
            strcpy(chosen,entries[i].name); ++mods;
        }
    }
    if(mods!=1)FAIL("Package needs exactly one MOD, or select an explicit MOD member");
    if(plain_dir(cache) && manifest(cache,entries,count,0))goto ready;
    if(GetFileAttributesW(cache)!=INVALID_FILE_ATTRIBUTES)FAIL("Package cache is damaged/unsafe; remove its generated hash directory then retry");
    swprintf(stage,PATH_CAP,L"%ls\\%hs.stage-%lu-%llu",root,hex,GetCurrentProcessId(),(unsigned long long)GetTickCount64());
    if(!CreateDirectoryW(stage,NULL))FAIL("Cannot create private package staging directory");
    staged=1;
    for(i=0;i<count;++i) {
        if(!join(path,stage,entries[i].name) || !parents(path,1))FAIL("Unsafe staging path");
        if(entries[i].dir) { if(!CreateDirectoryW(path,NULL) && !plain_dir(path))FAIL("Cannot create package directory"); continue; }
        f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(f==INVALID_HANDLE_VALUE)FAIL("Cannot create package asset");
        if(!run7(absolute,entries[i].name,f,NULL,entries[i].size) || !FlushFileBuffers(f))FAIL("Package extraction failed, timed out, or exceeded declared size");
        CloseHandle(f); f=INVALID_HANDLE_VALUE;
    }
    if(!manifest(stage,entries,count,1))FAIL("Cannot validate package assets");
    if(!MoveFileExW(stage,cache,MOVEFILE_WRITE_THROUGH)) {
        if(!plain_dir(cache) || !manifest(cache,entries,count,0))FAIL("Cannot publish package cache atomically");
    } else staged=0;
ready:
    if(!join(path,cache,chosen) || !utf8(path,out,(int)out_size))FAIL("Prepared MOD path exceeds core path limit");
    ok=1;
end:
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
    if(source_file!=INVALID_HANDLE_VALUE)CloseHandle(source_file);
    if(staged)discard(stage);
    free(entries); free(listing);
    if(error_size)snprintf(error,error_size,"%s",ok?"":why);
    return ok;
#undef FAIL
}
#else
bool zcontent_prepare(const char *source,const char *member,char *out,size_t out_size,char *error,size_t error_size) {
    (void)source; (void)member;
    if(out_size)out[0]=0;
    if(error_size)snprintf(error,error_size,"ZIP/7z packages require Windows desktop and installed 7-Zip; extract the complete package and open its MOD");
    return false;
}
#endif
