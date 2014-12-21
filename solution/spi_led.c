/* *********************************************************************
 *
 * Device driver SpiLed
 *
 * Program Name:        SpiLed
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
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/mutex.h>

//#define DEBUG 
/*
 * Number of devices that are need to be created by the driver at the end of the
 * driver initialization using udev
 */
#define NUMBER_OF_DEVICES   1

/*
 * Length of the device name string
 */
#define DEVICE_NAME_LENGTH   20
/*
 * driver name
 */
#define DEVICE_NAME    "spi_led"

/*
 *  Sends the message to SPI
 */
#define SPI_MESSAGE_SEND() \
   do \
   { \
	   spi_message_init(&(Device->SpiLedMessage)); \
	   spi_message_add_tail(&(Device->SpiLedTransfer),&(Device->SpiLedMessage)); \
	   spi_sync(SpiLedDevice,&(Device->SpiLedMessage)); \
   } \
   while(0)


/* uint8 and unsigned char are used interchangeably in the program */
typedef unsigned char uint8;

typedef enum DispayOperation_Tag {
	FREE,
	ONGOING
}DisplayOperation_Type;

typedef struct SpiLedDevTag
{
	struct cdev cdev; /* cdev structure */
	char name[DEVICE_NAME_LENGTH];   /* Driver Name*/
	unsigned char Pattern[10][8]; /* Display pattern */
	unsigned short Sequence[10][2]; /* Display pattern */
	struct mutex DisplayCompleteFlagMutex; /* Mutex to protect Display complete flag */
	volatile DisplayOperation_Type DisplayCompleteFlag; /* Flag to accept new sequence */
	struct spi_message SpiLedMessage; /* Spi message structure required by the spi core */
	struct spi_transfer SpiLedTransfer; /* Spi transfer structure required by the spi core */
}SpiLedDevType;


/*
 * Device pointer which stores the upper layer device structure
 */
static SpiLedDevType *SpiLedDevMem = NULL;

/* Device number alloted */
static dev_t SpiLedDevNumber;

/* Create class and device which are required for udev */
struct class *SpiLedDevClass;
static struct device *SpiLedDevName;

/* the variable that contains the thread data */
static struct task_struct *PatternDisplayTask = NULL;

/*
 * This will point to local kmalloc structure
 */
struct spi_device *SpiLedDevice = NULL;
struct spi_device_id SpiLedDeviceID[] = {
	{"spidev",0},
	{}
	};
	
/* *********************************************************************
 * NAME:             SpiLedDisplayThread
 * CALLED BY:        Kernel after creating the lightweight process
 * DESCRIPTION:      This thread send the pattern to the display and sleeps 
 *                   for the specified amount of time indicated by the sequence
 * INPUT PARAMETERS: Device structure pointer
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
static int SpiLedDisplayThread(void *dev)
{
	unsigned char LoopIndex1, LoopIndex2,EndSequence = 0;
	unsigned char LedMessage[2] = {0x0F,0x01};
	unsigned char LedMessageRecv[2];
    SpiLedDevType *Device = dev;
#ifdef DEBUG  
    printk(KERN_INFO "/n Runnning SpiLedDisplay \n");
#endif
	 Device->SpiLedTransfer.tx_buf = &LedMessage[0];
	 Device->SpiLedTransfer.rx_buf = &LedMessageRecv[0];

    /* Transfer other patterns */
    for (LoopIndex1 = 0; (LoopIndex1 < 10) && (0 == EndSequence); LoopIndex1++)
    {
		if ((Device->Sequence[LoopIndex1][0]) || (Device->Sequence[LoopIndex1][1]))
		{
			for (LoopIndex2 = 0; LoopIndex2 < 8; LoopIndex2++)
			{
			   LedMessage[0] = LoopIndex2 + 1;
			   LedMessage[1] = Device->Pattern[(Device->Sequence[LoopIndex1][0])][LoopIndex2];
			   SPI_MESSAGE_SEND();
#ifdef DEBUG
		       printk(KERN_INFO "\n Display Frame %d written with %d",LedMessage[0],LedMessage[1]);
#endif
			}
			msleep((Device->Sequence[LoopIndex1][1]));
	    }
	    else
	    {
			EndSequence = 1;
			/* clear the display at the end of the sequence */
			for(LoopIndex2 = 1;LoopIndex2 < 9;LoopIndex2++)
			{
			   LedMessage[0] = LoopIndex2;
			   LedMessage[1] = 0x00;
			   SPI_MESSAGE_SEND();
			}
		}
#ifdef DEBUG
		printk("\n Frame %d is send to the display",LoopIndex1);
#endif
	}
   /* Lock the mutex */
    mutex_lock(&(Device->DisplayCompleteFlagMutex));
	Device->DisplayCompleteFlag = FREE;
    /* unlock the mutex and return */
    mutex_unlock(&(Device->DisplayCompleteFlagMutex));
    return 0;
}

/* *********************************************************************
 * NAME:             SpiLedProbe
 * CALLED BY:        spi-core
 * DESCRIPTION:      this is called if spi core finds the device at the
 *                   when device is added by this module. 
 * INPUT PARAMETERS: pointer to the device structture created by
 *                   the spi-core
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
int SpiLedProbe(struct spi_device *ReceivedSpiDevice)
{
#ifdef DEBUG
	printk(KERN_INFO "\n SpiLedProbe function called with device pointer  ReceivedSpiDevice = %d \n",(unsigned int)ReceivedSpiDevice);
#endif
    SpiLedDevice = ReceivedSpiDevice;

    return 0;
}
/* *********************************************************************
 * NAME:             SpiLedRemove
 * CALLED BY:        spi-core
 * DESCRIPTION:      this is called if spi core finds the device at the
 *                   when device is removed by this module. 
 * INPUT PARAMETERS: pointer to the device structture created by
 *                   the spi-core
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
int SpiLedRemove(struct spi_device *ReceivedSpiDevice)
{
#ifdef DEBUG
	printk(KERN_INFO "\n SpiLedRemove function called with device pointer \n");
#endif
    SpiLedDevice = NULL;

    return 0;
}
/* This is the driver that will be inserted */
static struct spi_driver SpiLedDriver = {
	.id_table   = SpiLedDeviceID,
	.driver     = {
	                .owner = THIS_MODULE,
	                .name		= "spidev"
	             },
	.probe = &SpiLedProbe,
	.remove = &SpiLedRemove,
};

/* *********************************************************************
 * NAME:             SpiLedDriverOpen
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      copies the device structure pointer to the private  
 *                   data of the file pointer. 
 * INPUT PARAMETERS: inode pointer:pointer to the inode of the caller
 *                   filept:file pointer used by this inode
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
int SpiLedDriverOpen(struct inode *inode, struct file *filept)
{
	SpiLedDevType *Device; /* dev pointer for the present device */
	unsigned char LedMessage[2] = {0x0F,0x01};
	unsigned char LedMessageRecv[2],LoopIndex;
#ifdef DEBUG
    printk(KERN_INFO "\n  Driver open was called \n\n ");
#endif
	/* to get the device specific structure from cdev pointer */
	Device = container_of(inode->i_cdev, SpiLedDevType, cdev);
	/* stored to private data so that next time filept can be directly used */
	filept->private_data = Device;
	/* Test the display if the display is free */
	if (FREE == SpiLedDevMem->DisplayCompleteFlag)
    {
		/* Enable cs, mosi ans sck */
		gpio_request_one(42,GPIOF_OUT_INIT_LOW,"SpiCsEnable");
		gpio_set_value_cansleep(42,0);
		gpio_request_one(43,GPIOF_OUT_INIT_LOW,"SpiMosiEnable");
		gpio_set_value_cansleep(43,0);
		gpio_request_one(55,GPIOF_OUT_INIT_LOW,"SpiSckEnable");
		gpio_set_value_cansleep(55,0);

		/* Test the display */
		/* Initiate the SPI message and transfer structure */
		Device->SpiLedTransfer.tx_buf = &LedMessage[0];
		Device->SpiLedTransfer.rx_buf = &LedMessageRecv[0];
		Device->SpiLedTransfer.len = 2;
		Device->SpiLedTransfer.cs_change = 1;
		Device->SpiLedTransfer.bits_per_word = 8;
		Device->SpiLedTransfer.speed_hz = 500000;

        SPI_MESSAGE_SEND();
		LedMessage[0] = 0x0F;
		LedMessage[1] = 0x00;
		SPI_MESSAGE_SEND();

		/* Select No decode */
		LedMessage[0] = 0x09;
		LedMessage[1] = 0x00;
		SPI_MESSAGE_SEND();
		/* intensity level medium */
		LedMessage[0] = 0x0A;
		LedMessage[1] = 0x00;
		SPI_MESSAGE_SEND();
		/* scan all the data register for displaying */
		LedMessage[0] = 0x0B;
		LedMessage[1] = 0x07;
		SPI_MESSAGE_SEND();
		/* shutdown register - select normal operation */
		LedMessage[0] = 0x0C;
		LedMessage[1] = 0x01;
		SPI_MESSAGE_SEND();
		/* clear the display */
		for(LoopIndex = 1;LoopIndex < 9;LoopIndex++)
		{
		   LedMessage[0] = LoopIndex;
		   LedMessage[1] = 0x00;
		   SPI_MESSAGE_SEND();
		}
    }

#ifdef DEBUG
	/* Print that device has opened succesfully */
	printk("Device %s opened succesfully ! \n",(char *)&(Device->name));
#endif
    return 0;
}

/* *********************************************************************
 * NAME:             SpiLedDriverRelease
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      Releases the file structure
 * INPUT PARAMETERS: inode pointer:pointer to the inode of the caller
 *                   filept:file pointer used by this inode
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
int SpiLedDriverRelease(struct inode *inode, struct file *filept)
{
	SpiLedDevType *dev = (SpiLedDevType*)(filept->private_data);
	printk("\n%s is closing\n", dev->name);
	return 0;
}

/* *********************************************************************
 * NAME:             SpiLedDriverWrite
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
ssize_t SpiLedDriverWrite(struct file *filept, const char *buf,size_t count, loff_t *offp)
{
	ssize_t RetValue =  0; /* Error code sent when the buffer is full */
	unsigned char *LocalBuffer;
	unsigned char LoopIndex,SequenceIndex=0;
	unsigned short FrameNumber,FrameTime;
    SpiLedDevType *dev = (SpiLedDevType*)(filept->private_data);
    /* If no read-write operation is going on , invoke new write operation */
    if (FREE == SpiLedDevMem->DisplayCompleteFlag)
    {
		LocalBuffer = (unsigned char*)kzalloc(count,GFP_KERNEL);
		if (copy_from_user(LocalBuffer,buf,count))
		{
		   printk(" \nError copying from user space");
		   return 0;
		}
		else
		{
#ifdef DEBUG
				printk(" Driver received data from userspace \n ");
#endif
			for(LoopIndex = 0; LoopIndex < count; LoopIndex+=4, SequenceIndex++)
			{
				/* Copy the FrameNumber */
				memcpy(&FrameNumber,(LocalBuffer + LoopIndex),sizeof(short));
				/* Copy the FrameTime */
				memcpy(&FrameTime,(LocalBuffer + LoopIndex + sizeof(short)),sizeof(short));
				/* Frame Number */
				SpiLedDevMem->Sequence[SequenceIndex][0] = FrameNumber;
				/* Frame Display time */
				SpiLedDevMem->Sequence[SequenceIndex][1] = FrameTime;
			}
            mutex_lock(&(SpiLedDevMem->DisplayCompleteFlagMutex));
			/* intiate a kernel thread that sends trigger pulse and waits for irq */
			SpiLedDevMem->DisplayCompleteFlag = ONGOING;
		    mutex_unlock(&(SpiLedDevMem->DisplayCompleteFlagMutex));
			PatternDisplayTask = kthread_run(&SpiLedDisplayThread,dev,"SpiLedDisplayThread");
			if (IS_ERR(PatternDisplayTask))
			{
				/* failed to create kthread */
				printk(KERN_INFO "\n Failed to create Display thread ");
				mutex_lock(&(SpiLedDevMem->DisplayCompleteFlagMutex));
				SpiLedDevMem->DisplayCompleteFlag = FREE;
				mutex_unlock(&(SpiLedDevMem->DisplayCompleteFlagMutex));
			}
		 }
		 kfree(LocalBuffer);
    }
    else
    {
		return -1;
	}
    return RetValue;
}

/* *********************************************************************
 * NAME:             SpiLedDriverRead
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      reads chunk of data from the message buffer
 * INPUT PARAMETERS: filept:file pointer used by this inode
 *                   buf : pointer to the user data
 *                   count : no of bytes to be copied to the user buffer
 *                   offp: offset from which the string to be read
 *                         (not used)
 * RETURN VALUES:    ssize_t : number of bytes written to the user space
 *                  -EBUSY, if the LED driver is busy with displaying 
 ***********************************************************************/
ssize_t SpiLedDriverRead(struct file *filept, char *buf,size_t count, loff_t *offp)
{
	if (FREE == SpiLedDevMem->DisplayCompleteFlag)
    {
		return 1;
	}
    else
    {
		return -EBUSY;
	}
}

/* *********************************************************************
 * NAME:             SpiLedDriverIoctl
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      Receives the pattern sent by user
 * INPUT PARAMETERS: PatternPtr:pointer to eight byte data of a pattern
 *                   PatternNumber : diaply pattern number
 * RETURN VALUES:    long : error codes / return success
 ***********************************************************************/
long SpiLedDriverIoctl(struct file *filept,unsigned int PatternPtr, unsigned long PatternNumber)
{
	unsigned char LocalBuffer[8];
    if (FREE == SpiLedDevMem->DisplayCompleteFlag)
    {
		if (copy_from_user(&LocalBuffer,(const void __user *)PatternPtr,8))
		{
		   printk(" \n IOCTL : Error copying from user space");
		   return -1;
		}
		else
		{
			memcpy(&(SpiLedDevMem->Pattern[PatternNumber][0]),&LocalBuffer,8);
		}
    }
    else
    {
		return -1;
	}
	return 0;
}

/* Assigning operations to file operation structure */
static struct file_operations SpiLedFops = {
    .owner = THIS_MODULE, /* Owner */
    .open = SpiLedDriverOpen, /* Open method */
    .release = SpiLedDriverRelease, /* Release method */
    .write = SpiLedDriverWrite, /* Write method */
    .read = SpiLedDriverRead, /* Read method */
    .unlocked_ioctl = SpiLedDriverIoctl,
};

/* *********************************************************************
 * NAME:             SpiLedDriverInit
 * CALLED BY:        By system when the driver is installed
 * DESCRIPTION:      Initializes the driver
 * INPUT PARAMETERS: None
 * RETURN VALUES:    int : initialization status
 ***********************************************************************/
int __init SpiLedDriverInit(void)
{
	int Ret = -1; /* return variable */

	/* Allocate device major number dynamically */
	if (alloc_chrdev_region(&SpiLedDevNumber, 0, NUMBER_OF_DEVICES, DEVICE_NAME) < 0)
	{
         printk("Device could not acquire a major number ! \n");
         return -1;
	}
	
	/* Populate sysfs entries */
	SpiLedDevClass = class_create(THIS_MODULE, DEVICE_NAME);
   
    /* Allocate memory for all the devices */
    SpiLedDevMem = (SpiLedDevType*)kzalloc(((sizeof(SpiLedDevType)) * NUMBER_OF_DEVICES), GFP_KERNEL);
    
    /* Check if memory was allocated properly */
   	if (NULL == SpiLedDevMem)
	{
       printk("Kmalloc Fail \n");

	   /* Remove the device class that was created earlier */
	   class_destroy(SpiLedDevClass);
       /* Unregister devices */
	   unregister_chrdev_region(MKDEV(MAJOR(SpiLedDevNumber), 0), NUMBER_OF_DEVICES);
       return -ENOMEM;
	} 

    /* Device Creation */ 
    /* Copy the respective device name */
    sprintf(SpiLedDevMem->name,DEVICE_NAME);
    mutex_init(&(SpiLedDevMem->DisplayCompleteFlagMutex));
    SpiLedDevMem->DisplayCompleteFlag = FREE;

    /* Connect the file operations with the cdev */
    cdev_init(&SpiLedDevMem->cdev,&SpiLedFops);
    SpiLedDevMem->cdev.owner = THIS_MODULE;
    /* Connect the major/minor number to the cdev */
    Ret = cdev_add(&SpiLedDevMem->cdev,SpiLedDevNumber,1);
	if (Ret)
	{
	    printk("Bad cdev\n");
	    return Ret;
	}

	SpiLedDevName = device_create(SpiLedDevClass,NULL,SpiLedDevNumber,NULL,DEVICE_NAME);

    Ret = spi_register_driver(&SpiLedDriver);
#ifdef DEBUG
    printk("\n Added Driver");
#endif
	if (Ret)
	{
		printk(KERN_ERR "SpiLed.ko: Driver registration failed, module not inserted.\n");
       /* Destroy the devices first */
	   device_destroy(SpiLedDevClass,SpiLedDevNumber);

	   /* Delete each of the cdevs */
	   cdev_del(&(SpiLedDevMem->cdev));

	   /* Free up the allocated memory for all of the device */
	   kfree(SpiLedDevMem);

	   /* Remove the device class that was created earlier */
	   class_destroy(SpiLedDevClass);
	
	   /* Unregister devices */
	   unregister_chrdev_region(SpiLedDevNumber, NUMBER_OF_DEVICES);

	   return Ret;
	}
	else
	{
		Ret = 0;
	}
	printk("\n SpiLed Driver is initialized \n");
	
	return Ret;
}
/* *********************************************************************
 * NAME:             SpiLedDriverExit
 * CALLED BY:        By system when the driver is removes
 * DESCRIPTION:      Deinitializes the driver
 * INPUT PARAMETERS: None
 * RETURN VALUES:    None
 ***********************************************************************/
void __exit SpiLedDriverExit(void)
{
    /* Destroy the devices first */
	device_destroy(SpiLedDevClass,SpiLedDevNumber);

	/* Delete each of the cdevs */
	cdev_del(&(SpiLedDevMem->cdev));

	/* Free up the allocated memory for all of the device */
	 kfree(SpiLedDevMem);

	/* Remove the device class that was created earlier */
	class_destroy(SpiLedDevClass);
	
	/* Unregister char devices */
	unregister_chrdev_region(SpiLedDevNumber, NUMBER_OF_DEVICES);
    
    /* Unregistering driver */
    spi_unregister_driver(&SpiLedDriver);
	printk("\n SpiLed driver is removed ! \n ");
}

module_init(SpiLedDriverInit);
module_exit(SpiLedDriverExit);
MODULE_LICENSE("GPL v2");
