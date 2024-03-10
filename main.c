#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "Server.h"

int main(int argc, char* argv[]) {
	/*if (argc < 3) {
		printf("./aout port path\n");
		return -1;
	}*/
	unsigned short port = 2000;
	//chdir(argv[2]);
	chdir("/home/goldlin/os-lab");


	int lfd = init_listenSocket(port);
	epollRun(lfd);

	return 0;
}