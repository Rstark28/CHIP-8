#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"

// Add audio support to the CHIP-8 emulator
#include <SDL.h>

// SDL Audio callback function
void audio_callback(void *userdata, uint8_t *stream, int len)
{
        Config_t *config = (Config_t *)userdata;

        int16_t *audio_data = (int16_t *)stream;
        static uint32_t running_sample_index = 0;
        const int32_t square_wave_period = config->speed / config->scale;
        const int32_t half_square_wave_period = square_wave_period / 2;

        for (int i = 0; i < len / 2; i++) {
                if ((running_sample_index / half_square_wave_period) % 2 == 0) {
                        audio_data[i] = config->speed;
                } else {
                        audio_data[i] = -config->speed;
                }
                running_sample_index++;
        }
}

// Add color interpolation function
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

typedef struct {
        SDL_Window *window;     // Pointer to the SDL window
        SDL_Renderer *renderer; // Pointer to the SDL renderer
} SDL_t;

typedef struct {
        uint32_t window_width;     // Width of the window
        uint32_t window_height;    // Height of the window
        uint32_t foreground_color; // Foreground color
        uint32_t background_color; // Background color
        uint32_t scale;            // Scale factor for the window
        uint32_t speed;            // Speed of the emulator
} Config_t;

typedef enum {
        QUIT,
        RUNNING,
        PAUSED,
} State_t;

typedef struct {
        uint16_t opcode;
        uint16_t NNN;
        uint8_t NN;
        uint8_t N;
        uint8_t X;
        uint8_t Y;
} Instruction_t;

// Add support for CHIP-8 extensions
typedef enum {
        CHIP8,
        SUPERCHIP,
        XOCHIP,
} Extension_t;

// Update the CHIP-8 structure to include extension support
typedef struct {
        State_t state;                 // Current state of the emulator
        uint8_t memory[4096];          // Memory
        bool display[64 * 32];         // Display buffer
        uint32_t pixel_color[64 * 32]; // Pixel colors for display
        uint16_t stack[12];            // Subroutine stack
        uint16_t *stack_ptr;
        uint8_t V[16];         // General purpose registers
        uint16_t I;            // Index register
        uint16_t PC;           // Program counter
        uint8_t delay_timer;   // Delay timer
        uint8_t sound_timer;   // Sound timer
        bool key[16];          // Keypad state
        char *rom;             // ROM filename
        Instruction_t inst;    // Current instruction
        Extension_t extension; // Current extension mode
} CHIP8_t;

bool init_SDL(SDL_t *SDL, Config_t *config)
{
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
                SDL_Log("Unable to initialize SDL: %s\n", SDL_GetError());
                return false;
        }

        SDL->window = SDL_CreateWindow(
            "CHIP8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            config->window_width * config->scale,
            config->window_height * config->scale, 0);

        if (!SDL->window) {
                SDL_Log("Could not create window: %s\n", SDL_GetError());
                SDL_Quit();
                return false;
        }

        SDL->renderer =
            SDL_CreateRenderer(SDL->window, -1, SDL_RENDERER_ACCELERATED);

        if (!SDL->renderer) {
                SDL_Log("Could not create renderer: %s\n", SDL_GetError());
                SDL_DestroyWindow(SDL->window);
                SDL_Quit();
                return false;
        }

        return true;
}

bool set_CONFIG(Config_t *config, int argc, char **argv)
{
        (void)argc;
        (void)argv;

        *config = (Config_t){
            .window_width = 64,             // CHIP8 screen width
            .window_height = 32,            // CHIP8 screen height
            .foreground_color = 0xFFFFFFFF, // White color
            .background_color = 0x00000000, // Black color
            .scale = 10,                    // Scale factor
            .speed = 500,                   // Speed of the emulator
        };

        for (int i = 0; i < argc; i++) {
                (void)argv[i];
        }

        return true;
}

bool init_CHIP8(CHIP8_t *chip8, char rom[])
{
        const uint8_t fontset[80] = {
            // Fontset data (0-F)
            0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
            0x20, 0x60, 0x20, 0x20, 0x70, // 1
            0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
            0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
            0x90, 0x90, 0xF0, 0x10, 0x10, // 4
            0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
            0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
            0xF0, 0x10, 0x20, 0x40, 0x40, // 7
            0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
            0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
            0xF0, 0x90, 0xF0, 0x90, 0x90, // A
            0xE0, 0x90, 0xE0, 0x90, 0xF0, // B
            0xF0, 0x80, 0x80, 0x80, 0xF0, // C
            0xF0, 0x80, 0xF0, 0x90, 0xF0, // D
            0xF0, 0x80, 0xF0, 0x80, 0x80, // E
            0xF0, 0x90, 0xF0, 0x80, 0x80, // F
        };

        memcpy(chip8->memory, fontset,
               sizeof(fontset)); // Load fontset into memory

        FILE *rom_file = fopen(rom, "rb");
        if (!rom_file) {
                SDL_Log("Failed to open ROM file: %s\n", rom);
                return false;
        }

        // Check ROM size
        fseek(rom_file, 0, SEEK_END);
        size_t rom_size = ftell(rom_file); // Get the size of the file
        rewind(rom_file);

        const size_t max_rom_size = sizeof(chip8->memory) - 0x200;
        if (rom_size > max_rom_size) {
                SDL_Log("ROM size exceeds memory limit\n");
                fclose(rom_file);
                return false;
        }

        if (fread(chip8->memory + 0x200, 1, rom_size, rom_file) != rom_size) {
                SDL_Log("Could not read ROM into memory\n");
                fclose(rom_file);
                return false;
        }

        fclose(rom_file);

        // Default values
        chip8->state = RUNNING; // Set the initial state to RUNNING
        chip8->PC = 0x200;      // Entry point
        chip8->rom = rom;
        chip8->stack_ptr = &chip8->stack[0];

        return true;
}

void cleanup(SDL_t *SDL)
{
        SDL_DestroyRenderer(SDL->renderer); // Destroy the renderer
        SDL_DestroyWindow(SDL->window);     // Destroy the window
        SDL_Quit();                         // Clean up SDL resources
}

void clear_window(SDL_t *SDL, Config_t *config)
{
        // Clear the window with the background color
        const uint8_t r = (config->background_color >> 24) & 0xFF;
        const uint8_t g = (config->background_color >> 16) & 0xFF;
        const uint8_t b = (config->background_color >> 8) & 0xFF;
        const uint8_t a = (config->background_color >> 0) & 0xFF;

        SDL_SetRenderDrawColor(SDL->renderer, r, g, b, a); // Set the draw color
        SDL_RenderClear(SDL->renderer);                    // Clear the window
}

void update_window(SDL_t *SDL, const Config_t *config, const CHIP8_t chip8)
{
        SDL_Rect rect = {
            .x = 0, .y = 0, .w = config->scale, .h = config->scale};

        // Color grab
        const uint8_t fg_r = (config->foreground_color >> 24) & 0xFF;
        const uint8_t fg_g = (config->foreground_color >> 16) & 0xFF;
        const uint8_t fg_b = (config->foreground_color >> 8) & 0xFF;
        const uint8_t fg_a = (config->foreground_color >> 0) & 0xFF;

        const uint8_t bg_r = (config->background_color >> 24) & 0xFF;
        const uint8_t bg_g = (config->background_color >> 16) & 0xFF;
        const uint8_t bg_b = (config->background_color >> 8) & 0xFF;
        const uint8_t bg_a = (config->background_color >> 0) & 0xFF;

        for (uint32_t i = 0; i < sizeof(chip8.display); i++) {
                rect.x = i % config->window_width * config->scale;
                rect.y = i / config->window_width * config->scale;

                if (chip8.display[i]) {
                        // Pixel on, draw foreground color
                        SDL_SetRenderDrawColor(SDL->renderer, fg_r, fg_g, fg_b,
                                               fg_a);
                        SDL_RenderFillRect(SDL->renderer, &rect);
                } else {
                        // Pixel off, draw background color
                        SDL_SetRenderDrawColor(SDL->renderer, bg_r, bg_g, bg_b,
                                               bg_a);
                        SDL_RenderFillRect(SDL->renderer, &rect);
                }
        }

        SDL_RenderPresent(SDL->renderer); // Update the window with the renderer
}

// CHIP8 Keypad mapping
// 1 2 3 C      1 2 3 4
// 4 5 6 D      Q W E R
// 7 8 9 E      A S D F
// A 0 B F      Z X C V
void handle_input(CHIP8_t *chip8)
{
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT:
                        chip8->state = QUIT; // Set state to QUIT
                        break;
                case SDL_KEYDOWN:
                        switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                                chip8->state = QUIT; // Set state to QUIT
                                break;
                        case SDLK_SPACE:
                                if (chip8->state == RUNNING) {
                                        chip8->state = PAUSED;
                                        fprintf(stderr, "PAUSED\n");
                                } else {
                                        chip8->state = RUNNING;
                                }
                                break;
                        case SDLK_1:
                                chip8->key[0x1] = true;
                                break;
                        case SDLK_2:
                                chip8->key[0x2] = true;
                                break;
                        case SDLK_3:
                                chip8->key[0x3] = true;
                                break;
                        case SDLK_4:
                                chip8->key[0xC] = true;
                                break;
                        case SDLK_q:
                                chip8->key[0x4] = true;
                                break;
                        case SDLK_w:
                                chip8->key[0x5] = true;
                                break;
                        case SDLK_e:
                                chip8->key[0x6] = true;
                                break;
                        case SDLK_r:
                                chip8->key[0xD] = true;
                                break;
                        case SDLK_a:
                                chip8->key[0x7] = true;
                                break;
                        case SDLK_s:
                                chip8->key[0x8] = true;
                                break;
                        case SDLK_d:
                                chip8->key[0x9] = true;
                                break;
                        case SDLK_f:
                                chip8->key[0xE] = true;
                                break;
                        case SDLK_z:
                                chip8->key[0xA] = true;
                                break;
                        case SDLK_x:
                                chip8->key[0x0] = true;
                                break;
                        case SDLK_c:
                                chip8->key[0xB] = true;
                                break;
                        case SDLK_v:
                                chip8->key[0xF] = true;
                                break;
                        default:
                                // Handle other key events here
                                break;
                        }
                        break;
                case SDL_KEYUP:
                        switch (event.key.keysym.sym) {
                        case SDLK_1:
                                chip8->key[0x1] = false;
                                break;
                        case SDLK_2:
                                chip8->key[0x2] = false;
                                break;
                        case SDLK_3:
                                chip8->key[0x3] = false;
                                break;
                        case SDLK_4:
                                chip8->key[0xC] = false;
                                break;
                        case SDLK_q:
                                chip8->key[0x4] = false;
                                break;
                        case SDLK_w:
                                chip8->key[0x5] = false;
                                break;
                        case SDLK_e:
                                chip8->key[0x6] = false;
                                break;
                        case SDLK_r:
                                chip8->key[0xD] = false;
                                break;
                        case SDLK_a:
                                chip8->key[0x7] = false;
                                break;
                        case SDLK_s:
                                chip8->key[0x8] = false;
                                break;
                        case SDLK_d:
                                chip8->key[0x9] = false;
                                break;
                        case SDLK_f:
                                chip8->key[0xE] = false;
                                break;
                        case SDLK_z:
                                chip8->key[0xA] = false;
                                break;
                        case SDLK_x:
                                chip8->key[0x0] = false;
                                break;
                        case SDLK_c:
                                chip8->key[0xB] = false;
                                break;
                        case SDLK_v:
                                chip8->key[0xF] = false;
                                break;
                        default:
                                break;
                        }
                        break;
                default:
                        // Handle key events here
                        break;
                }
        }
}

#ifdef DEBUG
void print_debug_info(CHIP8_t *chip8)
{
        printf("Address: 0x%04X, Opcode: 0x%04X, Desc: ", chip8->PC - 2,
               chip8->inst.opcode);
        switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
                if (chip8->inst.NN == 0xE0) {
                        printf("Clear screen\n");
                } else if (chip8->inst.NN == 0xEE) {
                        printf("Return from subroutine to address "
                               "0x%04X\n",
                               *(chip8->stack_ptr - 1));
                }
                break;
        case 0x01:
                printf("PC set to NNN (0x%04X)\n", chip8->inst.NNN);
                break;

        case 0x02:
                *chip8->stack_ptr++ = chip8->PC;
                chip8->PC = chip8->inst.NNN;
                break;
        case 0x03:
                printf("If V%X (0x%02X) == NN (0x%02X), skip next instr\n",
                       chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
                break;
        case 0x04:
                printf("If V%X (0x%02X) != NN (0x%02X), skip next instr\n",
                       chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
                break;
        case 0x05:
                printf("If V%X (0x%02X) == V%X (0x%02X), skip next "
                       "instr\n",
                       chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y,
                       chip8->V[chip8->inst.Y]);
                break;
        case 0x06:
                printf("Set register V%X = NN (0x%02X)\n", chip8->inst.X,
                       chip8->inst.NN);
                break;
        case 0x07:
                printf("Set register V%X (0x%02X) += NN (0x%02X). Result: "
                       "0x%02X\n",
                       chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN,
                       chip8->V[chip8->inst.X] + chip8->inst.NN);
                break;
        case 0x08:
                switch (chip8->inst.N) {
                case 0:
                        printf("Set V%X (0x%02X) += V%X (0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X],
                               chip8->inst.Y, chip8->V[chip8->inst.Y]);
                        break;
                case 1:
                        printf("Set V%X (0x%02X) |= V%X (0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X],
                               chip8->inst.Y, chip8->V[chip8->inst.Y]);
                        break;
                case 2:
                        printf("Set V%X (0x%02X) &= V%X (0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X],
                               chip8->inst.Y, chip8->V[chip8->inst.Y]);
                        break;
                case 3:
                        printf("Set V%X (0x%02X) ^= V%X (0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X],
                               chip8->inst.Y, chip8->V[chip8->inst.Y]);
                        break;
                case 4:
                        printf("Set V%X (0x%02X) += V%X (0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X],
                               chip8->inst.Y, chip8->V[chip8->inst.Y]);
                        break;
                case 5:
                        printf("Set V%X (0x%02X) -= V%X (0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X],
                               chip8->inst.Y, chip8->V[chip8->inst.Y]);
                        break;
                case 6:
                        printf("Shift right: Set VF = LSB of V%X "
                               "(0x%02X), then shift right\n",
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                case 7:
                        printf("Set V%X (0x%02X) = V%X (0x%02X) - V%X "
                               "(0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X],
                               chip8->inst.Y, chip8->V[chip8->inst.Y],
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                case 0xE:
                        printf("Shift left: Set VF = MSB of V%X "
                               "(0x%02X), then shift left\n",
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                default:
                        printf("Unimplemented\n");
                        break;
                }
                break;
        case 0x09:
                printf("If V%X (0x%02X) != V%X (0x%02X), skip next "
                       "instr\n",
                       chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y,
                       chip8->V[chip8->inst.Y]);
                break;
        case 0x0A:
                printf("Set I to NNN (0x%04X)\n", chip8->inst.NNN);
                break;
        case 0x0B:
                printf("Jump to NNN (0x%04X) + V0 (0x%02X)\n", chip8->inst.NNN,
                       chip8->V[0]);
                break;
        case 0x0C:
                printf("Set V%X (0x%02X) to random number AND NN "
                       "(0x%02X)\n",
                       chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
                break;
        case 0x0D:
                printf("Draw N (%u) height sprite at V%X (0x%02X), V%X "
                       "(0x%02X) from memory location I (0x%04X).\n",
                       chip8->inst.N, chip8->inst.X, chip8->V[chip8->inst.X],
                       chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->I);
                break;
        case 0x0E:
                switch (chip8->inst.NN) {
                case 0x9E:
                        printf("Skip next instruction if key in V%X "
                               "(0x%02X) is pressed\n",
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                case 0xA1:
                        printf("Skip next instruction if key in V%X "
                               "(0x%02X) is not pressed\n",
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                default:
                        printf("Unimplemented\n");
                        break;
                }
                break;
        case 0x0F:
                switch (chip8->inst.NN) {
                case 0x07:
                        printf("Set V%X (0x%02X) to delay timer\n",
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                case 0x0A:
                        printf("Wait for key press and store in V%X "
                               "(0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                case 0x15:
                        printf("Set delay timer to V%X (0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                case 0x18:
                        printf("Set sound timer to V%X (0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                case 0x1E:
                        printf("Add V%X (0x%02X) to I (0x%04X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X],
                               chip8->I);
                        break;
                case 0x29:
                        printf("Set I to the location of the sprite "
                               "for digit "
                               "V%X (0x%02X)\n",
                               chip8->inst.X, chip8->V[chip8->inst.X]);
                        break;
                case 0x33:
                        printf("Store BCD representation of V%X "
                               "(0x%02X) in "
                               "memory at I (0x%04X), I+1, and I+2\n",
                               chip8->inst.X, chip8->V[chip8->inst.X],
                               chip8->I);
                        break;
                case 0x55:
                        printf("Store registers V0 through V%X in memory "
                               "starting at address I (0x%04X)\n",
                               chip8->inst.X, chip8->I);
                        break;
                case 0x65:
                        printf("Read registers V0 through V%X from memory "
                               "starting at address I (0x%04X)\n",
                               chip8->inst.X, chip8->I);
                        break;
                default:
                        printf("Unimplemented\n");
                        break;
                }
                break;
        default:
                printf("Unimplemented\n");
                break;
        }
}
#endif

void execute_instruction(CHIP8_t *chip8, const Config_t config)
{
        chip8->inst.opcode =
            (chip8->memory[chip8->PC] << 8) | chip8->memory[chip8->PC + 1];
        chip8->PC += 2;

        chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
        chip8->inst.NN = chip8->inst.opcode & 0x0FF;
        chip8->inst.N = chip8->inst.opcode & 0x0F;
        chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
        chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

#ifdef DEBUG
        print_debug_info(chip8);
#endif

        switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
                if (chip8->inst.NN == 0xE0) {
                        memset(&chip8->display[0], false,
                               sizeof(chip8->display));
                } else if (chip8->inst.NN == 0xEE) {
                        chip8->PC = *--chip8->stack_ptr;
                }
                break;
        case 0x01:
                chip8->PC = chip8->inst.NNN;
                break;
        case 0x02:
                *chip8->stack_ptr++ = chip8->PC;
                chip8->PC = chip8->inst.NNN;
                break;
        case 0x03:
                if (chip8->V[chip8->inst.X] == chip8->inst.NN) {
                        chip8->PC += 2;
                }
                break;
        case 0x04:
                if (chip8->V[chip8->inst.X] != chip8->inst.NN) {
                        chip8->PC += 2;
                }
                break;
        case 0x05:
                if (chip8->inst.N != 0) {
                        break;
                }
                if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]) {
                        chip8->PC += 2;
                }
                break;
        case 0x06:
                chip8->V[chip8->inst.X] = chip8->inst.NN;
                break;
        case 0x07:
                chip8->V[chip8->inst.X] += chip8->inst.NN;
                break;
        case 0x08:
                switch (chip8->inst.N) {
                case 0:
                        chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                        break;
                case 1:
                        chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                        break;
                case 2:
                        chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                        break;
                case 3:
                        chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                        break;
                case 4:
                        // carry flag if necessary
                        if ((uint16_t)(chip8->V[chip8->inst.X] +
                                       chip8->V[chip8->inst.Y]) > 255) {
                                chip8->V[0xF] = 1;
                        }
                        chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                        break;
                case 5:
                        // VY subtracted from VX, VF set to 0 when
                        // there's a borrow and 1 when there is not.
                        if (chip8->V[chip8->inst.X] < chip8->V[chip8->inst.Y]) {
                                chip8->V[0xF] = 0; // Borrow occurred
                        } else {
                                chip8->V[0xF] = 1; // No borrow
                        }
                        chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                        break;
                case 6:
                        // Store the least significant bit of VX in VF
                        chip8->V[0xF] = chip8->V[chip8->inst.X] & 0x1;
                        chip8->V[chip8->inst.X] >>= 1;
                        break;
                case 7:
                        // VY subtracted from VX, VF set to 0 when
                        // there's a borrow and 1 when there is not.
                        if (chip8->V[chip8->inst.Y] < chip8->V[chip8->inst.X]) {
                                chip8->V[0xF] = 0; // Borrow occurred
                        } else {
                                chip8->V[0xF] = 1; // No borrow
                        }
                        chip8->V[chip8->inst.X] =
                            chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                        break;
                case 0xE:
                        // Store the most significant bit of VX in VF
                        chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80) >> 7;
                        chip8->V[chip8->inst.X] <<= 1;
                        break;
                default:
                        break;
                }
                break;
        case 0x09:
                if (chip8->inst.N != 0) {
                        break;
                }
                if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]) {
                        chip8->PC += 2;
                }
                break;
        case 0x0A:
                chip8->I = chip8->inst.NNN;
                break;
        case 0x0B:
                // Jump to address NNN + V0
                chip8->PC = chip8->inst.NNN + chip8->V[0];
                break;
        case 0x0C:
                // Set Vx to a random number ANDed with NN
                chip8->V[chip8->inst.X] =
                    (rand() % 256) & chip8->inst.NN; // Random number
                break;
        case 0x0D:
                uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
                uint8_t Y_coord =
                    chip8->V[chip8->inst.Y] % config.window_height;
                const uint8_t orig_X = X_coord; // Original X value

                chip8->V[0xF] = 0; // Initialize carry flag to 0

                // Loop over all N rows of the sprite
                for (uint8_t i = 0; i < chip8->inst.N; i++) {
                        // Get next byte/row of sprite data
                        const uint8_t sprite_data = chip8->memory[chip8->I + i];
                        X_coord = orig_X; // Reset X for next row to draw

                        for (int8_t j = 7; j >= 0; j--) {
                                // If sprite pixel/bit is on and display pixel
                                // is on, set carry flag
                                bool *pixel =
                                    &chip8->display[Y_coord *
                                                        config.window_width +
                                                    X_coord];
                                const bool sprite_bit =
                                    (sprite_data & (1 << j));

                                if (sprite_bit && *pixel) {
                                        chip8->V[0xF] = 1;
                                }

                                // XOR display pixel with sprite pixel/bit to
                                // set it on or off
                                *pixel ^= sprite_bit;

                                // Stop drawing this row if hit right edge of
                                // screen
                                if (++X_coord >= config.window_width) {
                                        break;
                                }
                        }

                        // Stop drawing entire sprite if hit bottom edge of
                        // screen
                        if (++Y_coord >= config.window_height) {
                                break;
                        }
                }
                break;
        case 0x0E:
                switch (chip8->inst.NN) {
                case 0x9E:
                        // Skip next instruction if key stored in Vx is
                        // pressed
                        if (chip8->key[chip8->V[chip8->inst.X]]) {
                                chip8->PC += 2;
                        }
                        break;
                case 0xA1:
                        // Skip next instruction if key stored in Vx is
                        // not pressed
                        if (!chip8->key[chip8->V[chip8->inst.X]]) {
                                chip8->PC += 2;
                        }
                        break;
                default:
                        break;
                }
                break;
        case 0x0F:
                switch (chip8->inst.NN) {
                case 0x0A:
                        // 0xFX0A: VX = get_key(); Await until a keypress, and
                        // store in VX
                        static bool any_key_pressed = false;
                        static uint8_t key_p = 0xFF;

                        for (uint8_t i = 0;
                             key_p == 0xFF && i < sizeof chip8->key; i++) {
                                if (chip8->key[i]) {
                                        key_p = i; // Save pressed key to check
                                                   // until it is released
                                        any_key_pressed = true;
                                        break;
                                }
                        }

                        // If no key has been pressed yet, keep getting the
                        // current opcode & running this instruction
                        if (!any_key_pressed) {
                                chip8->PC -= 2;
                        } else {
                                // A key has been pressed, also wait until it is
                                // released to set the key in VX
                                if (chip8->key[key_p]) { // "Busy loop" CHIP8
                                        // emulation until key
                                        // is released
                                        chip8->PC -= 2;
                                } else {
                                        chip8->V[chip8->inst.X] =
                                            key_p;    // VX = key
                                        key_p = 0xFF; // Reset key to not found
                                        any_key_pressed =
                                            false; // Reset to nothing pressed
                                                   // yet
                                }
                        }
                        break;
                case 0x15:
                        // Set delay timer to Vx
                        chip8->delay_timer = chip8->V[chip8->inst.X];
                        break;
                case 0x07:
                        // Set Vx to the value of the delay timer
                        chip8->V[chip8->inst.X] = chip8->delay_timer;
                        break;
                case 0x18:
                        // Set sound timer to Vx
                        chip8->sound_timer = chip8->V[chip8->inst.X];
                        break;
                case 0x1E:
                        // Add Vx to I
                        chip8->I += chip8->V[chip8->inst.X];
                        break;
                case 0x29:
                        // Set I to the location of the sprite for digit
                        // Vx
                        chip8->I = chip8->V[chip8->inst.X] * 5;
                        break;
                case 0x33:
                        // Store BCD representation of Vx in memory at
                        // I, I+1, and I+2
                        uint8_t bcd = chip8->V[chip8->inst.X];
                        chip8->memory[chip8->I + 2] = bcd % 10;
                        bcd /= 10;
                        chip8->memory[chip8->I + 1] = bcd % 10;
                        bcd /= 10;
                        chip8->memory[chip8->I] = bcd;
                        break;
                case 0x55:
                        // Store registers V0 through Vx in memory
                        // starting at address I
                        memcpy(&chip8->memory[chip8->I], &chip8->V,
                               sizeof(chip8->V));
                        break;
                case 0x65:
                        // Read registers V0 through Vx from memory
                        // starting at address I
                        memcpy(&chip8->V, &chip8->memory[chip8->I],
                               sizeof(chip8->V));
                        break;
                default:
                        break;
                }
        default:
                break;
        }
}

void update_timers(const SDL_t SDL, CHIP8_t *chip8)
{
        if (chip8->delay_timer > 0) {
                chip8->delay_timer--;
        }
        (void)SDL;
}

int main(int argc, char **argv)
{

        if (argc < 2) {
                fprintf(stderr, "Usage: %s <rom>\n", argv[0]);
                exit(EXIT_FAILURE);
        }

        // Initialize the configuration
        Config_t config = {0};
        if (!set_CONFIG(&config, argc, argv)) {
                return (EXIT_FAILURE);
        }

        // Initialize SDL
        SDL_t SDL = {0};
        if (!init_SDL(&SDL, &config)) {
                return (EXIT_FAILURE);
        }

        CHIP8_t chip8 = {0}; // Initialize the CHIP8 emulator state
        if (!init_CHIP8(&chip8, argv[1])) {
                cleanup(&SDL);
                return (EXIT_FAILURE);
        }

        clear_window(&SDL, &config); // Clear the window

        // Main loop
        while (chip8.state != QUIT) {

                handle_input(&chip8); // Handle user input

                if (chip8.state == PAUSED) {
                        continue;
                }

                uint64_t current_time = SDL_GetPerformanceCounter();

                for (uint32_t i = 0; i < config.speed / 60; i++) {
                        // Execute the instruction
                        execute_instruction(&chip8, config);
                }

                double elapsed_time =
                    (double)(SDL_GetPerformanceCounter() - current_time) /
                    1000 / SDL_GetPerformanceFrequency();

                SDL_Delay(16.67f > elapsed_time ? 16.67f - elapsed_time
                                                : 0); // Delay to maintain speed
                update_window(&SDL, &config,
                              chip8);       // Update the window
                update_timers(SDL, &chip8); // Update timers
        }

        cleanup(&SDL); // Clean up SDL resources

        return (EXIT_SUCCESS);
}