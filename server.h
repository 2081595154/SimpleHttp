#pragma once

int listenSocket(int port);
int runepoll(int lsfd);
//int acceptclient(int lsfd,int epfd);
void* acceptclient(void* arg);
//int recvhttp(int cfd,int epfd);//第二个参数,当断开连接时,从epoll树上删除该节点
void* recvhttp(void* arg);
int parse(const char* line,int cfd);
int sendFile(const char* filename,int cfd);
//发送响应头（状态行，响应头）只要通信就要cfd
int sendheadmesg(int cfd,int status,const char* descri,const char* type,int len);
const char* getFileType(const char* name);
int senddir(const char* dirname,int cfd);