#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Display configuration
#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 32
#define DISPLAY_SCALE 20
#define PIXEL_OUTLINES true

// Colors (RGBA format)
#define COLOR_FOREGROUND 0xFFD700FF // Gold
#define COLOR_BACKGROUND 0x000080FF // Navy Blue

// Timing and performance
#define CLOCK_SPEED 600 // Instructions per second
#define FRAME_RATE 60   // Target frames per second
#define INSTRUCTIONS_PER_FRAME (CLOCK_SPEED / FRAME_RATE)
#define DELAY 16.67f
float COLOR_TRANSITION_RATE = 0.7f; // Color interpolation speed

// CHIP-8 memory layout
#define MEMORY_SIZE 4096    // Total RAM size
#define PROGRAM_START 0x200 // Program entry point
#define FONT_START 0x000    // Font data location
#define FONT_SIZE 80        // Size of font data in bytes

// SDL Specs
SDL_Window *window;
SDL_Renderer *renderer;

// State Specs
#define RUNNING 0
#define PAUSED 1
#define QUIT 2

/********** CHIP8 **********
 * Complete state of the CHIP-8 emulator.
 *
 * Fields:
 *      state:       Current emulator state
 *      ram:         4KB of RAM
 *      display:     64x32 display buffer
 *      pixels:      Color for each pixel (for smooth transitions)
 *      stack:       Call stack for subroutines
 *      stack_ptr:   Current stack pointer
 *      V:           16 general-purpose registers (V0-VF)
 *      I:           Index register for memory operations
 *      PC:          Program counter
 *      delay_timer: Delay timer (decrements at 60Hz)
 *      sound_timer: Sound timer (decrements at 60Hz)
 *      keypad:      Keypad state (16 keys)
 *      rom_name:    Current ROM filename
 *      draw:        Whether display needs update
 ************************/
uint8_t state = RUNNING;
uint8_t ram[MEMORY_SIZE];
bool display[DISPLAY_WIDTH * DISPLAY_HEIGHT];
uint32_t pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
uint16_t stack[12];
uint16_t *stack_ptr;
uint8_t V[16];
uint16_t I;
uint16_t PC;
uint8_t delay_timer;
uint8_t sound_timer;
bool keypad[16];
const char *rom_name;
bool draw;

/********** COLOR_LERP **********
 * Macro for linear interpolation between two colors for smooth transitions.
 *
 * Parameters:
 *      start_color: Starting color in RGBA format
 *      end_color:   Ending color in RGBA format
 *      t:           Interpolation factor (0.0 to 1.0)
 *
 * Returns:
 *      uint32_t:    Interpolated color in RGBA format
 *
 * Notes:
 *      Each color component (R,G,B,A) is interpolated independently
 *      Used for smooth pixel color transitions
 ************************/
#define COLOR_LERP(start_color, end_color, t)                                            \
        ((((uint8_t)((1 - (t)) * (((start_color) >> 24) & 0xFF) +                        \
                     (t) * (((end_color) >> 24) & 0xFF)))                                \
          << 24) |                                                                       \
         (((uint8_t)((1 - (t)) * (((start_color) >> 16) & 0xFF) +                        \
                     (t) * (((end_color) >> 16) & 0xFF)))                                \
          << 16) |                                                                       \
         (((uint8_t)((1 - (t)) * (((start_color) >> 8) & 0xFF) +                         \
                     (t) * (((end_color) >> 8) & 0xFF)))                                 \
          << 8) |                                                                        \
         (((uint8_t)((1 - (t)) * (((start_color) >> 0) & 0xFF) +                         \
                     (t) * (((end_color) >> 0) & 0xFF)))                                 \
          << 0))

/********** EXTRACT_RGBA **********
 * Macro to extract individual RGBA components from a 32-bit color value.
 *
 * Parameters:
 *      color: 32-bit RGBA color value
 *      r:     Variable to store the red component
 *      g:     Variable to store the green component
 *      b:     Variable to store the blue component
 *      a:     Variable to store the alpha component
 *
 * Notes:
 *      Extracts each component by masking and shifting
 ************************/
#define EXTRACT_RGBA(color, r, g, b, a)                                                  \
        do {                                                                             \
                r = ((color) >> 24) & 0xFF;                                              \
                g = ((color) >> 16) & 0xFF;                                              \
                b = ((color) >> 8) & 0xFF;                                               \
                a = ((color) >> 0) & 0xFF;                                               \
        } while (0)

void init_SDL();
void init_chip8();
void clear_window();
void update_window();
void input();
void exec_instruction();
bool save_state(const char *filename);
bool load_state(const char *filename);

/********** init_SDL **********
 * Initializes SDL subsystems and creates the main window.
 *
 * Parameters:
 *
 * Returns:
 *      bool:    true if initialization succeeded, false otherwise
 *
 * Notes:
 *      Initializes video and timer subsystems
 *      Creates a window and renderer with the specified dimensions
 ************************/
void init_SDL()
{
        // Create the main window
        window = SDL_CreateWindow("CHIP8 Emulator", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, DISPLAY_WIDTH * DISPLAY_SCALE,
                                  DISPLAY_HEIGHT * DISPLAY_SCALE, 0);
        if (!window) {
                SDL_Log("Failed to create window: %s", SDL_GetError());
                exit(EXIT_FAILURE);
        }

        // Create the renderer
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) {
                SDL_Log("Failed to create renderer: %s", SDL_GetError());
                SDL_DestroyWindow(window);
                exit(EXIT_FAILURE);
        }
}

/********** init_chip8 **********
 * Initializes the CHIP-8 emulator state.
 *
 * Parameters:
 *      chip8:     Pointer to CHIP8_t to initialize
 *      rom_name:  Path to the ROM file to load
 *
 * Returns:
 *      bool:      true if initialization succeeded, false otherwise
 *
 * Notes:
 *      Loads the font data into memory at FONT_START
 *      Loads the ROM file starting at PROGRAM_START
 *      Initializes all registers and timers to zero
 ************************/
void init_chip8()
{
        // Define entry point and font data
        const uint32_t entry_point = PROGRAM_START;
        const uint8_t font[] = {
            0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70, 0xF0, 0x10,
            0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0, 0x90, 0x90, 0xF0, 0x10,
            0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0, 0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0,
            0x10, 0x20, 0x40, 0x40, 0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0,
            0x10, 0xF0, 0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
            0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0, 0xF0, 0x80,
            0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80,
        };

        // Clear CHIP-8 state and load font into memory
        memset(ram, 0, sizeof(ram));
        memset(display, 0, sizeof(display));
        memset(pixels, 0, sizeof(pixels));
        memset(stack, 0, sizeof(stack));
        memset(V, 0, sizeof(V));
        memset(keypad, 0, sizeof(keypad));
        memcpy(&ram[FONT_START], font, sizeof(font));

        // Open ROM file
        FILE *rom = fopen(rom_name, "rb");
        if (!rom) {
                SDL_Log("Failed to open ROM: %s\n", rom_name);
                exit(EXIT_FAILURE);
        }

        // Determine ROM size and validate it
        fseek(rom, 0, SEEK_END);
        size_t rom_size = ftell(rom);
        rewind(rom);
        if (rom_size > MEMORY_SIZE - entry_point) {
                SDL_Log("ROM too large: %s (size: %zu bytes)\n", rom_name, rom_size);
                fclose(rom);
                exit(EXIT_FAILURE);
        }

        // Load ROM into memory
        if (fread(&ram[entry_point], rom_size, 1, rom) != 1) {
                SDL_Log("Failed to load ROM: %s\n", rom_name);
                fclose(rom);
                exit(EXIT_FAILURE);
        }
        fclose(rom);

        // Initialize emulator state
        state = RUNNING;
        PC = entry_point;
        stack_ptr = stack;
        memset(pixels, COLOR_BACKGROUND, sizeof(pixels));
}

/********** clear_window **********
 * Clears the SDL window with the background color.
 *
 * Parameters:
 *
 * Notes:
 *      Sets the render draw color to COLOR_BACKGROUND
 *      Clears the entire window
 ************************/
void clear_window()
{
        uint8_t r, g, b, a;
        EXTRACT_RGBA(COLOR_BACKGROUND, r, g, b, a);

        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderClear(renderer);
}

/********** update_window **********
 * Updates the display with the current CHIP-8 window state.
 *
 * Parameters:
 *
 * Notes:
 *      Renders each pixel with color interpolation
 *      Optionally draws pixel outlines
 *      Updates the renderer with the new display state
 ************************/
void update_window()
{
        // Define a rectangle for each pixel, scaled to the display size
        SDL_Rect rect = {.w = DISPLAY_SCALE, .h = DISPLAY_SCALE};
        uint8_t bg_r, bg_g, bg_b, bg_a, r, g, b, a;

        // Extract the RGBA components of the background color
        EXTRACT_RGBA(COLOR_BACKGROUND, bg_r, bg_g, bg_b, bg_a);

        // Loop through each pixel in the display buffer
        for (uint32_t i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
                // Calculate the position of the pixel on the window
                rect.x = (i % DISPLAY_WIDTH) * DISPLAY_SCALE;
                rect.y = (i / DISPLAY_WIDTH) * DISPLAY_SCALE;

                // Determine the target color based on the display buffer
                uint32_t target_color = display[i] ? COLOR_FOREGROUND : COLOR_BACKGROUND;

                // Smoothly transition the pixel color to the target color
                if (pixels[i] != target_color) {
                        pixels[i] =
                            COLOR_LERP(pixels[i], target_color, COLOR_TRANSITION_RATE);
                }

                // Extract the RGBA components of the current pixel color
                EXTRACT_RGBA(pixels[i], r, g, b, a);

                // Set the draw color and fill the rectangle for the pixel
                SDL_SetRenderDrawColor(renderer, r, g, b, a);
                SDL_RenderFillRect(renderer, &rect);

                // Optionally draw outlines around active pixels
                if (PIXEL_OUTLINES && display[i]) {
                        SDL_SetRenderDrawColor(renderer, bg_r, bg_g, bg_b, bg_a);
                        SDL_RenderDrawRect(renderer, &rect);
                }
        }

        SDL_RenderPresent(renderer);
}

/********** input **********
 * Processes SDL input events and updates emulator state.
 *
 * Parameters:
 *      chip8:     Pointer to CHIP8_t to update
 *
 * Notes:
 *      Handles window close events
 *      Processes keyboard input for emulator control
 *      Updates keypad state based on key presses
 *      Supports save/load state with F5/F9
 *      Controls volume and color transition rate
 ************************/
void input()
{
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                        state = QUIT;
                        return;
                }

                if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                        bool pressed = (event.type == SDL_KEYDOWN);
                        switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                                state = QUIT;
                                break;
                        case SDLK_SPACE:
                                state = (state == RUNNING) ? PAUSED : RUNNING;
                                break;
                        case SDLK_EQUALS:
                                init_chip8(rom_name);
                                break;
                        case SDLK_j:
                                COLOR_TRANSITION_RATE =
                                    fmax(COLOR_TRANSITION_RATE - 0.1, 0.1);
                                break;
                        case SDLK_k:
                                COLOR_TRANSITION_RATE =
                                    fmin(COLOR_TRANSITION_RATE + 0.1, 1.0);
                                break;
                        case SDLK_F5:
                                puts(save_state("save_state.bin") ? "State saved."
                                                                  : "Save failed.");
                                break;
                        case SDLK_F9:
                                puts(load_state("save_state.bin") ? "State loaded."
                                                                  : "Load failed.");
                                break;

                        case SDLK_1:
                                keypad[0x1] = pressed;
                                break;
                        case SDLK_2:
                                keypad[0x2] = pressed;
                                break;
                        case SDLK_3:
                                keypad[0x3] = pressed;
                                break;
                        case SDLK_4:
                                keypad[0xC] = pressed;
                                break;
                        case SDLK_q:
                                keypad[0x4] = pressed;
                                break;
                        case SDLK_w:
                                keypad[0x5] = pressed;
                                break;
                        case SDLK_e:
                                keypad[0x6] = pressed;
                                break;
                        case SDLK_r:
                                keypad[0xD] = pressed;
                                break;
                        case SDLK_a:
                                keypad[0x7] = pressed;
                                break;
                        case SDLK_s:
                                keypad[0x8] = pressed;
                                break;
                        case SDLK_d:
                                keypad[0x9] = pressed;
                                break;
                        case SDLK_f:
                                keypad[0xE] = pressed;
                                break;
                        case SDLK_z:
                                keypad[0xA] = pressed;
                                break;
                        case SDLK_x:
                                keypad[0x0] = pressed;
                                break;
                        case SDLK_c:
                                keypad[0xB] = pressed;
                                break;
                        case SDLK_v:
                                keypad[0xF] = pressed;
                                break;

                        default:
                                break;
                        }
                }
        }
}

/********** exec_instruction **********
 * Executes a single CHIP-8 instruction.
 *
 * Parameters:
 *      chip8:     Pointer to CHIP8_t to update
 *
 * Notes:
 *      Decodes and executes the instruction at PC
 *      Updates PC to point to the next instruction
 *      Handles all CHIP-8 opcodes and extensions
 *      Sets draw flag when display is modified
 ************************/
void exec_instruction()
{
        bool carry;
        // Fetch and decode the current instruction
        const uint16_t opcode = (ram[PC] << 8) | ram[PC + 1];
        PC += 2; // Move PC to next instruction

        // Extract instruction components
        const uint16_t NNN = opcode & 0x0FFF;  // 12-bit address
        const uint8_t NN = opcode & 0xFF;      // 8-bit constant
        const uint8_t N = opcode & 0xF;        // 4-bit constant
        const uint8_t X = (opcode >> 8) & 0xF; // 4-bit register index
        const uint8_t Y = (opcode >> 4) & 0xF; // 4-bit register index

        // Decode and execute based on instruction type (first nibble)
        switch ((opcode >> 12) & 0x0F) {
        case 0x00: // System instructions
                if (NN == 0xE0) {
                        // Clear display
                        memset(display, false, sizeof(display));
                        draw = true;
                } else if (NN == 0xEE) {
                        // Return from subroutine
                        PC = *--stack_ptr;
                }
                break;

        case 0x01: // Jump to address NNN
                PC = NNN;
                break;

        case 0x02:                 // Call subroutine at NNN
                *stack_ptr++ = PC; // Save return address
                PC = NNN;
                break;

        case 0x03: // Skip if VX equals NN
                if (V[X] == NN) {
                        PC += 2;
                }
                break;

        case 0x04: // Skip if VX not equal to NN
                if (V[X] != NN) {
                        PC += 2;
                }
                break;

        case 0x05: // Skip if VX equals VY
                if (V[X] == V[Y]) {
                        PC += 2;
                }
                break;

        case 0x06: // Set VX to NN
                V[X] = NN;
                break;

        case 0x07: // Add NN to VX
                V[X] += NN;
                break;

        case 0x08: // Register operations
                switch (N) {
                case 0: // Set VX to VY
                        V[X] = V[Y];
                        break;

                case 1: // Set VX to VX OR VY
                        V[X] |= V[Y];
                        if (strcmp(rom_name, "CHIP8") == 0) {
                                V[0xF] = 0; // VF not affected in original CHIP-8
                        }
                        break;

                case 2: // Set VX to VX AND VY
                        V[X] &= V[Y];
                        if (strcmp(rom_name, "CHIP8") == 0) {
                                V[0xF] = 0;
                        }
                        break;

                case 3: // Set VX to VX XOR VY
                        V[X] ^= V[Y];
                        if (strcmp(rom_name, "CHIP8") == 0) {
                                V[0xF] = 0;
                        }
                        break;

                case 4: // Add VY to VX, set VF to carry
                        carry = ((uint16_t)(V[X] + V[Y]) > 255);
                        V[X] += V[Y];
                        V[0xF] = carry;
                        break;

                case 5: // Subtract VY from VX, set VF to NOT borrow
                        carry = (V[Y] <= V[X]);
                        V[X] -= V[Y];
                        V[0xF] = carry;
                        break;

                case 6: // Shift VX right by 1, set VF to least significant bit
                        if (strcmp(rom_name, "CHIP8") == 0) {
                                // Original CHIP-8: VY is shifted, VX is set to result
                                carry = V[Y] & 1;
                                V[X] = V[Y] >> 1;
                        } else {
                                // Super-CHIP: VX is shifted
                                carry = V[X] & 1;
                                V[X] >>= 1;
                        }
                        V[0xF] = carry;
                        break;

                case 7: // Set VX to VY minus VX, set VF to NOT borrow
                        carry = (V[X] <= V[Y]);
                        V[X] = V[Y] - V[X];
                        V[0xF] = carry;
                        break;

                case 0xE: // Shift VX left by 1, set VF to most significant bit
                        if (strcmp(rom_name, "CHIP8") == 0) {
                                // Original CHIP-8: VY is shifted, VX is set to result
                                carry = (V[Y] & 0x80) >> 7;
                                V[X] = V[Y] << 1;
                        } else {
                                // Super-CHIP: VX is shifted
                                carry = (V[X] & 0x80) >> 7;
                                V[X] <<= 1;
                        }
                        V[0xF] = carry;
                        break;
                }
                break;

        case 0x09: // Skip if VX not equal to VY
                if (V[X] != V[Y]) {
                        PC += 2;
                }
                break;

        case 0x0A: // Set I to NNN
                I = NNN;
                break;

        case 0x0B: // Jump to NNN + V0
                PC = V[0] + NNN;
                break;

        case 0x0C: // Set VX to random number AND NN
                V[X] = (rand() % 256) & NN;
                break;

        case 0x0D: { // Draw sprite at (VX,VY) with N bytes of sprite data
                uint8_t X_coord = V[X] % DISPLAY_WIDTH;
                uint8_t Y_coord = V[Y] % DISPLAY_HEIGHT;
                const uint8_t orig_X = X_coord;
                V[0xF] = 0; // Collision flag

                // Draw N rows of sprite data
                for (uint8_t i = 0; i < N; i++) {
                        const uint8_t sprite_data = ram[I + i];
                        X_coord = orig_X;

                        // Draw 8 pixels per row
                        for (int8_t j = 7; j >= 0; j--) {
                                bool *pixel =
                                    &display[(Y_coord * DISPLAY_WIDTH) + X_coord];
                                const bool sprite_bit = (sprite_data & (1 << j));

                                // Check for collision
                                if (sprite_bit && *pixel) {
                                        V[0xF] = 1;
                                }

                                // XOR sprite bit with display
                                *pixel ^= sprite_bit;

                                if (++X_coord >= DISPLAY_WIDTH) {
                                        break;
                                }
                        }

                        if (++Y_coord >= DISPLAY_HEIGHT) {
                                break;
                        }
                }
                draw = true;
                break;
        }

        case 0x0E: // Key input instructions
                if (NN == 0x9E) {
                        // Skip if key in VX is pressed
                        if (keypad[V[X]]) {
                                PC += 2;
                        }
                } else if (NN == 0xA1) {
                        // Skip if key in VX is not pressed
                        if (!keypad[V[X]]) {
                                PC += 2;
                        }
                }
                break;

        case 0x0F: // Extended instructions
                switch (NN) {
                case 0x0A: { // Wait for key press, store in VX
                        static bool any_key_pressed = false;
                        static uint8_t key = 0xFF;

                        // Check for any key press
                        for (uint8_t i = 0; key == 0xFF && i < sizeof(keypad); i++) {
                                if (keypad[i]) {
                                        key = i;
                                        any_key_pressed = true;
                                        break;
                                }
                        }

                        if (!any_key_pressed) {
                                // No key pressed, repeat instruction
                                PC -= 2;
                        } else {
                                if (keypad[key]) {
                                        // Key still pressed, repeat instruction
                                        PC -= 2;
                                } else {
                                        // Key released, store it
                                        V[X] = key;
                                        key = 0xFF;
                                        any_key_pressed = false;
                                }
                        }
                        break;
                }

                case 0x1E: // Add VX to I
                        I += V[X];
                        break;

                case 0x07: // Set VX to delay timer value
                        V[X] = delay_timer;
                        break;

                case 0x15: // Set delay timer to VX
                        delay_timer = V[X];
                        break;

                case 0x18: // Set sound timer to VX
                        sound_timer = V[X];
                        break;

                case 0x29:            // Set I to location of sprite for digit VX
                        I = V[X] * 5; // Each sprite is 5 bytes
                        break;

                case 0x33: { // Store BCD representation of VX
                        uint8_t bcd = V[X];
                        // Store hundreds digit
                        ram[I + 2] = bcd % 10;
                        bcd /= 10;
                        // Store tens digit
                        ram[I + 1] = bcd % 10;
                        bcd /= 10;
                        // Store ones digit
                        ram[I] = bcd;
                        break;
                }

                case 0x55: // Store V0 to VX in memory starting at I
                        for (uint8_t i = 0; i <= X; i++) {
                                if (strcmp(rom_name, "CHIP8") == 0) {
                                        // Original CHIP-8: I is incremented
                                        ram[I++] = V[i];
                                } else {
                                        // Super-CHIP: I is not modified
                                        ram[I + i] = V[i];
                                }
                        }
                        break;

                case 0x65: // Fill V0 to VX with values from memory starting at I
                        for (uint8_t i = 0; i <= X; i++) {
                                if (strcmp(rom_name, "CHIP8") == 0) {
                                        // Original CHIP-8: I is incremented
                                        V[i] = ram[I++];
                                } else {
                                        // Super-CHIP: I is not modified
                                        V[i] = ram[I + i];
                                }
                        }
                        break;
                }
                break;
        }
}

/********** save_state **********
 * Saves the current emulator state to a file.
 *
 * Parameters:
 *      chip8:     Pointer to CHIP8_t to save
 *      filename:  Path to the save file
 *
 * Returns:
 *      bool:      true if save succeeded, false otherwise
 *
 * Notes:
 *      Saves the entire emulator state in binary format
 *      Creates a new file or overwrites an existing one
 ************************/
bool save_state(const char *filename)
{
        FILE *file = fopen(filename, "wb");
        if (!file) {
                SDL_Log("Failed to open file %s for saving state\n", filename);
                return false;
        }

        // Save all global state variables
        if (fwrite(ram, sizeof(ram), 1, file) != 1 ||
            fwrite(display, sizeof(display), 1, file) != 1 ||
            fwrite(pixels, sizeof(pixels), 1, file) != 1 ||
            fwrite(stack, sizeof(stack), 1, file) != 1 ||
            fwrite(V, sizeof(V), 1, file) != 1 || fwrite(&I, sizeof(I), 1, file) != 1 ||
            fwrite(&PC, sizeof(PC), 1, file) != 1 ||
            fwrite(&delay_timer, sizeof(delay_timer), 1, file) != 1 ||
            fwrite(&sound_timer, sizeof(sound_timer), 1, file) != 1 ||
            fwrite(keypad, sizeof(keypad), 1, file) != 1) {
                SDL_Log("Failed to write state to file %s\n", filename);
                fclose(file);
                return false;
        }

        fclose(file);
        return true;
}

/********** load_state **********
 * Loads an emulator state from a file.
 *
 * Parameters:
 *      chip8:     Pointer to CHIP8_t to load into
 *      filename:  Path to the save file
 *
 * Returns:
 *      bool:      true if load succeeded, false otherwise
 *
 * Notes:
 *      Loads the entire emulator state from binary format
 *      File must have been created by save_state
 ************************/
bool load_state(const char *filename)
{
        FILE *file = fopen(filename, "rb");
        if (!file) {
                SDL_Log("Failed to open file %s for loading state\n", filename);
                return false;
        }

        // Load all global state variables
        if (fread(ram, sizeof(ram), 1, file) != 1 ||
            fread(display, sizeof(display), 1, file) != 1 ||
            fread(pixels, sizeof(pixels), 1, file) != 1 ||
            fread(stack, sizeof(stack), 1, file) != 1 ||
            fread(V, sizeof(V), 1, file) != 1 || fread(&I, sizeof(I), 1, file) != 1 ||
            fread(&PC, sizeof(PC), 1, file) != 1 ||
            fread(&delay_timer, sizeof(delay_timer), 1, file) != 1 ||
            fread(&sound_timer, sizeof(sound_timer), 1, file) != 1 ||
            fread(keypad, sizeof(keypad), 1, file) != 1) {
                SDL_Log("Failed to read state from file %s\n", filename);
                fclose(file);
                return false;
        }

        // Restore stack pointer
        stack_ptr = stack;
        fclose(file);
        return true;
}

/********** main **********
 * Main entry point for the CHIP-8 emulator.
 *
 * Parameters:
 *      argc:    Number of command line arguments
 *      argv:    Array of command line argument strings
 *
 * Returns:
 *      int:     EXIT_SUCCESS or EXIT_FAILURE
 *
 * Notes:
 *      Requires a ROM file path as the first argument
 *      Initializes SDL, loads ROM, and runs emulation loop
 *      Handles cleanup on return
 ************************/
int main(int argc, char **argv)
{
        // Ensure a ROM file is provided as an argument
        if (argc < 2) {
                fprintf(stderr, "Usage: %s <rom_name>\n", argv[0]);
                return EXIT_FAILURE;
        }

        // Initialize SDL context
        init_SDL();

        // Initialize CHIP-8 state
        rom_name = argv[1];
        init_chip8();

        // Clear the window and seed the random number generator
        clear_window();
        srand((unsigned int)time(NULL));

        // Main emulation loop
        while (state != QUIT) {
                // Handle user input
                input();

                // Pause execution if the emulator is paused
                if (state == PAUSED) {
                        continue;
                }

                // Measure the start time of the frame
                const uint64_t start_frame_time = SDL_GetPerformanceCounter();

                // Execute instructions for the current frame
                for (uint32_t i = 0; i < INSTRUCTIONS_PER_FRAME; i++) {
                        exec_instruction();

                        // Handle quirks for original CHIP-8
                        if ((strcmp(rom_name, "CHIP8") == 0) &&
                            (((ram[PC] << 8) | ram[PC + 1]) >> 12 == 0xD)) {
                                break;
                        }
                }

                // Measure the end time of the frame
                const uint64_t end_frame_time = SDL_GetPerformanceCounter();

                // Calculate elapsed time and delay to maintain frame rate
                const double time_elapsed_ms =
                    (double)((end_frame_time - start_frame_time) * 1000) /
                    SDL_GetPerformanceFrequency();
                SDL_Delay((DELAY > time_elapsed_ms) ? (DELAY - time_elapsed_ms) : 0);

                // Update the window if needed
                if (draw) {
                        update_window();
                        draw = false;
                }

                // Update timers
                if (delay_timer) {
                        delay_timer--;
                }
        }

        // Clean up resources and return
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_SUCCESS;
}
