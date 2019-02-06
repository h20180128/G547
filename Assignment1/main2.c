//#include<linux/kdev_t.h>
#include<linux/fs.h>
#include<linux/device.h>
#include<linux/cdev.h>
#include<linux/module.h>
#include<linux/moduleparam.h>
#include<linux/kernel.h>
#include<linux/types.h>
#include<linux/errno.h>
#include<linux/random.h>
#include<linux/uaccess.h>
#include<linux/typecheck.h>
#include<linux/init.h>
#include<linux/version.h>

static dev_t num;
static struct cdev* c_dev;
static struct class* cls;
static char* devnames[3] = {"adxl_x","adxl_y","adxl_z"};

static int perm_uevent(struct device* dev,struct kobj_uevent_env* env)
{
	add_uevent_var(env,"DEVMODE=%#o",0666);
	return 0;
}

static int my_open(struct inode *i, struct file *f)
{
	printk(KERN_INFO "open()\n");
	return 0;
}

static int my_close(struct inode* i, struct file* f)
{
	printk(KERN_INFO "close()\n");
	return 0;
}

static ssize_t my_read(struct file* f, char __user *buf,size_t sz,loff_t* t)
{
	static int i;
	int j =0;
	for(j=0;j<3;j++)
	{
		get_random_bytes(&i,2);
		i &= (0x03ff);
		if(copy_to_user(buf+(4*j),&i,4)>0)
		{
			printk(KERN_INFO "Read fail\n");
			return -1;
		}
	}

printk(KERN_INFO "Read done");
return sz;
}

static ssize_t my_write(struct file* f, const char __user*buf, size_t sz, loff_t* t)
{
	printk(KERN_INFO"write()\n");
	return sz;

}

static struct file_operations fops = { .owner = THIS_MODULE,
.open = my_open,
.release = my_close,
.read = my_read,
.write = my_write
};


static int firstmod_init(void)
{
	
	int i=0;
	if(alloc_chrdev_region(&num,0,3,"adxl")<0)
	{
		return -1;
	}
	printk(KERN_INFO "Device registration successful");
	 printk(KERN_INFO"<Major,Minor>:<%d,%d>\n",MAJOR(num),MINOR(num));
	if((cls=class_create(THIS_MODULE,"adxl"))==NULL)
	{
		unregister_chrdev_region(num,3);
		return -1;
	}


cls->dev_uevent = perm_uevent;
for (i =0;i<=2;i++)
{
	if (device_create(cls,NULL,MKDEV(MAJOR(num),MINOR(num)+i),NULL,devnames[i])==NULL)
	{
		class_destroy(cls);
		unregister_chrdev_region(num,3);
		return -1;
	}
printk(KERN_INFO "Device %s is created\n",devnames[i]);
}

cdev_init(&c_dev,&fops);
cdev_add(&c_dev,num,3);
printk(KERN_INFO"cdev,fops created\n");
return 0;
}


static void firstmod_exit(void)
{
	cdev_del(&c_dev);
	int i=0;
	for(i=0;i<3;i++)
	device_destroy(cls,MKDEV(MAJOR(num),MINOR(num)+i));
	class_destroy(cls);
	unregister_chrdev_region(num,3);
	printk(KERN_INFO "Character driver unregistered\n");
}


module_init(firstmod_init);
module_exit(firstmod_exit);

MODULE_DESCRIPTION("Char driver - 1st asgnmnt");
MODULE_AUTHOR("SUMEDH");
MODULE_LICENSE("GPL");
MODULE_INFO(SupportedChips, "PCA9685, FT232RL");
