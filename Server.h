#pragma once
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <stdio.h>

int init_listenSocket(unsigned short port);

int epollRun(int lfd);

int recvHttpRequest(int cfd, int epfd);

int sendFile(const char* fileName, int cfd);

int parseRequestLine(const char* line, int fd);

const char* getFileType(const char* fileName);