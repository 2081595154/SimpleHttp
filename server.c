#include <server.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

int listenSocket(int port){
    //创建监听套接字
    int listenfd=socket(AF_INET,SOCK_STREAM,0);
    if(listenfd==-1){
        perror("socket");
        return -1;
    }
    //设置端口复用
    int multi=1;
    int ret_1=setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&multi,sizeof(multi));
    if(ret_1==-1){
        perror("multiport");
        return -1;
    }
    //绑定
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons(port);
    int ret_2=bind(listenfd,(struct sockaddr*)&addr,sizeof(addr));
    if(ret_2==-1){
        perror("bind");
        return -1;
    }
    //监听
    int ret_3=listen(listenfd,128);
    if(ret_3==-1){
        perror("listen");
        return -1;
    }
}

int runepoll(int lsfd){
    //创建epoll
    int epfd=epoll_create(1);
    if(epfd==-1){
        perror("epoll_create");
        return -1;
    }
    //挂载监听文件描述符上红黑树
    struct epoll_event epevnt;
    epevnt.data.fd=lsfd;
    epevnt.events=EPOLLIN;
    int ret=epoll_ctrl(epfd,EPOLL_CTL_ADD,lsfd,&epevnt);
    if(ret==1){
        perror("epolllink");
        return -1;
    }
    //持续监测
    struct epoll_event epevnts[1024];
    while(1){
        int num=sizeof(epevnts)/sizeof(struct epoll_event);
        epoll_wait(epfd,&epevnts,num,-1);
        //对每一个事件判断
        for(int i=0;i<num;i++){
            if(epevnts[i].data.fd==lsfd){
                //建立新连接
            }else{
                //数据接收
            }
        }
    }
}