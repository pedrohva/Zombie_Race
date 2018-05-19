/** ---------------------------------- INCLUDES ----------------------------------- **/
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <cpu_speed.h>
#include <graphics.h>
#include <sprite.h>
#include <lcd.h>
#include <macros.h>
#include <lcd_model.h>

/** ----------------------------------- GLOBALS ----------------------------------- **/
#define CPU_FREQ            (8000000.0) // The frequency of the CPU clock we set
#define LCD_MAX_CONTRAST    0x7F
#define TIMER0_PRESCALE     (256.0) // The prescale we used when setting up Timer0
#define DASHBOARD_BORDER_X  26

// The pin numbers for each switch (still need to manually find the port letter)
#define BUTTON_LEFT         6
#define BUTTON_RIGHT        5
#define STICK_CENTRE        0
#define STICK_LEFT          1
#define STICK_RIGHT         0
#define STICK_UP            1
#define STICK_DOWN          7

// Determines the effect of the speed on the objects
#define SPEED_FACTOR        (4.0)
#define SPEED_MIN           0      
#define SPEED_MAX           10
#define SPEED_OFFROAD_MAX   3

// Determines direction of movement of the road
#define ROAD_LEFT           0
#define ROAD_RIGHT          1
#define ROAD_STRAIGHT       2
// The minimum and maximum curve allowed for the road (see road_curve)
#define ROAD_CURVE_MIN      2
#define ROAD_CURVE_MAX      3
// How many steps the road can take before it has to change directions
#define ROAD_SECTION_MIN    15
#define ROAD_SECTION_MAX    35

// Controls - Used to check if any button/joystick has been activated (don't check PINs)
uint8_t button_left_state;
uint8_t button_right_state;
uint8_t stick_centre_state;
uint8_t stick_left_state;
uint8_t stick_right_state;
uint8_t stick_up_state;
uint8_t stick_down_state;

// Debounce
uint8_t button_left_history = 0;
uint8_t button_right_history = 0;
uint8_t stick_centre_history = 0;
uint8_t stick_left_history = 0;
uint8_t stick_right_history = 0;
uint8_t stick_up_history = 0;
uint8_t stick_down_history = 0;

// Game information
uint8_t condition;
double speed;
uint8_t fuel;
uint8_t distance;

// Game time control
uint16_t game_timer_counter;
uint8_t game_paused;
double time_paused;  // The time the game was paused

// The player
Sprite player;

// Road
uint8_t road[LCD_Y];            // The x-coordinates of each road piece
int road_width = 16;   
double road_y = 0.0;             // Used to decide when to move the road down (so it matches speed of player)
uint8_t road_counter;           // Counts how many steps the road has taken before being moved horizontally
uint8_t road_curve;             // This decides how many times the road must move before it is moved horizontally
int road_direction;
uint8_t road_section_length;    // How many steps the road has taken in the current length

/**
 * Holds information regarding what screen the player should be seeing right now. 
 * The state should only be changed through the function change_screen()
 **/
enum GameScreens {
	START_SCREEN,
	GAME_SCREEN,
	GAME_OVER_SCREEN,
	HIGHSCORE_SCREEN,
	EXIT_SCREEN
} game_screen;

// Game loop controls 
const double loop_freq = 60.0;
uint16_t loop_counter;

// Bitmaps
uint8_t car_image[] = {
    0b01100000,
    0b11110000,
    0b01100000,
    0b01100000,
    0b11110000,
};
int car_width = 4;
int car_height = 5;

/** ---------------------------- FUNCTION DECLARATIONS ---------------------------- **/
// Helper functions
double elapsed_time(uint16_t timer_counter);
bool in_bounds(double x, double y);

// Game loop functions
void update(void);
void draw(void);

// General draw functions
void draw_borders(void);
void draw_formatted(int x, int y, char * buffer, int buffer_size, const char * format, ...);

// Screen manager
void change_screen(int new_screen);

// START_SCREEN
void start_screen_update(void);
void start_screen_draw(void);

// GAME_SCREEN
void game_screen_update(void);
void game_screen_draw(void);
void dashboard_draw(void);
void game_screen_setup(void);

// Player location and movement functions
void player_car_setup(void);
void player_car_reset(void);
void player_car_move(double dx);

// Road functions
void road_step(void);

// Teensy functions
void teensy_setup(void);
void adc_init(void);
uint16_t adc_read(uint8_t channel);

/** ---------------------------------- FUNCTIONS ---------------------------------- **/
/**
 * The entry point for the program
 **/
int main(void) {
    // Set clock speed, contrast settings and initial register setups
    teensy_setup();

    // Initialise random number generator
    srand(100);

    // Go to the Splash Screen
    change_screen(START_SCREEN);

    // The minimum time we'll take to execute one iteration of the game loop
    double time_step = 1/loop_freq;

    // Start the main game loop
    loop_counter = 0;
    while(game_screen != EXIT_SCREEN) {
        // Time execution started
        double t = elapsed_time(loop_counter);
        // Update all of the relevant game logic (sprites, collision, input, etc)
        update();
        // Draw the current screen to the LCD
        draw();
        // Pause the thread for a bit to stop updating
        while(elapsed_time(loop_counter)-t < time_step) { } 
    }

    return 0;
}

/**
 * Returns how many seconds (accurate to the 1/100th second) since the player started
 * the game. 
 * Note that the time starts when the player switches to the game screen
 **/
double elapsed_time(uint16_t timer_counter) {
    double time = (timer_counter * 256.0 + TCNT0 ) * TIMER0_PRESCALE  / CPU_FREQ;
    return time;
}

/**
 * Checks if the given coordinate falls in bounds of the playable area
 **/
bool in_bounds(double x, double y) {
    if((x <= DASHBOARD_BORDER_X) || (x > LCD_X-1)) {
        return false;
    } else if((y <= 1) || (y > LCD_Y-1)) {
        return false;
    }

    return true;
}

/**
 * Update all of the relevant game logic (sprites, collision, input, etc)
 **/
void update(void) {
    // Update the game logic depending on the current screen
    switch(game_screen) {
        case START_SCREEN:
            start_screen_update();
            break;
        case GAME_SCREEN:
            game_screen_update();
            break;
        default:
            break;
    }
}

/**
 * Draw the current screen to the LCD
 **/
void draw(void) {
    clear_screen();

    // Draw the current screen
    switch(game_screen) {
        case START_SCREEN:
            start_screen_draw();
            break;
        case GAME_SCREEN:
            game_screen_draw();
            break;
        default:
            break;
    }

    draw_borders();
    show_screen();
}

/**
 * Draw lines around the edges of the LCD
 **/
void draw_borders(void) {
    draw_line(0,0,LCD_X-1,0, FG_COLOUR);
	draw_line(0,LCD_Y-1,LCD_X-1,LCD_Y-1, FG_COLOUR);
	draw_line(0,0,0,LCD_Y-1,FG_COLOUR);
	draw_line(LCD_X-1,0,LCD_X-1,LCD_Y-1,FG_COLOUR);
}

/**
 * Draws a variable to the LCD screen. 
 * 
 * Taken from Topic 11 adc_pwm_backlight (Author:  Lawrence Buckingham, Queensland University of Technology)
 **/
void draw_formatted(int x, int y, char * buffer, int buffer_size, const char * format, ...) {
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, buffer_size, format, args);
	draw_string(x, y, buffer, FG_COLOUR);
}

/**
 * Performs the setup required to change to a different screen then switches the game
 * to that screen.
 **/
void change_screen(int new_screen) {
	//purge_input_buffer();

	// Decide if we need to initiate the screen before switching to it
	switch(new_screen) {
		case GAME_SCREEN:
			game_screen_setup();
			break;
		default:
			break;
	}

	game_screen = new_screen;
}

/**
 * Will change to the Game screen if the user presses the left or right button
 **/
void start_screen_update(void) {
    // Determine what contrast to set the screen to depending on Pot 0
    int pot0 = adc_read(0);
    int LCD_contrast = (int)(pot0/1024.0 * LCD_MAX_CONTRAST);
    LCD_CMD(lcd_set_function, lcd_instr_extended);
    LCD_CMD(lcd_set_contrast, LCD_contrast);
    LCD_CMD(lcd_set_function, lcd_instr_basic);

    // Check if a button has been pressed and proceed to the game screen if it has
    if(button_left_state || button_right_state) {
        change_screen(GAME_SCREEN);
    }
}

/**
 * Displays basic information about the game
 **/
void start_screen_draw(void) {
    draw_string(13, 3, "Zombie Race", FG_COLOUR);
    draw_string(6, 30, "Pedro Alves", FG_COLOUR);
    draw_string(6, 38, "n9424342", FG_COLOUR);
}

/**
 * Updates logic relating to the main game
 **/
void game_screen_update(void) {
    // Deals with input that decides if should pause the game or not
    if(stick_centre_state) {
        game_paused ^= 1;
        if(game_paused) {
            time_paused = elapsed_time(game_timer_counter);
        }
    }

    if(!game_paused) {
        road_step();
        // Deals with input that controls horizontal movement
        if(stick_left_state) {
            player_car_move(-1.0);
        } else if(stick_right_state) {
            player_car_move(1.0);
        }

        // Accelerate or decelerate controls
        if(button_left_state) {
            // If breaking
            if(speed > 0) {
                // Calculate rate to decrease speed in order to go from 10 to 0 in 2 seconds
                double rate = (5.0) / loop_freq;
                speed -= 10*rate;
                // Protect against overshoot
                if(speed < 1) {
                    speed = 0;
                }
            }
        }else if(button_right_state) {
            // If accelerating
            if(speed < SPEED_MAX) {
                // Calculate rate to increase speed in order to go from 1 to 10 in 5 seconds
                double rate = (9.0/2.0) / loop_freq;
                speed += 10*rate;
                // Protect against overshoot
                if(speed > SPEED_MAX-1) {
                    speed = SPEED_MAX;
                }
            }
        }else { // If no button is being pressed
            // Decrease speed if above 1 or increase to 1 if below
            if(speed > 1) {
                double rate = (9.0/3.0) / loop_freq;
                speed -= 9.0*rate;
                // Protect against overshoot
                if(speed < 1) {
                    speed = 1;
                }
            }else {
                double rate = (1.0/2.5) / loop_freq;
                speed += 1.0*rate;
                // Protect against overshoot
                if(speed > 1) {
                    speed = 1;
                }
            }
        }
    }
}

/**
 * Draws all of the game objects (car, terrain, etc) and the UI to the LCD
 **/
void game_screen_draw(void) {
    dashboard_draw();

    // Draw the paused screen
    if(game_paused) {
        char buffer[80];
        draw_string(30, 2, "TIME:", FG_COLOUR);
        draw_formatted(30, 12, buffer, sizeof(buffer), "%.3f", time_paused);
        draw_string(30, 22, "DISTANCE:", FG_COLOUR);
        draw_formatted(30, 32, buffer, sizeof(buffer), "%d", distance);
    } else {
        // Draw the road
        for(int y=0; y<LCD_Y; y++) {
            draw_pixel(road[y], y, FG_COLOUR);
            draw_pixel(road[y]+road_width, y, FG_COLOUR);
        }
    }

    // Draw the player
    sprite_draw(&player);
}

/**
 * Draws the dashboard containing info about the current game
 **/
void dashboard_draw(void) {
    // Draw the border separating from the playable area
    draw_line(DASHBOARD_BORDER_X, 1, DASHBOARD_BORDER_X, LCD_Y-1, FG_COLOUR);

    // Draw the car's information
    char buffer[80];
    draw_string(2, 2, "H:", FG_COLOUR);
    draw_formatted(10, 2, buffer, sizeof(buffer), "%d", condition);
    draw_string(2, 12, "F:", FG_COLOUR);
    draw_formatted(10, 12, buffer, sizeof(buffer), "%d", fuel);
    draw_string(2, 22, "S:", FG_COLOUR);
    draw_formatted(11, 22, buffer, sizeof(buffer), "%.0f", speed);
}

/**
 * Performs the initial setup/reset of the game state so that the player can start the
 * game
 **/
void game_screen_setup(void) {
    game_paused = 0;
    
    // Setup the initial car information
    condition = 100;
    fuel = 100;
    speed = 10;
    distance = 0;

    // Reset the game time
    game_timer_counter = 0;

    // Setup the road
    int road_x = ((LCD_X-DASHBOARD_BORDER_X)/2) - (road_width/2) + DASHBOARD_BORDER_X - 1;
    for(int y=0; y<LCD_Y; y++) {
        road[y] = road_x;
    }
    road_counter = 0;
    road_curve = ROAD_CURVE_MIN;
    road_direction = ROAD_STRAIGHT;
    road_section_length = rand() % (ROAD_SECTION_MAX + 1 - ROAD_SECTION_MIN) + ROAD_SECTION_MIN;;
    
    // Setup the player
    player_car_setup();
}

/**
 * Place the player's car sprite in the middle of the road
 **/
void player_car_setup(void) {
    int y = LCD_Y - car_height - 2;
    int x = (road_width/2) + road[y] - (car_width/2) + 1;

    sprite_init(&player, x, y, car_width, car_height, car_image);
}

/**
 * Add the player to the middle of the road again
 **/
void player_car_reset(void) {

}

/**
 * Changes the horizontal displacement of the player's car.
 * Will take into account collisions and the speed of the car to modify the dx given
 **/
void player_car_move(double dx) {
    double dilation = (speed/SPEED_FACTOR) / 2;
    dx = dx * dilation;

    double x = player.x + dx;
    // Check if the player will still be bounded
    if(!in_bounds(x, player.y)) {
        dx = 0;
    } else if(!in_bounds(x+player.width, player.y)) {
        dx = 0;
    }

    player.x += dx;
}

/**
 * Steps the x-coordinate of the road down by 1 and creates a new road piece
 * at the top of the screen.
 **/
void road_step(void) {
    // Decide if the road should be moved
    bool step = false;
    road_y += (speed/SPEED_FACTOR);
    if(road_y >= 1.0) {
        step = true;
        road_y = 0.0;
    }

    if(step) {
        road_counter++;

        // The x-coordinate of the new road piece being added
        int x = road[0];
        int dx;
        
        // Decide which direction the new road piece will be placed
        switch(road_direction) {
            case ROAD_STRAIGHT:
                dx = 0;
                break;
            case ROAD_LEFT:
                dx = -1;
                break;
            case ROAD_RIGHT:
                dx = 1;
                break;
            default:
                dx = 0;
                break;
        }

        if((x+dx+road_width < LCD_X-1) && (x+dx > DASHBOARD_BORDER_X) && (road_counter > road_curve)) {
            road_counter = 0;
            x += dx;
        }
        
        // Remove the bottom road piece and shift everything down
        memmove(&road[1], &road[0], (LCD_Y-1)*sizeof(uint8_t));
        // Add the new piece of road
        road[0] = x;

        road_section_length--;
        // If it's time to switch directions (added another check in case of overflow)
        if((road_section_length == 0) || (road_section_length > ROAD_SECTION_MAX)) {
            road_direction = rand() % 2;    // After the initial road straight, we only want to have turns naturally
            road_curve = rand() % (ROAD_CURVE_MAX + 1 - ROAD_CURVE_MIN) + ROAD_CURVE_MIN;
            road_section_length = rand() % (ROAD_SECTION_MAX + 1 - ROAD_SECTION_MIN) + ROAD_SECTION_MIN;
            road_counter = 0;
        }
    }
}

/**
 * Set clock speed, contrast settings and initial register setups
 **/
void teensy_setup(void) {
    set_clock_speed(CPU_8MHz);
    lcd_init(LCD_DEFAULT_CONTRAST);

    // Setup the ADC that will be used for the potentionmeters
    adc_init();

    // Initialise Timer 0 which will be used for debouncing purposes 
    TCCR0A = 0x00;
    TCCR0B = 1<<CS02;
    TIMSK0 = 1<<TOIE0; 

    // Setup the buttons
    DDRF &= ~(1<<BUTTON_LEFT | 1<<BUTTON_RIGHT);

    // Setup the joystick
    DDRB &= ~(1<<STICK_CENTRE);

    // Enable interrupts
    sei();
} 

void adc_init(void) {
	// ADC Enable and pre-scaler of 128: ref table 24-5 in datasheet
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

uint16_t adc_read(uint8_t channel) {
	// Select AVcc voltage reference and pin combination.
	// Low 5 bits of channel spec go in ADMUX(MUX4:0)
	// 5th bit of channel spec goes in ADCSRB(MUX5).
	ADMUX = (channel & ((1 << 5) - 1)) | (1 << REFS0);
	ADCSRB = (channel & (1 << 5));

	// Start single conversion by setting ADSC bit in ADCSRA
	ADCSRA |= (1 << ADSC);

	// Wait for ADSC bit to clear, signalling conversion complete.
	while ( ADCSRA & (1 << ADSC) ) {}

	// Result now available.
	return ADC;
}

/** ------------------------------------- ISR ------------------------------------- **/
/**
 * Interrupt that processes Timer0 overflow. 
 * Used for debouncing and elapsed time control
 **/
ISR(TIMER0_OVF_vect) {
    // Increase the overflow counter in order to calculate how much time has elapsed
    if(!game_paused && (game_screen == GAME_SCREEN)) {
        game_timer_counter++;
    }

    // Increase the overflow counter to control the speed of the game loop
    loop_counter++;

    // Observe the same state 6 times before changing the control states
    uint8_t mask = 0b00111111;
    // The current state read from the pins for each control
    uint8_t b;

    // BUTTON_LEFT
    b = (PINF>>BUTTON_LEFT) & 0x01;
    button_left_history = ((button_left_history << 1) & mask) | b;
    if(button_left_history == 0) {
        button_left_state = 0;
    } else if(button_left_history == mask) {
        button_left_state = 1;
    }

    // BUTTON_RIGHT
    b = (PINF>>BUTTON_RIGHT) & 0x01;
    button_right_history = ((button_right_history << 1) & mask) | b;
    if(button_right_history == 0) {
        button_right_state = 0;
    } else if(button_right_history == mask) {
        button_right_state = 1;
    }

    // STICK_CENTRE
    b = (PINB>>STICK_CENTRE) & 0x01;
    stick_centre_history = ((stick_centre_history << 1) & mask) | b;
    if(stick_centre_history == 0) {
        stick_centre_state = 0;
    } else if(stick_centre_history == mask) {
        stick_centre_state = 1;
    }

    // STICK_LEFT
    b = (PINB>>STICK_LEFT) & 0x01;
    stick_left_history = ((stick_left_history << 1) & mask) | b;
    if(stick_left_history == 0) {
        stick_left_state = 0;
    } else if(stick_left_history == mask) {
        stick_left_state = 1;
    }

    // STICK_RIGHT
    b = (PIND>>STICK_RIGHT) & 0x01;
    stick_right_history = ((stick_right_history << 1) & mask) | b;
    if(stick_right_history == 0) {
        stick_right_state = 0;
    } else if(stick_right_history == mask) {
        stick_right_state = 1;
    }
}