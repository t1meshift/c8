#include <time.h>
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "c8.h"

enum c8_frontend_params {
    MAX_AUDIO_SAMPLE_SIZE = 512,
    BEEP_FREQ = 440,
    SCREEN_WIDTH = 800,
    SCREEN_HEIGHT = 600,
    PIXEL_SIZE = 8,
};

const uint8_t TEST_ROM[] = {
        0xA2, 0x1A, // ld i, 0x21A
        0x60, 0x12, // ld v0, 18
        0xF0, 0x18, // ld dt, v0
        0x60, 0xB4, // ld v0, 180
        0xF0, 0x15, // ld dt, v0
        0xC1, 0x3F, // rnd v1, 63
        0xC2, 0x1F, // rnd v2, 31
        0xD1, 0x25, // drw v1, v2, 5
        0xF0, 0x07, // ld v0, dt
        0x50, 0x30, // se v0, v3
        0x12, 0x10, // jp 0x210
        0xD1, 0x25, // drw v1, v2, 5
        0x12, 0x06, // jp 0x206
        0xEE, 0x8A, 0x84, 0x8A, 0xEE
};

const int KEY_BINDS[16] = {
    KEY_X, KEY_ONE, KEY_TWO, KEY_THREE,
    KEY_Q, KEY_W, KEY_E, KEY_A,
    KEY_S, KEY_D, KEY_Z, KEY_C,
    KEY_FOUR, KEY_R, KEY_F, KEY_V
};

void beep_callback(void* buffer, unsigned int frames) {
    static float sine_arg = 0.f;
    int16_t* b = (int16_t*)buffer;
    for (unsigned int i = 0; i < frames; ++i) {
        b[i] = (int16_t)(32000.f * sinf(2*PI*sine_arg));
        sine_arg += BEEP_FREQ / 44100.f;
        if (sine_arg > 1.f) {
            sine_arg = -1.f;
        }
    }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "c8");
    SetTargetFPS(60);

    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(MAX_AUDIO_SAMPLE_SIZE);
    AudioStream audio = LoadAudioStream(44100, 16, 1);
    SetAudioStreamCallback(audio, beep_callback);

    c8_machine_config vm_config = c8_get_default_machine_config();
    c8_state* vm = c8_create(vm_config);
    c8_set_rng_seed(vm, time(nullptr));

    uint32_t display_size;
    const uint8_t* vm_display = c8_get_display(vm, &display_size);
    const c8_registers* vm_regs = c8_get_registers(vm);
    const uint8_t* vm_mem = c8_get_memory(vm);

    uint8_t* rom = (uint8_t*)TEST_ROM;
    int rom_size = sizeof(TEST_ROM);
    c8_load_rom(vm, rom, rom_size);

    while (!WindowShouldClose()) {
        if (IsFileDropped()) {
            FilePathList list = LoadDroppedFiles();
            for (int i = 0; i < list.count; ++i) {
                const char* path = list.paths[i];
                if (FileExists(path)) {
                    rom = LoadFileData(path, &rom_size);
                    c8_reset(vm);
                    c8_load_rom(vm, rom, rom_size);
                    SetWindowTitle(TextFormat("c8 - %s", GetFileName(path)));
                    UnloadFileData(rom);
                    break;
                }
            }
            UnloadDroppedFiles(list);
        }

        const bool isAudioPlaying = IsAudioStreamPlaying(audio);
        if (vm_regs->st > 0) {
            if (!isAudioPlaying) {
                PlayAudioStream(audio);
            }
        }
        else {
            if (isAudioPlaying) {
                PauseAudioStream(audio);
            }
        }

        c8_step_frame(vm);

        BeginDrawing();
        ClearBackground(BLACK);

        for (int y = 0; y < vm_config.screen_height; ++y) {
            for (int x = 0; x < vm_config.screen_width; ++x) {
                if (vm_display[y * vm_config.screen_width + x]) {
                    DrawRectangle(x * PIXEL_SIZE, y * PIXEL_SIZE, PIXEL_SIZE, PIXEL_SIZE, WHITE);
                }
            }
        }

        float uiOffsetY = (float)(vm_config.screen_height * PIXEL_SIZE + 3);
        GuiGroupBox((Rectangle){1, uiOffsetY, 699, 599 - uiOffsetY}, "Debug");
        const uint8_t* mem_at_pc = vm_mem + vm_regs->pc;
        GuiDrawText(
                TextFormat("OP: %04X", ((uint16_t)*mem_at_pc << 8) | *(mem_at_pc + 1)),
                (Rectangle){5, uiOffsetY + 10, 60, 20},
                TEXT_ALIGN_LEFT,
                WHITE
                );

        for (int i = 0; i < 16; ++i) {
            GuiDrawText(
                    TextFormat("V%X: %02X", i, vm_regs->v[i]),
                    (Rectangle){5 + 60 * (i / 8), uiOffsetY + 40 + 20 * (i % 8), 60, 16},
                    TEXT_ALIGN_LEFT,
                    WHITE
            );
        }

        GuiDrawText(
                TextFormat("PC: %04X", vm_regs->pc),
                (Rectangle){125, uiOffsetY + 40, 60, 16},
                TEXT_ALIGN_LEFT,
                WHITE
        );

        GuiDrawText(
                TextFormat("I: %04X", vm_regs->i),
                (Rectangle){125, uiOffsetY + 60, 100, 16},
                TEXT_ALIGN_LEFT,
                WHITE
        );

        GuiDrawText(
                TextFormat("DT: %02X", vm_regs->dt),
                (Rectangle){125, uiOffsetY + 80, 100, 16},
                TEXT_ALIGN_LEFT,
                WHITE
        );

        GuiDrawText(
                TextFormat("ST: %02X", vm_regs->st),
                (Rectangle){125, uiOffsetY + 100, 100, 16},
                TEXT_ALIGN_LEFT,
                WHITE
        );

        GuiGroupBox(
                (Rectangle){
                    700, uiOffsetY,
                    100, 599 - uiOffsetY
                    },
                    "Stack"
                    );

        for (int i = 0; i < vm_regs->sp; ++i) {
            const uint8_t stack_idx = vm_regs->sp - 1 - i;
            GuiDrawText(
                    TextFormat("STACK %d: %04X", stack_idx, vm_regs->stack[stack_idx]),
                    (Rectangle){710, uiOffsetY + 10 + 20 * i, 80, 20},
                    TEXT_ALIGN_LEFT,
                    WHITE
            );
        }

        EndDrawing();

        c8_update_timers(vm, GetFrameTime() * 1000.f);

        for (int i = 0; i < 16; ++i) {
            if (IsKeyDown(KEY_BINDS[i])) {
                c8_press_key(vm, i);
            }
            else {
                c8_release_key(vm, i);
            }
        }
    }

    c8_destroy(vm);
    UnloadAudioStream(audio);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}