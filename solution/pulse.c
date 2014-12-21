/* *********************************************************************
 *
 * Device driver for Distance measurement
 *
 * Program Name:        Pulse
 * Target:              Intel Galileo Gen1
 * Architecture:		x86
 * Compiler:            i586-poky-linux-gcc
 * File version:        v1.0.0
 * Author:              Brahmesh S D Jain
 * Email Id:            Brahmesh.Jain@asu.edu
 **********************************************************************/
 
 /* *************** INCLUDE DIRECTIVES FOR STANDARD HEADERS ************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/init.h>
#include <asm/msr.h>
#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/math64.h>

//#define DEBUG
/*
 * Number of devices that are need to be created by the driver at the end of the
 * driver initialization using udev
 */
#define NUMBER_OF_DEVICES   1

/*
 * driver name
 */
#define DEVICE_NAME    "pulse"

/*
 * Length of the device name string
 */
#define DEVICE_NAME_LENGTH   20
/*
 * This is the clock frequency at which the CPU is running. This is used
 * for converting cpu ticks to micro seconds.
 * It has been defined with the clock frequency of the Intel Galileo
 * Quark processor. If you are running on other processor please change
 * accordingy
 */
#define CPU_FREQ_MHZ 399.088

/* uint8 and unsigned char are used interchangeably in the program */
typedef unsigned char uint8;
typedef enum MesurementOperation_Tag {
	FREE,
	ONGOING
}MesurementOperation_Type;

typedef enum MesurementEdge_Tag {
	RISING,
	FALLING
}MesurementEdge_Type;

/* Device structure */
typedef struct PulseDevTag
{
	struct cdev cdev; /* cdev structure */
	char name[DEVICE_NAME_LENGTH];   /* Driver Name */
	MesurementOperation_Type MesurementOperation; /* To store the operation status */
	struct completion MeasurementCompletion; /* For Completion event handling */
	unsigned long long MeasurementStartTime; /* Start time of the pulse */
	unsigned long long MeasurementEndTime; /* End time of the pulse */
	MesurementEdge_Type MeasurementEdge; /* Measurement edge */
}PulseDevType;

/* the variable that contains the thread data */
static struct task_struct *PulseMeasurementTask = NULL;

/*
 * Device pointer which stores the upper layer device structure
 */
static PulseDevType *PulseDevMem = NULL;

/* Device number alloted */
static dev_t PulseDevNumber;

/* Create class and device which are required for udev */
struct class *PulseDevClass;
static struct device *PulseDevName;


/* *********************************************************************
 * NAME:             PulseEchoIrqHandler
 * CALLED BY:        interrupt service routine
 * DESCRIPTION:      Detects rising and falling edge and updat the
 *                   time stamp counters
 * INPUT PARAMETERS: IrqNumber : Irq number of this interrupt
 *                   dev:device structure pointer
 * RETURN VALUES:    irqreturn_t : status - Fail/IRQ_HANDLED
 ***********************************************************************/
static irqreturn_t PulseEchoIrqHandler(int IrqNumber, void *dev)
{
    unsigned long long CurrentCounter = 0; /* Counter that gets the current cpu time ticks */
#ifdef DEBUG    
    printk(KERN_INFO "\n IRQ called !!! ");
#endif
    if (RISING == ((PulseDevType*)dev)->MeasurementEdge)
	{
		/* This IRQ must be rising edge, so take the time stamp */
		/* Get the current counter */
		rdtscll(CurrentCounter);
		/* copy this counter to global device structure */
		((PulseDevType*)dev)->MeasurementStartTime = CurrentCounter;
        if (!(irq_set_irq_type(IrqNumber,IRQ_TYPE_EDGE_FALLING)))
        {
	    	((PulseDevType*)dev)->MeasurementEdge = FALLING;
	    }
	}
	else
	{
		/* this must be falling edge */
		rdtscll(CurrentCounter);
		/* copy this counter to global device structure */
		((PulseDevType*)dev)->MeasurementEndTime = CurrentCounter;
        /* set the irq to rising edge */
        if (!(irq_set_irq_type(IrqNumber,IRQ_TYPE_EDGE_RISING)))
        {
	    	((PulseDevType*)dev)->MeasurementEdge = RISING;
	    }
		/* Pulse measurment is complete at this point */
		complete(&(((PulseDevType*)dev)->MeasurementCompletion));	
	}
	return IRQ_HANDLED;
}

/* *********************************************************************
 * NAME:             MeasurementThread
 * CALLED BY:        Kernel after creating this lieghtweight thread
 * DESCRIPTION:      Sends the trigger pulse to the Distance sensor and
 *                   waits for the Irq to complete the operation
 * INPUT PARAMETERS: dev pointer:global device structure pointer
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
static int PulseMeasurementThread(void *dev)
{
    /* Trigger pulse of Gpio14/IO2 */
    gpio_set_value(14,1);
    
	/* sleep for 15 micro seconds */
	udelay(150);
	
    /* Trigger pulse of Gpio14/IO2 */
    gpio_set_value(14,0);
#ifdef DEBUG  
    printk(KERN_INFO "/n before waiting for wait_for_completion_interruptible_timeout\n");
#endif
    /* Wait for the IRQ to complete the pulse measurement */
    wait_for_completion_interruptible_timeout(&(((PulseDevType*)dev)->MeasurementCompletion),1000*HZ);
#ifdef DEBUG
    printk(KERN_INFO "/n before waiting for wait_for_completion_interruptible_timeout\n");
#endif
    /* change the measurement operation to ongoing */
    /* Do I need a mutex OR just volatile is enough ?? */
    ((PulseDevType*)dev)->MesurementOperation = FREE;

    /* What to return ????? */
    return 0;
}


/* *********************************************************************
 * NAME:             PulseDriverOpen
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      copies the device structure pointer to the private  
 *                   data of the file pointer. 
 * INPUT PARAMETERS: inode pointer:pointer to the inode of the caller
 *                   filept:file pointer used by this inode
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
int PulseDriverOpen(struct inode *inode, struct file *filept)
{
	PulseDevType *dev; /* dev pointer for the present device */
	int EchoIrq;
	/*  make gpio15 as input as this is required for sensing echo signal */
    gpio_request_one(15,GPIOF_IN,"IO3");
    
    /* get irq of gpio15 */
    EchoIrq = gpio_to_irq(15);

	/* to get the device specific structure from cdev pointer */
	dev = container_of(inode->i_cdev, PulseDevType, cdev);
	/* stored to private data so that next time filept can be directly used */
	filept->private_data = dev;
#ifdef DEBUG  
    printk(KERN_INFO "\n Registering IRQ handler %i \n",EchoIrq);
#endif
    /* Request the IRQ for gpio15 */
    if (request_irq(EchoIrq,&PulseEchoIrqHandler,IRQF_TRIGGER_RISING,"PulseEchoIrqHandler",dev))
    {
		printk(KERN_INFO "\n first PulseMeasurementThread Irq Request failed ");
	}
#ifdef DEBUG  
    printk(KERN_INFO "\n Registering IRQ handler %i is done\n",EchoIrq);
	/* Print that device has opened succesfully */
	printk(KERN_INFO "Device %s opened succesfully ! \n",(char *)&(dev->name));
#endif
    return 0;
}

/* *********************************************************************
 * NAME:             PulseDriverRelease
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      Releases the file structure
 * INPUT PARAMETERS: inode pointer:pointer to the inode of the caller
 *                   filept:file pointer used by this inode
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
int PulseDriverRelease(struct inode *inode, struct file *filept)
{
	PulseDevType *dev = (PulseDevType*)(filept->private_data);
    /* Free the Irq to be safer*/
    free_irq(gpio_to_irq(15),dev);
	printk(KERN_INFO "\n%s is closing\n", dev->name);
	return 0;
}

/* *********************************************************************
 * NAME:             PulseDriverWrite
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      writes chunk of data to the message buffer
 * INPUT PARAMETERS: filept:file pointer used by this inode
 *                   buf : pointer to the user data
 *                   count : no of bytes to be copied to the msg buffer
 *                   offp: offset from which the string to be written
 *                         (not used)
 * RETURN VALUES:    ssize_t : remaining number of bytes that are to be
 *                             written. EBUSY if EEPROM is busy
 ***********************************************************************/
ssize_t PulseDriverWrite(struct file *filept, const char *buf,size_t count, loff_t *offp)
{
	ssize_t RetValue =  0; /* Error code sent when the buffer is full */
    /* If no measurement operation is going on , invoke new write operation */
	PulseDevType *dev = (PulseDevType*)(filept->private_data);
	if (FREE == dev->MesurementOperation)
	{
		/* intiate a kernel thread that sends trigger pulse and waits for irq */
		/* intiate a kernel thread that sends trigger pulse and waits for irq */
		dev->MesurementOperation = ONGOING;
		PulseMeasurementTask = kthread_run(&PulseMeasurementThread,dev,"DisMeasurementThread");
		if (IS_ERR(PulseMeasurementTask))
		{
			/* failed to create kthread */
			printk(KERN_INFO "\n Failed to create measurment thread ");
			dev->MesurementOperation = FREE;
		}
	}
	else
	{
		/* Measurement operation is going on */
		RetValue = -1;
	}
	
    return RetValue;
}

/* *********************************************************************
 * NAME:             PulseDriverRead
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      reads chunk of data from the message buffer
 * INPUT PARAMETERS: filept:file pointer used by this inode
 *                   buf : pointer to the user data
 *                   count : no of bytes to be copied to the user buffer
 *                   offp: offset from which the string to be read
 *                         (not used)
 * RETURN VALUES:    ssize_t : number of bytes written to the user space
 *                  -EAGAIN, if the request is submitted to the workqueue
 *                  -EBUSY, if the EEPROM is busy with read or write oprtn 
 ***********************************************************************/
ssize_t PulseDriverRead(struct file *filept, char *buf,size_t count, loff_t *offp)
{
	ssize_t RetValue = -1;
	PulseDevType *dev = (PulseDevType*)(filept->private_data);
	unsigned int PulseWidth;
	unsigned long long PulseWidth64 = ((dev->MeasurementEndTime) - (dev->MeasurementStartTime));
	if (FREE == dev->MesurementOperation)
	{
		/* Measured data is ready*/
		
		PulseWidth = div_u64(PulseWidth64,400);
        /* Copy to the user space*/
        if(copy_to_user(buf,&PulseWidth,sizeof(PulseWidth)))
        {
           printk(KERN_INFO "\n PulseDriverRead : Buffer Reading failed ");
	    }
	    else
	    {
			RetValue = sizeof(PulseWidth);
		}
	}
    return RetValue;
}

/* Assigning operations to file operation structure */
static struct file_operations PulseFops = {
    .owner = THIS_MODULE, /* Owner */
    .open = PulseDriverOpen, /* Open method */
    .release = PulseDriverRelease, /* Release method */
    .write = PulseDriverWrite, /* Write method */
    .read = PulseDriverRead, /* Read method */
};

/* *********************************************************************
 * NAME:             PulseDriverInit
 * CALLED BY:        By system when the driver is installed
 * DESCRIPTION:      Initializes the driver
 * INPUT PARAMETERS: None
 * RETURN VALUES:    int : initialization status
 ***********************************************************************/
int __init PulseDriverInit(void)
{
	int Ret = -1; /* return variable */
    const struct gpio AllGpios[4] = {
		{31,GPIOF_OUT_INIT_LOW,"IO2Enable"},
		{30,GPIOF_OUT_INIT_LOW,"IO3Enable"},
		{14,GPIOF_OUT_INIT_LOW,"IO2"},
		{15,GPIOF_OUT_INIT_LOW,"IO3"} } ;
    
	/* Allocate device major number dynamically */
	if (alloc_chrdev_region(&PulseDevNumber, 0, NUMBER_OF_DEVICES, DEVICE_NAME) < 0)
	{
         printk(KERN_INFO "Device could not acquire a major number ! \n");
         return -1;
	}
	
	/* Populate sysfs entries */
	PulseDevClass = class_create(THIS_MODULE, DEVICE_NAME);
   
    /* Allocate memory for all the devices */
    PulseDevMem = (PulseDevType*)kmalloc(((sizeof(PulseDevType)) * NUMBER_OF_DEVICES), GFP_KERNEL);
    
    /* Driver Initialization */
    PulseDevMem->MesurementOperation = FREE;
    PulseDevMem->MeasurementEdge = RISING;
    PulseDevMem->MeasurementEndTime = 0;
    PulseDevMem->MeasurementStartTime = 0;
    sprintf(PulseDevMem->name,DEVICE_NAME);
    /* Initialize complettion event */
    init_completion(&(PulseDevMem->MeasurementCompletion));

    /* Check if memory was allocated properly */
   	if (NULL == PulseDevMem)
	{
       printk(KERN_INFO "Kmalloc Fail \n");

	   /* Remove the device class that was created earlier */
	   class_destroy(PulseDevClass);
       /* Unregister devices */
	   unregister_chrdev_region(MKDEV(MAJOR(PulseDevNumber), 0), NUMBER_OF_DEVICES);
       return -ENOMEM;
	} 

    /* Device Creation */ 
    /* Copy the respective device name */
    sprintf(PulseDevMem->name,DEVICE_NAME);

    /* Connect the file operations with the cdev */
    cdev_init(&PulseDevMem->cdev,&PulseFops);    

    PulseDevMem->cdev.owner = THIS_MODULE;
    /* Connect the major/minor number to the cdev */
    Ret = cdev_add(&PulseDevMem->cdev,PulseDevNumber,1);
	if (Ret)
	{
	    printk(KERN_INFO "Bad cdev\n");
	    return Ret;
	}

	PulseDevName = device_create(PulseDevClass,NULL,PulseDevNumber,NULL,DEVICE_NAME);
	
    gpio_request_array(&AllGpios[0],4);
    gpio_set_value_cansleep(31,0);
    gpio_set_value_cansleep(30,0);
    gpio_set_value(14,0);
    gpio_set_value(15,0);
    gpio_free(15);

	printk(KERN_INFO "\n Pulse Driver is initialized \n");
	
	return Ret;
}
/* *********************************************************************
 * NAME:             PulseDriverExit
 * CALLED BY:        By system when the driver is removed
 * DESCRIPTION:      Deinitializes the driver
 * INPUT PARAMETERS: None
 * RETURN VALUES:    None
 ***********************************************************************/
void __exit PulseDriverExit(void)
{
    const struct gpio AllGpios[4] = {
		{31,GPIOF_OUT_INIT_LOW,"IO2Enable"},
		{30,GPIOF_OUT_INIT_LOW,"IO3Enable"},
		{14,GPIOF_OUT_INIT_LOW,"IO2"},
		{15,GPIOF_OUT_INIT_LOW,"IO3"} } ;

     /* unregister gpios */
    gpio_free_array(&AllGpios[0],4);

    /* Destroy the devices first */
	device_destroy(PulseDevClass,PulseDevNumber);

	/* Delete each of the cdevs */
	cdev_del(&(PulseDevMem->cdev));

	/* Free up the allocated memory for all of the device */
	 kfree(PulseDevMem);

	/* Remove the device class that was created earlier */
	class_destroy(PulseDevClass);
	
	/* Unregister char devices */
	unregister_chrdev_region(PulseDevNumber, NUMBER_OF_DEVICES);

	printk(KERN_INFO "\n Pulse device and driver are removed ! \n ");
}

module_init(PulseDriverInit);
module_exit(PulseDriverExit);
MODULE_LICENSE("GPL v2");
