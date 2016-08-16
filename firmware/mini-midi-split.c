////////////////////////////////////////////////////////////
// 
//  //     /////  //////  /////  //////  /////  
//  //         // //   //     // //   //     // 
//  /////  ////// //   // ////// //   // ////// 
//  //  // //  // //   // //  // //   // //  // 
//  /////   ///// //   //  ///// //   //  ///// 
//
//   ///// /////  //   //  //		MIDI SPLITTER
//  //     //  // //       ///      BEAT INDICATOR FIRMWARE
//   ////  /////  //   //  //       SOURCEBOOST C
//      // //     //   //  //		PIC12F1822
//  /////  //      //  //   //		hotchk155/2016
//
//
// VERSION
// 1 	16aug16		initial release
//	
#define VERSION 1
////////////////////////////////////////////////////////////

//
// INCLUDE FILES
//
#include <system.h>

//
// PIC12F1822 MCU CONFIG
//

// 8MHz internal oscillator block, reset disabled
#pragma DATA _CONFIG1, _FOSC_INTOSC & _WDTE_OFF & _MCLRE_OFF &_CLKOUTEN_OFF
#pragma DATA _CONFIG2, _WRT_OFF & _PLLEN_OFF & _STVREN_ON & _BORV_19 & _LVP_OFF
#pragma CLOCK_FREQ 8000000

//
// CONSTANTS
//
#define P_CLKLED		porta.1
#define P_DATLED		porta.0
#define P_RUN			porta.2
#define P_DIV2			porta.4

// Timer settings
#define TIMER_0_INIT_SCALAR		5	// Timer 0 is an 8 bit timer counting at 250kHz

// LED flash durations
#define DAT_LED_TIMEOUT	1
#define CLK_LED_TIMEOUT	50

// Duration of pulses on clock/2 output
#define DIV2_TIMEOUT 15

typedef unsigned char byte;

// COUNTERS
volatile byte dat_timeout;
volatile byte clk_timeout;
volatile byte div2_timeout;
volatile byte clk_count;
volatile byte div2;

////////////////////////////////////////////////////////////
//
// INTERRUPT HANDLER 
//
////////////////////////////////////////////////////////////
void interrupt( void )
{
	//////////////////////////////////////////////////////////
	// SERIAL RX INTERRUPT
	if(pir1.5)
	{

		// get the byte
		byte b = rcreg;

		// flash the data LED
		P_DATLED = 1;
		dat_timeout = DAT_LED_TIMEOUT;
		
		// handle clock LED
		switch(b)
		{
			case 0xf8: // CLOCK
				if(!clk_count) {
					P_CLKLED = 1;	
					clk_timeout = CLK_LED_TIMEOUT;
					
					if(!div2) {
						P_DIV2 = 1;
						div2_timeout = DIV2_TIMEOUT;
					}
					div2 = !div2;
				}
				if(++clk_count >= 24) {
					clk_count = 0;
				}
				break;
			case 0xfa: // START
				clk_count = 0;
				P_RUN = 1;
				break;
			case 0xfb: // CONTINUE
				P_RUN = 1;
				break;
			case 0xfc: // STOP
				P_RUN = 0;
				break;
		}		
		pir1.5 = 0;
	}
	
	//////////////////////////////////////////////////////////
	// TIMER0 OVERFLOW
	// Timer 0 overflow is used to create a once per millisecond
	// signal for blinking LEDs etc
	if(intcon.2)
	{
		tmr0 = TIMER_0_INIT_SCALAR;
		if(dat_timeout) {
			if(--dat_timeout == 0) {
				P_DATLED = 0;
			}
		}
		if(clk_timeout) {
			if(--clk_timeout == 0) {
				P_CLKLED = 0;
			}
		}
		if(div2_timeout) {
			if(--div2_timeout == 0) {
				P_DIV2 = 0;
			}
		}
		intcon.2 = 0;
	}		
}

////////////////////////////////////////////////////////////
// INITIALISE SERIAL PORT FOR MIDI
void init_usart()
{
	pir1.1 = 0;		//TXIF 		
	pir1.5 = 0;		//RCIF
	
	pie1.1 = 0;		//TXIE 		no interrupts
	pie1.5 = 1;		//RCIE 		interrupt on receive
	
	baudcon.4 = 0;	// SCKP		synchronous bit polarity 
	baudcon.3 = 1;	// BRG16	enable 16 bit brg
	baudcon.1 = 0;	// WUE		wake up enable off
	baudcon.0 = 0;	// ABDEN	auto baud detect
		
	txsta.6 = 0;	// TX9		8 bit transmission
	txsta.5 = 0;	// TXEN		transmit enable
	txsta.4 = 0;	// SYNC		async mode
	txsta.3 = 0;	// SEDNB	break character
	txsta.2 = 0;	// BRGH		high baudrate 
	txsta.0 = 0;	// TX9D		bit 9

	rcsta.7 = 1;	// SPEN 	serial port enable
	rcsta.6 = 0;	// RX9 		8 bit operation
	rcsta.5 = 1;	// SREN 	enable receiver
	rcsta.4 = 1;	// CREN 	continuous receive enable
		
	spbrgh = 0;		// brg high byte
	spbrg = 15;		// brg low byte (31250)	
	
}

////////////////////////////////////////////////////////////
//
// MILLISECOND TIMER INITIALISATION
//
////////////////////////////////////////////////////////////
void init_timer() 
{
	// Configure timer 0 (controls systemticks)
	// 	timer 0 runs at 2MHz
	// 	prescaled 1/8 = 250kHz
	// 	rollover at 250 = 1kHz
	// 	1ms per rollover	
	option_reg.5 = 0; // timer 0 driven from instruction cycle clock
	option_reg.3 = 0; // timer 0 is prescaled
	option_reg.2 = 0; // }
	option_reg.1 = 1; // } 1/16 prescaler
	option_reg.0 = 0; // }
	intcon.5 = 1; 	  // enabled timer 0 interrrupt
	intcon.2 = 0;     // clear interrupt fired flag	
}

////////////////////////////////////////////////////////////
//
// MAIN ENTRY POINT
//
////////////////////////////////////////////////////////////
void main()
{ 
	// osc control / 8MHz / internal
	osccon = 0b01110010;
	
	// configure io
	trisa 	= 0b00100000;              	
	ansela 	= 0b00000000;
	porta	= 0b00000000;

	apfcon.7=1; // RX on RA5
	apfcon.2=1;	// TX on RA4

	P_RUN = 0;
	P_DIV2 = 0;
	
	// startup flash
	P_DATLED = 1;
	P_CLKLED = 1;
	delay_ms(200);
	P_DATLED = 0;
	P_CLKLED = 0;

	dat_timeout = 0;
	clk_timeout = 0;
	div2_timeout = 0;
	clk_count = 0;
	div2 = 0;

	init_timer();
	init_usart();
	
	// enable interrupts
	intcon.7 = 1;
	intcon.6 = 1;

	while(1);
}	
