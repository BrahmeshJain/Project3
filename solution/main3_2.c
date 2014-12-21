/* *********************************************************************
 *
 * User level program 
 *
 * Program Name:        Collision Avoidance System
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
#include <pthread.h>

//#define DEBUG

/*
 * Car's sensing range
 */
#define MINIMUM_DISTANCE_TO_STOP 300
/*
 * Car speed when there is no abtacles
 */
#define CAR_DEFAULT_SPEED 150
/*
 * Car's stop speed when there is an obstacle within its sensing range
 */
#define CAR_SLOWDOW_SPEED 2000
/*
 * Time gap in us between two distance measurements
 */
#define DISTANCE_MEASUTEMENT_TIME 100000
/* 
 * Total application Runtime
 */
#define PROGRAM_RUN_TIME 90000000
/*
 * Timeout flag set by the main thread
 */
unsigned char TimeoutFlag = 0;
/* 
 * Distance measured by the Distance measurment thread is updated on here
 */
unsigned int GlobalDistance = 800;
/*
 * Mutex to protect the global variable distance
 */
pthread_mutex_t DistanceMutex = PTHREAD_MUTEX_INITIALIZER;

/* *********************************************************************
 * NAME:             WritePattern
 * CALLED BY:        Display Tasks
 * DESCRIPTION:      This function sends the ioctl command to the file with 
 *                   the display pattern
 * INPUT PARAMETERS: PatternNumber : ranging from 0-9
 *                   Pattern: pointer to eight byte array
 *                   fd:  function descriptor to which the ioctl command
 *                        is sent
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
int WritePattern(unsigned char PatternNumber, const unsigned char *Pattern, int Fd)
{
   int Res = 0;
   Res = ioctl(Fd,(unsigned int)(Pattern),(unsigned long)PatternNumber);
   if (Res < 0)
   {
	   printf("IOCTL error in WritePattern ");
       perror("Error is :");
   }
   return Res;
}
/* *********************************************************************
 * NAME:             DistanceMeasurementTask
 * CALLED BY:        Created by main() thread
 * DESCRIPTION:      Thread to trigger and gather measurement
 * INPUT PARAMETERS: TimeoutFlagLocal : To end the thread
 * RETURN VALUES:    None
 ***********************************************************************/
void* DistanceMeasurementTask(void *TimeoutFlagLocal)
{
	int FdPulse,Result;
	unsigned int ReceivedValue = 0, LocalMaximum;
	 /* Distance measurement */
    FdPulse = open("/dev/pulse",O_RDWR);
	if (FdPulse < 0)
	{
		printf("\n pulse driver file open failed");
	}
	do
	{
		/* Trigger measurement */
		Result  = write(FdPulse,&ReceivedValue,4);
		if (Result < 0)
		{
#ifdef DEBUG
		  printf("\n WRITE call:  driver BUSY");
#endif
		}
		else
		{
#ifdef DEBUG
			   printf("\n WRITE call: Measurement STARTED");
#endif 
               /* Give some time for the measument to happen */
			   usleep(10000);
#ifdef DEBUG
			   printf("\n Reading Driver now");
#endif
               /* Try reading the measured value */
			   do
			   {
				   /* Read the measured value */
				   Result  = read(FdPulse,&ReceivedValue,sizeof(ReceivedValue));
				   if (Result < 0)
				   {
#ifdef DEBUG
					   printf("\n READ call: Measurement IN PROGRESS");
#endif
				       /* Measurement is not ready yet , give some more time */
				       usleep(10000);
				   }
				   else
				   {
#ifdef DEBUG
					   printf("\n READ call:  driver SUCCESS");
					   printf("\n Received pulse width : %d us\n",ReceivedValue);
#endif
					   printf("\n Distance = %d mm\n",(unsigned int)(ReceivedValue*0.150));
					   /* Updat the measured value with distance in mm*/
				       pthread_mutex_lock(&DistanceMutex);
				       GlobalDistance = (unsigned int)(ReceivedValue*0.150);
				       pthread_mutex_unlock(&DistanceMutex);
				   }
		       }while(Result < 0);
		}
		usleep(DISTANCE_MEASUTEMENT_TIME);
	}while(0 == (*((unsigned char *)TimeoutFlagLocal)));
	close(FdPulse);
	return NULL;
}

/* *********************************************************************
 * NAME:             ESPDisplayTask
 * CALLED BY:        Created by main() thread
 * DESCRIPTION:      Thread to display moving alphabets "ESP"
 * INPUT PARAMETERS: TimeoutFlagLocal : Not used
 * RETURN VALUES:    None
 ***********************************************************************/
void* ESPDisplayTask(void *TimeoutFlagLocal)
{
	int FdDisplay;
	unsigned char count = 0, LoopIndex, ReadBuff;
	const unsigned char PatternESP[23][8] = {
		{0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
		{0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03},
		{0x07, 0x07, 0x06, 0x07, 0x07, 0x06, 0x07, 0x07},
		{0x0f, 0x0f, 0x0c, 0x0f, 0x0f, 0x0c, 0x0f, 0x0f},
		{0x1f, 0x1f, 0x18, 0x1f, 0x1f, 0x18, 0x1f, 0x1f},
		{0x3e, 0x3e, 0x30, 0x3e, 0x3e, 0x30, 0x3e, 0x3e},
		{0x7d, 0x7d, 0x61, 0x7d, 0x7d, 0x60, 0x7d, 0x7d},
		{0xfb, 0xfb, 0xc3, 0xfb, 0xfb, 0xc0, 0xfb, 0xfb},
		{0xf7, 0xf7, 0x86, 0xf7, 0xf7, 0x81, 0xf7, 0xf7},
		{0xef, 0xef, 0x0c, 0xef, 0xef, 0x03, 0xef, 0xef},
		{0xde, 0xde, 0x18, 0xde, 0xde, 0x06, 0xde, 0xde},
		{0xbd, 0xbd, 0x31, 0xbd, 0xbd, 0x0d, 0xbd, 0xbd},
		{0x7b, 0x7b, 0x63, 0x7b, 0x7b, 0x1b, 0x7b, 0x7b},
		{0xf7, 0xf7, 0xc6, 0xf6, 0xf7, 0x36, 0xf6, 0xf6},
		{0xef, 0xef, 0x8c, 0xec, 0xef, 0x2c, 0xec, 0xec},
		{0xdf, 0xdf, 0x19, 0xd9, 0xdf, 0xd8, 0xd8, 0xd8},
		{0xbe, 0xbe, 0x32, 0xb2, 0xbe, 0xb0, 0xb0, 0xb0},
		{0x7c, 0x7c, 0x64, 0x64, 0x7c, 0x60, 0x60, 0x60},
		{0xf8, 0xf8, 0xc8, 0xc8, 0xf8, 0xc0, 0xc0, 0xc0},
		{0xf0, 0xf0, 0x90, 0x90, 0xf0, 0x80, 0x80, 0x80},
		{0xe0, 0xe0, 0x20, 0x20, 0xe0, 0x00, 0x00, 0x00},
		{0xc0, 0xc0, 0x40, 0x40, 0xc0, 0x00, 0x00, 0x00},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00}
	};
	unsigned short DisplaySequence[10][2]={
		{0,500},{1,500},{2,500},
		{3,500},{4,500},{5,500},
		{6,500},{7,500},{8,500},
		{9,400}/*Last one will be followed by a pattern change, hence less time*/
		};
    FdDisplay = open("/dev/spi_led",O_RDWR);
	if (FdDisplay < 0)
	{
		printf("\n spi_led driver file open failed");
	}
	/* write first 10 pattern */
	for(LoopIndex = 0; LoopIndex < 10; LoopIndex++)
	{
    	WritePattern(LoopIndex,&(PatternESP[LoopIndex][0]),FdDisplay);
    }

	/* Write the display sequence */ 
	write(FdDisplay,&DisplaySequence,sizeof(DisplaySequence));
	
	/* poll if the display has become free to accept new pattern */
	do
	{
		usleep(10000);
	}while (0 > read(FdDisplay,&ReadBuff,1));
	/* By now it will be ready to accept new commands */
	/* write next 10 pattern */
	for(LoopIndex = 0; LoopIndex < 10; LoopIndex++)
	{
    	WritePattern(LoopIndex,&(PatternESP[LoopIndex + 10][0]),FdDisplay);
    }

	/* Write the display sequence */ 
	write(FdDisplay,&DisplaySequence,sizeof(DisplaySequence));
	
	/* poll if the display has become free to accept new pattern */
	do
	{
		usleep(10000);
	}while (0 > read(FdDisplay,&ReadBuff,1));
	/* By now it will be ready to accept new commands */
	

	/* write next 10 pattern */
	for(LoopIndex = 0; LoopIndex < 3; LoopIndex++)
	{
    	WritePattern(LoopIndex,&(PatternESP[LoopIndex + 20][0]),FdDisplay);
    }
    /* Display only last 3 patterns */
    DisplaySequence[3][0] = 0;
    DisplaySequence[3][1] = 0;
    
	/* Write the display sequence */ 
	write(FdDisplay,&DisplaySequence,sizeof(DisplaySequence));	
	
#ifdef DEBUG
	printf("\n Display programmed %i \n",Result);
#endif
    close(FdDisplay);
    return NULL;
}

/* *********************************************************************
 * NAME:             CollisionAvoidanceTask
 * CALLED BY:        Created by main() thread
 * DESCRIPTION:      Thread to Display movement of Car. This car speed
 *                   is controlled the measured distance.
 * INPUT PARAMETERS: TimeoutFlagLocal : used for ending this task
 * RETURN VALUES:    None
 ***********************************************************************/
void* CollisionAvoidanceTask(void *TimeoutFlagLocal)
{
	int FdDisplay;
	unsigned char count = 0, LoopIndex,SlowdownFlag = 0,LocalLineNum = 0,ReadBuff;
	/* Pattern that defines the CAR structure */
	const unsigned char PatternESP[8][8] = {
     	{0x00, 0x7c, 0x44, 0x47, 0x41, 0x7f, 0x22, 0x00},
		{0x00, 0x3e, 0x22, 0xa3, 0xa0, 0xbf, 0x11, 0x00},
		{0x00, 0x1f, 0x11, 0xd1, 0x50, 0xdf, 0x88, 0x00},
		{0x00, 0x8f, 0x88, 0xe8, 0x28, 0xef, 0x44, 0x00},
		{0x00, 0xc7, 0x44, 0x74, 0x14, 0xf7, 0x22, 0x00},
		{0x00, 0xe3, 0x22, 0x3a, 0x0a, 0xfb, 0x11, 0x00},
		{0x00, 0xf1, 0x11, 0x1d, 0x05, 0xfd, 0x88, 0x00},
		{0x00, 0xf8, 0x88, 0x8e, 0x82, 0xfe, 0x44, 0x00}
	};
	/* Two speeds for the car */
	unsigned short DisplaySequenceRun[10][2]={
		{0,CAR_DEFAULT_SPEED},{1,CAR_DEFAULT_SPEED},{2,CAR_DEFAULT_SPEED},
		{3,CAR_DEFAULT_SPEED},{4,CAR_DEFAULT_SPEED},{5,CAR_DEFAULT_SPEED},
		{6,CAR_DEFAULT_SPEED},{7,CAR_DEFAULT_SPEED},{0,0},
		{0,0}
		};
	unsigned short DisplaySequenceSlow[10][2]={
		{0,CAR_SLOWDOW_SPEED},{1,CAR_SLOWDOW_SPEED},{2,CAR_SLOWDOW_SPEED},
		{3,CAR_SLOWDOW_SPEED},{4,CAR_SLOWDOW_SPEED},{5,CAR_SLOWDOW_SPEED},
		{6,CAR_SLOWDOW_SPEED},{7,CAR_SLOWDOW_SPEED},{0,0},
		{0,0}
		};

    FdDisplay = open("/dev/spi_led",O_RDWR);
	if (FdDisplay < 0)
	{
		printf("\n spi_led driver file open failed");
	}
	/* Check if the display is free to accept new pattern */
	do
    {
#ifdef DEBUG
		printf("Waiting for display to get free ");
#endif
	    usleep(1000);
	}while (0 > read(FdDisplay,&ReadBuff,1));
	
    /* write the car pattern */
	for (LoopIndex = 0; LoopIndex < 8; LoopIndex++)
	{
        WritePattern(LoopIndex,&(PatternESP[LoopIndex][0]),FdDisplay);
	}
	
    /* Keep sending the sequence untill the timeout */
	do
	{
	    /* poll if the display has become free to accept new sequnece */
		do
		{
#ifdef DEBUG
			printf("Waiting for display to get free ");
#endif
			usleep(1000);
		}while (0 > read(FdDisplay,&ReadBuff,1));
		/* Read the distance and decide whether the car needs to be slowed down */
		pthread_mutex_lock(&DistanceMutex);
		SlowdownFlag = (GlobalDistance < MINIMUM_DISTANCE_TO_STOP) ? (1) : (0);
		pthread_mutex_unlock(&DistanceMutex);
		/* Check whether car needs to be slowed down */
		if(1 == SlowdownFlag)
		{
			/* Car needs to be slowed down , hence send the slow movement sequence */
			write(FdDisplay,&DisplaySequenceSlow,sizeof(DisplaySequenceSlow));
		}
		else
		{
			/* No obstacle with in the scan area, hence send full run sequence */
			write(FdDisplay,&DisplaySequenceRun,sizeof(DisplaySequenceRun));
		}
	}while(0 == (*((unsigned char *)TimeoutFlagLocal)));

#ifdef DEBUG
	printf("\n Display programmed %i \n",Result);
#endif
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
    pthread_t DistanceMeasurementThreadId, CollisionAvoidanceTaskId, ESPDisplayTaskId, BoxDisplayTaskId;
    
    /* Testing sensor : Start distance measurement thread to see the distance meeasured on theconsole */
    pthread_create(&DistanceMeasurementThreadId,NULL,&DistanceMeasurementTask,&TimeoutFlag);
    /* Testing Display : start the display moving "ESP" */
    pthread_create(&ESPDisplayTaskId,NULL,&ESPDisplayTask,&TimeoutFlag);
    /* Wait for the ESP to end */
	pthread_join(ESPDisplayTaskId, NULL);
	/* Testing Distance controlled display : Start the car collision avoidance TASK */
    pthread_create(&CollisionAvoidanceTaskId,NULL,&CollisionAvoidanceTask,&TimeoutFlag);
    /* Total time for which this application should read */
    usleep(PROGRAM_RUN_TIME);
    /* Stop distance measurement and display*/
    TimeoutFlag = 1;
	printf("\nWaiting for Distance measurement and display thread to stop \n");
	pthread_join(DistanceMeasurementThreadId, NULL);
	pthread_join(CollisionAvoidanceTaskId, NULL);
	return 0;
}
