#include <stdio.h>
#include<unistd.h>
#include "server.h"
#include <stdlib.h>

int main(int argc,char* argv[]) {
    if(argc<3){
        printf("./a.out port path\n");
        return -1;
    }
    int port=atoi(argv[1]);
    //切换工作目录
    chdir(argv[2]);
    //初始化监听套接字
    int lsfd=listenSocket(port);
    //启动套接字
    runepoll(lsfd);
    return 0;
}