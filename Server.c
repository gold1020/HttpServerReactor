#include "Server.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/sendfile.h>

int compare(const struct dirent** a, const struct dirent** b) {
	return strcoll((*a)->d_name, (*b)->d_name);
}

int init_listenSocket(unsigned short port) {
	//����
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) {
		printf("create socket failed...");
		return -1;
	}

	//���ö˿ڸ���
	int opt = 1;
	int ret = 0;
	if ((ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) < 0) {
		printf("");
		return -1;
	}

	struct sockaddr_in servaddr;
	servaddr.sin_family = AF_INET;//��ַЭ��,ָ��ʹ�� ipv4 ���� ipv6
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	//��
	int res = bind(lfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if (res == -1)
		perror("bind_error\n");

	//����
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

	//��ӵ�epoll����
	epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

	struct epoll_event evs[1024];
	int size = sizeof(evs) / sizeof(evs[0]);
	while (1) {
		//��ȡ����д�¼�
		int num = epoll_wait(epfd, evs, size, -1);
		for (int i = 0; i < num; i++) {
			int fd = evs[i].data.fd;

			if (fd == lfd) {//����Ǽ�������accept �����뵽epoll��
				int cfd = accept(fd, NULL, NULL);
				
				// ET ����
				ev.events = EPOLLIN | EPOLLET;
				ev.data.fd = cfd;
				
				//���÷�����
				int flag = fcntl(cfd, F_GETFL);
				flag |= O_NONBLOCK;
				fcntl(cfd, F_SETFL, flag);

				epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);

			}
			else {//ͨ��
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
		//����������
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
		sendHeadMsg(fd, 200, "Ok", getFileType(".html"), -1);
		sendDir(file, fd);
	}
	else {//�����ļ����ͻ���
		sendHeadMsg(fd, 200, "Ok",
			getFileType("file"), st.st_size);
		sendFile(file, fd);
	}
	return 0;
}

int sendFile(const char* fileName, int cfd) {
	int fd = open(fileName, O_RDONLY);
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


