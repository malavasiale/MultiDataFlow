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
#define TO_READ 20
#define BUFF_SIZE 4096

char buff[BUFF_SIZE];

// Struct that is used for data in ioctl() function
typedef struct _ioctl_params{
   	int prio; // 0 : low , 1 : high
	char* path;
} ioctl_params;

void *write_and_read(void *data){

	int fd;
	char* path = (char*)data;
	int ret;

	printf("Hi i m a tread starting\n");
	fd = open(path,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",path);
		return NULL;
	}

	printf("device %s correctly opened with fd %d\n",path,fd);

	ret = write(fd,DATA,strlen(DATA));
	if(ret == -1){
		printf("error writing the file %d\n",fd);
		return NULL;
	}

	printf("data written %d of %ld\n",ret,strlen(DATA));

	printf("start reading of %d bytes from file with fd %d\n",TO_READ,fd);
	ret = read(fd,buff,TO_READ);
	if(ret == -1){
		printf("error reading the file %d\n",fd);
		return NULL;
	}

	printf("success reading %d of %d\n",ret,TO_READ);
	printf("Buffer read content : %s\n",buff);

	return NULL;
}

void* only_read(void *data){

	int fd;
	char* path = (char*)data;
	int ret;


	fd = open(path,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",path);
		return NULL;
	}

	printf("start reading of %d bytes from file with fd %d\n",TO_READ,fd);
	ret = read(fd,buff,TO_READ);
	if(ret == -1){
		printf("error reading the file %d\n",fd);
		return NULL;
	}

	printf("success reading %d of %d\n",ret,TO_READ);
	printf("Buffer read content : %s\n",buff);

	return NULL;
}

void* change_prio(void *data){

	ioctl_params *params;
	int fd;

	params = (ioctl_params *)data;
	printf("Change priority command with prio : %d\n",params->prio);

	
	fd = open(params->path,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",params->path);
		return NULL;
	}

	ioctl(fd,0,(unsigned long)&params->prio);
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
     			pthread_create(&tid,NULL,&write_and_read,(void*)device);
     		}
     		break;
     	case 1:
     		printf("calling read thread\n");
     		pthread_create(&tid,NULL,&only_read,(void*)device);
     		break;
     	case 2:
     		printf("calling ioctl thread\n");
     		ioctl_params params;
     		params.path = device;
     		params.prio = 0;
     		pthread_create(&tid,NULL,&change_prio,(void*)&params);
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