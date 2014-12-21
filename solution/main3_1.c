/* *********************************************************************
 *
 * User level program 
 *
 * Program Name:        Dog Catcher
 * Target:              Intel Galileo Gen1
 * Architecture:		x86
 * Compiler:            i586-poky-linux-gcc
 * File version:        v1.0.0
 * Author:              Brahmesh S D Jain
 * Email Id:            Brahmesh.Jain@asu.edu
 **********************************************************************/

/* *************** INCLUDE DIRECTIVES FOR STANDARD HEADERS ************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

//#define DEBUG

/* 
 * Total application Runtime
 */
#define PROGRAM_RUN_TIME 30000000


#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
/*
 * Minimum distance for which the dog starts running
 */
#define DISTANCE_SKIP_ZONE 120
/*
 * Global time out flag
 */
unsigned char TimeoutFlag = 0;
/*
 * Gloabl distance
 */
unsigned int GlobalDistance = 300;
/*
 * Mutex to protect the global variable distance
 */
pthread_mutex_t DistanceMutex = PTHREAD_MUTEX_INITIALIZER;
/*
 * Enum to check whether the Dog should move to the right or left
 */
typedef enum DogDirection_Tag {
	RIGHT,
	LEFT
}DogDirection_Type;
/*
 * Assembly level read Time stamp counter
 */
static __inline__ unsigned long long rdtsc(void)
{
    unsigned long long int x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
}
/* *********************************************************************
 * NAME:             DistanceMeasurementTask
 * CALLED BY:        Thread created by the main thread
 * DESCRIPTION:      Continously reads the distance from the sensor and 
 *                   updates the global vvariable
 * INPUT PARAMETERS: TimeoutFlagLocal : Pointer to the timeout flag
 * RETURN VALUES:    None
 ***********************************************************************/
void* DistanceMeasurementTask(void *TimeoutFlagLocal)
{
	int FdTrig,FdEch,FdEchV,res;
	struct pollfd PollEch = {0};
	unsigned long long StartTime, StopTime;
	unsigned char ReadValue[2];
	/* First change the direction of the Echo to 'in' */
	FdEch = open("/sys/class/gpio/gpio15/direction", O_WRONLY);
	if (FdEch < 0)
	{
		printf("\n gpio15 direction open failed");
	}
	write(FdEch,"in",2);
	close(FdEch);

	/* Open the edge and value files */
	FdEch = open("/sys/class/gpio/gpio15/edge", O_WRONLY);
	if (FdEch < 0)
	{
		printf("\n gpio15 edge open failed");
	}
	FdEchV = open("/sys/class/gpio/gpio15/value", O_RDONLY|O_NONBLOCK);
	if (FdEchV < 0)
	{
		printf("\n gpio15 value open failed");
	}

    /* Open the value file of gpio14 */
	FdTrig = open("/sys/class/gpio/gpio14/value", O_WRONLY);
	if (FdTrig < 0)
	{
		printf("\n FdTrig : gpio14 vale open failed");
	}
    /* Prepare poll fd structure */
    PollEch.fd = FdEchV;
    PollEch.events = POLLPRI|POLLERR;
    lseek(FdEchV, 0, SEEK_SET);
	res = pread(FdEchV,&ReadValue,sizeof(ReadValue),0);
#ifdef DEBUG
	printf("\n Res = %i",res);
    printf("\nRead out");
#endif
    do
    {
		/* Change the edge trigger to rising edge */
		write(FdEch,"rising",6);
		lseek(FdEchV, 0, SEEK_SET);
	    /* Send the ON signal to Trigger port */
		write(FdTrig,"1",1);
		/* Trigger pulse width atleast 10us */
		usleep(12);
		write(FdTrig,"0",1);
		/* Start polling for the rising edge now */
		poll(&PollEch,1,1000);
        
		if (PollEch.revents & POLLPRI)
		{
			/* Start the timer */
			StartTime = rdtsc();
#ifdef DEBUG
			do
			{

				printf("\n Clearing the read1");
#endif
				res = pread(FdEchV,&ReadValue,sizeof(ReadValue),0);
#ifdef DEBUG
				printf("\n Res rising = %i",res);

			}while(0 < res);
#endif
            /* Now detect the falling edge */
			write(FdEch,"falling",7);
		    lseek(FdEchV, 0, SEEK_SET);
			/* Start polling for the falling edge now */
			poll(&PollEch,1,1000);
			/* Stop the timer */
			StopTime = rdtsc();
			/* clear the read buffer */
#ifdef DEBUG
            do
            {

				printf("\n Clearing the read2");
#endif
				res = pread(FdEchV,&ReadValue,sizeof(ReadValue),0);
#ifdef DEBUG
				printf("\n Res = %i",res);

			}while(0 < res);
#endif
			/* calculate the distance */
		    if (PollEch.revents & POLLPRI)
		    {
				pthread_mutex_lock(&DistanceMutex);
				GlobalDistance = (unsigned int)((StopTime - StartTime)*(7.5/20000));
				pthread_mutex_unlock(&DistanceMutex);
		    }
		    else
		    {
			    printf("\nError detecting falling edge");
			}
		}
		else
		{
			printf("\nError detecting rising edge");
		}
		usleep(100000);
    }
	while(0 == (*((unsigned char *)TimeoutFlagLocal)));
    /*Run till the timout flag is set by the main thread */
	printf("\n Ending Distance measurement");
	close(FdTrig);
	close(FdEch);
	return NULL;
}
/* *********************************************************************
 * NAME:             DisplayTask
 * CALLED BY:        Thread created by the main thread
 * DESCRIPTION:      Continously send the pattern to the display
 *                   updates the global vvariable
 * INPUT PARAMETERS: TimeoutFlagLocal : Pointer to the timeout flag
 * RETURN VALUES:    None
 ***********************************************************************/
void* DisplayTask(void *TimeoutFlagLocal)
{
	int FdLed;
    unsigned char LedMessage[2] ={0},ReceivedMsg[2] = {0,0};
    unsigned char LoopIndex = 0;
    struct spi_ioc_transfer spi_transfer_structure = {
		.tx_buf = (unsigned long)LedMessage,
		.rx_buf = (unsigned long)ReceivedMsg,
		.len = ARRAY_SIZE(LedMessage),
		.speed_hz = 500000,
		.cs_change = 1,
		.bits_per_word = 8,
	};
    unsigned int LocalDistancePresent = 1500,LocalDistancePast = 0;
    unsigned char DogStillRight[8] = {0x19, 0xFB, 0xEC, 0x08, 0x08, 0x0F, 0x09, 0x10};
    unsigned char DogRunRight[8] = {0x18, 0xFF, 0xE9, 0x08, 0x0B, 0x0E, 0x08,0x04};
    unsigned char DogStillLeft[8] = {0x10, 0x09, 0x0F, 0x08, 0x08, 0xEC, 0xFB, 0x19};
    unsigned char DogRunLeft[8] = {0x04, 0x08, 0x0E, 0x0B, 0x08, 0xE9, 0xFF, 0x18};
    DogDirection_Type DogDirection = RIGHT;
    FdLed = open("/dev/spidev1.0",O_WRONLY);
	if (FdLed < 0)
	{
		printf("\n LED driver open failed");
	}

    /* Test display */
    LedMessage[0] = 0x0F;
    LedMessage[1] = 0x01;
    ioctl(FdLed,SPI_IOC_MESSAGE(1), &spi_transfer_structure);
    usleep(10000);
    LedMessage[0] = 0x0F;
    LedMessage[1] = 0x00;
    ioctl(FdLed,SPI_IOC_MESSAGE(1), &spi_transfer_structure);
    usleep(10000);
    /* Select No decode */
    LedMessage[0] = 0x09;
    LedMessage[1] = 0x00;
    ioctl(FdLed,SPI_IOC_MESSAGE(1), &spi_transfer_structure);
    usleep(10000);
    /* intensity level medium */
    LedMessage[0] = 0x0A;
    LedMessage[1] = 0x00;
    ioctl(FdLed,SPI_IOC_MESSAGE(1), &spi_transfer_structure);
    usleep(10000);
    /* scan all the data register for displaying */
    LedMessage[0] = 0x0B;
    LedMessage[1] = 0x07;
    ioctl(FdLed,SPI_IOC_MESSAGE(1), &spi_transfer_structure);
    usleep(10000);
    /* shutdown register - select normal operation */
    LedMessage[0] = 0x0C;
    LedMessage[1] = 0x01;
    ioctl(FdLed,SPI_IOC_MESSAGE(1), &spi_transfer_structure);
    usleep(10000);
    
    /* clear the display */
    for(LoopIndex = 1;LoopIndex < 9;LoopIndex++)
    {
       LedMessage[0] = LoopIndex;
       LedMessage[1] = 0;
       ioctl(FdLed,SPI_IOC_MESSAGE(1), &spi_transfer_structure);
       usleep(10000);
	}

    do
    {
		if((LocalDistancePresent > 1500) || (LocalDistancePresent < 100))
		{
			/* Above sensing range, Dog should move to the right */
			DogDirection = RIGHT;
		}
		else if (LocalDistancePresent > (LocalDistancePast + 180 /* (LocalDistancePast * 0.1) */))
		{
			/* person is moving backwards, dog moves left */
			DogDirection = LEFT;
	    }
	    else if (LocalDistancePresent < (LocalDistancePast - 80/* (LocalDistancePast * 0.1) */))
	    {
			/* person is moving front, so dog moves right */
			DogDirection = RIGHT;
		}
		else
		{
			/* Person is neither moving front or backward, so maintain the present direction*/
		}
		/* Dog still */
		for(LoopIndex = 1;LoopIndex < 9;LoopIndex++)
		{
		   LedMessage[0] = LoopIndex;
		   LedMessage[1] = (RIGHT == DogDirection) ? (DogStillRight[LoopIndex - 1]) :(DogStillLeft[LoopIndex - 1]);
		   ioctl(FdLed,SPI_IOC_MESSAGE(1), &spi_transfer_structure);
		}
		usleep((DISTANCE_SKIP_ZONE + (unsigned int)(LocalDistancePresent*0.4))*1000);
		/* Dog Run */
		for(LoopIndex = 1;LoopIndex < 9;LoopIndex++)
		{
		   LedMessage[0] = LoopIndex;
		   LedMessage[1] = (RIGHT == DogDirection) ? (DogRunRight[LoopIndex - 1]) :(DogRunLeft[LoopIndex - 1]);
		   ioctl(FdLed,SPI_IOC_MESSAGE(1), &spi_transfer_structure);
		}
		usleep((DISTANCE_SKIP_ZONE + (unsigned int)(LocalDistancePresent*0.4))*1000);
	    LocalDistancePast = LocalDistancePresent;
		pthread_mutex_lock(&DistanceMutex);
		LocalDistancePresent = (GlobalDistance < 1500) ? (GlobalDistance) : (LocalDistancePresent);
		pthread_mutex_unlock(&DistanceMutex);
		printf("\n Distance in display = %d mm",LocalDistancePresent);
    }while(0 == (*((unsigned char *)TimeoutFlagLocal)));
    close(FdLed);
    return NULL;
}

/* *********************************************************************
 * NAME:             main
 * CALLED BY:        user call this app on the terminal
 * DESCRIPTION:      User test application to test distance measurement
 *                   sensor and 8x8 matrix display
 * INPUT PARAMETERS: None
 * RETURN VALUES:    int : status - Fail/Pass(0) 
 ***********************************************************************/
int main()
{
	int FdE,Fd31,Fd30,Fd14,Fd15,Fd42,Fd43,Fd55;

    pthread_t DistanceMeasurementThreadId, DisplayTaskId;

	/* Enable mux gpio31 to activate gpio14(IO2)*/
	FdE = open("/sys/class/gpio/export", O_WRONLY);
	if (FdE < 0)
	{
		printf("\n gpio export open failed");
	}
	write(FdE,"31",2);
	write(FdE,"30",2);
	write(FdE,"14",2);
    write(FdE,"15",2);
    write(FdE,"42",2);
    write(FdE,"43",2);
    write(FdE,"55",2);

    close(FdE);

    /* Initialize all GPIOs */
	Fd31 = open("/sys/class/gpio/gpio31/direction", O_WRONLY);
	if (Fd31 < 0)
	{
		printf("\n gpio31 direction open failed");
	}
	Fd30 = open("/sys/class/gpio/gpio30/direction", O_WRONLY);
	if (Fd30 < 0)
	{
		printf("\n gpio30 direction open failed");
	}
	Fd14 = open("/sys/class/gpio/gpio14/direction", O_WRONLY);
	if (Fd14 < 0)
	{
		printf("\n gpio14 direction open failed");
	}
	Fd15 = open("/sys/class/gpio/gpio15/direction", O_WRONLY);
	if (Fd15 < 0)
	{
		printf("\n gpio15 direction open failed");
	}
	Fd42 = open("/sys/class/gpio/gpio42/direction", O_WRONLY);
	if (Fd42 < 0)
	{
		printf("\n gpio42 direction open failed");
	}
	Fd43 = open("/sys/class/gpio/gpio43/direction", O_WRONLY);
	if (Fd43 < 0)
	{
		printf("\n gpio43 direction open failed");
	}
	Fd55 = open("/sys/class/gpio/gpio55/direction", O_WRONLY);
	if (Fd55 < 0)
	{
		printf("\n gpio15 direction open failed");
	}
	write(Fd31,"out",3);
	write(Fd30,"out",3);
	write(Fd14,"out",3);
    write(Fd15,"out",3);
    write(Fd42,"out",3);
    write(Fd43,"out",3);
    write(Fd55,"out",3);
	/* After setting the direction, closr the direction file */
	close(Fd31);
	close(Fd30);
	close(Fd14);
	close(Fd15);
	close(Fd42);
	close(Fd43);
	close(Fd55);
	
    /* Now open the value files */
    Fd31 = open("/sys/class/gpio/gpio31/value", O_WRONLY);
	if (Fd31 < 0)
	{
		printf("\n gpio31 value open failed");
	}
    Fd30 = open("/sys/class/gpio/gpio30/value", O_WRONLY);
	if (Fd30 < 0)
	{
		printf("\n gpio30 value open failed");
	}
    Fd14 = open("/sys/class/gpio/gpio14/value", O_WRONLY);
	if (Fd14 < 0)
	{
		printf("\n gpio14 value open failed");
	}
    Fd15 = open("/sys/class/gpio/gpio15/value", O_WRONLY);
	if (Fd15 < 0)
	{
		printf("\n gpio15 value open failed");
	}
	Fd42 = open("/sys/class/gpio/gpio42/value", O_WRONLY);
	if (Fd42 < 0)
	{
		printf("\n gpio42 value open failed");
	}
	Fd43 = open("/sys/class/gpio/gpio43/value", O_WRONLY);
	if (Fd43 < 0)
	{
		printf("\n gpio43 value open failed");
	}
	Fd55 = open("/sys/class/gpio/gpio55/value", O_WRONLY);
	if (Fd55 < 0)
	{
		printf("\n gpio15 value open failed");
	}
    write(Fd31,"0",1);
    write(Fd30,"0",1);
    /* According to the HC-SR04 user guide, Init should set the ECHO and Trigger to 0 */
	write(Fd14,"0",1);
    write(Fd15,"0",1); 
    write(Fd42,"0",1); 
    write(Fd43,"0",1); 
    write(Fd55,"0",1); 
    close(Fd31);
    close(Fd30);
    close(Fd14);
    close(Fd15);
    close(Fd42);
    close(Fd43);
    close(Fd55);
    /* Create Diaply and measurement threads to work on the Dog animation */
    pthread_create(&DistanceMeasurementThreadId,NULL,&DistanceMeasurementTask,&TimeoutFlag);
    pthread_create(&DisplayTaskId,NULL,&DisplayTask,&TimeoutFlag);
    usleep(PROGRAM_RUN_TIME);
    /* Stop distance measurement and display */
    TimeoutFlag = 1;
	printf("\nWaiting for Distance measurement to stop \n");
	pthread_join(DistanceMeasurementThreadId, NULL);
	pthread_join(DisplayTaskId, NULL);

	FdE = open("/sys/class/gpio/unexport", O_WRONLY);
	if (FdE < 0)
	{
		printf("\n gpio unexport open failed");
	}
	write(FdE,"31",2);
	write(FdE,"30",2);
	write(FdE,"14",2);
    write(FdE,"15",2);
    write(FdE,"42",2);
    write(FdE,"43",2);
    write(FdE,"55",2);
    close(FdE);
    return 0;
}
