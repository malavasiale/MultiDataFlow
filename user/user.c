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


#define BUFF_SIZE 4096

/*
Command list : 
	0 : start n thread for write
	1 : start n thread for read
	2 : change priority of the dev
	3 : change timeout for blocking op of the dev
	4 : change blocking / non-blocking dev
	5 : launch the test routine on the device
*/

// Buffer for device name
char device[128];

// Funcion that execute the write
void *only_write(void *data){

	int fd;
	char* to_write = (char*)data;
	int ret;

	// open the session
	fd = open(device,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}

	printf("device %s correctly opened with fd %d\n",device,fd);

	// write the data - delete last char \n
	ret = write(fd,to_write,strlen(to_write)-1);
	if(ret == -1){
		printf("error writing the file %d\n",fd);
		return NULL;
	}

	printf("data written %d of %ld\n\n\n",ret,strlen(to_write)-1);

	close(fd);

	return NULL;
}

void* only_read(void *data){

	int fd;
	int ret;
	int *len = (int*)data;

	// Buffer to recieve the data from kernel
	char buff[BUFF_SIZE];

	// open the session
	fd = open(device,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}

	// read from the file
	printf("start reading of %d bytes from file with fd %d\n",*len,fd);
	ret = read(fd,buff,*len);
	if(ret == -1){
		printf("error reading the file %d\n",fd);
		return NULL;
	}

	printf("success reading %d of %d\n",ret,*len);
	printf("Buffer read content : %s\n\n\n",buff);
	memset(buff,0,*len);

	close(fd);

	return NULL;
}

void* change_prio(void *data){

	int fd;

	int *prio = (int*)data;
	printf("Change priority command with value : %d\n\n\n",*prio);

	// open the session
	fd = open(device,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}

	// call the ioctl to change priority
	ioctl(fd,0,(unsigned long)prio);

	close(fd);
	return NULL;
}

void* change_timer(void *data){
	int *timer = (int*)data;
	int fd;

	printf("Change timer command with value : %d\n\n\n",*timer);

	// open the dev
	fd = open(device,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}

	// call the ioctl to change timer
	ioctl(fd,1,(unsigned long)timer);

	close(fd);

	return NULL;

}

void* change_blocking(void* data){

	int *block = (int*)data;
	int fd;

	printf("Changing blocking param of device to %d\n\n\n",*block);

	// open the session
	fd = open(device,O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}

	// call the ioctl to change blocking param
	ioctl(fd,3,(unsigned long)block);

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
     int dev_num;
     pthread_t tid;

     if(argc<5){
		printf("useg: prog pathname major minor command\n");
		return -1;
     }

     // Take command line parameters passed from the user
     path = argv[1];
     major = strtol(argv[2],NULL,10);
     minor = strtol(argv[3],NULL,10);
     command = strtol(argv[4],NULL,10);
     printf("creating %d minor for device %s with major %d\n",minor,path,major);

     // The device name will be {pathname}{minor}
     strcat(strcpy(device,path),argv[3]);

     // Create dev number with major and minor
     printf("device complete name : %s\n",device);
     dev_num = MKDEV(major,minor);
     
     // Create the new node
     ret = mknod(device,S_IFCHR | 0666 ,dev_num);
     if(ret == -1){
     	if(errno == EEXIST){ // Check if alredy exists
     		printf("device %s alredy exists\n",device);
     	}else{ // Check for errors
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
     		printf("--- Starting write threads ---\n");
     		int n_wthreads;
     		char to_write[128];

     		// How many thread to spawn
     		printf("Insert how many thread spawn : ");
     		ret = scanf("%d",&n_wthreads);
     		if(ret == 0){
     			printf("Invalid number\n");
     			exit(-1);
     		}
     		getchar();

     		// String to write
     		printf("Insert string to write : ");
     		if(fgets(to_write,128,stdin) == NULL){
     			printf("Error reading the string\n");
     			exit(-1);
     		}

     		printf("%s",to_write);
     		
     		// Spawn n_threads that write to_write buff
     		for(int i=0;i<n_wthreads;i++){
     			pthread_create(&tid,NULL,&only_write,(void*)to_write);
     		}
     		break;
     	case 1:
     		printf("--- Starting read threads ---\n");
     		int to_read;
     		int n_rthreads;

     		// How many threads to spawn
     		printf("Insert how many thread spawn : ");
     		ret = scanf("%d",&n_rthreads);
     		if(ret == 0){
     			printf("Invalid number\n");
     			exit(-1);
     		}
     		getchar();

     		// How many bytes t read
     		printf("Insert how many bytes to read : ");
     		ret = scanf("%d",&to_read);
     		if(ret == 0){
     			printf("Invalid number\n");
     			exit(-1);
     		}
     		getchar();

     		// Spawn n_threads that write to_write buff
     		for(int i=0;i<n_rthreads;i++){
     			pthread_create(&tid,NULL,&only_read,&to_read);
     		}
     		break;
     	case 2:
     		printf("--- Starting ioctl change priority ---\n");
     		int prio;

     		// Chose priority value
     		printf("Insert priority to set\n");
     		printf("0 : high\n1 : low\n");
     		ret = scanf("%d",&prio);
     		if(ret == 0 || (prio != 0 && prio != 1)){
     			printf("Invalid number\n");
     			exit(-1);
     		}
     		getchar();

     		pthread_create(&tid,NULL,&change_prio,&prio);
     		break;
     	case 3:
     		printf("--- Starting ioctl change timer ---\n");
     		int timer;

     		// Chose timer value
     		printf("Insert timer value to set (in seconds)\n0 : no timer\n");
     		ret = scanf("%d",&timer);
     		if(ret == 0 || timer < 0){
     			printf("Invalid number\n");
     			exit(-1);
     		}
     		getchar();

     		pthread_create(&tid,NULL,&change_timer,&timer);
     		break;
     	case 4:
     		printf("--- Starting ioctl change blocking ---\n");
     		int block;

     		// Chose block value
     		printf("Insert value to set blocking operatins\n");
     		printf("0 : blocking\n1 : non-blocking\n");
     		ret = scanf("%d",&block);
     		if(ret == 0 || (prio != 0 && prio != 1)){
     			printf("Invalid number\n");
     			exit(-1);
     		}
     		getchar();
     		
     		pthread_create(&tid,NULL,&change_blocking,&block);
     		break;
     	case 5:
     		printf("\n--- Init test routine ---\n");
     		char *test_write = "testwrite\n";
     		int test_read = strlen(test_write)-1;
     		int test_block = 0;
     		int test_prio = 1;
     		int test_timeout = 8;

     		// Spawn a non blocking read thread
     		pthread_create(&tid,NULL,&only_read,&test_read);

     		sleep(2);

     		// Spawn 2 write threads
     		for(int i=0;i<2;i++){
     			pthread_create(&tid,NULL,&only_write,(void*)test_write);
     		}

     		sleep(2);

     		// set the device in blocking mode
     		pthread_create(&tid,NULL,&change_blocking,&test_block);

     		sleep(2);

     		// Spawn 3 blocking read threads.
     		// 2 success - 1 blocked
     		for(int i=0;i<3;i++){
     			pthread_create(&tid,NULL,&only_read,&test_read);
     		}

     		// Wait 8 seconds
     		sleep(8);

     		// Launch one write thread. The blocked read one will wake up and read the data.
     		pthread_create(&tid,NULL,&only_write,(void*)test_write);

     		sleep(2);

     		// reset the device in non-blocking mode
     		test_block = 1;
     		pthread_create(&tid,NULL,&change_blocking,&test_block);

     		sleep(2);

     		// Set priority to low
     		pthread_create(&tid,NULL,&change_prio,&test_prio);

     		sleep(2);

     		// Launch one write on low priority
     		pthread_create(&tid,NULL,&only_write,(void*)test_write);

     		sleep(2);

     		// Spawn two read threads non-blocking on low priority
     		// Could run before or after the write thread!!
     		for(int i=0;i<2;i++){
     			pthread_create(&tid,NULL,&only_read,&test_read);
     		}

     		sleep(2);
     		printf("\n--- Test routine completed ---\n");
     		break;
     	default:
     		printf("Invalid command : %d\n",command);
     		break;
     } 

     pthread_exit(NULL);
     return 0;

}