#include <time.h>
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "c8.h"

const int MAX_AUDIO_SAMPLE_SIZE = 512;
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const int PIXEL_SIZE = 8;
const uint8_t TEST_ROM[] = {
        0xa2, 0x1e, 0xc2, 0x01, 0x32, 0x01, 0xa2, 0x1a,
        0xd0, 0x14, 0x70, 0x04, 0x30, 0x40, 0x12, 0x00,
        0x60, 0x00, 0x71, 0x04, 0x31, 0x20, 0x12, 0x00,
        0x12, 0x18, 0x80, 0x40, 0x20, 0x10, 0x20, 0x40,
        0x80, 0x10,
};
const int KEY_BINDS[16] = {
    KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR,
    KEY_Q, KEY_W, KEY_E, KEY_R,
    KEY_A, KEY_S, KEY_D, KEY_F,
    KEY_Z, KEY_X, KEY_C, KEY_V
};

int main(void) {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "c8");
    SetTargetFPS(60);

    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(MAX_AUDIO_SAMPLE_SIZE);
    AudioStream audio = LoadAudioStream(44100, 16, 1);
    int16_t beep[MAX_AUDIO_SAMPLE_SIZE];
    for (int i = 0; i < MAX_AUDIO_SAMPLE_SIZE; ++i) {
        beep[i] = (int16_t)(sinf(2*PI*(float)i/MAX_AUDIO_SAMPLE_SIZE) * 32000);
    }

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

        if (IsAudioStreamProcessed(audio)) {
            UpdateAudioStream(audio, beep, MAX_AUDIO_SAMPLE_SIZE);
        }
        if (vm_regs->st > 0) {
            PlayAudioStream(audio);
        }
        else {
            PauseAudioStream(audio);
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
        GuiGroupBox((Rectangle){1, uiOffsetY, 799, 599 - uiOffsetY}, "Debug");
        GuiDrawText(
                TextFormat("OP: %04X", *(uint16_t*)(vm_mem + vm_regs->pc)),
                (Rectangle){5, uiOffsetY + 10, 100, 20},
                TEXT_ALIGN_LEFT,
                WHITE
                );

        for (int i = 0; i < 16; ++i) {
            GuiDrawText(
                    TextFormat("V%X: %02X", i, vm_regs->v[i]),
                    (Rectangle){5 + 100 * (i / 8), uiOffsetY + 40 + 20 * (i % 8), 100, 16},
                    TEXT_ALIGN_LEFT,
                    WHITE
            );
        }

        GuiDrawText(
                TextFormat("PC: %04X", vm_regs->pc),
                (Rectangle){205, uiOffsetY + 40, 100, 16},
                TEXT_ALIGN_LEFT,
                WHITE
        );

        GuiDrawText(
                TextFormat("I: %04X", vm_regs->i),
                (Rectangle){205, uiOffsetY + 60, 100, 16},
                TEXT_ALIGN_LEFT,
                WHITE
        );

        GuiDrawText(
                TextFormat("DT: %02X", vm_regs->dt),
                (Rectangle){205, uiOffsetY + 80, 100, 16},
                TEXT_ALIGN_LEFT,
                WHITE
        );

        GuiDrawText(
                TextFormat("ST: %02X", vm_regs->st),
                (Rectangle){205, uiOffsetY + 100, 100, 16},
                TEXT_ALIGN_LEFT,
                WHITE
        );

        GuiGroupBox(
                (Rectangle){
                    vm_config.screen_width * PIXEL_SIZE + 3, 5,
                    800 - vm_config.screen_width * PIXEL_SIZE - 3, uiOffsetY - 5
                    },
                    "Stack"
                    );

        for (int i = vm_regs->sp - 1; i >= 0; --i) {
            GuiDrawText(
                    TextFormat("STACK %d: %02X", vm_regs->sp - 1 - i, vm_regs->stack[i]),
                    (Rectangle){vm_config.screen_width * PIXEL_SIZE + 10, 5 + 20 * i, 100, 20},
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