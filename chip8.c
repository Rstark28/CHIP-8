#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"

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
} Config_t;

typedef enum {
        QUIT,
        RUNNING,
        PAUSED,
} State_t;

typedef struct {
        State_t state; // Current state of the emulator
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
            .foreground_color = 0xFFFFFFFF, // White
            .background_color = 0xFFFF00FF, // Yellow
            .scale = 10,                    // Scale factor
        };

        for (int i = 0; i < argc; i++) {
                (void)argv[i];
        }

        return true;
}

bool init_CHIP8(CHIP8_t *chip8)
{
        chip8->state = RUNNING; // Set the initial state to RUNNING
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
        uint8_t r = (config->background_color >> 24) & 0xFF;
        uint8_t g = (config->background_color >> 16) & 0xFF;
        uint8_t b = (config->background_color >> 8) & 0xFF;
        uint8_t a = (config->background_color >> 0) & 0xFF;

        SDL_SetRenderDrawColor(SDL->renderer, r, g, b, a); // Set the draw color
        SDL_RenderClear(SDL->renderer);                    // Clear the window
}

void update_window(SDL_t *SDL)
{
        SDL_RenderPresent(SDL->renderer); // Update the window with the renderer
}

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
                        default:
                                // Handle other key events here
                                break;
                        }
                case SDL_KEYUP:
                default:
                        // Handle key events here
                        break;
                }
        }
}

int main(int argc, char **argv)
{

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
        if (!init_CHIP8(&chip8)) {
                cleanup(&SDL);
                return (EXIT_FAILURE);
        }

        clear_window(&SDL, &config); // Clear the window

        // Main loop
        while (chip8.state != QUIT) {

                handle_input(&chip8); // Handle user input

                // Emulate the CHIP8 CPU here

                SDL_Delay(16);       // Simulate a frame delay (60 FPS)
                update_window(&SDL); // Update the window
        }

        cleanup(&SDL); // Clean up SDL resources

        return (EXIT_SUCCESS);
}