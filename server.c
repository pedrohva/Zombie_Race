#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <cab202_graphics.h>
#include <cab202_sprites.h>
#include "cab202_timers.h"

void setup(const char * serial_device);
void setup_usb_serial(const char * serial_device);
void process(void);
void save(void);
void debug(void);
uint8_t usb_receive_string(char *buffer, uint8_t size);

FILE *usb_serial;
FILE *save_file;

//-------------------------------------------------------------------

int main(int argc, char *argv[]) {
	if ( argc != 2 ) {
		fprintf(stderr, "Expected 1 command line argument containing serial device name.\n");
		fprintf(stderr, "Example: usb_zdk /dev/ttyS3\n");
		return 1;
	}

	setup(argv[1]);

	for ( ;; ) {
		process();
	}
}

//-------------------------------------------------------------------
void process(void) {
    clear_screen();
    draw_string(1, 1, "Mode:");

    char code = fgetc(usb_serial);

    switch(code) {
        case 1:
            draw_string(7, 1, "Saving");
            save();
            break;
        case 3:
            draw_string(7, 1, "Debugging");
            debug();
        default:
            break;
    }

    show_screen();
}

void decode(char* data, int index) {
    
}

void save(void) {
    //uint8_t condition;
    //double speed;
    //double fuel;
    //double distance;
    //uint16_t game_timer_counter;
    // Road
    //uint8_t road[LCD_Y];            // The x-coordinates of each road piece
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

    int num_lines = fgetc(usb_serial);
    char data[num_lines][100];
    int cnt = 0;
    while((fgets(data[cnt], 100, usb_serial) != NULL) && cnt < num_lines) {
        data[cnt][strcspn(data[cnt], "\n")] = 0;
        decode(data[cnt], cnt);
        cnt++;
    }
    for(int i=0; i<cnt; i++) {
        draw_string(1,i+3,data[i]);
    }
}

void debug(void) {
    int num_lines = fgetc(usb_serial);
    char data[num_lines][100];
    int cnt = 0;
    while((fgets(data[cnt], 100, usb_serial) != NULL) && cnt < num_lines) {
        data[cnt][strcspn(data[cnt], "\n")] = 0;
        cnt++;
    }
    for(int i=0; i<cnt; i++) {
        draw_string(1,i+3,data[i]);
    }
}

//-------------------------------------------------------------------

void setup(const char * serial_device) {
	setup_screen();
	setup_usb_serial(serial_device);
}

// ---------------------------------------------------------
//	USB serial business.
// ---------------------------------------------------------

void setup_usb_serial(const char * serial_device) {
	usb_serial = fopen(serial_device, "r+");

	if ( usb_serial == NULL ) {
		fprintf(stderr, "Unable to open device \"%s\"\n", serial_device);
		exit(1);
	}
}

uint8_t usb_receive_string(char *buffer, uint8_t size) {
    int tmp;

    uint8_t count = 0;
    while(count < size) {
        tmp = fgetc(usb_serial);
        if(tmp != -1) {
            *buffer++ = tmp;
            count++;
        }
    }

    return count;
}

//-------------------------------------------------------------------