#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "SDL.h"

/********** SDL_Context **********
 * Holds SDL-related resources and state for the emulator.
 *
 * Fields:
 *      window:     Main SDL window for display
 *      renderer:   SDL renderer for drawing
 *      audio_spec: Audio configuration
 *      audio_dev:  Audio device handle
 ************************/
typedef struct {
        SDL_Window *window;
        SDL_Renderer *renderer;
        SDL_AudioSpec want, have;
        SDL_AudioDeviceID dev;
} SDL_Context;

/********** EmulatorState **********
 * Represents the current state of the CHIP-8 emulator.
 *
 * Values:
 *      QUIT:    Emulator should terminate
 *      RUNNING: Normal execution
 *      PAUSED:  Execution suspended
 ************************/
typedef enum {
        QUIT,
        RUNNING,
        PAUSED,
} EmulatorState;

/********** Chip8Extension **********
 * Specifies which CHIP-8 variant is being emulated.
 *
 * Values:
 *      CHIP8:      Standard CHIP-8
 *      SUPERCHIP:  Super-CHIP extension
 *      XOCHIP:     XO-CHIP extension
 ************************/
typedef enum {
        CHIP8,
        SUPERCHIP,
        XOCHIP,
} Chip8Extension;

/************************
 * Configuration parameters for the CHIP-8 emulator.
 ************************/
const int WINDOW_WIDTH = 64;
const int WINDOW_HEIGHT = 32;
const uint32_t FG_COLOR = 0xFFFFFFFF;
const uint32_t BG_COLOR = 0x000000FF;
const int SCALE_FACTOR = 20;
const bool PIXEL_OUTLINES = true;
const int INSTS_PER_SECOND = 600;
const int SQUARE_WAVE_FREQ = 440;
const int AUDIO_SAMPLE_RATE = 44100;
int VOLUME = 3000;
float COLOR_LERP_RATE = 0.7f;
const Chip8Extension CURRENT_EXTENSION = CHIP8;

/********** Chip8State **********
 * Complete state of the CHIP-8 emulator.
 *
 * Fields:
 *      state:        Current emulator state
 *      ram:          4KB of RAM
 *      display:      64x32 display buffer
 *      pixel_color:  Color for each pixel
 *      stack:        Call stack
 *      stack_ptr:    Current stack pointer
 *      V:            16 general-purpose registers
 *      I:            Index register
 *      PC:           Program counter
 *      delay_timer:  Delay timer
 *      sound_timer:  Sound timer
 *      keypad:       Keypad state
 *      rom_name:     Current ROM filename
 *      draw:         Whether display needs update
 ************************/
typedef struct {
        EmulatorState state;
        uint8_t ram[4096];
        bool display[64 * 32];
        uint32_t pixel_color[64 * 32];
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
} Chip8State;

/********** color_lerp **********
 * Performs linear interpolation between two colors.
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
 *      Each color component is interpolated independently
 ************************/
uint32_t color_lerp(const uint32_t start_color, const uint32_t end_color,
                    const float t)
{
        const uint8_t s_r = (start_color >> 24) & 0xFF;
        const uint8_t s_g = (start_color >> 16) & 0xFF;
        const uint8_t s_b = (start_color >> 8) & 0xFF;
        const uint8_t s_a = (start_color >> 0) & 0xFF;

        const uint8_t e_r = (end_color >> 24) & 0xFF;
        const uint8_t e_g = (end_color >> 16) & 0xFF;
        const uint8_t e_b = (end_color >> 8) & 0xFF;
        const uint8_t e_a = (end_color >> 0) & 0xFF;

        const uint8_t ret_r = ((1 - t) * s_r) + (t * e_r);
        const uint8_t ret_g = ((1 - t) * s_g) + (t * e_g);
        const uint8_t ret_b = ((1 - t) * s_b) + (t * e_b);
        const uint8_t ret_a = ((1 - t) * s_a) + (t * e_a);

        return (ret_r << 24) | (ret_g << 16) | (ret_b << 8) | ret_a;
}

/********** audio_callback **********
 * SDL audio callback function that generates a square wave.
 *
 * Parameters:
 *      stream:     Audio output buffer
 *      len:        Length of the buffer in bytes
 *
 * Notes:
 *      Called by SDL when more audio data is needed
 *      Generates a square wave at the configured frequency
 ************************/
void audio_callback(void *userdata, Uint8 *stream, int len)
{
        int16_t *audio_data = (int16_t *)stream;
        static uint32_t running_sample_index = 0;
        const int32_t square_wave_period = AUDIO_SAMPLE_RATE / SQUARE_WAVE_FREQ;
        const int32_t half_square_wave_period = square_wave_period / 2;

        for (int i = 0; i < len / 2; i++) {
                audio_data[i] =
                    ((running_sample_index++ / half_square_wave_period) % 2)
                        ? VOLUME
                        : -VOLUME;
        }
        (void)userdata;
}

/********** init_SDL **********
 * Initializes SDL subsystems and creates the main window.
 *
 * Parameters:
 *      SDL:     Pointer to SDL_Context to initialize
 *
 * Returns:
 *      bool:    true if initialization succeeded, false otherwise
 *
 * Notes:
 *      Initializes video, audio, and timer subsystems
 *      Creates a window and renderer with the specified dimensions
 ************************/
bool init_SDL(SDL_Context *SDL)
{
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
                SDL_Log("Could not initialize SDL subsystems! %s\n",
                        SDL_GetError());
                return false;
        }

        SDL->window = SDL_CreateWindow(
            "CHIP8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            WINDOW_WIDTH * SCALE_FACTOR, WINDOW_HEIGHT * SCALE_FACTOR, 0);
        if (!SDL->window) {
                SDL_Log("Could not create SDL window %s\n", SDL_GetError());
                return false;
        }

        SDL->renderer =
            SDL_CreateRenderer(SDL->window, -1, SDL_RENDERER_ACCELERATED);
        if (!SDL->renderer) {
                SDL_Log("Could not create SDL renderer %s\n", SDL_GetError());
                return false;
        }

        SDL->want = (SDL_AudioSpec){
            .freq = 44100,
            .format = AUDIO_S16LSB,
            .channels = 1,
            .samples = 512,
            .callback = audio_callback,
            .userdata = NULL,
        };

        SDL->dev = SDL_OpenAudioDevice(NULL, 0, &SDL->want, &SDL->have, 0);

        if (SDL->dev == 0) {
                SDL_Log("Could not get an Audio Device %s\n", SDL_GetError());
                return false;
        }

        if ((SDL->want.format != SDL->have.format) ||
            (SDL->want.channels != SDL->have.channels)) {

                SDL_Log("Could not get desired Audio Spec\n");
                return false;
        }

        return true;
}

/********** init_chip8 **********
 * Initializes the CHIP-8 emulator state.
 *
 * Parameters:
 *      chip8:     Pointer to Chip8State to initialize
 *      rom_name:  Path to the ROM file to load
 *
 * Returns:
 *      bool:      true if initialization succeeded, false otherwise
 *
 * Notes:
 *      Loads the font data into memory
 *      Loads the ROM file starting at address 0x200
 *      Initializes all registers and timers to zero
 ************************/
bool init_chip8(Chip8State *chip8, const char rom_name[])
{
        const uint32_t entry_point = 0x200;
        const uint8_t font[] = {
            0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70,
            0xF0, 0x10, 0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0,
            0x90, 0x90, 0xF0, 0x10, 0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0,
            0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0, 0x10, 0x20, 0x40, 0x40,
            0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0, 0x10, 0xF0,
            0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
            0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0,
            0xF0, 0x80, 0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80,
        };

        memset(chip8, 0, sizeof(Chip8State));

        memcpy(&chip8->ram[0], font, sizeof(font));

        FILE *rom = fopen(rom_name, "rb");
        if (!rom) {
                SDL_Log("Rom file %s is invalid or does not exist\n", rom_name);
                return false;
        }

        fseek(rom, 0, SEEK_END);
        const size_t rom_size = ftell(rom);
        const size_t max_size = sizeof chip8->ram - entry_point;
        rewind(rom);

        if (rom_size > max_size) {
                SDL_Log("Rom file %s is too big! Rom size: %llu, Max size "
                        "allowed: %llu\n",
                        rom_name, (long long unsigned)rom_size,
                        (long long unsigned)max_size);
                return false;
        }

        if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1) {
                SDL_Log("Could not read Rom file %s into CHIP8 memory\n",
                        rom_name);
                return false;
        }
        fclose(rom);

        chip8->state = RUNNING;
        chip8->PC = entry_point;
        chip8->rom_name = rom_name;
        chip8->stack_ptr = &chip8->stack[0];
        memset(&chip8->pixel_color[0], BG_COLOR, sizeof chip8->pixel_color);
        return true;
}

/********** final_cleanup **********
 * Cleans up SDL resources and shuts down the emulator.
 *
 * Parameters:
 *      SDL:    SDL_Context containing resources to clean up
 *
 * Notes:
 *      Destroys the renderer and window
 *      Closes the audio device
 *      Shuts down all SDL subsystems
 ************************/
void final_cleanup(const SDL_Context SDL)
{
        SDL_DestroyRenderer(SDL.renderer);
        SDL_DestroyWindow(SDL.window);
        SDL_CloseAudioDevice(SDL.dev);
        SDL_Quit();
}

/********** clear_screen **********
 * Clears the SDL window with the background color.
 *
 * Parameters:
 *      SDL:     SDL_Context containing the renderer
 *
 * Notes:
 *      Sets the render draw color to the background color
 *      Clears the entire window
 ************************/
void clear_screen(const SDL_Context SDL)
{
        const uint8_t r = (BG_COLOR >> 24) & 0xFF;
        const uint8_t g = (BG_COLOR >> 16) & 0xFF;
        const uint8_t b = (BG_COLOR >> 8) & 0xFF;
        const uint8_t a = (BG_COLOR >> 0) & 0xFF;

        SDL_SetRenderDrawColor(SDL.renderer, r, g, b, a);
        SDL_RenderClear(SDL.renderer);
}

/********** update_screen **********
 * Updates the display with the current CHIP-8 screen state.
 *
 * Parameters:
 *      SDL:     SDL_Context containing the renderer
 *      chip8:   Pointer to Chip8State containing display data
 *
 * Notes:
 *      Renders each pixel with color interpolation
 *      Optionally draws pixel outlines
 *      Updates the renderer with the new display state
 ************************/
void update_screen(const SDL_Context SDL, Chip8State *chip8)
{
        SDL_Rect rect = {.x = 0, .y = 0, .w = SCALE_FACTOR, .h = SCALE_FACTOR};

        const uint8_t bg_r = (BG_COLOR >> 24) & 0xFF;
        const uint8_t bg_g = (BG_COLOR >> 16) & 0xFF;
        const uint8_t bg_b = (BG_COLOR >> 8) & 0xFF;
        const uint8_t bg_a = (BG_COLOR >> 0) & 0xFF;

        for (uint32_t i = 0; i < sizeof chip8->display; i++) {
                rect.x = (i % WINDOW_WIDTH) * SCALE_FACTOR;
                rect.y = (i / WINDOW_WIDTH) * SCALE_FACTOR;

                if (chip8->display[i]) {
                        if (chip8->pixel_color[i] != FG_COLOR) {
                                chip8->pixel_color[i] =
                                    color_lerp(chip8->pixel_color[i], FG_COLOR,
                                               COLOR_LERP_RATE);
                        }

                        const uint8_t r = (chip8->pixel_color[i] >> 24) & 0xFF;
                        const uint8_t g = (chip8->pixel_color[i] >> 16) & 0xFF;
                        const uint8_t b = (chip8->pixel_color[i] >> 8) & 0xFF;
                        const uint8_t a = (chip8->pixel_color[i] >> 0) & 0xFF;

                        SDL_SetRenderDrawColor(SDL.renderer, r, g, b, a);
                        SDL_RenderFillRect(SDL.renderer, &rect);

                        if (PIXEL_OUTLINES) {
                                SDL_SetRenderDrawColor(SDL.renderer, bg_r, bg_g,
                                                       bg_b, bg_a);
                                SDL_RenderDrawRect(SDL.renderer, &rect);
                        }

                } else {
                        if (chip8->pixel_color[i] != BG_COLOR) {
                                chip8->pixel_color[i] =
                                    color_lerp(chip8->pixel_color[i], BG_COLOR,
                                               COLOR_LERP_RATE);
                        }

                        const uint8_t r = (chip8->pixel_color[i] >> 24) & 0xFF;
                        const uint8_t g = (chip8->pixel_color[i] >> 16) & 0xFF;
                        const uint8_t b = (chip8->pixel_color[i] >> 8) & 0xFF;
                        const uint8_t a = (chip8->pixel_color[i] >> 0) & 0xFF;

                        SDL_SetRenderDrawColor(SDL.renderer, r, g, b, a);
                        SDL_RenderFillRect(SDL.renderer, &rect);
                }
        }

        SDL_RenderPresent(SDL.renderer);
}

/********** save_state **********
 * Saves the current emulator state to a file.
 *
 * Parameters:
 *      chip8:     Pointer to Chip8State to save
 *      filename:  Path to the save file
 *
 * Returns:
 *      bool:      true if save succeeded, false otherwise
 *
 * Notes:
 *      Saves the entire emulator state in binary format
 *      Creates a new file or overwrites an existing one
 ************************/
bool save_state(const Chip8State *chip8, const char *filename)
{
        FILE *file = fopen(filename, "wb");
        if (!file) {
                SDL_Log("Failed to open file %s for saving state\n", filename);
                return false;
        }

        if (fwrite(chip8, sizeof(Chip8State), 1, file) != 1) {
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
 *      chip8:     Pointer to Chip8State to load into
 *      filename:  Path to the save file
 *
 * Returns:
 *      bool:      true if load succeeded, false otherwise
 *
 * Notes:
 *      Loads the entire emulator state from binary format
 *      File must have been created by save_state
 ************************/
bool load_state(Chip8State *chip8, const char *filename)
{
        FILE *file = fopen(filename, "rb");
        if (!file) {
                SDL_Log("Failed to open file %s for loading state\n", filename);
                return false;
        }

        if (fread(chip8, sizeof(Chip8State), 1, file) != 1) {
                SDL_Log("Failed to read state from file %s\n", filename);
                fclose(file);
                return false;
        }

        fclose(file);
        return true;
}

/********** handle_input **********
 * Processes SDL input events and updates emulator state.
 *
 * Parameters:
 *      chip8:     Pointer to Chip8State to update
 *
 * Notes:
 *      Handles window close events
 *      Processes keyboard input for emulator control
 *      Updates keypad state based on key presses
 *      Supports save/load state with F5/F9
 ************************/
void handle_input(Chip8State *chip8)
{
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT:
                        chip8->state = QUIT;
                        break;

                case SDL_KEYDOWN:
                        switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                                chip8->state = QUIT;
                                break;

                        case SDLK_SPACE:
                                if (chip8->state == RUNNING) {
                                        chip8->state = PAUSED;
                                        puts("==== PAUSED ====");
                                } else {
                                        chip8->state = RUNNING;
                                }
                                break;

                        case SDLK_EQUALS:
                                init_chip8(chip8, chip8->rom_name);
                                break;

                        case SDLK_j:
                                if (COLOR_LERP_RATE > 0.1) {
                                        COLOR_LERP_RATE -= 0.1;
                                }
                                break;

                        case SDLK_k:
                                if (COLOR_LERP_RATE < 1.0) {
                                        COLOR_LERP_RATE += 0.1;
                                }
                                break;

                        case SDLK_o:
                                if (VOLUME > 0) {
                                        VOLUME -= 500;
                                }
                                break;

                        case SDLK_p:
                                if (VOLUME < INT16_MAX) {
                                        VOLUME += 500;
                                }
                                break;

                        case SDLK_F5:
                                if (save_state(chip8, "save_state.bin")) {
                                        puts("State saved successfully.");
                                } else {
                                        puts("Failed to save state.");
                                }
                                break;

                        case SDLK_F9:
                                if (load_state(chip8, "save_state.bin")) {
                                        puts("State loaded successfully.");
                                } else {
                                        puts("Failed to load state.");
                                }
                                break;

                        case SDLK_1:
                                chip8->keypad[0x1] = true;
                                break;
                        case SDLK_2:
                                chip8->keypad[0x2] = true;
                                break;
                        case SDLK_3:
                                chip8->keypad[0x3] = true;
                                break;
                        case SDLK_4:
                                chip8->keypad[0xC] = true;
                                break;

                        case SDLK_q:
                                chip8->keypad[0x4] = true;
                                break;
                        case SDLK_w:
                                chip8->keypad[0x5] = true;
                                break;
                        case SDLK_e:
                                chip8->keypad[0x6] = true;
                                break;
                        case SDLK_r:
                                chip8->keypad[0xD] = true;
                                break;

                        case SDLK_a:
                                chip8->keypad[0x7] = true;
                                break;
                        case SDLK_s:
                                chip8->keypad[0x8] = true;
                                break;
                        case SDLK_d:
                                chip8->keypad[0x9] = true;
                                break;
                        case SDLK_f:
                                chip8->keypad[0xE] = true;
                                break;

                        case SDLK_z:
                                chip8->keypad[0xA] = true;
                                break;
                        case SDLK_x:
                                chip8->keypad[0x0] = true;
                                break;
                        case SDLK_c:
                                chip8->keypad[0xB] = true;
                                break;
                        case SDLK_v:
                                chip8->keypad[0xF] = true;
                                break;

                        default:
                                break;
                        }
                        break;

                case SDL_KEYUP:
                        switch (event.key.keysym.sym) {
                        case SDLK_1:
                                chip8->keypad[0x1] = false;
                                break;
                        case SDLK_2:
                                chip8->keypad[0x2] = false;
                                break;
                        case SDLK_3:
                                chip8->keypad[0x3] = false;
                                break;
                        case SDLK_4:
                                chip8->keypad[0xC] = false;
                                break;

                        case SDLK_q:
                                chip8->keypad[0x4] = false;
                                break;
                        case SDLK_w:
                                chip8->keypad[0x5] = false;
                                break;
                        case SDLK_e:
                                chip8->keypad[0x6] = false;
                                break;
                        case SDLK_r:
                                chip8->keypad[0xD] = false;
                                break;

                        case SDLK_a:
                                chip8->keypad[0x7] = false;
                                break;
                        case SDLK_s:
                                chip8->keypad[0x8] = false;
                                break;
                        case SDLK_d:
                                chip8->keypad[0x9] = false;
                                break;
                        case SDLK_f:
                                chip8->keypad[0xE] = false;
                                break;

                        case SDLK_z:
                                chip8->keypad[0xA] = false;
                                break;
                        case SDLK_x:
                                chip8->keypad[0x0] = false;
                                break;
                        case SDLK_c:
                                chip8->keypad[0xB] = false;
                                break;
                        case SDLK_v:
                                chip8->keypad[0xF] = false;
                                break;

                        default:
                                break;
                        }
                        break;

                default:
                        break;
                }
        }
}

/********** emulate_instruction **********
 * Executes a single CHIP-8 instruction.
 *
 * Parameters:
 *      chip8:     Pointer to Chip8State to update
 *
 * Notes:
 *      Decodes and executes the instruction at PC
 *      Updates PC to point to the next instruction
 *      Handles all CHIP-8 opcodes and extensions
 *      Sets draw flag when display is modified
 ************************/
void emulate_instruction(Chip8State *chip8)
{
        bool carry;
        const uint16_t opcode =
            (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC + 1];
        chip8->PC += 2;
        const uint16_t NNN = opcode & 0x0FFF;
        const uint8_t NN = opcode & 0xFF;
        const uint8_t N = opcode & 0xF;
        const uint8_t X = (opcode >> 8) & 0xF;
        const uint8_t Y = (opcode >> 4) & 0xF;

        switch ((opcode >> 12) & 0x0F) {
        case 0x00:
                if (NN == 0xE0) {
                        memset(&chip8->display[0], false,
                               sizeof chip8->display);
                        chip8->draw = true;
                } else if (NN == 0xEE) {
                        chip8->PC = *--chip8->stack_ptr;

                } else {
                }

                break;

        case 0x01:
                chip8->PC = NNN;
                break;

        case 0x02:
                *chip8->stack_ptr++ = chip8->PC;
                chip8->PC = NNN;
                break;

        case 0x03:
                if (chip8->V[X] == NN) {
                        chip8->PC += 2;
                }
                break;

        case 0x04:
                if (chip8->V[X] != NN) {
                        chip8->PC += 2;
                }
                break;

        case 0x05:
                if (N != 0) {
                        break;
                }

                if (chip8->V[X] == chip8->V[Y]) {
                        chip8->PC += 2;
                }

                break;

        case 0x06:
                chip8->V[X] = NN;
                break;

        case 0x07:
                chip8->V[X] += NN;
                break;

        case 0x08:
                switch (N) {
                case 0:
                        chip8->V[X] = chip8->V[Y];
                        break;

                case 1:
                        chip8->V[X] |= chip8->V[Y];
                        if (CURRENT_EXTENSION == CHIP8) {
                                chip8->V[0xF] = 0;
                        }
                        break;

                case 2:
                        chip8->V[X] &= chip8->V[Y];
                        if (CURRENT_EXTENSION == CHIP8) {
                                chip8->V[0xF] = 0;
                        }
                        break;

                case 3:
                        chip8->V[X] ^= chip8->V[Y];
                        if (CURRENT_EXTENSION == CHIP8) {
                                chip8->V[0xF] = 0;
                        }
                        break;

                case 4:
                        carry = ((uint16_t)(chip8->V[X] + chip8->V[Y]) > 255);

                        chip8->V[X] += chip8->V[Y];
                        chip8->V[0xF] = carry;
                        break;

                case 5:
                        carry = (chip8->V[Y] <= chip8->V[X]);

                        chip8->V[X] -= chip8->V[Y];
                        chip8->V[0xF] = carry;
                        break;

                case 6:
                        if (CURRENT_EXTENSION == CHIP8) {
                                carry = chip8->V[Y] & 1;
                                chip8->V[X] = chip8->V[Y] >> 1;
                        } else {
                                carry = chip8->V[X] & 1;
                                chip8->V[X] >>= 1;
                        }

                        chip8->V[0xF] = carry;
                        break;

                case 7:
                        carry = (chip8->V[X] <= chip8->V[Y]);

                        chip8->V[X] = chip8->V[Y] - chip8->V[X];
                        chip8->V[0xF] = carry;
                        break;

                case 0xE:
                        if (CURRENT_EXTENSION == CHIP8) {
                                carry = (chip8->V[Y] & 0x80) >> 7;
                                chip8->V[X] = chip8->V[Y] << 1;
                        } else {
                                carry = (chip8->V[X] & 0x80) >> 7;
                                chip8->V[X] <<= 1;
                        }

                        chip8->V[0xF] = carry;
                        break;

                default:
                        break;
                }
                break;

        case 0x09:
                if (chip8->V[X] != chip8->V[Y]) {
                        chip8->PC += 2;
                }
                break;

        case 0x0A:
                chip8->I = NNN;
                break;

        case 0x0B:
                chip8->PC = chip8->V[0] + NNN;
                break;

        case 0x0C:
                chip8->V[X] = (rand() % 256) & NN;
                break;

        case 0x0D: {
                uint8_t X_coord = chip8->V[X] % WINDOW_WIDTH;
                uint8_t Y_coord = chip8->V[Y] % WINDOW_HEIGHT;
                const uint8_t orig_X = X_coord;
                chip8->V[0xF] = 0;
                for (uint8_t i = 0; i < N; i++) {
                        const uint8_t sprite_data = chip8->ram[chip8->I + i];
                        X_coord = orig_X;
                        for (int8_t j = 7; j >= 0; j--) {
                                bool *pixel =
                                    &chip8->display[Y_coord * WINDOW_WIDTH +
                                                    X_coord];
                                const bool sprite_bit =
                                    (sprite_data & (1 << j));

                                if (sprite_bit && *pixel) {
                                        chip8->V[0xF] = 1;
                                }

                                *pixel ^= sprite_bit;

                                if (++X_coord >= WINDOW_WIDTH) {
                                        break;
                                }
                        }

                        if (++Y_coord >= WINDOW_HEIGHT) {
                                break;
                        }
                }
                chip8->draw = true;
                break;
        }

        case 0x0E:
                if (NN == 0x9E) {
                        if (chip8->keypad[chip8->V[X]]) {
                                chip8->PC += 2;
                        }

                } else if (NN == 0xA1) {
                        if (!chip8->keypad[chip8->V[X]]) {
                                chip8->PC += 2;
                        }
                }
                break;

        case 0x0F:
                switch (NN) {
                case 0x0A: {
                        static bool any_key_pressed = false;
                        static uint8_t key = 0xFF;

                        for (uint8_t i = 0;
                             key == 0xFF && i < sizeof chip8->keypad; i++) {
                                if (chip8->keypad[i]) {
                                        key = i;
                                        any_key_pressed = true;
                                        break;
                                }
                        }

                        if (!any_key_pressed) {
                                chip8->PC -= 2;
                        } else {
                                if (chip8->keypad[key]) {
                                        chip8->PC -= 2;
                                } else {
                                        chip8->V[X] = key;
                                        key = 0xFF;
                                        any_key_pressed = false;
                                }
                        }
                        break;
                }

                case 0x1E:
                        chip8->I += chip8->V[X];
                        break;

                case 0x07:
                        chip8->V[X] = chip8->delay_timer;
                        break;

                case 0x15:
                        chip8->delay_timer = chip8->V[X];
                        break;

                case 0x18:
                        chip8->sound_timer = chip8->V[X];
                        break;

                case 0x29:
                        chip8->I = chip8->V[X] * 5;
                        break;

                case 0x33: {
                        uint8_t bcd = chip8->V[X];
                        chip8->ram[chip8->I + 2] = bcd % 10;
                        bcd /= 10;
                        chip8->ram[chip8->I + 1] = bcd % 10;
                        bcd /= 10;
                        chip8->ram[chip8->I] = bcd;
                        break;
                }

                case 0x55:
                        for (uint8_t i = 0; i <= X; i++) {
                                if (CURRENT_EXTENSION == CHIP8) {
                                        chip8->ram[chip8->I++] = chip8->V[i];
                                } else {
                                        chip8->ram[chip8->I + i] = chip8->V[i];
                                }
                        }
                        break;

                case 0x65:
                        for (uint8_t i = 0; i <= X; i++) {
                                if (CURRENT_EXTENSION == CHIP8) {
                                        chip8->V[i] = chip8->ram[chip8->I++];
                                } else {
                                        chip8->V[i] = chip8->ram[chip8->I + i];
                                }
                        }
                        break;

                default:
                        break;
                }
                break;

        default:
                break;
        }
}

/********** update_timers **********
 * Updates the delay and sound timers.
 *
 * Parameters:
 *      SDL:     SDL_Context containing audio device
 *      chip8:   Pointer to Chip8State containing timers
 *
 * Notes:
 *      Decrements delay timer if non-zero
 *      Decrements sound timer if non-zero
 *      Controls audio output based on sound timer
 ************************/
void update_timers(const SDL_Context SDL, Chip8State *chip8)
{
        if (chip8->delay_timer > 0) {
                chip8->delay_timer--;
        }

        if (chip8->sound_timer > 0) {
                chip8->sound_timer--;
                SDL_PauseAudioDevice(SDL.dev, 0);
        } else {
                SDL_PauseAudioDevice(SDL.dev, 1);
        }
}

/********** main **********
 * Main entry point for the CHIP-8 emulator.
 *s
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
 *      Handles cleanup on exit
 ************************/
int main(int argc, char **argv)
{
        if (argc < 2) {
                fprintf(stderr, "Usage: %s <rom_name>\n", argv[0]);
                exit(EXIT_FAILURE);
        }

        SDL_Context SDL = {0};
        if (!init_SDL(&SDL)) {
                exit(EXIT_FAILURE);
        }

        Chip8State chip8 = {0};
        const char *rom_name = argv[1];
        if (!init_chip8(&chip8, rom_name)) {
                exit(EXIT_FAILURE);
        }

        clear_screen(SDL);

        srand(time(NULL));

        while (chip8.state != QUIT) {
                handle_input(&chip8);

                if (chip8.state == PAUSED) {
                        continue;
                }

                const uint64_t start_frame_time = SDL_GetPerformanceCounter();

                for (uint32_t i = 0; i < (uint32_t)(INSTS_PER_SECOND / 60);
                     i++) {
                        emulate_instruction(&chip8);

                        if ((CURRENT_EXTENSION == CHIP8) &&
                            (((chip8.ram[chip8.PC] << 8) |
                              chip8.ram[chip8.PC + 1] >> 12) == 0xD)) {
                                break;
                        }
                }

                const uint64_t end_frame_time = SDL_GetPerformanceCounter();

                const double time_elapsed =
                    (double)((end_frame_time - start_frame_time) * 1000) /
                    SDL_GetPerformanceFrequency();

                SDL_Delay(16.67f > time_elapsed ? 16.67f - time_elapsed : 0);

                if (chip8.draw) {
                        update_screen(SDL, &chip8);
                        chip8.draw = false;
                }

                update_timers(SDL, &chip8);
        }

        final_cleanup(SDL);

        exit(EXIT_SUCCESS);
}
