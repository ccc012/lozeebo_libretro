/* Standalone native regression: includes private run7, never exposes runtime knobs. */
#define ZCONTENT_RUN7_TEST
#include "../src/loader/content_package.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s (Win32 %lu)\n",__LINE__,#x,GetLastError()); ExitProcess(3); } } while (0)

static void record_pid(const wchar_t *path) {
    HANDLE f=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,0,NULL);
    DWORD pid=GetCurrentProcessId(),n;
    CHECK(f!=INVALID_HANDLE_VALUE); CHECK(WriteFile(f,&pid,sizeof(pid),&n,NULL) && n==sizeof(pid)); CloseHandle(f);
}
static void spawn(const wchar_t *role,const wchar_t *mode,const wchar_t *path) {
    wchar_t cmd[8192]=L""; STARTUPINFOW si={0}; PROCESS_INFORMATION pi={0};
    CHECK(quote(cmd,8192,seven) && quote(cmd,8192,role) && quote(cmd,8192,mode) && quote(cmd,8192,path));
    si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE); si.hStdInput=GetStdHandle(STD_INPUT_HANDLE); si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    CHECK(CreateProcessW(seven,cmd,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}
int wmain(int argc,wchar_t **argv) {
    wchar_t path[PATH_CAP],temp[PATH_CAP]; const wchar_t *modes[]={L"silent",L"stream",L"blocked"}; unsigned i;
    CHECK(GetModuleFileNameW(NULL,seven,PATH_CAP));
    if (argc>1 && !*argv[0]) { ++argv; --argc; }
    if (argc>1) {
        const wchar_t *mode,*marker;
        if (!wcscmp(argv[1],L"child") || !wcscmp(argv[1],L"grandchild")) {
            mode=argv[2]; marker=argv[3]; record_pid(marker);
            if (!wcscmp(argv[1],L"child")) spawn(L"grandchild",mode,marker);
            else if (wcscmp(mode,L"silent")) {
                char buf[4096]={0}; DWORD n;
                for (;;) if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),buf,sizeof(buf),&n,NULL)) Sleep(INFINITE);
            }
            Sleep(INFINITE); return 0;
        }
        /* run7's fixed CLI; source is last for listing, penultimate for extraction. */
        marker=argv[argc-(!wcscmp(argv[1],L"x")?2:1)];
        if (!wcscmp(marker,L"normal")) { DWORD n; CHECK(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"abc",3,&n,NULL)); return 0; }
        mode=wcsstr(marker,L"silent")?L"silent":wcsstr(marker,L"blocked")?L"blocked":L"stream";
        record_pid(marker); spawn(L"child",mode,marker); Sleep(INFINITE); return 0;
    }
    { char *text=NULL; HANDLE f;
      CHECK(run7(L"normal",NULL,INVALID_HANDLE_VALUE,&text,32)); CHECK(!strcmp(text,"abc")); free(text);
      f=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
      CHECK(f!=INVALID_HANDLE_VALUE); CHECK(run7(L"normal","member",f,NULL,3)); CloseHandle(f); }
    CHECK(GetTempPathW(PATH_CAP,temp));
    for (i=0;i<3;++i) {
        HANDLE f,rd=NULL,wr=NULL; DWORD pids[8],n,j; ULONGLONG start; int result;
        swprintf(path,PATH_CAP,L"%lsrun7-%lu-%ls.pids",temp,GetCurrentProcessId(),modes[i]); DeleteFileW(path);
        if (i==2) CHECK(CreatePipe(&rd,&wr,NULL,4096));
        else { wr=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL); CHECK(wr!=INVALID_HANDLE_VALUE); }
        start=GetTickCount64();
        result=run7(path,"member",wr,NULL,UINT64_MAX/2);
        CHECK(!result); CHECK(GetTickCount64()-start<10000);
        if(rd)CloseHandle(rd); if(wr)CloseHandle(wr);
        f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
        CHECK(f!=INVALID_HANDLE_VALUE); CHECK(ReadFile(f,pids,sizeof(pids),&n,NULL)); CloseHandle(f);
        CHECK(n>=3*sizeof(DWORD) && n%sizeof(DWORD)==0);
        for(j=0;j<n/sizeof(DWORD);++j) {
            HANDLE p=OpenProcess(SYNCHRONIZE,FALSE,pids[j]);
            if(p) { CHECK(WaitForSingleObject(p,5000)==WAIT_OBJECT_0); CloseHandle(p); }
            else CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
        }
        DeleteFileW(path); printf("PASS %ls timeout: tree gone\n",modes[i]);
    }
    puts("PASS normal run7"); return 0;
}
