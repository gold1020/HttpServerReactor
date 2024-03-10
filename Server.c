#include "Server.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <sys/sendfile.h>

struct FdInfo 
{
	int fd;
	int epfd;
	pthread_t pid;
};

int compare(const struct dirent** a, const struct dirent** b) {
	return strcoll((*a)->d_name, (*b)->d_name);
}

int init_listenSocket(unsigned short port) {
	//创建
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) {
		printf("create socket failed...");
		return -1;
	}

	//设置端口复用
	int opt = 1;
	int ret = 0;
	if ((ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) < 0) {
		printf("");
		return -1;
	}

	struct sockaddr_in servaddr;
	servaddr.sin_family = AF_INET;//地址协议,指明使用 ipv4 或者 ipv6
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	//绑定
	int res = bind(lfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if (res == -1)
		perror("bind_error\n");

	//监听
	if (listen(lfd, 128) < 0) {
		printf("listen_error\n");
		return -1;
	}
	return lfd;
}

int epollRun(int lfd) {
	int epfd = epoll_create(100);
	if (epfd < 0) {
		perror("epoll_create");
		return -1;
	}

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd;

	//添加到epoll树上
	epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

	struct epoll_event evs[1024];
	int size = sizeof(evs) / sizeof(evs[0]);
	while (1) {
		//获取到读写事件
		int num = epoll_wait(epfd, evs, size, -1);
		for (int i = 0; i < num; i++) {
			int fd = evs[i].data.fd;
			struct FdInfo* info = (struct FdInfo*)malloc(sizeof(struct FdInfo));
			info->epfd = epfd;
			info->fd = fd;
			if (fd == lfd) {//如果是监听，则accept 并加入到epoll中
				pthread_create(&info->pid, NULL, acceptRequest, info);
			}
			else {//通信
				pthread_create(&info->pid, NULL, recvHttpRequest, info);
			}
		}
	}
}

void* acceptRequest(void* arg) {
	struct FdInfo* info = (struct FdInfo*)arg;

	int cfd = accept(info->fd, NULL, NULL);

	// ET 触发
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = cfd;

	//设置非阻塞
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);
	epoll_ctl(info->epfd, EPOLL_CTL_ADD, cfd, &ev);
	free(info);
}

void* recvHttpRequest(void* arg) {
	struct FdInfo* info = (struct FdInfo*)arg;
	printf("Begin receive data...\n");
	char tmp[1024];
	int len = 0, total = 0;
	char buf[4096];
	while ((len = recv(info->fd, tmp, sizeof(tmp), 0)) > 0) {
		if (total + len < sizeof buf) {
			memcpy(buf + total, tmp, len);
		}
		total += len;
	}

	if (len == -1 && errno == EAGAIN) {
		//解析请求行
		char* pt = strstr(buf, "\r\n");
		pt[0] = '\0';
		parseRequestLine(buf, info->fd);
	}
	else if (len == 0) {
		epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);
		close(info->fd);
	}
	free(info);
	return 0;
}

int hexToDec(char ch) {
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'a' && ch <= 'f') {
		return ch - 'a' + 10;
	}
	if (ch >= 'A' && ch <= 'F') {
		return ch - 'A' + 10;
	}
	return 0;
}

void decodeMsg(char* to, char* from) {
	for (; *from != '\0'; ++to, ++from) {
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
			*to = hexToDec(from[1]) * 16 + hexToDec(from[2]);
			from += 2;
		}
		else {
			*to = *from;
		}
	}
	while (*to != '\0') {
		*to = 0;
		++to;
	}

}


int parseRequestLine(const char* line, int fd) {
	char method[12];
	char path[1024];
	sscanf(line, "%[^ ] %[^ ]", method, path);
	decodeMsg(path, path);
	
	printf("method %s, path: %s\n", method, path);
	if (strcasecmp(method, "get") != 0) {
		return -1;
	}
	char* file = NULL;
	if (strcmp(path, "/") == 0) {
		file = "./";
	}
	else {
		file = path + 1;
	}
	struct stat st;
	if (stat(file, &st) < 0) {
		//404
		sendHeadMsg(fd, 404, "Not Found", getFileType(".html"), -1);
		sendFile("404.html", fd);
		return 0;
	};

	if (S_ISDIR(st.st_mode)) {
		sendHeadMsg(fd, 200, "Ok", getFileType(".html"), -1);
		sendDir(file, fd);
	}
	else {//发送文件给客户端
		sendHeadMsg(fd, 200, "Ok",
			getFileType("file"), st.st_size);
		sendFile(file, fd);
	}
	return 0;
}

int sendFile(const char* fileName, int cfd) {
	int fd = open(fileName, O_RDONLY);
	printf("sendfile...\n");
	assert(fd);  
#if 1
	while (1) {
		char buf[1024];
		int len = read(fd, buf, sizeof buf);
		if (len > 0) {
			send(cfd, buf, len, 0);
			usleep(10);
		}
		else {
			break;
		}
	}
#else
	off_t offset = 0;
	int size = lseek(fd, 0, SEEK_END);
	while (offset < size) {
		int ret = sendfile(cfd, fd, &offset, size);
	}
#endif
	close(fd);
	return 0;
}

int sendDir(const char* dirName, int cfd) {
	char buf[4096] = { 0 };
	sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName);

	struct dirent** nameList;
	int num = scandir(dirName, &nameList, NULL, compare);

	for (int i = 0; i < num; i++) {

		char* name = nameList[i]->d_name;
		struct stat st;
		char subPath[1024];
		if (strcmp(dirName, "./") == 0) {
			sprintf(subPath, "%s%s", dirName, name);
		}
		else
			sprintf(subPath, "%s/%s", dirName, name);
		stat(subPath, &st);
		printf("current path:%s\n", subPath);
		/*if (subPath[0] == '.' && subPath[1] == '/') {
			memcpy(subPath, subPath, strlen(subPath) - 2);
		}*/

		if (S_ISDIR(st.st_mode)) {
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>", 
				subPath, name, st.st_size);
		}
		else {
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
				name, name, st.st_size);
		}
		send(cfd, buf, strlen(buf), 0);
		memset(buf, 0, sizeof(buf));
		free(nameList[i]);
	}
	sprintf(buf + strlen(buf), "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);
	free(nameList);
}

int sendHeadMsg(int cfd, int status, const char* descr, 
	const char* type, int length) {
	char buf[4096];
	sprintf(buf, "http.1.1 %d %s\r\n", status, descr);
	sprintf(buf + strlen(buf), "content-type: %s\r\n", type);
	sprintf(buf + strlen(buf), "content-length: %d\r\n\r\n", length);
	send(cfd, buf, strlen(buf), 0);
	return 0;
}

const char* getFileType(const char* fileName) {
	const char* dot = strrchr(fileName, '.');

	if (dot == NULL)
		return "text/plain; charset=utf-8";
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) {
		return "text/html; charset=utf-8";
	}
	if (strcmp(dot, ".jpg") == 0) {
		return "image/jpeg";
	}
	if (strcmp(dot, ".txt") == 0) {
		return "text/plain";
	}
	return "";
}


