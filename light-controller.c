#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>

// Pins
#define ON 0x10
#define OFF 0x08
#define DIM 0x02
#define PWM 0x01
#define SENSE 0x04

// EEPROM Addresses
#define OFF_TIME_ADDR    (const uint16_t *) 0x00 // Time to remain off in 0.768'ths of seconds (40000)
#define RAMP_TIME_ADDR 	 (const uint8_t *) 0x02 // Time in tens of seconds for wake fade (30)
#define FADE_TIME_ADDR   (const uint8_t *) 0x03 // Time to fade in tenths of secs (20)
#define DIM_LVL_ADDR     (const uint8_t *) 0x04 // Level at dim (100)
// 40 9C 1E 14 64

#define BOUNCE_OVF 15


// Globals

unsigned char dim_level;
unsigned int off_time;
unsigned int ramp_time;
unsigned char fade_time;


unsigned char current_level, state = 0;
unsigned char volatile target_level, bounce, sleep_count;
int volatile step_rate;
unsigned int volatile sleep_remaining;


// Clock cycle is 0.0000133 sec
// Overflow is 3ms

// Fade to the specified level over the specified time (in tenths of a second) 
void fade(unsigned char brightness, unsigned int time){
	TCCR0A |= 0x80; // Start PWM
	target_level = brightness;
	int diff = target_level-current_level;
	int ovfs = 29*time;
	step_rate = ovfs/diff;
	if(diff < 0)
		diff = -diff;
	// Round without using floating point numbers
	if(ovfs%diff >= diff/2){
		if(step_rate > 0)
			step_rate++;
		else
			step_rate--;
	}
}

ISR(PCINT0_vect){
	if(bounce == 0){
		bounce = BOUNCE_OVF;

		unsigned char level = current_level;
		if(~PINB & (DIM | OFF)){
			sleep_remaining = off_time;
			if(~PINB & DIM)
				level = dim_level;
			else if(~PINB & OFF)
				level = 0;
		} else if(~PINB & ON) {
			sleep_remaining = 0;
			level = 255;
		}
		fade(level, fade_time);
		
	}
}

// Overflow
ISR(TIM0_OVF_vect){
	// Debounce inputs
	if(bounce > 0)
		bounce--;
	
	// Handle sleep time
	sleep_count++;
	if(sleep_count == 0 && sleep_remaining > 0){
		sleep_remaining--;
		if(sleep_remaining == 0)
			fade(255, ramp_time);
	}

	// Handle fade routine
	state++;
	if(state%step_rate == 0 && current_level != target_level){
		if(current_level < target_level)
			current_level++;
		else
			current_level--;
		OCR0A = current_level;
		if(current_level == 0){
			TCCR0A &= ~(0x80); // Stop PWM
			PORTB &= ~(PWM);
		} else if(current_level == 255) {
			TCCR0A &= ~(0x80); // Stop PWM
			PORTB |= PWM;
		}
	}
		

}

// Setup the registers and read settings from EEPROM
int main(){
	// Read settings from EEPROM
	
	off_time 	= ((unsigned int )eeprom_read_word(OFF_TIME_ADDR ));
	ramp_time   = ((unsigned char )eeprom_read_byte(RAMP_TIME_ADDR))*(unsigned int)100;
	fade_time   = ((unsigned char )eeprom_read_byte(FADE_TIME_ADDR));
	dim_level	= ((unsigned char)eeprom_read_byte(DIM_LVL_ADDR));
	

	DDRB |= PWM; // Set SWITCH to output
	DDRB &= ~(ON | DIM | OFF | SENSE); // Set ON, OFF, DIM, SENSE to input
	PORTB |= (ON | DIM | OFF ); // Enable Pull Up resistors

	TIMSK0 |= 0x02; // Enable timer overflow interrupts

	GIMSK |= 0x20; // Enable Pin Change interrupts
	PCMSK |= 0x1A; // Interupt on 1, 3, 4

	// Enable PWM on OCR0A
	TCCR0B |= 0x03; // Divide clock by 64	
	TCCR0A |= 0x83; // Fast PWM
	current_level = 255;
	target_level = 255;
	OCR0A = 255; // Start lights at full

	sei(); // Enable global interrupts

	while(1);
}


