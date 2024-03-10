#include "Server.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/sendfile.h>

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
	bind(lfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

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

			if (fd == lfd) {//如果是监听，则accept 并加入到epoll中
				int cfd = accept(fd, NULL, NULL);
				
				// ET 触发
				ev.events = EPOLLIN | EPOLLET;
				ev.data.fd = cfd;
				
				//设置非阻塞
				int flag = fcntl(cfd, F_GETFL);
				flag |= O_NONBLOCK;
				fcntl(cfd, F_SETFL, flag);

				epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);

			}
			else {//通信
				recvHttpRequest(fd, epfd);
			}
		}
	}
}

int recvHttpRequest(int cfd, int epfd) {
	printf("Begin receive data...\n");
	char tmp[1024];
	int len = 0, total = 0;
	char buf[4096];
	while ((len = recv(cfd, tmp, sizeof(tmp), 0)) > 0) {
		if (total + len < sizeof buf) {
			memcpy(buf + total, tmp, len);
		}
		total += len;
	}

	if (len == -1 && errno == EAGAIN) {
		//解析请求行
		char* pt = strstr(buf, "\r\n");
		pt[0] = '\0';
		parseRequestLine(buf, cfd);
	}
	else if (len == 0) {
		epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
		close(cfd);
	}
	return 0;
}

int parseRequestLine(const char* line, int fd) {
	char method[12];
	char path[1024];
	sscanf(line, "%[^ ] %[^ ]", method, path);
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
	assert(fd);  
#if 0
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
	int size = lseek(fd, 0, SEEK_END);
	sendfile(cfd, fd, NULL, size);
	return 0;
#endif
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
	return "";
}


