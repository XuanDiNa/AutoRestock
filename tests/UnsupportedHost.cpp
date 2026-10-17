#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>
int wmain(int argc,wchar_t** argv){
    if(argc!=2) return 1;
    auto dll=LoadLibraryW(argv[1]);if(!dll){std::cerr<<"FAIL: LoadLibrary "<<GetLastError()<<'\n';return 1;}
    auto status=reinterpret_cast<int(*)()>(GetProcAddress(dll,"AutoRestockStatus"));
    if(!status){std::cerr<<"FAIL: status export missing\n";return 1;}
    auto deadline=GetTickCount64()+5000;
    while(status()==0 && GetTickCount64()<deadline) Sleep(10);
    if(status()!=-1){std::cerr<<"FAIL: unsupported host must be refused\n";return 1;}
    std::cout<<"PASS: built DLL refuses unsupported host before native binding/hook installation\n";return 0;
}
