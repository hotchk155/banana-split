
//
// HEADER FILES
//
#include <system.h>
#include <rand.h>
#include <eeprom.h>

#define BEAT_DELAY 30
// PIC CONFIG BITS
// - RESET INPUT DISABLED
// - WATCHDOG TIMER OFF
// - INTERNAL OSC
#pragma DATA _CONFIG1, _FOSC_INTOSC & _WDTE_OFF & _MCLRE_OFF &_CLKOUTEN_OFF
#pragma DATA _CONFIG2, _WRT_OFF & _PLLEN_OFF & _STVREN_ON & _BORV_19 & _LVP_OFF
#pragma CLOCK_FREQ 16000000

#define TRIS_A 0b11111011
#define TRIS_C 0b11110011

//
// TYPE DEFS
//
typedef unsigned char byte;

//
// MACRO DEFS
//

#define P_LED1		latc.2 	// MIDI input red LED
#define P_LED2		latc.3 	// Blue LED
#define P_LED3		lata.2 	// Yellow LED
#define P_SWITCH	porta.5

#define P_RED		P_LED1
#define P_BLUE		P_LED2
#define P_YELLOW	P_LED3


//#define P_LEDR 	lata.0
//#define P_LEDG  lata.1
//#define P_LEDY  lata.2
//#define P_LEDB  latc.0
//#define P_BUTTON  portc.3

// MIDI beat clock messages
#define MIDI_SYNCH_TICK     	0xf8
#define MIDI_SYNCH_START    	0xfa
#define MIDI_SYNCH_CONTINUE 	0xfb
#define MIDI_SYNCH_STOP     	0xfc


#define TIMER_0_INIT_SCALAR		5	// Timer 0 is an 8 bit timer counting at 250kHz
									// using this init scalar means that rollover
									// interrupt fires once per ms

// Tempo defs
#define BPM_MIN					30
#define BPM_MAX					300
#define BPM_DEFAULT				120

// EEPROM usage
#define EEPROM_ADDR_MAGIC_COOKIE 9
#define EEPROM_ADDR_OPTIONS	10
#define EEPROM_MAGIC_COOKIE 0xC5

// Menu size
#define MENU_SIZE 6 

//
// GLOBAL DATA
//

// timer stuff
volatile byte tick_flag = 0;
volatile unsigned int timer_init_scalar = 0;
volatile unsigned long systemTicks = 0; // each system tick is 1ms

// define the buffer used to receive MIDI input
#define SZ_RXBUFFER 20
volatile byte rxBuffer[SZ_RXBUFFER];
volatile byte rxHead = 0;
volatile byte rxTail = 0;


// BPM setting
int _bpm = 0;

// Count of MIDI clock ticks
byte tickCount = 0;

////////////////////////////////////////////////////////////
// INTERRUPT HANDLER CALLED WHEN CHARACTER RECEIVED AT 
// SERIAL PORT OR WHEN TIMER 1 OVERLOWS
void interrupt( void )
{
	// timer 0 rollover ISR. Maintains the count of 
	// "system ticks" that we use for key debounce etc
	if(intcon.2)
	{
		tmr0 = TIMER_0_INIT_SCALAR;
		systemTicks++;
		intcon.2 = 0;
	}

	// timer 1 rollover ISR. Responsible for timing
	// the tempo of the MIDI clock
	if(pir1.0)
	{
		tmr1l=(timer_init_scalar & 0xff); 
		tmr1h=(timer_init_scalar>>8); 
		tick_flag = 1;
		pir1.0 = 0;
	}
		
	// serial rx ISR
	if(pir1.5)
	{	
		// get the byte
		byte b = rcreg;
		
		// calculate next buffer head
		byte nextHead = (rxHead + 1);
		if(nextHead >= SZ_RXBUFFER) 
		{
			nextHead -= SZ_RXBUFFER;
		}
		
		// if buffer is not full
		if(nextHead != rxTail)
		{
			// store the byte
			rxBuffer[rxHead] = b;
			rxHead = nextHead;
		}		
	}
}



////////////////////////////////////////////////////////////
// INITIALISE SERIAL PORT FOR MIDI
void initUSART()
{
	pir1.1 = 1;		//TXIF 		
	pir1.5 = 0;		//RCIF
	
	pie1.1 = 0;		//TXIE 		no interrupts
	pie1.5 = 1;		//RCIE 		enable
	
	baudcon.4 = 0;	// SCKP		synchronous bit polarity 
	baudcon.3 = 1;	// BRG16	enable 16 bit brg
	baudcon.1 = 0;	// WUE		wake up enable off
	baudcon.0 = 0;	// ABDEN	auto baud detect
		
	txsta.6 = 0;	// TX9		8 bit transmission
	txsta.5 = 1;	// TXEN		transmit enable
	txsta.4 = 0;	// SYNC		async mode
	txsta.3 = 0;	// SEDNB	break character
	txsta.2 = 0;	// BRGH		high baudrate 
	txsta.0 = 0;	// TX9D		bit 9

	rcsta.7 = 1;	// SPEN 	serial port enable
	rcsta.6 = 0;	// RX9 		8 bit operation
	rcsta.5 = 1;	// SREN 	enable receiver
	rcsta.4 = 1;	// CREN 	continuous receive enable
		
	spbrgh = 0;		// brg high byte
	spbrg = 31;		// brg low byte (31250)	
	
}

////////////////////////////////////////////////////////////
// SEND A BYTE ON SERIAL PORT
void send(byte c)
{
	txreg = c;
	while(!txsta.1);
}

byte read_midi() {

	// buffer overrun error?
	if(rcsta.1)
	{
		rcsta.4 = 0;
		rcsta.4 = 1;
	}
	systemTicks = 0;
	while(rxHead == rxTail && systemTicks < 200);
	if(systemTicks == 200)
		return 0;
		
	// read the character out of buffer
	byte q = rxBuffer[rxTail];
	if(++rxTail >= SZ_RXBUFFER) 
		rxTail -= SZ_RXBUFFER;

	return q;
}

byte test_midi(byte q) {
	send(q);
	if(read_midi() == q) 
		return 1;
	return 0;
}

byte beat = 0;
////////////////////////////////////////////////////////////
// RUN MIDI THRU
byte do_test()
{
	rxHead = rxTail;

	if(!test_midi(MIDI_SYNCH_START))
		return 0;
	beat = 0;
	
	for(int i=0; i<80; i++)
	{		
		P_BLUE = !beat;
		if(++beat == 24) beat = 0;
		if(!test_midi(MIDI_SYNCH_TICK))
			return 0;
		if(!test_midi(0x90))
			return 0;
		if(!test_midi(i))
			return 0;
		if(!test_midi(0x7F))
			return 0;
		delay_ms(BEAT_DELAY);
		if(!test_midi(0x80))
			return 0;
		if(!test_midi(i))
			return 0;
		if(!test_midi(0x00))
			return 0;
	}		
	return 1;
}

////////////////////////////////////////////////////////////
// SETUP THE TIMER FOR A SPECIFIC BPM
void setBPM(int b)
{
	if(b < BPM_MIN)
		b = BPM_MIN;
	else if(b > BPM_MAX)
		b = BPM_MAX;
	_bpm = b;
/*
	beats per second = bpm / 60 
	midi ticks per second = 24 * (bpm / 60)	
	timer counts per MIDI tick = (timer counts per second)/(midi ticks per second)
		= (timer counts per second)/(24 * (bpm / 60))
		= (timer counts per second/24)/(bpm / 60)
		= 60 * (timer counts per second/24)/bpm
	timer init scalar = 65536 - timer counts per MIDI tick	
*/
	#define TIMER_COUNTS_PER_SECOND (unsigned long)500000
	unsigned long x = (60 * TIMER_COUNTS_PER_SECOND)/24;
	x = x / _bpm;
	timer_init_scalar = 65535 - x;
}


#define P_LED1		latc.2 	// MIDI input red LED
#define P_LED2		latc.3 	// Blue LED
#define P_LED3		lata.2 	// Yellow LED
#define P_SWITCH	porta.5

////////////////////////////////////////////////////////////
// MAIN
void main()
{ 
	
	// osc control / 16MHz / internal
	osccon = 0b01111010;
	
	// configure io
	trisa = TRIS_A;              	
    trisc = TRIS_C;              
	ansela = 0b00000000;
	anselc = 0b00000000;
	porta=0;
	portc=0;
	wpua.5=1;
	option_reg.7=0	;
		
	// initialise MIDI comms
	initUSART();

	// setup default BPM
	setBPM(BPM_DEFAULT);

	// Configure timer 1 (controls tempo)
	// Input 4MHz
	// Prescaled to 500KHz
	tmr1l = 0;	 // reset timer count register
	tmr1h = 0;
	t1con.7 = 0; // } Fosc/4 rate
	t1con.6 = 0; // }
	t1con.5 = 1; // } 1:8 prescale
	t1con.4 = 1; // }
	t1con.0 = 1; // timer 1 on
	pie1.0 = 1;  // timer 1 interrupt enable
	
	// Configure timer 0 (controls systemticks)
	// 	timer 0 runs at 4MHz
	// 	prescaled 1/16 = 250kHz
	// 	rollover at 250 = 1kHz
	// 	1ms per rollover	
	option_reg.5 = 0; // timer 0 driven from instruction cycle clock
	option_reg.3 = 0; // timer 0 is prescaled
	option_reg.2 = 0; // }
	option_reg.1 = 1; // } 1/16 prescaler
	option_reg.0 = 1; // }
	intcon.5 = 1; 	  // enabled timer 0 interrrupt
	intcon.2 = 0;     // clear interrupt fired flag
	
	// enable interrupts	
	intcon.7 = 1; //GIE
	intcon.6 = 1; //PEIE
	
	delay_ms(200);
	// App loop
	for(;;)
	{	
		P_YELLOW = 0;
		while(P_SWITCH) {
			P_BLUE = !beat;
			if(++beat == 24) beat = 0;
			send(MIDI_SYNCH_TICK);			
			delay_ms(BEAT_DELAY);
		}		
		P_RED = 1;
		byte result = do_test();
		P_RED = 0;
		if(result) {
				P_BLUE=1;
				P_YELLOW=1;
				delay_s(1);
				P_BLUE=1;
				P_YELLOW=1;
		}
		else {
			for(int i=0; i<3; ++i) {
				P_BLUE = 0;
				P_YELLOW=1;
				delay_s(1);
				P_YELLOW=0;
				delay_ms(200);
			}
		}
	}
}