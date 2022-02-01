
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
#include <linux/pid.h>		/* For pid types */
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessio Malavasi");

#define MODNAME "MULTI FLOW"



static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

#define DEVICE_NAME "multi-flow-dev"  /* Device file name in /dev/ - not mandatory  */


static int Major;            /* Major number assigned to broadcast device driver */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)	MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_dentry->d_inode->i_rdev)
#endif

// Struct that represents the I/O node with its state
typedef struct _object_state{
	struct mutex operation_synchronizer;
	int valid_bytes;
	char * stream_content;//the I/O node is a buffer in memory
} object_state;

#define MINORS 128
object_state objects[MINORS];

#define OBJECT_MAX_SIZE  (4096) //just one page

/* the actual driver */

static int dev_open(struct inode *inode, struct file *file) {

   int minor;
   minor = get_minor(file);

   if(minor >= MINORS){
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
  int ret;
  object_state *the_object;

  the_object = objects + minor;

  // Check if there is nothing to write
  if(len == 0){
   return 0;
  }

  mutex_lock(&(the_object->operation_synchronizer)); 
  printk("%s: somebody called a write on dev with [major,minor] number [%d,%d] starting from offest %lld\n",MODNAME,get_major(filp),get_minor(filp),*off);

  // Check if thw write reaches memory bound and resize the write
  if(the_object->valid_bytes + len > OBJECT_MAX_SIZE){
   len = OBJECT_MAX_SIZE - the_object->valid_bytes;
  }

  // Do not write the string terminator '\0'
  ret = copy_from_user(&(the_object->stream_content[the_object->valid_bytes]),buff,len);
  
  // Update valid bytes in the buffer
  the_object->valid_bytes = the_object->valid_bytes + (len - ret);

  printk("%s: valid bytes are %d on dev with [major,minor] number [%d,%d]\n",MODNAME,the_object->valid_bytes,get_major(filp),get_minor(filp));

  mutex_unlock(&(the_object->operation_synchronizer));
  return len - ret;

}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

  int minor = get_minor(filp);
  int ret;
  object_state *the_object;
  char *tmpbuff =  (char*)__get_free_page(GFP_KERNEL);

  the_object = objects + minor;

  mutex_lock(&(the_object->operation_synchronizer)); 
  printk("%s: somebody called a read of %ld bytes on dev with [major,minor] number [%d,%d]\n",MODNAME,len,get_major(filp),get_minor(filp));

  // Resize the reading len if major then the valid bytes in the buffer
  if(len > the_object->valid_bytes){
      len = the_object->valid_bytes;
  }

  ret = copy_to_user(buff,the_object->stream_content,len);
  // Update the valid bytes count in the stream
  the_object->valid_bytes = the_object->valid_bytes - len;

  // Delete the read bytes from the stream
  memcpy(tmpbuff,the_object->stream_content + len,OBJECT_MAX_SIZE-len);
  memset(the_object->stream_content,0,OBJECT_MAX_SIZE);
  memcpy(the_object->stream_content,tmpbuff,OBJECT_MAX_SIZE);

  printk("%s: this is the buffer read -> %s\n",MODNAME,buff);
  mutex_unlock(&(the_object->operation_synchronizer));

  printk("mutex read unlocked");

  return len - ret;
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {

  int minor = get_minor(filp);
  object_state *the_object;

  the_object = objects + minor;
  printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u, param is %s\n",MODNAME,get_major(filp),get_minor(filp),command,(char*)param);

  //do here whathever you would like to control the state of the device
  return 0;

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
		mutex_init(&(objects[i].operation_synchronizer));
		objects[i].valid_bytes = 0;
		objects[i].stream_content = NULL;
		objects[i].stream_content = (char*)__get_free_page(GFP_KERNEL);
		if(objects[i].stream_content == NULL) goto revert_allocation;
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
		free_page((unsigned long)objects[i].stream_content);
	}
	return -ENOMEM;
}

void cleanup_module(void) {

	int i;
	for(i=0;i<MINORS;i++){
		free_page((unsigned long)objects[i].stream_content);
	}

	unregister_chrdev(Major, DEVICE_NAME);

	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

	return;

}
