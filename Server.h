#pragma once
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>

int init_listenSocket(unsigned short port);

int epollRun(int lfd);

void* acceptRequest(void* arg);

void* recvHttpRequest(void* arg);

void decodeMsg(char* to, char* from);

int sendFile(const char* fileName, int cfd);

int sendDir(const char* dirName, int cfd);

int parseRequestLine(const char* line, int fd);

const char* getFileType(const char* fileName);