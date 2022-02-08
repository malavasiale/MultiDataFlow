
/* 
 * Author : Alessio Malavasi - mat. 0287437 
 * char device driver that implements the project's specification for Multi-flow device file
 */

#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/wait.h>
#include <linux/pid.h>		/* For pid types */
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessio Malavasi");

#define MODNAME "MULTI FLOW"

// Struct that represents the I/O node with its state
typedef struct _object_state{
   int minor; // minor of the dev
   int prio; // 0 : high , 1 : low
   int blocking; // 0 : blocking , 1 : non-blocking
   struct mutex operation_synchronizer[2];
   int valid_bytes[2]; // number of valid bytes in the two streams
   char * stream_content[2];//the I/O node is a buffer in memory
   wait_queue_head_t rd_queue;
} object_state;

// Struct used for delayed work
typedef struct _packed_task{
        void* buffer;
        const char* to_write;
        int bytes_to_write;
        struct tasklet_struct the_tasklet;
} packed_task;


static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
int put_work(object_state *the_object,const char* buff,int len);
void low_prio_write(unsigned long data);

#define DEVICE_NAME "multi-flow-dev"  /* Device file name in /dev/ - not mandatory  */


static int Major;            /* Major number assigned to broadcast device driver */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)	MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_dentry->d_inode->i_rdev)
#endif


#define MINORS 128
object_state objects[MINORS];


/* Permission to open the session with a minor
   0 : enabled
   1 : disabled
*/
static int open_permissions[MINORS]; 
module_param_array(open_permissions, int, NULL, 0660);

static int bytes_high[MINORS]; 
module_param_array(bytes_high, int, NULL, 0660);

static int bytes_low[MINORS]; 
module_param_array(bytes_low, int, NULL, 0660);



#define OBJECT_MAX_SIZE  (4096) //just one page

/* the actual driver */

static int dev_open(struct inode *inode, struct file *file) {

   int minor;
   minor = get_minor(file);

   // Number of minors major then supported
   if(minor >= MINORS){
	  return -ENODEV;
   }

   // Check if permissions to open the session are enabled
   if(open_permissions[minor] == 1){
      return -ENODEV;
   }


   printk("%s: device file successfully opened for object with minor %d\n",MODNAME,minor);

   //device opened by a default nop
   return 0;


}


static int dev_release(struct inode *inode, struct file *file) {

   int minor;
   minor = get_minor(file);

   printk("%s: device file closed\n",MODNAME);
   //device closed by default nop
   return 0;

}



static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {

  int minor = get_minor(filp);
  int ret = 0;
  object_state *the_object;
  int priority;

  the_object = objects + minor;

  // Check if there is nothing to write
  if(len == 0){
   return 0;
  }

  // Check the priority of the operation
  priority = the_object->prio;

  
  if(priority == 1){
      printk("%s: somebody called a low priority write on dev with [major,minor] number [%d,%d] starting from offest %d with priority %d\n",MODNAME,get_major(filp),get_minor(filp),the_object->valid_bytes[1],priority);
      put_work(the_object,buff,len);
  }else if (priority == 0){
      mutex_lock(&(the_object->operation_synchronizer[priority])); 
      printk("%s: somebody called a high priority write on dev with [major,minor] number [%d,%d] starting from offest %d with priority %d\n",MODNAME,get_major(filp),get_minor(filp),the_object->valid_bytes[0],priority);

      // Check if thw write reaches memory bound and resize the write
      if(the_object->valid_bytes[priority] + len > OBJECT_MAX_SIZE){
         len = OBJECT_MAX_SIZE - the_object->valid_bytes[priority];
      }

      // Do not write the string terminator '\0'
      ret = copy_from_user(&(the_object->stream_content[priority][the_object->valid_bytes[priority]]),buff,len);
  
      // Update valid bytes in the buffer
      the_object->valid_bytes[priority] = the_object->valid_bytes[priority] + (len - ret);

      // Update parameter array of valid bytes
      bytes_high[minor] = the_object->valid_bytes[priority];

      printk("%s: valid bytes are %d on dev with [major,minor] number [%d,%d]\n",MODNAME,the_object->valid_bytes[priority],get_major(filp),get_minor(filp));

      mutex_unlock(&(the_object->operation_synchronizer[priority]));
   }

  return len - ret;

}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

  int minor = get_minor(filp);
  int ret;
  object_state *the_object;
  char *tmpbuff =  (char*)__get_free_page(GFP_KERNEL);
  int priority;

  the_object = objects + minor;

  // Check the priority of the operation
  priority = the_object->prio;

retry_read:

  mutex_lock(&(the_object->operation_synchronizer[priority])); 
  printk("%s: somebody called a read of %ld bytes on dev with [major,minor] number [%d,%d]\n",MODNAME,len,get_major(filp),get_minor(filp));

  // Resize the reading len if major then the valid bytes in the buffer
  if(len > the_object->valid_bytes[priority]){
      if(the_object->blocking == 0){
         mutex_unlock(&(the_object->operation_synchronizer[priority]));
         printk("%s : Insufficient number of bytes to read on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));
         wait_event_timeout(the_object->rd_queue, the_object->blocking, 2*HZ);
         goto retry_read;
      }else if (the_object->blocking == 1){
         len = the_object->valid_bytes[priority];
      }   
  }

  ret = copy_to_user(buff,the_object->stream_content[priority],len);
  // Update the valid bytes count in the stream
  the_object->valid_bytes[priority] = the_object->valid_bytes[priority] - len - ret;

  // Update parameter array of valid bytes
  if(priority == 0){
   bytes_high[minor] = the_object->valid_bytes[priority];
  }else if (priority == 1){
   bytes_low[minor] = the_object->valid_bytes[priority];
  }

  // Delete the read bytes from the stream
  memcpy(tmpbuff,the_object->stream_content[priority] + len,OBJECT_MAX_SIZE-len);
  memset(the_object->stream_content[priority],0,OBJECT_MAX_SIZE);
  memcpy(the_object->stream_content[priority],tmpbuff,OBJECT_MAX_SIZE);

  printk("%s: this is the buffer read -> %s\n",MODNAME,buff);
  mutex_unlock(&(the_object->operation_synchronizer[priority]));


  return len - ret;
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {

  int minor = get_minor(filp);
  object_state *the_object;

  the_object = objects + minor;
  printk("HELLOOO COMMAND IS %d\n",command);

  /*
   List of commands:
      0 : change priority
      1 : lock open session for a given minor
      2 : blocking/non-blocking operations for a given minor
  */

  // Called change priority
  if(command == 0){
   int *priority = (int*)param;
   printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u, param is %d\n",MODNAME,get_major(filp),get_minor(filp),command,*priority);
   the_object->prio = *priority;
  }else if (command == 1){ // Called file lock on session open
   int *to_lock = (int*)param;
   printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u, param is %d\n",MODNAME,get_major(filp),*to_lock,command,*to_lock);
   open_permissions[*to_lock] = 1;
  }else if (command == 3){
   int *block = (int*)param;
   printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u, param is %d\n",MODNAME,get_major(filp),get_minor(filp),command,*block);
   the_object->blocking = *block;
  }else{
   printk("%s : somebody called an ioctl on dev with [major,minor] number [%d,%d] with invalid command %u\n",MODNAME,get_major(filp),get_minor(filp),command);
  }

  return 0;

}

// Function used for delayed work using tasklet
int put_work(object_state *the_object,const char* buff,int len){

   packed_task *the_task;

   if(!try_module_get(THIS_MODULE)) return -ENODEV;


   printk("%s: ------------------------\n",MODNAME);
   printk("%s: requested deferred write with priority: %d\n",MODNAME,the_object->prio);

   the_task = kzalloc(sizeof(packed_task),GFP_ATOMIC);//non blocking memory allocation

   if (the_task == NULL) {
      printk("%s: tasklet buffer allocation failure\n",MODNAME);
      module_put(THIS_MODULE);
      return -1;
   }

   the_task->buffer = (void*)the_object;
   the_task->to_write = buff;
   the_task->bytes_to_write = len;


   printk("%s: tasklet buffer allocation success - address is %p\n",MODNAME,the_task);

   tasklet_init(&the_task->the_tasklet,low_prio_write,(unsigned long)the_task);

   tasklet_schedule(&the_task->the_tasklet);

   return 0;
}

//in a real usage this should ever run as interrupt-context
void low_prio_write(unsigned long data){

   int ret;
   object_state *the_object = (object_state*)(((packed_task*)data)->buffer);
   size_t len = ((packed_task*)data)->bytes_to_write;
   const char* buff = ((packed_task*)data)->to_write;
   int minor = the_object->minor;

   mutex_lock(&(the_object->operation_synchronizer[1])); 

   // Check if thw write reaches memory bound and resize the write
   if(the_object->valid_bytes[1] + len > OBJECT_MAX_SIZE){
      len = OBJECT_MAX_SIZE - the_object->valid_bytes[1];
   }

   // Do not write the string terminator '\0'
   ret = copy_from_user(&(the_object->stream_content[1][the_object->valid_bytes[1]]),buff,len);
  
   // Update valid bytes in the buffer
   the_object->valid_bytes[1] = the_object->valid_bytes[1] + (len - ret);

   // Update parameter array of valid bytes
   bytes_high[minor] = the_object->valid_bytes[1];

   printk("%s: valid bytes are %d on dev low priority\n",MODNAME,the_object->valid_bytes[1]);

   mutex_unlock(&(the_object->operation_synchronizer[1]));

   kfree((void*)data);

   module_put(THIS_MODULE);

}

static struct file_operations fops = {
  .owner = THIS_MODULE,//do not forget this
  .write = dev_write,
  .read = dev_read,
  .open =  dev_open,
  .release = dev_release,
  .unlocked_ioctl = dev_ioctl
};



int init_module(void) {

	int i;
   
	//initialize the drive internal state
	for(i=0;i<MINORS;i++){
		mutex_init(&(objects[i].operation_synchronizer[0]));
      mutex_init(&(objects[i].operation_synchronizer[1]));
      init_waitqueue_head(&(objects[i].rd_queue));
		objects[i].valid_bytes[0] = 0;
      objects[i].valid_bytes[1] = 0;
      objects[i].prio = 0;
      objects[i].minor = i;
      objects[i].blocking = 1;
		objects[i].stream_content[0] = NULL;
      objects[i].stream_content[1] = NULL;
		objects[i].stream_content[0] = (char*)__get_free_page(GFP_KERNEL);
      objects[i].stream_content[1] = (char*)__get_free_page(GFP_KERNEL);

      // Init of the module parameter arrays
      open_permissions[i] = 0;
      bytes_high[i] = 0;
      bytes_low[i] = 0;
		if(objects[i].stream_content[0] == NULL || objects[i].stream_content[1] == NULL) goto revert_allocation;
	}

	Major = __register_chrdev(0, 0, 256, DEVICE_NAME, &fops);
	//actually allowed minors are directly controlled within this driver

	if (Major < 0) {
	  printk("%s: registering device failed\n",MODNAME);
	  return Major;
	}

	printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n",MODNAME, Major);
   
	return 0;

revert_allocation:
	for(;i>=0;i--){
		free_page((unsigned long)objects[i].stream_content[0]);
      free_page((unsigned long)objects[i].stream_content[1]);
	}
	return -ENOMEM;
}

void cleanup_module(void) {

	int i;
	for(i=0;i<MINORS;i++){
		free_page((unsigned long)objects[i].stream_content[0]);
      free_page((unsigned long)objects[i].stream_content[1]);
	}

	unregister_chrdev(Major, DEVICE_NAME);

	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

	return;

}
