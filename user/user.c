#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/kdev_t.h>


#define DATA "provascrittura"
#define SIZE strlen(DATA)
#define TO_READ 10
#define BUFF_SIZE 4096

/*
Command list : 
	0 : start n thread for write
	1 : start n thread for read
	2 : change prioority of the dev
	3 : change timeout for blocking op of the dev
	4 : change blocking / non-blocking dev
*/

// Buffer to recieve the data from kernel
char buff[BUFF_SIZE];

// Struct that is used for input data in ioctl() for all the command
typedef struct _ioctl_input{
   	int value; // 0 : low , 1 : high
	char* path;
} ioctl_input;

// Funcion that execute the write
void *only_write(void *data){

	int fd;
	char* path = (char*)data;
	int ret;

	// open the session
	fd = open(path,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",path);
		return NULL;
	}

	printf("device %s correctly opened with fd %d\n",path,fd);

	// write the data
	ret = write(fd,DATA,strlen(DATA));
	if(ret == -1){
		printf("error writing the file %d\n",fd);
		return NULL;
	}

	printf("data written %d of %ld\n",ret,strlen(DATA));

	close(fd);

	return NULL;
}

void* only_read(void *data){

	int fd;
	char* path = (char*)data;
	int ret;

	// open the session
	fd = open(path,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",path);
		return NULL;
	}

	// read from the file
	printf("start reading of %d bytes from file with fd %d\n",TO_READ,fd);
	ret = read(fd,buff,TO_READ);
	if(ret == -1){
		printf("error reading the file %d\n",fd);
		return NULL;
	}

	printf("success reading %d of %d\n",ret,TO_READ);
	printf("Buffer read content : %s\n",buff);

	close(fd);

	return NULL;
}

void* change_prio(void *data){

	ioctl_input *params;
	int fd;

	params = (ioctl_input *)data;
	printf("Change priority command with value : %d\n",params->value);

	// open the session
	fd = open(params->path,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",params->path);
		return NULL;
	}

	// call the ioctl to change priority
	ioctl(fd,0,(unsigned long)&params->value);

	close(fd);
	return NULL;
}

void* change_timer(void *data){
	ioctl_input *params;
	params = (ioctl_input *)data;
	int fd;

	printf("Change timer command with value : %d\n",params->value);

	// open the dev
	fd = open(params->path,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",params->path);
		return NULL;
	}

	// call the ioctl to change timer
	ioctl(fd,1,(unsigned long)&params->value);

	close(fd);

	return NULL;

}

void* change_blocking(void* data){
	ioctl_input *params;
	params = (ioctl_input *)data;
	int fd;

	printf("Changing blocking param of device to %d\n",params->value);

	// open the session
	fd = open(params->path,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",params->path);
		return NULL;
	}

	// call the ioctl to change blocking param
	ioctl(fd,3,(unsigned long)&params->value);

	close(fd);
	
	return NULL;
}


int main(int argc, char** argv){

     int ret;
     int major;
     int minor;
     int fd;
     int command;
     char* path;
     char device[128];
     int dev_num;
     pthread_t tid;

     if(argc<5){
		printf("useg: prog pathname major minor command\n");
		return -1;
     }

     path = argv[1];
     major = strtol(argv[2],NULL,10);
     minor = strtol(argv[3],NULL,10);
     command = strtol(argv[4],NULL,10);
     printf("creating %d minor for device %s with major %d\n",minor,path,major);

     strcat(strcpy(device,path),argv[3]);

     printf("device complete name : %s\n",device);
     dev_num = MKDEV(major,minor);
     
     ret = mknod(device,S_IFCHR | 0666 ,dev_num);
     if(ret == -1){
     	if(errno == EEXIST){
     		printf("device %s alredy exists\n",device);
     	}else{
     		printf("Cannot create node %s\n",device);
     		return -1;
     	}
     }
     ret = chmod(device, 0666);
     if(ret == -1){
     	printf("Cannot change node %s permissions",device);
     	return -1;
     }

     switch(command){
     	case 0:
     		printf("calling threaddd\n");
     		for(int i=0;i<1;i++){
     			pthread_create(&tid,NULL,&only_write,(void*)device);
     		}
     		break;
     	case 1:
     		printf("calling read thread\n");
     		pthread_create(&tid,NULL,&only_read,(void*)device);
     		break;
     	case 2:
     		// TODO: FARE LA SCELTA DEI PARAETRI DINAMICO
     		printf("calling ioctl thread\n");
     		ioctl_input params_prio;
     		params_prio.path = device;
     		params_prio.value = 1;
     		pthread_create(&tid,NULL,&change_prio,(void*)&params_prio);
     		break;
     	case 3:
     		// TODO: FARE LA SCELTA DEI PARAETRI DINAMICO
     		printf("calling ioctl thread\n");
     		ioctl_input params_timer;
     		params_timer.value = 0;
     		params_timer.path = device;
     		pthread_create(&tid,NULL,&change_timer,(void*)&params_timer);
     		break;
     	case 4:
     		// TODO: FARE LA SCELTA DEI PARAETRI DINAMICO
     		printf("calling ioctl thread\n");
     		ioctl_input params_block;
     		params_block.value = 0;
     		params_block.path = device;
     		pthread_create(&tid,NULL,&change_blocking,(void*)&params_block);
     		break;
     	default:
     		printf("Invalid command : %d\n",command);
     		break;
     } 

     //while(1);
     //pthread_join(tid,NULL);
     pthread_exit(NULL);
     return 0;

}