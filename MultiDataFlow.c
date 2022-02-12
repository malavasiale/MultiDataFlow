
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
#include <linux/pid.h>
#include <linux/tty.h>
#include <linux/version.h>



MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessio Malavasi");

#define MODNAME "MULTI FLOW"

// Data struct that represents the device with its state and wait queues
typedef struct _object_state{
   int minor; // minor of the dev
   int prio; // 0 : high , 1 : low
   int blocking; // 0 : blocking , 1 : non-blocking
   int timeout;   // timeout for blocking operations
   wait_queue_head_t rd_queue[2];
   wait_queue_head_t wt_queue[2]; // wait queues for read and write op
   struct mutex operation_synchronizer[2]; // mutex for op sync
   int valid_bytes[2]; // number of valid bytes in the two streams
   char * stream_content[2];//the I/O node is a buffer in memory
} object_state;

/*
   Struct used for delayed work.
   It include the tasklet struct, info of the bytes to write
   and the pointer "buffer" for the object_state
*/
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

#define DEVICE_NAME "multi-flow-dev"


/* Major number assigned to broadcast device driver */
static int Major;


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)	MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_dentry->d_inode->i_rdev)
#endif

/* Number of minors accepted*/
#define MINORS 128
object_state objects[MINORS];


/* Permission to open the session with a specific minor
   0 : enabled
   1 : disabled
*/
static int open_permissions[MINORS]; 
module_param_array(open_permissions, int, NULL, 0660);

/*Number of bytes in low and high priority flow for every minor*/
static int bytes_high[MINORS]; 
module_param_array(bytes_high, int, NULL, 0440);

static int bytes_low[MINORS]; 
module_param_array(bytes_low, int, NULL, 0440);


/* Number of thread waiting for data in the read wait queue (high and low priority) */
static unsigned long high_wait_queue_counter[MINORS]; 
module_param_array(high_wait_queue_counter, ulong, NULL, 0440);

static unsigned long low_wait_queue_counter[MINORS]; 
module_param_array(low_wait_queue_counter, ulong, NULL, 0440);


/* Size of the two buffers of every device */
#define OBJECT_MAX_SIZE  (4096)


/* Open operation of the driver */
static int dev_open(struct inode *inode, struct file *file) {

   int minor;
   minor = get_minor(file);

   // Check if minor number is supported
   if(minor >= MINORS){
	  return -ENODEV;
   }

   // Check if permissions to open the session are enabled
   if(open_permissions[minor] == 1){
      return -ENODEV;
   }

   printk("%s: device file successfully opened for object with minor %d\n",MODNAME,minor);

   return 0;
}

/* Release operation of the driver */
static int dev_release(struct inode *inode, struct file *file) {

   int minor;
   minor = get_minor(file);

   printk("%s: device file wit minor %d closed\n",MODNAME,minor);
   //device closed by default nop
   return 0;

}


/* Write operation of the driver */
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

  // Check the priority of the device
  priority = the_object->prio;

retry_write_high:

  if(priority == 1){
      printk("%s: somebody called a low priority write on dev with [major,minor] number [%d,%d] starting from offest %d with priority %d\n",MODNAME,get_major(filp),get_minor(filp),the_object->valid_bytes[1],priority);
      put_work(the_object,buff,len);
  }else if (priority == 0){

      // Get the lock for opertion on the device
      mutex_lock(&(the_object->operation_synchronizer[priority])); 
      printk("%s: called high priority write on dev with minor %d, starting from offest %d with priority %d\n",MODNAME,get_minor(filp),the_object->valid_bytes[priority],priority);

      // Check if the write reaches memory bound,then resize the write or go on wait_queue
      if(the_object->valid_bytes[priority] + len > OBJECT_MAX_SIZE){

         // Case object is blocking
         if(the_object->blocking == 0){
            // Release the lock for operations
            mutex_unlock(&(the_object->operation_synchronizer[priority]));
            printk("%s : Insufficient space on high priority buffer to write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));
            
            // Check timeout value to go in wait queue
            if(the_object->timeout > 0){
               // Going sleep with timeout
               printk("%s : go to sleep for high priority write on dev %d with timeout %d s\n",MODNAME,get_minor(filp),the_object->timeout);
               ret = wait_event_timeout(the_object->wt_queue[priority], the_object->blocking || (the_object->valid_bytes[priority] + len) <= OBJECT_MAX_SIZE, the_object->timeout*HZ);
            }else{
               // Going sleep without timeout
               printk("%s : go to sleep for high priority write on dev %d without timeout\n",MODNAME,get_minor(filp));
               wait_event(the_object->wt_queue[priority], the_object->blocking || the_object->valid_bytes[priority] + len <= OBJECT_MAX_SIZE);
            }

         // retry write when wake up from wait queue
         goto retry_write_high;
         }
         else if (the_object->blocking == 1){ // Case object is non-blocking
            // Set the len of bytes to write to max remaining bytes
            len = OBJECT_MAX_SIZE - the_object->valid_bytes[priority];
         }
         
      }

      // Copy data from user buffer to kernel buffer
      ret = copy_from_user(&(the_object->stream_content[priority][the_object->valid_bytes[priority]]),buff,len);
      if(ret != 0){
         printk("%s : Error in high priority write, could only write %ld bytes of %ld",MODNAME,len-ret,len);
      }

      // Update valid bytes in the buffer
      the_object->valid_bytes[priority] = the_object->valid_bytes[priority] + (len - ret);

      // Update module parameter array of valid bytes
      bytes_high[minor] = the_object->valid_bytes[priority];

      // Wake up process waiting in read queue with high priority
      wake_up_all(&(the_object->rd_queue[priority]));

      printk("%s: Done high priority write. Valid bytes are now %d on dev with [major,minor] number [%d,%d]\n",MODNAME,the_object->valid_bytes[priority],get_major(filp),get_minor(filp));

      // Release the lock fo operations on the device
      mutex_unlock(&(the_object->operation_synchronizer[priority]));
   }

  // Return the written bytes
  return len - ret;

}

/* Read operation of the driver */
static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

  int minor = get_minor(filp);
  int ret;
  object_state *the_object;
  char *tmpbuff =  (char*)__get_free_page(GFP_KERNEL);
  int priority;
  atomic_t *counter;

  the_object = objects + minor;

  // Check the priority of the operation
  priority = the_object->prio;

retry_read:
  // Get the lock for opertion on the device
  mutex_lock(&(the_object->operation_synchronizer[priority])); 
  printk("%s: somebody called a read of %ld bytes on flow with priority %d, on dev with [major,minor] number [%d,%d]\n",MODNAME,len,priority,get_major(filp),get_minor(filp));

  /*
      Resize the reading len if major then then the valid bytes,
      or go to sleep in read waiting queues.
  */
  if(len > the_object->valid_bytes[priority]){

      // Case object is blocking
      if(the_object->blocking == 0){
         // Release the lock for operations
         mutex_unlock(&(the_object->operation_synchronizer[priority]));
         printk("%s : Insufficient number of bytes to read on flow with priority %d, on dev with [major,minor] number [%d,%d]\n",MODNAME,priority,get_major(filp),get_minor(filp));
         
         // Increase counter in the module parameter array of thread waiting for data 
         if(priority == 0){
            counter = (atomic_t*)&high_wait_queue_counter[minor];
         }else if (priority == 1){
            counter = (atomic_t*)&low_wait_queue_counter[minor];
         }
         atomic_inc(counter);

         // Check timeout value to go in wait queue
         if(the_object->timeout > 0){
            // Going sleep with timeout
            printk("%s : go to sleep for read with priority %d on dev %d with timeout %d s\n",MODNAME,priority,get_minor(filp),the_object->timeout);
            wait_event_timeout(the_object->rd_queue[priority], the_object->blocking || len <= the_object->valid_bytes[priority], the_object->timeout*HZ);
         }else{
            // Going sleep without timeout
            printk("%s : go to sleep for read with priority %d on dev %d without timeout\n",MODNAME,priority,get_minor(filp));
            wait_event(the_object->rd_queue[priority], the_object->blocking || len <= the_object->valid_bytes[priority]);
         }

         // Decrease counter in the module parameter array of thread waiting for data 
         atomic_dec(counter);
         goto retry_read;
      }else if (the_object->blocking == 1){ // Case object is non-blocking
         // Set the len of bytes to read to max readable bytes
         len = the_object->valid_bytes[priority];
      }   
  }

  // Copy data from kernel buffer to user buffer
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
  free_page((unsigned long)tmpbuff);

  // Wake up process waiting in write queue of the appropriate priority
  wake_up_all(&(the_object->wt_queue[priority]));

  printk("%s: Done read with priority %d. Valid bytes are now %d on dev with [major,minor] number [%d,%d]\n",MODNAME,priority,the_object->valid_bytes[priority],get_major(filp),get_minor(filp));
  
  // Release the lock for operations on the device
  mutex_unlock(&(the_object->operation_synchronizer[priority]));
  
  // Return the read bytes
  return len - ret;
}

/* ioctl operation of the driver */
static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {

  int minor = get_minor(filp);
  object_state *the_object;

  the_object = objects + minor;

  /*
   List of commands:
      0 : change priority for a given minor
      1 : change timeout for a given minor
      3 : blocking/non-blocking operations for a given minor
  */

  // Called change priority
  if(command == 0){
      int *priority = (int*)param;
      printk("%s: Called an ioctl on dev with [major,minor] number [%d,%d]. Update priority with value %d\n",MODNAME,get_major(filp),get_minor(filp),*priority);
      // Update priority of the specific minor
      the_object->prio = *priority;
  }else if (command == 1){
      int *timer = (int*)param;
      printk("%s: Called an ioctl on dev with [major,minor] number [%d,%d]. Update wait queue timeout with value %d\n",MODNAME,get_major(filp),get_minor(filp),*timer);
      // Update priority of the specific minor
      the_object->timeout = *timer;
  }else if (command == 3){
      int *block = (int*)param;
      printk("%s: Called an ioctl on dev with [major,minor] number [%d,%d]. Update blocking param with value %d\n",MODNAME,get_major(filp),get_minor(filp),*block);
      // Update priority of the specific minor
      the_object->blocking = *block;
      
      // Check if call wake up all the wait queues
      if(the_object->blocking == 1){
         wake_up_all(&(the_object->rd_queue[0]));
         wake_up_all(&(the_object->rd_queue[1]));
         wake_up_all(&(the_object->wt_queue[0]));
         wake_up_all(&(the_object->wt_queue[1]));
   }
  }else{
      // Invalid command
      printk("%s : Called an ioctl on dev with [major,minor] number [%d,%d] with invalid command %u\n",MODNAME,get_major(filp),get_minor(filp),command);
  }

  return 0;

}

// Function used to queue delayed write using tasklets
int put_work(object_state *the_object,const char* buff,int len){

   packed_task *the_task;

   // Try to lock module
   if(!try_module_get(THIS_MODULE)) return -ENODEV;


   printk("%s: requested deferred write on dev with minor %d\n",MODNAME,the_object->minor);

   // Alloc memory for the tasklet struct
   the_task = kzalloc(sizeof(packed_task),GFP_ATOMIC);//non blocking memory allocation
   if (the_task == NULL) {
      printk("%s: tasklet buffer allocation failure\n",MODNAME);
      module_put(THIS_MODULE);
      return -1;
   }

   // Prepare the struct
   the_task->buffer = (void*)the_object;
   the_task->to_write = buff;
   the_task->bytes_to_write = len;


   printk("%s: tasklet buffer allocation success - address is %p\n",MODNAME,the_task);

   // Inizialize tasklet struct
   tasklet_init(&the_task->the_tasklet,low_prio_write,(unsigned long)the_task);

   // Schedule the task in the tasklet queue
   tasklet_schedule(&the_task->the_tasklet);

   return 0;
}

// Delayed work for the low priority write
void low_prio_write(unsigned long data){

   int ret;
   object_state *the_object = (object_state*)(((packed_task*)data)->buffer);
   size_t len = ((packed_task*)data)->bytes_to_write;
   const char* buff = ((packed_task*)data)->to_write;
   int minor = the_object->minor;

retry_write_low:
   // Get the lock for opertion on the device
   mutex_lock(&(the_object->operation_synchronizer[1])); 
   printk("%s: called low priority write on dev with minor %d, starting from offest %d\n",MODNAME,the_object->minor,the_object->valid_bytes[1]);

   // Check if the write reaches memory bound,then resize the write or go on wait_queue
   if(the_object->valid_bytes[1] + len > OBJECT_MAX_SIZE){

      // Case object is blocking
      if(the_object->blocking == 0){

         // Release the lock for operations
         mutex_unlock(&(the_object->operation_synchronizer[1]));
         printk("%s : Insufficient space of buffer to write low priority on dev with minor number %d\n",MODNAME,minor);
            
         // Check timeout value to go in wait queue
         if(the_object->timeout > 0){
            // Going sleep with timeout
            printk("%s : go to sleep for low priority write on dev %d with timeout %d s\n",MODNAME,the_object->minor,the_object->timeout);
            ret = wait_event_timeout(the_object->wt_queue[1], the_object->blocking || the_object->valid_bytes[1] + len <= OBJECT_MAX_SIZE, the_object->timeout*HZ);
         }else{
            // Going sleep without timeout
            printk("%s : go to sleep for low priority write on dev %d without timeout\n",MODNAME,the_object->minor);
            wait_event(the_object->wt_queue[1], the_object->blocking || the_object->valid_bytes[1] + len <= OBJECT_MAX_SIZE);
         }

      // retry write when wake up from wait queue
      goto retry_write_low;
      }
      else if (the_object->blocking == 1){ // Case object is non-blocking
         // Set the len of bytes to write to max remaining bytes
         len = OBJECT_MAX_SIZE - the_object->valid_bytes[1];
      }
   }


   // Copy data from user buffer to kernel buffer
   ret = copy_from_user(&(the_object->stream_content[1][the_object->valid_bytes[1]]),buff,len);
   if(ret != 0){
      printk("%s : Error in delayed low prio write, could only write %ld bytes of %ld",MODNAME,len-ret,len);
   }
  
   // Update valid bytes in the buffer
   the_object->valid_bytes[1] = the_object->valid_bytes[1] + (len - ret);

   // Update parameter array of valid bytes
   bytes_low[minor] = the_object->valid_bytes[1];

   
   // Wake up process waiting in read queue low prio
   wake_up_all(&(the_object->rd_queue[1]));

   printk("%s: Done low priority write. Valid bytes are now %d on dev with minor %d\n",MODNAME,the_object->valid_bytes[1],the_object->minor);

  // Release the lock for operations on the device
   mutex_unlock(&(the_object->operation_synchronizer[1]));

   kfree((void*)data);

   // Release lock module
   module_put(THIS_MODULE);

}

static struct file_operations fops = {
  .owner = THIS_MODULE,
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
      init_waitqueue_head(&(objects[i].rd_queue[0]));
      init_waitqueue_head(&(objects[i].rd_queue[1]));
      init_waitqueue_head(&(objects[i].wt_queue[0]));
      init_waitqueue_head(&(objects[i].wt_queue[1]));
		objects[i].valid_bytes[0] = 0;
      objects[i].valid_bytes[1] = 0;
      objects[i].prio = 0; // Init with high priority
      objects[i].minor = i;
      objects[i].blocking = 1; // Init with non-blocking mode
      objects[i].timeout = 0; // init with no timeout
		objects[i].stream_content[0] = NULL;
      objects[i].stream_content[1] = NULL;
		objects[i].stream_content[0] = (char*)__get_free_page(GFP_KERNEL);
      objects[i].stream_content[1] = (char*)__get_free_page(GFP_KERNEL);

      // Init of the module parameter arrays
      open_permissions[i] = 0; // Init all open permissions unlocked
      bytes_high[i] = 0;
      bytes_low[i] = 0;
      atomic_set((atomic_t*)&high_wait_queue_counter[i], 0);
      atomic_set((atomic_t*)&low_wait_queue_counter[i], 0);
		if(objects[i].stream_content[0] == NULL || objects[i].stream_content[1] == NULL) goto revert_allocation;
	}

   // Register my chardev and check the minor returned
	Major = __register_chrdev(0, 0, 128, DEVICE_NAME, &fops);
	if (Major < 0) {
	  printk("%s: registering device failed\n",MODNAME);
	  return Major;
	}

	printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n",MODNAME, Major);
   
	return 0;

   // Deallocate memory in case of mount failure
revert_allocation:
	for(;i>=0;i--){
		free_page((unsigned long)objects[i].stream_content[0]);
      free_page((unsigned long)objects[i].stream_content[1]);
	}
	return -ENOMEM;
}

void cleanup_module(void) {

	int i;

   // Deallocation of memory unmounting module
	for(i=0;i<MINORS;i++){
		free_page((unsigned long)objects[i].stream_content[0]);
      free_page((unsigned long)objects[i].stream_content[1]);
	}

	unregister_chrdev(Major, DEVICE_NAME);

	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

	return;

}
