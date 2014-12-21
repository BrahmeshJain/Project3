/***********************************************************************************************************
******A Not so advanced collision avoidance system using SPI Device Programming and Pulse measurement ******
***********************************************************************************************************/
Author: Brahmesh Jain
EmailId: Brahmesh.Jain@asu.edu
Document version : 1.0.0
Youtube link : https://www.youtube.com/watch?v=Q5JnKQ_3kMM
Note for evauating Task2 : 
1) Task 2 application is related to collision avoidance system of vehicles. This is achieved by displaying a
   moving car on the 8x8 matrix. Before that, to just test the display, a moving characters 'ESP' appears on
   the display.Then, the Car appears moving.
2) Car speed is controlled by the distance sensor. If an object is very close to the car, then the car
   automatically stops. The distance from the sensor for which the car is sentive is 300mm. To test this, you
   can placce your hand about 300mm away from the sensor, and check if the car automatically stops.
3) Once the car stops, it will drive very very slow till the object is removed. It will gain the original speed
   after some time (roughly 20secs)


The rest  of the read me is about this assignment, you may skip it ! :)

This zip file contains four source files.

1) main3_1.c is the user level application for task1. Which communicates with spidev to send spi messages to the
   8x8 LED matrix.
2) pulse.c is the driver for distance sensor. spi_led.c is the driver source for the 8x8 LED matrix display.
   main3_2.c is the user application to test the above two drivers

Apart from assignement requirement, there are few other specific policies that driver adhere to :

1) spi_led driver also implements the read function to read the status of the display. This will be helpful to 
  get to know the status of the previously sent display sequence.

2) Display pattern should be a uint8 2-D array Pattern[10][8] and sequence should be of uint16 or unsigned short
   type like Sequence[10][2]

3) Before loading spi_led driver, please remove the module 'spidev' it is already installed.

4) At important steps in the driver execution, drivers and application can print the messages if the macro #define DEBUG is 
   uncommented.

5) Finally steps to run the program on Intel Galielo Board :
   a) Load the SDK source of galileo y running : "source ~/SDK/environment-setup-i586-poky-linux"
   b) open terminal with root permission , run the command "make all" to compile the drivers
   c) Compile the tester(user application) program, "$CC main3_2.c -o main3_2 -lpthread"
   d) Compile the tester(user application) program for task1 with "$CC main3_1.c -o main3_1 -lpthread"
   e) Transfer all the files to the galielo board using secured copy
   f) Open Galileo's terminal using putty and Install the driver by running the command "modprobe spidev"
   g) run the user application with the command "./main3_1". Enjoy playing with the dog for next 30s :D
   h) Next you may install custom driver for Led display and Distance sensor by first removing "spidev"
   i) Install spi_led and pulse by running commands "insmod spi_led.ko" and "insmod pulse.ko"
   j) Run the second application by executing the command "./main3_2" and enjoy with the car for next 90s.
   k) to cleanup the generated files, run "make clean"

