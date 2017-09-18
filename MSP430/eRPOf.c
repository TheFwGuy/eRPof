/**
 *  @file eRPOf.c 
 *  Copyright (c) 2017 - Stefano Bodini
 *
 *  This code is designed to act as On/Off pushbutton to correctly power up and power down 
 *  a Raspberry Pi
 *  The board is a LaunchPad board, using a MSP430F2012 and the code is compiled using MSPGCC 4.4.6
 *  using the -mmcu=msp430f2012  parameter
 */
#include <msp430.h>
#include <legacymsp430.h>

/*
 *  Defines for I/O
 *   P1.0  -> Power LED  (OUTPUT - red LED)
 *   P1.1  -> Shutdown signal (OUTPUT)
 *   P1.2  -> Power Relay (OUTPUT)
 *   P1.3  -> Pushbutton (INPUT)
 *   P1.4  -> Shutdown confirmation (INPUT)
 *   P1.5  -> not used - input
 *   P1.6  -> Debug LED (OUTPUT)
 *   P1.7  -> Generic Debug  (OUTPUT)
 */
#define     RPOF_PORT      P1OUT
#define     PB_MASK        0x18  /* 0001 1000 */

#define		PORTIO		0xC7		/*  I/O defines 
											 *  0 - input
											 *  1 - output
											 *  1100 0111
											 */

#define		LED			BIT0
#define		SHTDOUT		BIT1
#define		RELAY			BIT2
#define		PUSHBTN		BIT3
#define		SHTDIN		BIT4
#define		LED_DBG		BIT6
#define		GEN_DBG		BIT7

#define		IDLE					0
#define		POWERON_START		1
#define		POWERON_WAIT		2
#define		POWERON				3
#define		POWEROFF_START		4
#define		POWEROFF_WAIT		5
#define		POWEROFF				6

#define     TMRVALUE          160    	/* Timer will generate a interrupt every 10ms */
#define		FLASHTIME			50000		/* Value to obtain .5 sec from the 10 ms timer */
#define		PRESCALER			50000		/* Value to obtain .5 sec from the 10 ms timer */
#define		TURN_OFF_TIME		8			/* 4 seconds (8) */
#define		SAFETY_STOP_TIME	200		/* 100 seconds (200) */
#define		SAFETY_START_TIME 200		/* 100 seconds (200) */

#define		OFF				0
#define 		ON 				1
#define		FLASH				2
/*
 *  Functions prototypes
 */
unsigned char readPushbutton();
unsigned char isRaspberryRunning();
void LEDPower();
void powerRaspiOff();
void Relay();
 
/*
 *  Global variables
 */
unsigned char StateMachine = IDLE;
unsigned char LEDStatus    = OFF;
unsigned char RelayStatus  = OFF;
int TimeFlash              = 0;
int PrescalerTime          = 0;
int OFFTimer               = 0;

int main(void)
{
	unsigned char buttonOnOff;
	
   WDTCTL = WDTPW + WDTHOLD;	  /* Stop WDT */
   /*
    *  Halt the watchdog timer
    *  According to the datasheet the watchdog timer
    *  starts automatically after powerup. It must be
    *  configured or halted at the beginning of code
    *  execution to avoid a system reset. Furthermore,
    *  the watchdog timer register (WDTCTL) is
    *  password protected, and requires the upper byte
    *  during write operations to be 0x5A, which is the
    *  value associated with WDTPW.
    */

   /*
    *  Set DCO
    */
   DCOCTL  = CALDCO_16MHZ;
   BCSCTL1 = CALBC1_16MHZ;       /* Set up 16 Mhz using internal calibration value */

   /*
    *  Set I/O port
    *  CAUTION ! Since are used the internal pull up resistors, remember
    *  to force the corresponded PxOUT bits, otherwise the internal resistor
    *  is not connected !!
    */
   P1DIR = PORTIO;              /* Set up port 1 for input and output */
   P1SEL = 0;                   /* Set all I/O */
   P1OUT = 0;                   /* Force output to zero and input pins to 1 (for pull-up resistors) */
   
   /*
    *  Set Timer
    */
   TACTL = TASSEL_2 + MC_1;    /* Uses SMCLK, count in up mode */
   TACCTL0 = CCIE;             /* Use TACCR0 to generate interrupt */
   TACCR0 = TMRVALUE;          /* Approx .01 ms */

   /*  NORMAL MODE */
   TACCTL0 &= ~0x0080;         /* Disable Out0 */

   _BIS_SR(GIE);               /* Enable interrupt */
   eint();	//Enable global interrupts

   while(ON)
   {
		LEDPower();								/* Handle Power LED status */
		Relay();									/* Handle Relay */
		buttonOnOff = readPushbutton();	/* Read pushbutton */

		switch(StateMachine)
		{
			case IDLE:
				if(buttonOnOff == ON)
				{
					StateMachine = POWERON_START;
//					RPOF_PORT |= LED_DBG;	/* Turn ON debug LED */
				}
				break;
			
			case POWERON_START:
				if(buttonOnOff == OFF)	/* Wait for the release of the pushbutton */
				{
					LEDStatus    = FLASH;		/* Turn FLASHING pushbutton LED */
					RelayStatus  = ON;			/* Turn ON Relay */
					OFFTimer     = SAFETY_START_TIME;	/* Start safety timer */

					StateMachine = POWERON_WAIT;
//					RPOF_PORT &= ~LED_DBG;	/* Turn OFF debug LED */
				}
				break;

			case POWERON_WAIT:
				if(OFFTimer == 0)
				{
					/* 
					 *  Safety start timer expired !
					 *  The Raspberry has not started ! Turned it OFF
					 */
					powerRaspiOff();
					StateMachine = IDLE;
				} 
				else if(isRaspberryRunning() )
				{
					/*
					 *  Raspberry Pi started !
					 */
					OFFTimer     = 0;				/* Stop timer */
					LEDStatus    = ON;			
					StateMachine = POWERON;
//					RPOF_PORT |= LED_DBG;	/* Turn ON debug LED */
				}
				break;
				
			case POWERON:
				if(buttonOnOff == ON)
				{
					StateMachine = POWEROFF_START;
//					RPOF_PORT |= LED_DBG;	/* Turn ON debug LED */
				}
				break;

			case POWEROFF_START:
				if(buttonOnOff == OFF)	/* Wait for the release of the pushbutton */
				{
					LEDStatus    = FLASH;			
					RPOF_PORT    |= SHTDOUT;		/* Signal to start shutdown to Raspberry Pi */
					OFFTimer     = SAFETY_STOP_TIME;	/* Start safety OFF timer */
					
					StateMachine = POWEROFF_WAIT;
//					RPOF_PORT &= ~LED_DBG;	/* Turn OFF debug LED */
				}
				break;

			case POWEROFF_WAIT:
				/*
			    *  Wait for safety timer expiration or signal indicating
			    *  the end of the shutdown
			    */
				if(OFFTimer == 0)
				{
					powerRaspiOff();
					StateMachine = IDLE;
				}
				else if(!isRaspberryRunning())
				{
				   /*
					 *  Raspberry Pi shutdown completed !
					 *  Wait few seconds before to remove the power
					 */
					OFFTimer     = TURN_OFF_TIME;	/* Start turning OFF timer */
					StateMachine = POWEROFF;
				}
				break;

			case POWEROFF:
				/*
			    *  Wait for OFF timer expiration 
			    */
				if(OFFTimer == 0)
				{
					powerRaspiOff();
					StateMachine = IDLE;
				}
				break;
				
			default:
				StateMachine = IDLE;
				break;
		}
	}
}

/*
 *  Read pushbutton
 *  Pushbutton OFF - reading 1
 *  Pushbutton ON  - reading 0
 *
 *  Return 1 if the pushbutton is pressed, 0 otherwise
 */ 
unsigned char readPushbutton()
{
	unsigned short count;
	
	if(!(P1IN & PUSHBTN))
	{
		for(count=0; count< 2000; count++);	/* Small delay */

		if(!(P1IN & PUSHBTN)) /* Re-read */
		{
			return(ON);
		}
	}

	return(OFF);
}

/*
 *  Read shutdwon feedback
 *  Raspberry Pi running - expect signal 1
 *  Raspberry Pi after shutdown - expect signal 0
 *
 *  Return 1 if the Raspberry Pi is running - 0 otherwise
 */ 
unsigned char isRaspberryRunning()
{
	unsigned short count;
	
	if((P1IN & SHTDIN) == 0)
	{
		for(count=0; count< 2000; count++);	/* Small delay */

		if((P1IN & SHTDIN) == 0)	/* Re-read */
		{
			return(OFF);
		}
	}

	return(ON);
}

/*
 *  Handle LED state
 */ 
void LEDPower()
{
   static unsigned char shadowLEDStatus = 0xFF;
	
	/* Write on the port only if there is a change in the status */

	if(shadowLEDStatus == LEDStatus && TimeFlash)
		return;
	
   switch(LEDStatus)
	{
		case ON:
			RPOF_PORT |= LED;
			break;

		case OFF:
			RPOF_PORT &= ~LED;
			break;
		
		case FLASH:
			if(TimeFlash == 0)
			{
				RPOF_PORT ^= LED;
				TimeFlash = FLASHTIME;
			}
			break;
		
		default:
			LEDStatus = OFF;
			break;
	}
	
	shadowLEDStatus = LEDStatus;
}

/*
 *  Control the Relay
 */ 
void Relay()
{
   static unsigned char shadowRelayStatus = 0xFF;
	
	/* Write on the port only if there is a change in the status */

	if(shadowRelayStatus == RelayStatus)
		return;
	
   switch(RelayStatus)
	{
		case ON:
			RPOF_PORT |= RELAY;
//			RPOF_PORT |= LED_DBG;	/* Turn ON debug LED */
			break;

		case OFF:
			RPOF_PORT &= ~RELAY;
//			RPOF_PORT &= ~LED_DBG;	/* Turn ON debug LED */
			break;
			
		default:
			RelayStatus = OFF;
			break;
	}
	shadowRelayStatus = RelayStatus;
}

/*
 *  Power OFF the Raspberry Pi
 */ 
void powerRaspiOff()
{
	LEDStatus    = OFF;			/* Turn OFF pushbutton LED */
	RelayStatus  = OFF;			/* Turn OFF Relay */
	RPOF_PORT    &= ~SHTDOUT;	/* Remove signal to start shutdown to Raspberry Pi */
	OFFTimer     = 0;				/* Stop Timer */
}

/*
 *  Timer interrupt - every .01 s (10 ms)
 */ 
interrupt(TIMERA0_VECTOR) TIMERA0_ISR(void)
{
	/* LED flash timer */
   if(TimeFlash)
		TimeFlash--;

	/* Turning OFF everything timer */
	
	if(OFFTimer)
	{
//		RPOF_PORT ^= GEN_DBG;		/* Debug */
		
		if(PrescalerTime)
			PrescalerTime--;
		else
		{
			PrescalerTime = PRESCALER;

			if(OFFTimer)
				OFFTimer--;
		}
	}
}


