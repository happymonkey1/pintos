#include "threads/shell.h"
#include "devices/input.h"
#include "threads/interrupt.h"
#include "devices/vga.h"
#include <stdio.h>
#include <string.h>

// defined here because I couldn't find a def
// #NOTE(Sean) enter scan code online says 0x1c but printing the returned key says 13?
#define CARRIAGE_RETURN 13
#define NEW_LINE_RETURN 10
#define BACKSPACE_KEY_CODE 8

// same size as intq buffer
#define KEY_BUFFER_SIZE 64

static char key_buffer[KEY_BUFFER_SIZE];

// private function declarations
void reset_buffer(void);
void readline(void);

// zero out buffer
void reset_buffer(void)
{
    // #NOTE(Sean) careful with overflow if buffer size is too large
    for (uint8_t i = 0; i < KEY_BUFFER_SIZE; ++i)
        key_buffer[i] = 0;
}

// reads user input until \n character, putting it in a local key buffer
// #NOTE(Sean) this should probably be in lib/input.c
void readline(void)
{
    // reset local key buffer
    reset_buffer();

    // loop until the input buffer is full or '\n' encountered
    uint8_t buffer_index = 0;
    while (buffer_index < KEY_BUFFER_SIZE)
    {
        char key = (char)input_getc();
        // check for newline or buffer overflow
        if (key == CARRIAGE_RETURN || key == NEW_LINE_RETURN || buffer_index >= KEY_BUFFER_SIZE)
            break;
        
        // check whether we are able to backspace
        bool can_backspace = key == BACKSPACE_KEY_CODE && buffer_index > 0;
        if (can_backspace)
        {
            // remove character from local buffer
            key_buffer[--buffer_index] = 0;
            // "delete" key from screen
            vga_backspace();
        }
        
        // display to screen
        if (key != BACKSPACE_KEY_CODE)
        {
            putchar((int)key);
            // store in buffer which we will use for command comparisons later
            key_buffer[buffer_index++] = key;
        }
        
    }

    // #NOTE(Sean) buffer should be nulled out already.

    putchar('\n');
}

// initialize and run interactive shell, which waits for user input and potentially executes commands
void shell_init(void)
{
    // parse user input, and try executing command(s)
    while (true)
    {
        printf("ICS143A> ");
        readline();
        // check for built-in commands
        if (strcmp(key_buffer, "exit") == 0)
            return;
        else if (strcmp(key_buffer, "whoami") == 0)
            printf("Sean Browne\n");
        else
            printf("invalid command\n");
    }
}