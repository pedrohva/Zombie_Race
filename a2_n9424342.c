/***********************************************************************************/
/* INCLUDES                                                                        */
/***********************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <cpu_speed.h>
#include <graphics.h>
#include <sprite.h>
#include <lcd.h>
#include <macros.h>
#include <lcd_model.h>
#include "usb_serial.h"

/***********************************************************************************/
/* GLOBALS                                                                         */
/***********************************************************************************/
#define CPU_FREQ            (8000000.0) // The frequency of the CPU clock we set
#define LCD_MAX_CONTRAST    0x7F
#define TIMER0_PRESCALE     (256.0)     // The prescale we used when setting up Timer0
#define TIMER1_PRESCALE     (1024.0)    // The prescale we used when setting up Timer3
#define TIMER1_FREQ         7812
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
#define SPEED_THRESH        8
#define SPEED_FACTOR        8
#define SPEED_MIN           0      
#define SPEED_MAX           10
#define SPEED_OFFROAD_MAX   3

// Determines direction of movement of the road
#define ROAD_LEFT           0
#define ROAD_RIGHT          1
#define ROAD_STRAIGHT       2
// The minimum and maximum curve allowed for the road (see road_curve)
#define ROAD_CURVE_MIN      1
#define ROAD_CURVE_MAX      3
// How many steps the road can take before it has to change directions
#define ROAD_SECTION_MIN    15
#define ROAD_SECTION_MAX    35

// The rate at which the fuel decreases
#define FUEL_FACTOR         3
#define FUEL_MAX            100
#define FUEL_STATION_MIN    140      // The minimum distance the fuel station can spawn
#define FUEL_STAION_MAX     180

// The different types of obstacles
#define TERRAIN             0
#define HAZARD              1

// The different terrain types
#define NUM_TERRAIN         10
#define NUM_TERRAIN_TYPES   2
#define TERRAIN_TREE        0
#define TERRAIN_SIGN        1

// The different hazard types
#define NUM_HAZARD          2
#define NUM_HAZARD_TYPES    2
#define HAZARD_TRIANGLE     0
#define HAZARD_SPIKE        1

// The chance each step that a hazard that is out of bounds spawns again
#define HAZARD_SPAWN_CHANCE 15

// Controls - Used to check if any button/joystick has been activated (don't check PINs)
uint8_t button_left_state;
uint8_t prev_button_left_state = 0;
uint8_t button_right_state;
uint8_t prev_button_right_state = 0;
uint8_t stick_centre_state;
uint8_t prev_stick_centre_state = 0;
uint8_t stick_left_state;
uint8_t prev_stick_left_state = 0;
uint8_t stick_right_state;
uint8_t prev_stick_right_state = 0;
uint8_t stick_up_state;
uint8_t prev_stick_up_state = 0;
uint8_t stick_down_state;
uint8_t prev_stick_down_state = 0;

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
double fuel;
double speed;
uint8_t distance;
uint8_t finish_line;
uint8_t distance_counter;
double speed_counter;
bool game_over_loss;

// Game time control
uint8_t step = 0;
uint16_t game_timer_counter;
uint8_t game_paused;
double time_paused;     // The time the game was paused. Used to avoid the decimal places changing constantly
                        // when elapsed_time is called during the pause screen due to double division inaccuracies 

// The player
Sprite player;

// Road
uint8_t road[LCD_Y];            // The x-coordinates of each road piece
uint8_t road_width = 16;   
uint8_t road_counter;           // Counts how many steps the road has taken before being moved horizontally
uint8_t road_curve;             // This decides how many times the road must move before it is moved horizontally
uint8_t road_direction;
uint8_t road_section_length;    // How many steps the road has taken in the current length

// Terrain
Sprite terrain[NUM_TERRAIN];                // Array that contains all of the terrain objects in the game
Sprite terrain_image[NUM_TERRAIN_TYPES];    // Contains sprite information about each type of terrain

// Hazards
Sprite hazard[NUM_HAZARD];
Sprite hazard_image[NUM_HAZARD_TYPES];

// Fuel station
Sprite fuel_station;
int fuel_station_counter;   // Counts down to when the fuel station can spawn again
bool refuelling;

/**
 * Holds information regarding what screen the player should be seeing right now. 
 * The state should only be changed through the function change_screen()
 **/
enum GameScreens {
	START_SCREEN = 1,
	GAME_SCREEN = 2,
	GAMEOVER_SCREEN = 3,
} game_screen;

/**
 * Commands used for USB communications
 **/
enum USBCommand {
    SAVE = 1,
    LOAD = 2,
    DEBUG = 3
};

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

uint8_t terrain_tree_image[] = {
    0b00111100,
    0b01111110,
    0b11111111,
    0b00011000,
    0b00011000,
};
int terrain_tree_width = 8;
int terrain_tree_height = 5;

uint8_t terrain_sign_image[] = {
    0b01010000,
    0b11111000,
    0b11111000,
    0b01010000,
    0b00000000,
};
int terrain_sign_width = 5;
int terrain_sign_height = 4;

uint8_t hazard_triangle_image[] = {
    0b00100000,
    0b01110000,
    0b11111000,
};
int hazard_triangle_width = 5;
int hazard_triangle_height = 3;

uint8_t hazard_spike_image[] = {
    0b10101000,
    0b11111000,
};
int hazard_spike_width = 5;
int hazard_spike_height = 2;

uint8_t fuel_station_image[] = {
    0b11111111,
    0b10000001,
    0b10000001,
    0b10000001,
    0b10000001,
    0b10000001,
    0b10000001,
    0b11111111,
};
int fuel_station_width = 8;
int fuel_station_height = 8;

/***********************************************************************************/
/* FUNCTION PROTOTYPES                                                             */
/***********************************************************************************/

// Helper functions
double elapsed_time(uint16_t timer_counter);
bool in_bounds(double x, double y);

// Game loop functions
void update(void);
void draw(void);

// General draw functions
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
void game_screen_step(void);
void game_screen_setup(void);

// GAMEOVER_SCREEN
void gameover_screen_update(void);
void gameover_screen_draw(void);

// Player location and movement functions
void player_car_setup(void);
void player_car_reset(void);
void player_car_move(int dx);
void player_speed_input(void);

// Road functions
void road_step(void);

// Obstacle functions
void terrain_image_setup(void);
void terrain_setup(void);
void terrain_reset(int index, int y_bot);
void terrain_step(void);
void hazard_image_setup(void);
void hazard_setup(void);
void hazard_reset(int index, int y_bot);
void hazard_step(void);

// Fuel functions
void fuel_station_reset(void);
void fuel_station_step(void);
void check_refuel(void);
void refuel(void);

// Collision detection
bool offroad(Sprite sprite);
bool check_collision(Sprite sprite);
bool check_sprite_collided(Sprite sprite1, Sprite sprite2);
bool check_sprite_collided_pixel(Sprite sprite1, Sprite sprite2);
void handle_collision(void);

// Save and load
void game_state_save(void);
void game_state_load(void);

// USB communication
void usb_send_message(enum USBCommand command, int line_num ,char * buffer, int buffer_size, const char * format, ...);

// Teensy functions
void teensy_setup(void);
void adc_init(void);
uint16_t adc_read(uint8_t channel);

/***********************************************************************************/
/* FUNCTIONS                                                                       */
/***********************************************************************************/

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
    while(1) {
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
        case GAMEOVER_SCREEN:
            gameover_screen_update();
            break;
        default:
            break;
    }

    // Determine what contrast to set the screen to depending on Pot 0
    int pot1 = adc_read(1);
    int LCD_contrast = (int)(pot1/1024.0 * LCD_MAX_CONTRAST);
    LCD_CMD(lcd_set_function, lcd_instr_extended);
    LCD_CMD(lcd_set_contrast, LCD_contrast);
    LCD_CMD(lcd_set_function, lcd_instr_basic);

    // Update all control states
    prev_button_left_state = button_left_state;
    prev_button_right_state = button_right_state;
    prev_stick_centre_state = stick_centre_state;
    prev_stick_left_state = stick_left_state;
    prev_stick_right_state = stick_right_state;
    prev_stick_up_state = stick_up_state;
    prev_stick_down_state = stick_down_state;
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
        case GAMEOVER_SCREEN:
            gameover_screen_draw();
            break;
        default:
            break;
    }

    show_screen();
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
	// Decide if we need to initiate the screen before switching to it
	switch(new_screen) {
		case GAME_SCREEN:
			game_screen_setup();
			break;
		default:
			break;
	}

	game_screen = new_screen;

    // Test: Splash screen
    //char buf[100];
    //usb_send_message(DEBUG, 4, buf, 100, "Timestep: %.3f\nSW2: %d\nSW3: %d\nScreen: %d\n%d\n", elapsed_time(game_timer_counter), button_left_state, button_right_state, game_screen, 0);
}

/**
 * Will change to the Game screen if the user presses the left or right button
 **/
void start_screen_update(void) {
    // Check if a button has been pressed and proceed to the game screen if it has
    int button_left_edge = (prev_button_left_state == 0) && (button_left_state != 0);
    int button_right_edge = (prev_button_right_state == 0) && (button_right_state != 0);
    if(button_left_edge || button_right_edge) {
        change_screen(GAME_SCREEN);
    }

    // Test: Splash screen
    //char buf[100];
    //usb_send_message(DEBUG, 4, buf, 100, "Timestep: %.3f\nSW2: %d\nSW3: %d\nScreen: %d\n%d\n", elapsed_time(game_timer_counter), button_left_state, button_right_state, game_screen, 0);
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
    // Deals with input that decides if should pause the game or not (only toggle on press not hold)
    if(((prev_stick_centre_state == 0) && (stick_centre_state != 0))) {
        game_paused ^= 1;
        if(game_paused) {
            time_paused = elapsed_time(game_timer_counter);
        }
    }

    if(!game_paused) {
        // Steps through all of the main game logic involving input, collisions, etc.
        if((speed_counter) > SPEED_THRESH) {
            step = false;
            speed_counter = 0;
            game_screen_step();
        }
        player_speed_input();
    } else {
        // Checks if the user wants to load or save the game
        if(((prev_stick_up_state == 0) && (stick_up_state != 0))) {
            game_state_save();
        }else if(((prev_stick_down_state == 0) && (stick_down_state != 0))) {
            game_state_load();
        }

        // Test: Paused View
        //char buf[100];
        //usb_send_message(DEBUG, 2, buf, 100, "Program time: %.3f\nGame time: %.3f\n%d\n", elapsed_time(loop_counter), time_paused, 0);
    }
}

/**
 * Draws all of the game objects (car, terrain, etc) and the UI to the LCD
 **/
void game_screen_draw(void) {
    dashboard_draw();
    // Draw the player
    sprite_draw(&player);

    // Draw the paused screen
    if(game_paused) {
        char buffer[80];
        draw_string(30, 2, "TIME:", FG_COLOUR);
        draw_formatted(30, 12, buffer, sizeof(buffer), "%.3f", time_paused);
        draw_string(30, 22, "DISTANCE:", FG_COLOUR);
        draw_formatted(30, 32, buffer, sizeof(buffer), "%d", distance);

        // Test: Paused View
        //usb_send_message(DEBUG, 2, buffer, 80, "Time step: %.3f\nDistance: %d\n%d\n", time_paused, distance, 0);

        // Test: Horizontal Movement
        //usb_send_message(DEBUG, 5, buffer, 80, "Time step: %.3f\nPlayer x: %.0f\nLeft: %d\nRight: %d\nSpeed: %.0f\n%d\n", time_paused, player.x, stick_left_state, stick_right_state, speed, 0);

        // Test: Acceleration and Speed
        //usb_send_message(DEBUG, 5, buffer, 80, "Time step: %.3f\nOffroad: %d\nLeft: %d\nRight: %d\nSpeed: %.0f\n%d\n", time_paused, offroad(player), button_left_state, button_right_state, speed, 0);

        // Test: Scenery and Obstacles
        //usb_send_message(DEBUG, 3, buffer, 80, "Time step: %.3f\nTerrain y: %.0f\nSpeed: %.0f\n%d\n", time_paused, terrain[1].y, speed, 0);

        // Test: Fuel Depot
        //usb_send_message(DEBUG, 3, buffer, 80, "Time step: %.3f\nDepot y: %.0f\nSpeed: %.0f\n%d\n", time_paused, fuel_station.y, speed, 0);

        // Test: Fuel
        //usb_send_message(DEBUG, 3, buffer, 80, "Time step: %.3f\nFuel: %.0f\nSpeed: %.0f\n%d\n", time_paused, fuel, speed, 0);

        // Test: Distance travelled
        //usb_send_message(DEBUG, 3, buffer, 80, "Time step: %.3f\nDistance: %d\nSpeed: %.0f\n%d\n", time_paused, distance, speed, 0);
    } else {
        // Draw the terrain
        for(int i=0; i<NUM_TERRAIN; i++) {
            sprite_draw(&terrain[i]);
        }

        // Draw the hazards
        for(int i=0; i<NUM_HAZARD; i++) {
            sprite_draw(&hazard[i]);
        }
        
        // Draw the road
        for(int y=0; y<LCD_Y; y++) {
            draw_pixel(road[y], y, FG_COLOUR);
            draw_pixel(road[y]+road_width, y, FG_COLOUR);
        }
        
        sprite_draw(&fuel_station);
    }
}

/**
 * Draws the dashboard containing info about the current game
 **/
void dashboard_draw(void) {
    // Draw the border separating from the playable area
    draw_line(DASHBOARD_BORDER_X, 0, DASHBOARD_BORDER_X, LCD_Y-1, FG_COLOUR);

    // Draw the car's information
    char buffer[80];
    draw_string(1, 2, "H:", FG_COLOUR);
    draw_formatted(10, 2, buffer, sizeof(buffer), "%d", condition);
    draw_string(1, 12, "F:", FG_COLOUR);
    draw_formatted(10, 12, buffer, sizeof(buffer), "%.0f", fuel);
    draw_string(1, 22, "S:", FG_COLOUR);
    draw_formatted(10, 22, buffer, sizeof(buffer), "%.0f", speed);

    // Warning lights
    if(refuelling) {
        draw_char(1, 32, 'R', FG_COLOUR);
    }

    // Test: Dashboard
    //char buf[100];
    //usb_send_message(DEBUG, 4, buf, 100, "Timestep: %.3f\nCondition: %d\nFuel: %d\nSpeed: %.0f\n%d\n", elapsed_time(game_timer_counter), condition, fuel, speed, 0);
}

/**
 * Steps through the game logic when game screen is not in a puased state. 
 * Will update fuel, check for input and collisions
 **/
void game_screen_step(void) {
    // Check if we ran out of fuel
    if(fuel <= 0) {
        change_screen(GAMEOVER_SCREEN);
    }

    // Deals with input that controls horizontal movement
    if(stick_left_state) {
        player_car_move(-1);
    } else if(stick_right_state) {
        player_car_move(1);
    }

    if(++distance_counter > FUEL_FACTOR) {
        // Update the fuel
        fuel--;
        // Update the distance
        distance++;
        finish_line--;

        distance_counter = 0;
    }

    // Check if the car has collided with an obstacle
    /*if(check_collision(player)) {
        // Check if the car has collided with a fuel station
        if(check_sprite_collided(player,fuel_station)) {
            change_screen(GAMEOVER_SCREEN);
        } else {
            handle_collision();
        }
    }*/

    // Refuel the car
    refuel();

    // Step through our objects
    terrain_step();
    hazard_step();
    fuel_station_step();
    road_step();

    // Check if the player has won
    if(finish_line < 1) {
        change_screen(GAMEOVER_SCREEN);
        game_over_loss = false;
    }

    // Testing: Fuel
    /*
    if(fuel == FUEL_MAX) {
        char buf[30];
        usb_send_message(DEBUG, 2, buf, 30, "Timestep: %.3f\nFuel: %d\n%d\n", elapsed_time(game_timer_counter), fuel, 0);
    } else if(fuel == 80) {
        char buf[100];
        usb_send_message(DEBUG, 2, buf, 30, "Timestep: %.3f\nFuel: %d\n%d\n", elapsed_time(game_timer_counter), fuel, 0);
    }*/
}

/**
 * Performs the initial setup/reset of the game state so that the player can start the
 * game
 **/
void game_screen_setup(void) {
    game_paused = 0;
    distance_counter = 0;
    speed_counter = 0;
    finish_line = 250;
    game_over_loss = true;
    
    // Setup the initial car information
    condition = 100;
    fuel = FUEL_MAX;
    speed = SPEED_MAX;
    distance = 0;

    // Reset the game time
    game_timer_counter = 0;

    // Set the timer so that the game can start stepping
    TCNT1 = 0x00;

    // Setup the road
    int road_x = ((LCD_X-DASHBOARD_BORDER_X)/2) - (road_width/2) + DASHBOARD_BORDER_X - 1;
    for(int y=0; y<LCD_Y; y++) {
        road[y] = road_x;
    }
    road_counter = 0;
    road_curve = ROAD_CURVE_MIN;
    road_direction = ROAD_STRAIGHT;
    road_section_length = rand() % (ROAD_SECTION_MAX + 1 - ROAD_SECTION_MIN) + ROAD_SECTION_MIN;
    
    // Decide when to spawn the first fuel station
    fuel_station_counter = rand() % (FUEL_STAION_MAX + 1 - FUEL_STATION_MIN) + FUEL_STATION_MIN;
    // Create the fuel station sprite
    sprite_init(&fuel_station, -10, -10, fuel_station_width, fuel_station_height, fuel_station_image);

    // Setup the player
    player_car_setup();
    // Setup the obstacles
    terrain_setup();
    hazard_setup();
}

/**
 * Wait for the player to indicate if they want to play again, return to title screen
 * or quit the game
 **/
void gameover_screen_update(void) {
    // If the left button is pressed, go to title screen
    if((prev_button_left_state == 0) && (button_left_state != 0)) {
        change_screen(START_SCREEN);
    }

    // If the right button is pressed, start a new game straight away
    if((prev_button_right_state == 0) && (button_right_state != 0)) {
        change_screen(GAME_SCREEN);
    }

    // If the down button is pressed, load a new game from the server via USB
    if(((prev_stick_down_state == 0) && (stick_down_state != 0))) {
        game_state_load();
    }
}

/**
 * Draw the prompts for the player to decide what to prooced to
 **/
void gameover_screen_draw(void) {
    if(game_over_loss) {
        draw_string(18, 2, "Game Over", FG_COLOUR);
    }else {
        draw_string(18, 2, "You won", FG_COLOUR);
    }
    char buf[30];
    draw_formatted(1, 10, buf, 30, "T:%.3f,D: %d", elapsed_time(game_timer_counter), distance);
    draw_string(1, LCD_Y-27, "SW2 for Splash", FG_COLOUR);
    draw_string(1, LCD_Y-17, "SW3 for Game", FG_COLOUR);
    draw_string(1, LCD_Y-7, "SWA for Load", FG_COLOUR);
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
    int y = LCD_Y - car_height - 2;
    int x = (road_width/2) + road[y] - (car_width/2) + 1;

    player.x = x;
    player.y = y;
}

/**
 * Changes the horizontal displacement of the player's car.
 * Will take into account collisions and the speed of the car to modify the dx given
 **/
void player_car_move(int dx) {
    double x = player.x + dx;
    // Check if the player will still be bounded
    if(!in_bounds(x, player.y)) {
        dx = 0;
    } else if(!in_bounds(x+player.width, player.y)) {
        dx = 0;
    }

    player.x += dx;

    // Check if the car will collide sideways with any object
    if(check_collision(player)) {
        player.x -= dx;
    }
}

/**
 * Handles input relating to speed
 **/
void player_speed_input(void) {
    // Sets the player's maximum speed through the ADC
    int max = SPEED_MAX;
    if(offroad(player)) {
        max = SPEED_OFFROAD_MAX;
    }

    int pot0 = adc_read(0);
    int speed_limit = (int)(pot0/1024.0 * max);
    speed_limit++;

    double rate = 0;
    // Handle acceleration
    // Accelerate or decelerate controls
    if(button_left_state) {
        // Calculate rate to decrease speed in order to go from 10 to 0 in 2 seconds
        rate = -10.0/40.0;
    }else if(button_right_state) {
        // Calculate rate to increase speed in order to go from 1 to 3 in 5 seconds
        if(offroad(player)) {
            rate = 3.0/120.0;
        }else { // Calculate rate to increase speed in order to go from 1 to 10 in 5 seconds
            rate = 10.0/90.0;
        }
    }else { // If no button is being pressed
        // Decrease speed if above 1 or increase to 1 if below
        if(speed > 1) {
            // Calculate rate to increase speed in order to go from 3 to 1 in 3 seconds
            if(offroad(player)) {
                rate = -3.0/75.0;
            }else { // Calculate rate to increase speed in order to go from 10 to 1 in 3 seconds
                rate = -10.0/80.0;
            }
        }else {
            // Calculate rate to increase speed in order to go from 0 to 1 in 3 seconds
            if(offroad(player)) {
                rate = 1.0/30.0;
            }else { // Calculate rate to increase speed in order to go from 0 to 1 in 2 seconds
                rate = 1.0/20.0;
            }
        }
    }

    speed += rate;

    if(speed > speed_limit) {
       speed = speed_limit;
    }else if(speed < 0) {
        speed = 0;
    }
}

/**
 * Steps the x-coordinate of the road down by 1 and creates a new road piece
 * at the top of the screen.
 **/
void road_step(void) {
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

/**
 * Create the sprites which hold the bitmap information about each terrain type
 **/
void terrain_image_setup(void){
    // Setup the tree bitmap
    sprite_init(&terrain_image[TERRAIN_TREE], -1, -1, terrain_tree_width, terrain_tree_height, terrain_tree_image);
    // Setup the sign bitmap
    sprite_init(&terrain_image[TERRAIN_SIGN], -1, -1, terrain_sign_width, terrain_sign_height, terrain_sign_image);
}

/**
 * Initialises the terrain array by setting each terrain sprite in the game world. 
 **/
void terrain_setup(void) {
    terrain_image_setup();

    // Add all of the terrain to the game world (need to fill the arrays before we can do collision checking)
    for(int i=0; i<NUM_TERRAIN; i++) {
        // Choose a type of terrain to spawn
        int type = rand() % NUM_TERRAIN_TYPES;
        // We don't care what the coordinates are for now
        int x = -10;
        int y = -20;
        // Create the terrain sprite
        sprite_init(&terrain[i], x, y, terrain_image[type].width, terrain_image[type].height, terrain_image[type].bitmap);
    }

    // Reset all of the terrain so they appear in the playing area 
    for(int i=0; i<NUM_TERRAIN; i++) {
        int y_bot = rand() % (LCD_Y - 3);
        terrain_reset(i, y_bot);
    }
}

/**
 * Moves the specified terrain to the y-coordinate give but will randomise the x-coordinate. Will randomise the terrain type
 * ybot - the sprite's bottom pixel y-coordinate
 **/
void terrain_reset(int index, int y_bot) {
    // Choose a new terrain type
    int type = rand() % NUM_TERRAIN_TYPES;
    int width = terrain_image[type].width;
    int height = terrain_image[type].height;

    // Minimum space from the road the terrain can spawn
    int padding = height / ROAD_CURVE_MIN;
    // Place the terrain at the y-coordinate
	int y = y_bot - height;

    // Check if we'll place the terrain on the left or right side of the road
	bool left = rand() % 2;
    // Check if there is any space to place the terrain (due to the road curving)
    if(left) {
        // If there's no space in the left side of the road, place it on the right side
        if(road[y_bot] - width - padding <= DASHBOARD_BORDER_X) {
            left = false;
        }
    } else {
        // If there's no space in the right side of the road, place it on the left side
        if(road[y_bot] + road_width + width + padding >= LCD_X - 1) {
            left = true;;
        }
    }
    // Find the x coordinate for the new terrain
	int x = -1 - width;
	if(left) {
		int min_x = DASHBOARD_BORDER_X + 1;
		int max_x = road[y_bot] - width - padding - 1;
		x = rand() % (max_x + 1 - min_x) + min_x;
	} else {
		int min_x = road[y_bot] + road_width + padding + 1;
		int max_x = LCD_X - 2 - width;
		x = rand() % (max_x + 1 - min_x) + min_x;
	}

    // Update the sprite's details
    terrain[index].bitmap = terrain_image[type].bitmap;
    terrain[index].x = x;
    terrain[index].y = y;
    terrain[index].width = width;
    terrain[index].height = height;

    // Check if there is any collision with other terrain
    bool collision = false;
	for(int i=0; i<NUM_TERRAIN; i++) {
		// We don't want to check if it is colliding with itself
		if(index != i) {
			if(check_sprite_collided(terrain[index],terrain[i])) {
				collision = true;
			}
		}
	}
    // Check if there is collision with the fuel station
    if(check_sprite_collided(terrain[index], fuel_station)) {
        collision = true;   
    }

    if(collision) {
        // Place the terrain on the bottom of the screen
        terrain[index].y = LCD_Y + 1;
    }
}


/**
 * Moves the terrain downwards proportionally to the current speed
 **/
void terrain_step(void) {
    for(int i=0; i<NUM_TERRAIN; i++) {
        terrain[i].y++;
        // Reset the terrain if it has gone out of bounds
        if(terrain[i].y > LCD_Y) {
            terrain_reset(i,0);
        }
    }
}

/**
 * Create the sprites which hold the bitmap information about each hazard type
 **/
void hazard_image_setup(void){
    // Setup the tree bitmap
    sprite_init(&hazard_image[HAZARD_TRIANGLE], -1, -1, hazard_triangle_width, hazard_triangle_height, hazard_triangle_image);
    // Setup the sign bitmap
    sprite_init(&hazard_image[HAZARD_SPIKE], -1, -1, hazard_spike_width, hazard_spike_height, hazard_spike_image);
}

/**
 * Initialises the hazard array by setting each hazard sprite in the game world. 
 **/
void hazard_setup(void) {
    hazard_image_setup();

    // Add all of the hazards to the game world (need to fill the arrays before we can do collision checking)
    for(int i=0; i<NUM_HAZARD; i++) {
        // Choose a type of hazard to spawn
        int type = rand() % NUM_HAZARD_TYPES;
        // We don't care what the coordinates are for now
        int x = -10;
        int y = -20;
        // Create the hazard sprite
        sprite_init(&hazard[i], x, y, hazard_image[type].width, hazard_image[type].height, hazard_image[type].bitmap);
    }

    // Reset all of the hazards so they appear in the playing area 
    for(int i=0; i<NUM_HAZARD; i++) {
        int y_bot = rand() % (LCD_Y - 20);
        hazard_reset(i, y_bot);
    }
}

/**
 * Moves the specified hazard to the y coordinate given but will randomise the x-coordinate. Will randomise the hazard type
 * ybot - the sprite's bottom pixel y-coordinate
 **/
void hazard_reset(int index, int y_bot) {
    // Choose a new hazard type
    int type = rand() % NUM_HAZARD_TYPES;
    int width = hazard_image[type].width;
    int height = hazard_image[type].height;

    // Minimum space from the road the hazard can spawn
    int padding = 1;
    // Place the terrain at the y-coordinate
	int y = y_bot - height;

    // Find the x coordinate for the new hazard
    int min_x = road[y_bot] + padding; 
    int max_x = road[y_bot] + road_width - width - padding;
	int x = rand() % (max_x + 1 - min_x) + min_x;
    
    // Update the sprite's details
    hazard[index].bitmap = hazard_image[type].bitmap;
    hazard[index].x = x;
    hazard[index].y = y;
    hazard[index].width = width;
    hazard[index].height = height;

    // Check if there is any collision with other hazards
    bool collision = false;
	for(int i=0; i<NUM_HAZARD; i++) {
		// We don't want to check if it is colliding with itself
		if(index != i) {
			if(check_sprite_collided(hazard[index],hazard[i])) {
				collision = true;
			}
		}
	}

    if(collision) {
        // Place the hazards on the bottom of the screen
        hazard[index].y = LCD_Y + 1;
    }
}

/**
 * Moves the hazards downwards proportionally to the current speed
 **/
void hazard_step(void) {
    for(int i=0; i<NUM_HAZARD; i++) {
        hazard[i].y++;
        // Reset the terrain if it has gone out of bounds
        if(hazard[i].y > LCD_Y) {
            int roll = rand() % 100;
            // Randomise whether it will actually spawn
            if(roll < HAZARD_SPAWN_CHANCE) {
                hazard_reset(i,0);
            }
        }
    }
}

/**
 * Chooses a new location at the top of the screen to spawn the fuel station, will also make 
 * sure the road remains straight in the area. 
 **/
void fuel_station_reset(void) {
    // Add the fuel station a bit above the screen
    double y = 0 - fuel_station_height - 3;
 
    // Keep the road straight while the fuel station is spawned
    road_direction = ROAD_STRAIGHT;
    road_section_length = fuel_station_height + 6;

    // Choose the side of the road to spawn
    bool left = rand() % 2;
    if(left) {
        // If there's no space in the left side of the road, place it on the right side
        if(road[0] - fuel_station_width <= DASHBOARD_BORDER_X) {
            left = false;
        }
    } else {
        // If there's no space in the right side of the road, place it on the left side
        if(road[0] + road_width + fuel_station_width >= LCD_X - 1) {
            left = true;;
        }
    }

    // Choose the x-coordinate depending on which side of the road we're spawning
    double x;
    if(left) {
        x = road[0] - fuel_station_width + 1;
    } else {
        x = road[0] + road_width;
    }

    // Change the location of the fuel station
    fuel_station.x = x;
    fuel_station.y = y;

    // Check if there is a terrain in the way and remove it
    for(int i=0; i<NUM_TERRAIN; i++) {
        if(check_sprite_collided(fuel_station, terrain[i])) {
            terrain_reset(i, 0);
        }
    }
}

/**
 * Scrolls the fuel station sprite down, checks if the car can be refuelled and spawns the 
 * fuel station when necessary
 **/
void fuel_station_step(void) {
    fuel_station_counter--;

    // Check if it's time to respawn the new fuel station
    if(fuel_station_counter < 0) {
        // Make sure the current fuel station has already gone out of bounds
        if(fuel_station.y > LCD_Y) {
            // Reset the location of the fuel station
            fuel_station_reset();
            fuel_station_counter = rand() % (FUEL_STAION_MAX + 1 - FUEL_STATION_MIN) + FUEL_STATION_MIN;
        }
    }

    // Scroll the fuel station
    fuel_station.y++;
}

/**
 * Checks if the car is next to a fuel station while travelling below the specified speed. 
 **/
void check_refuel(void) {
    // Round the coordinates of each sprite so we can equate them
    int x = round(player.x);
    int y = round(player.y);
    int fuelx = round(fuel_station.x);
    int fuely = round(fuel_station.y);

	// Check if the player is directly to the left or right of the fuel station
	if((x + player.width == fuelx) || (fuelx + fuel_station.width == x)) {
        // Check if the player is inside the bounds of the fuel station
        if((y >= fuely) && (y + player.height <= fuely + fuel_station.height)) {
            if((speed < 3) && button_left_state) {
			    refuelling = true;
		        speed = 0;
		    }
        }
	}
}

/**
 * Refuels the car in increments. Full fuel tank will be achieved in 3 seconds
 **/
void refuel(void) {
	if(!refuelling) {
		check_refuel();
	} else {
		// Cancel refuelling if the car starts moving again or brake is released
		if(speed > 0 || !button_left_state) {
			refuelling = false;
		} else {
            // Increment the fuel value with a rate that would go 0-100 in 3 seconds
            fuel++;

            // Prevent overshoot and cancel fuelling if reached max fuel
            if(fuel > FUEL_MAX) {
                fuel = FUEL_MAX;
                refuelling = false;
            }
        }
	}
}

/**
 * Checks if the sprite is off the road
 **/
bool offroad(Sprite sprite) {
    if(sprite.x < road[(int)round(sprite.y)]) {
		return true;
	}

	if((sprite.x + sprite.width - 1) > (road[(int)round(sprite.y)] + road_width)) {
		return true;
	}

	return false;
}

/**
 * Checks if there is any terrain, hazard or fuel station colliding with the sprite.
 **/
bool check_collision(Sprite sprite) {
	// Iterate through the terrain to see if there was a collision
	for(int i=0; i<NUM_TERRAIN; i++) {
		// We don't want to check if it is colliding with itself
		if(&sprite != &terrain[i]) {
			if(check_sprite_collided(sprite,terrain[i])) {
				return true;
			}
		}
	}

	// Iterate through the hazards to see if there was a collision
	for(int i=0; i<NUM_HAZARD; i++) {
		// We don't want to check if it is colliding with itself
		if(&sprite != &hazard[i]) {
			if(check_sprite_collided(sprite,hazard[i])) {
				return true;
			}
		}
	}

	// Check if collides with fuel station
	if(check_sprite_collided(sprite,fuel_station)) {
		return true;
	}

	return false;
}

/**
 * Checks if the two sprites collide with each other
 **/
bool check_sprite_collided(Sprite sprite1, Sprite sprite2) {
    // Check if both sprites are valid
	if((&sprite1 != NULL) && (&sprite2 != NULL)) {
		// Check if there is colllision in the x-axis
		if(!((sprite1.x + sprite1.width <= sprite2.x) || (sprite1.x >= sprite2.x + sprite2.width))) {
			// Check if there is collision in the y-axis
			if(!((sprite1.y + sprite1.height < sprite2.y) || (sprite1.y > sprite2.y + sprite2.height))) {
				return check_sprite_collided_pixel(sprite1, sprite2);
			}
		}
	}

	return false;
}

bool check_sprite_collided_pixel(Sprite sprite1, Sprite sprite2) {
    return true;
}

/**
 * Changes the speed to zero, reduces car condition and resets the player to the middle of the road
 **/
void handle_collision(void) {
	speed = 0;
	fuel = FUEL_MAX;
	condition -= 20;
	if(condition <= 0) {
		change_screen(GAMEOVER_SCREEN);
	}
	
    // Put the player in the middle of the road again
    player_car_reset();

	// Remove any hazards up to a car length above the player
	for(int i=0; i<NUM_HAZARD; i++) {
		if(hazard[i].y + hazard[i].height > player.y - player.height) {
            hazard_reset(i, 0);
        }
	}
}

/**
 * Pass game identifying variables such as current speed, distance, condition, fuel, etc to the server via USB 
 **/
void game_state_save(void) {
    //uint8_t condition;
    //double speed;
    //double fuel;
    //double distance;
    //uint16_t game_timer_counter;
    // Road
    //uint8_t road[LCD_Y];            // The x-coordinates of each road piece
    //double road_y;                  // Used to make sure the road stays synced with the other objects in the playing field
    //uint8_t road_counter;           // Counts how many steps the road has taken before being moved horizontally
    //uint8_t road_curve;             // This decides how many times the road must move before it is moved horizontally
    //int road_direction;
    //uint8_t road_section_length;    // How many steps the road has taken in the current length
    // The player
    //Sprite player;
    //Sprite terrain[NUM_TERRAIN];   
    //Sprite hazard[NUM_HAZARD];  
    // Fuel station
    //Sprite fuel_station;
    //double fuel_station_counter;   // Counts down to when the fuel station can spawn again
    //bool refuelling;
    char data[500];
    //int message_size = sprintf(data, "%d\n%d\n%d\n%d\n%d\n%d\n", 1, condition, (int)floor(speed), fuel, distance, 0);
    //char message[message_size+1];
    //int size = sprintf(message, "%d\n%s", message_size, data);
    //usb_serial_putchar(1);
    //usb_serial_putchar(4);
    //usb_serial_write((uint8_t *) data, message_size);
    usb_send_message(SAVE, 4, data, 500, "%d\n%d\n%d\n%d\n%d\n", condition, (int)round(speed), fuel, distance, 0);
    usb_serial_flush_output();
    usb_serial_flush_input();
}

/**
 * Communicate with the server via USB in order to load a game by getting critical variables
 **/
void game_state_load(void) {

}

/**
 * Sends the string input to the terminal
 **/
void usb_send_message(enum USBCommand command, int line_num ,char * buffer, int buffer_size, const char * format, ...) {
    usb_serial_putchar(command);
    usb_serial_putchar(line_num);
    va_list args;
	va_start(args, format);
	int size = vsnprintf(buffer, buffer_size, format, args);
    usb_serial_write((uint8_t *) buffer, size);
}

/**
 * Set clock speed, contrast settings and initial register setups
 **/
void teensy_setup(void) {
    set_clock_speed(CPU_8MHz);
    lcd_init(LCD_DEFAULT_CONTRAST);

    // Setup the ADC that will be used for the potentionmeters
    adc_init();

    // Setup USB communication
    usb_init();
	while ( !usb_configured() ) {
		// Block until USB is ready.
	}
    usb_serial_flush_input();

    // Initialise Timer 0 which will be used for debouncing purposes 
    TCCR0A = 0x00;
    TCCR0B = 1<<CS02;
    TIMSK0 = 1<<TOIE0; 

    // Initialise Timer 3 which will be used to control speed
    TCCR1B = 0x0C;	        // Prescaler 1024, enable CTC
	TIMSK1 = 0x02;          // Interrupt on 
	OCR1A = TIMER1_FREQ/60;

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
    
    // STICK_UP
    b = (PIND>>STICK_UP) & 0x01;
    stick_up_history = ((stick_up_history << 1) & mask) | b;
    if(stick_up_history == 0) {
        stick_up_state = 0;
    } else if(stick_up_history == mask) {
        stick_up_state = 1;
    }

    // STICK_DOWN
    b = (PIND>>STICK_DOWN) & 0x01;
    stick_down_history = ((stick_down_history << 1) & mask) | b;
    if(stick_down_history == 0) {
        stick_down_state = 0;
    } else if(stick_down_history == mask) {
        stick_down_state = 1;
    }
}

ISR(TIMER1_COMPA_vect) {
	if(!game_paused && (game_screen == GAME_SCREEN)) {
        double rate = speed/SPEED_FACTOR;
        speed_counter += rate;
    }
}