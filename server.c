#define _GNU_SOURCE
#include "server.h"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/sendfile.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

struct fdinfo{
    int epfd;
    int cfd;
    pthread_t thread_id;
};

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
    return listenfd;
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
    int ret=epoll_ctl(epfd,EPOLL_CTL_ADD,lsfd,&epevnt);
    if(ret==-1){
        perror("epolllink");
        return -1;
    }
    //持续监测
    struct epoll_event epevnts[1024];
    while(1){
        int num=sizeof(epevnts)/sizeof(struct epoll_event);
        int nready=epoll_wait(epfd,epevnts,num,-1);
        //对每一个事件判断
        for(int i=0;i<nready;i++){
            struct fdinfo* info =(struct info*)malloc(sizeof(struct fdinfo));
            info->epfd=epfd;
            info->cfd=epevnts[i].data.fd;
            if(epevnts[i].data.fd==lsfd){
                //acceptclient(lsfd,epfd);
                pthread_create(&info->thread_id,NULL,acceptclient,info);
            }else{
                //数据接收
                //recvhttp(epevnts[i].data.fd,epfd);
                pthread_create(&info->thread_id,NULL,recvhttp,info);
            }
        }
    }
    return 0;
}

void* acceptclient(void* arg){
    //建立连接
    struct fdinfo* info=(struct fdinfo*)arg;
    int cfd=accept(info->cfd,NULL,NULL);
    printf("accept 得到新连接, cfd=%d\n", cfd);
    if(cfd==-1){
        perror("acceptclient");
        return NULL;
    }
    //修改为非阻塞
    int flag=fcntl(cfd,F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd,F_SETFL,flag);
    //添加至epoll模型 修改边缘模式
    struct epoll_event epevnt;
    epevnt.data.fd=cfd;
    epevnt.events=EPOLLIN | EPOLLET;
    int ret=epoll_ctl(info->epfd,EPOLL_CTL_ADD,cfd,&epevnt);
    if(ret==-1){
        perror("acclient-epolllink");
        return NULL;
    }
    printf("acceptclient threadid:%ld\n",info->thread_id);
    free(info);
    return NULL;
}

void* recvhttp(void* arg){
    struct fdinfo* info=(struct fdinfo*)arg;
    printf("开始接收数据,cfd=%d\n",info->cfd);
    char buffer[4096]={0};//非阻塞只通知一次,故一次读出
    char temp[1024]={0};//避免覆盖buffer,故建临时缓冲
    int count=0;
    int nextcnt=0;
    while((count=recv(info->cfd,temp,sizeof(temp),0))>0){
        if(nextcnt+count<sizeof(buffer)){
            memcpy(buffer+nextcnt,temp,count);
            nextcnt+=count;
        }
    }
    //判断是否接收完毕
    if(count==-1&&errno==EAGAIN){
    //该情况为解析请求行
    char* pt=strstr(buffer,"\r\n");
    int len=pt-buffer;
    buffer[len]='\0';
    parseline(buffer,info->cfd);
    }else if(count==0){
        epoll_ctl(info->epfd,EPOLL_CTL_DEL,info->cfd,NULL);//该情况为断开连接
        close(info->cfd);
    }else{
        perror("recvhttp");
        return NULL;
    }
    printf("recvhttp threadid:%ld\n",info->thread_id);
    free(info);
    return NULL;
}


int parseline(const char* line,int cfd){
    //解析请求行 get /xx/xx http/1.1     **这里注意相对路径  ./
    char method[12];
    char path[1024];
    printf("parseline 收到的字符串:'%s'\n", line);
    sscanf(line,"%[^ ] %[^ ]",method,path);
    printf("method:%s,path:%s\n",method,path);
    if(strcasecmp(method,"GET")!=0){
         return -1;
    }
    //处理客户端请求的资源(目录或者文件)
    char* file=NULL;
    if(strcmp(path,"/")==0){
        file="./";
    }else{
        file=path+1;
    }
    //获取文件属性
    struct stat st;
    int ret=stat(file,&st);
    if(ret==-1){
        //文件不存在 返回404
        sendheadmesg(cfd,404,"not found",getFileType(".html"), -1);
        sendFile("404.html",cfd);
        return 0;
    }
    //判断文件类型
    if(S_ISDIR(st.st_mode)){
        //发送目录
        sendheadmesg(cfd,200,"OK",getFileType(".html"),-1);
        senddir(file,cfd);
    }else{
        //发送文件
        sendheadmesg(cfd,200,"OK",getFileType(file),-1);
        sendFile(file,cfd);
    }
    return 0;
}

int sendFile(const char* filename,int cfd){
    //打开指定文件,边读边发
    int fd=open(filename,O_RDONLY);
    assert(fd>0);
#if 0
    while(1){
        char buffer[1024];
        int len=read(fd,buffer,sizeof(buffer));
        if(len>0){
            send(cfd,buffer,len,0);
            usleep(10);//!!!
        }else if(len==0){
            break;
        }else{
            perror("read");
            return -1;
        }
    }
#else
    off_t offset = 0;
    int size=lseek(fd,0,SEEK_END);
    lseek(fd,0,SEEK_SET);
    while(offset<size){
        int ret=sendfile(cfd,fd,&offset,size);
        printf("ret value:%d\n",ret);
        if(ret==-1){
            perror("sendfile");
        }
    }
#endif
    close(fd);
    return 0;
}

int sendheadmesg(int cfd,int status,const char* descri,const char* type,int len){
    printf("DEBUG: 进入 sendheadmesg, status=%d, descri=%s\n", status, descri);
   char buffer[4096]={0};
   sprintf(buffer,"http/1.1 %d %s \r\n",status,descri);
   printf("DEBUG: 第一行写完\n");
   sprintf(buffer+strlen(buffer),"content-type: %s\r\n",type);
   printf("DEBUG: 第二行写完\n");
   sprintf(buffer+strlen(buffer),"content-length: %d\r\n\r\n",len);
   printf("DEBUG: 第三行写完\n");
   send(cfd,buffer,strlen(buffer),0); 
   printf("DEBUG: 发送完毕\n");

    return 0;
}

const char* getFileType(const char* name)
{
    // a.jpg a.mp4 a.html
    // 自右向左查找‘.’字符, 如不存在返回NULL
    const char* dot = strrchr(name, '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";	// 纯文本
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp(dot, ".wav") == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}

int senddir(const char* dirname,int cfd){
    struct dirent** namelist;
    int num=scandir(dirname,&namelist,NULL,alphasort);
    char buf[4096]={0};
    sprintf(buf,"<html><head><title>%s</title></head><body><table>",dirname);
    for(int i=0;i<num;i++){
        char* name=namelist[i]->d_name;
        struct stat st;
        char path[1024]={0};
        sprintf(path,"%s/%s",dirname,name);
        stat(path,&st);
        if(S_ISDIR(st.st_mode)){
            //跳转目录里面href加/
            sprintf(buf+strlen(buf),"<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",name,name,st.st_size);
        }else{
            sprintf(buf+strlen(buf),"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",name,name,st.st_size);
        }
        send(cfd,buf,strlen(buf),0);
        memset(buf,0,sizeof(buf));
        free(namelist[i]);
    }
    sprintf(buf,"</table></body></html>");
    send(cfd,buf,strlen(buf),0);
    free(namelist);
    return 0;
}