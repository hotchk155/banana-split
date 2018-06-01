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
// 2 	09may18		16th note pulse clock	
// 3 	01jun18		Allow clock pulse rate control via Sysex
//	
#define VERSION 3
////////////////////////////////////////////////////////////

//
// INCLUDE FILES
//
#include <system.h>
#include <eeprom.h>

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
#define P_CLKLED		lata.1
#define P_DATLED		lata.0
#define P_RUN			lata.2
#define P_PULSE			lata.4

// Timer settings
#define TIMER_0_INIT_SCALAR		5	// Timer 0 is an 8 bit timer counting at 250kHz

// LED flash durations
#define DAT_LED_TIMEOUT	1
#define CLK_LED_TIMEOUT	50

// Special value identifies initialised eeprom data
#define MAGIC_COOKIE 0xA5

// Config defaults
#define DEFAULT_PULSE_DIV 12	// 24ppqn divider (eighth notes)
#define DEFAULT_PULSE_DUR 15	// approx pulse length (15 ms)

//
// TYPES
//
typedef unsigned char byte;

// COUNTERS
volatile byte dat_timeout;
volatile byte clk_timeout;
volatile byte pulse_timeout;
volatile byte clk_count;
volatile byte pulse_count;
volatile byte new_div;
volatile byte new_dur;
volatile byte sysex_index;
byte pulse_div;
byte pulse_dur;

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
				}				
				if(++clk_count >= 24) {
					clk_count = 0;
				}
				
				if(!pulse_count) {
					P_PULSE = 1;
					pulse_timeout = pulse_dur;
				}
				if(++pulse_count >= pulse_div) {
					pulse_count = 0;
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
			default: // WATCH FOR CONFIG SYSEX
				switch(sysex_index) {
					case 0: if(b == 0xF0) sysex_index = 1; break;
					case 1:	if(b == 0x00) ++sysex_index; else sysex_index = 0; break;
					case 2:	if(b == 0x7F) ++sysex_index; else sysex_index = 0; break;
					case 3:	if(b == 0x18) ++sysex_index; else sysex_index = 0; break;
					case 4:	if(b == 0x0a) ++sysex_index; else sysex_index = 0; break;
					case 5:	if(b == 0x05) ++sysex_index; else sysex_index = 0; break;
					case 6:	new_div = b; ++sysex_index; break; 
					case 7:	new_dur = b; ++sysex_index; break; 
					case 8:	if(b == 0xF7) {					
						if(new_div && new_dur) {									
							// store the new divider and flash the LEDs. 
							// Do not return - device must be reset						
							eeprom_write(1, new_div);
							eeprom_write(2, new_dur);
							while(1) {
								P_DATLED = 0;
								P_CLKLED = 1;
								delay_ms(100);
								P_DATLED = 1;
								P_CLKLED = 0;
								delay_ms(100);			
							}
						}
					}
					// fall thru
					default:
						sysex_index = 0; 
						break;
					}
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
		if(pulse_timeout) {
			if(--pulse_timeout == 0) {
				P_PULSE = 0;
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

	// Read the divider value for the beat clock
	// - default is eighth beat (12 x ppqn)
	pulse_div = DEFAULT_PULSE_DIV;
	pulse_dur = DEFAULT_PULSE_DUR;
	if(eeprom_read(0) != MAGIC_COOKIE) {
		eeprom_write(1, pulse_div);
		eeprom_write(2, pulse_dur);
		eeprom_write(0, MAGIC_COOKIE);
	}
	else {
		pulse_div = eeprom_read(1);
		pulse_dur = eeprom_read(2
		);
	}

	apfcon.7=1; // RX on RA5
	apfcon.2=1;	// TX on RA4

	P_RUN = 0;
	P_PULSE = 0;

	
	// startup flash
	P_DATLED = 1;
	P_CLKLED = 1;
	delay_ms(200);
	P_DATLED = 0;
	P_CLKLED = 0;

	dat_timeout = 0;
	clk_timeout = 0;
	pulse_timeout = 0;
	clk_count = 0;
	pulse_count = 0;
	sysex_index = 0;
	
	init_timer();
	init_usart();
	
	// enable interrupts
	intcon.7 = 1;
	intcon.6 = 1;

	while(1);
}	
