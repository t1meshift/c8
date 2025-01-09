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

static c8_machine_config vm_config = {};

static c8_state* vm = nullptr;

static const uint8_t* vm_display = nullptr;

static const c8_registers* vm_regs = nullptr;

static const uint8_t* vm_mem = nullptr;

static const uint32_t seed = 0;

static bool file_rom_loaded = false;

static uint8_t* rom = (uint8_t*)TEST_ROM;

static int rom_size = sizeof(TEST_ROM);

void beep_callback(void* buffer, unsigned int frames) {
    static float sine_arg = 0.f;
    int16_t* b = (int16_t*)buffer;
    for (unsigned int i = 0; i < frames; ++i) {
        b[i] = (int16_t)(32000.f * sinf(2 * PI * sine_arg));
        sine_arg += BEEP_FREQ / 44100.f;
        if (sine_arg > 1.f) {
            sine_arg = -1.f;
        }
    }
}

void update_keys(c8_state* state) {
    for (int i = 0; i < 16; ++i) {
        if (IsKeyDown(KEY_BINDS[i])) {
            c8_press_key(state, i);
        }
        else {
            c8_release_key(state, i);
        }
    }
}

void recreate_state() {
    if (vm != nullptr) {
        c8_destroy(vm);
    }
    vm = c8_create(vm_config);
    c8_set_rng_seed(vm, seed != 0 ?: time(nullptr));

    uint32_t display_size;
    vm_display = c8_get_display(vm, &display_size);
    vm_regs = c8_get_registers(vm);
    vm_mem = c8_get_memory(vm);

    c8_load_rom(vm, rom, rom_size);
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "c8");
    SetTargetFPS(60);

    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(MAX_AUDIO_SAMPLE_SIZE);
    AudioStream audio = LoadAudioStream(
        44100, 16, 1
    );
    SetAudioStreamCallback(audio, beep_callback);

    vm_config = c8_get_default_machine_config();
    recreate_state();

    int16_t mem_view_offset = 0;
    uint16_t breakpoint_addr = 0xFFFF;
    bool execution_paused = false;

    bool options_opened = false;
    Color pixel_color = WHITE;
    Color bg_color = BLACK;
    bool enable_sound = true;

    bool quirk_shift = (vm_config.quirks & C8_QUIRK_SHIFT) != 0;
    bool quirk_ls_inc_by_x =
        (vm_config.quirks & C8_QUIRK_LOAD_STORE_INC_I_BY_X) != 0;
    bool quirk_ls_no_inc_i =
        (vm_config.quirks & C8_QUIRK_LOAD_STORE_NO_INC_I) != 0;
    bool quirk_wrap_sprite = (vm_config.quirks & C8_QUIRK_WRAP_SPRITES) != 0;
    bool quirk_jump = (vm_config.quirks & C8_QUIRK_BXNN_JUMP) != 0;
    bool quirk_vblank = (vm_config.quirks & C8_QUIRK_VBLANK) != 0;
    bool quirk_vf_reset = (vm_config.quirks & C8_QUIRK_VF_RESET) != 0;

    // Set GUI background color to black for options window
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x000000FF);

    while (!WindowShouldClose()) {
        if (IsFileDropped()) {
            FilePathList list = LoadDroppedFiles();
            for (int i = 0; i < list.count; ++i) {
                const char* path = list.paths[i];
                if (FileExists(path)) {
                    if (file_rom_loaded) {
                        UnloadFileData(rom);
                    }
                    file_rom_loaded = true;
                    rom = LoadFileData(path, &rom_size);
                    c8_reset(vm);
                    c8_load_rom(vm, rom, rom_size);
                    SetWindowTitle(
                        TextFormat("c8 - %s", GetFileName(path))
                    );
                    break;
                }
            }
            UnloadDroppedFiles(list);
        }

        const bool isAudioPlaying = IsAudioStreamPlaying(audio);
        if (vm_regs->st > 0 && enable_sound) {
            if (!isAudioPlaying) {
                PlayAudioStream(audio);
            }
        }
        else {
            if (isAudioPlaying) {
                PauseAudioStream(audio);
            }
        }

        if (!execution_paused) {
            for (int i = 0; i < vm_config.cycles_per_frame; ++i) {
                if (vm_regs->pc == breakpoint_addr) {
                    execution_paused = true;
                    break;
                }
                c8_step(vm);
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        DrawRectangle(
            0,
            0,
            vm_config.screen_width * PIXEL_SIZE,
            vm_config.screen_height * PIXEL_SIZE,
            bg_color
        );
        for (int y = 0; y < vm_config.screen_height; ++y) {
            for (int x = 0; x < vm_config.screen_width; ++x) {
                if (vm_display[y * vm_config.screen_width + x]) {
                    DrawRectangle(
                        x * PIXEL_SIZE,
                        y * PIXEL_SIZE,
                        PIXEL_SIZE,
                        PIXEL_SIZE,
                        pixel_color
                    );
                }
            }
        }

        const float
            uiOffsetY = (float)(vm_config.screen_height * PIXEL_SIZE + 3);
        const float
            uiOffsetX = (float)(vm_config.screen_width * PIXEL_SIZE + 3);

        GuiGroupBox(
            (Rectangle){
                uiOffsetX,
                5,
                800 - uiOffsetX,
                uiOffsetY - 5
            },
            "Debug"
        );

        GuiToggle(
            (Rectangle){
                uiOffsetX + 5,
                15,
                60,
                20
            },
            execution_paused ? "Continue" : "Pause", &execution_paused
        );

        if (GuiButton(
            (Rectangle){
                uiOffsetX + 70,
                15,
                60,
                20
            },
            "Step"
        )) {
            execution_paused = true;
            c8_step(vm);
            c8_update_timers(
                vm,
                1000.f / 60.f / (float)vm_config.cycles_per_frame
            );
            update_keys(vm);
        }

        if (GuiButton(
            (Rectangle){
                uiOffsetX + 5,
                40,
                60,
                20
            },
            "Reset"
        )) {
            execution_paused = false;
            c8_reset(vm);
            c8_load_rom(vm, rom, rom_size);
        }

        if (GuiButton(
            (Rectangle){
                uiOffsetX + 5,
                65,
                60,
                20
            },
            "Options"
        )) {
            options_opened = true;
        }

        GuiGroupBox(
            (Rectangle){
                1,
                uiOffsetY,
                225,
                599 - uiOffsetY
            },
            "Registers"
        );
        const uint8_t* mem_at_pc = vm_mem + vm_regs->pc;
        GuiDrawText(
            TextFormat(
                "OP: %04X", ((uint16_t)*mem_at_pc << 8) | *(mem_at_pc + 1)
            ), (Rectangle){
                5,
                uiOffsetY + 10,
                60,
                20
            },
            TEXT_ALIGN_LEFT,
            WHITE
        );

        for (int i = 0; i < 16; ++i) {
            GuiDrawText(
                TextFormat("V%X: %02X", i, vm_regs->v[i]),
                (Rectangle){
                    5 + 60 * (i / 8),
                    uiOffsetY + 40 + 20 * (i % 8),
                    60,
                    16
                },
                TEXT_ALIGN_LEFT,
                WHITE
            );
        }

        GuiDrawText(
            TextFormat("PC: %04X", vm_regs->pc),
            (Rectangle){
                125,
                uiOffsetY + 40,
                60,
                16
            },
            TEXT_ALIGN_LEFT,
            WHITE
        );

        GuiDrawText(
            TextFormat("I: %04X", vm_regs->i),
            (Rectangle){
                125,
                uiOffsetY + 60,
                100,
                16
            },
            TEXT_ALIGN_LEFT,
            WHITE
        );

        GuiDrawText(
            TextFormat("DT: %02X", vm_regs->dt),
            (Rectangle){
                125,
                uiOffsetY + 80,
                100,
                16
            },
            TEXT_ALIGN_LEFT,
            WHITE
        );

        GuiDrawText(
            TextFormat("ST: %02X", vm_regs->st),
            (Rectangle){
                125,
                uiOffsetY + 100,
                100,
                16
            },
            TEXT_ALIGN_LEFT,
            WHITE
        );

        GuiGroupBox(
            (Rectangle){
                225, uiOffsetY, 475, 599 - uiOffsetY
            }, "Memory"
        );

        float mem_label_width = (465.f - 40.f) / 16.f;
        for (int i = 0; i < 16; ++i) {
            GuiDrawText(
                TextFormat("%01X", i),
                (Rectangle){
                    250 + i * mem_label_width, uiOffsetY + 10,
                    mem_label_width, 20
                },
                TEXT_ALIGN_CENTER,
                WHITE
            );
        }

        for (int i = 0; i < 12; ++i) {
            const int row_num = mem_view_offset / 16 + i;
            if (row_num > 255) {
                break;
            }
            GuiDrawText(
                TextFormat("%02X", row_num),
                (Rectangle){
                    225, uiOffsetY + 30 + i * 20,
                    20, 20
                },
                TEXT_ALIGN_RIGHT,
                WHITE
            );
        }

        DrawLine(
            250,
            uiOffsetY + 30,
            250,
            uiOffsetY + 270,
            WHITE
        );

        DrawLine(
            250,
            uiOffsetY + 30,
            250 + 16 * mem_label_width,
            uiOffsetY + 30,
            WHITE
        );

        for (int i = 0; i < 192; ++i) {
            if (mem_view_offset + i >= vm_config.memory_size) {
                break;
            }

            Rectangle cell_rect = (Rectangle){
                250 + (i % 16) * mem_label_width,
                uiOffsetY + 30 + (i / 16) * 20,
                mem_label_width,
                20
            };

            Color cell_color = (mem_view_offset + i) == breakpoint_addr
                ? YELLOW
                : WHITE;

            // TODO: track mouse press like in GuiButton
            Vector2 mouse_point = GetMousePosition();
            if (CheckCollisionPointRec(mouse_point, cell_rect)) {
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    if (breakpoint_addr != mem_view_offset + i) {
                        breakpoint_addr = mem_view_offset + i;
                    }
                    else {
                        breakpoint_addr = 0xFFFF;
                    }
                }
            }

            GuiDrawText(
                TextFormat("%02X", vm_mem[mem_view_offset + i]),
                cell_rect,
                TEXT_ALIGN_CENTER,
                cell_color
            );
        }

        if (GuiButton(
            (Rectangle){
                670, uiOffsetY + 10,
                20, 20
            },
            "/\\"
        )) {
            mem_view_offset = C8_MAX(0, mem_view_offset - 16);
        }

        if (GuiButton(
            (Rectangle){
                670, uiOffsetY + 250,
                20, 20
            },
            "\\/"
        )) {
            mem_view_offset =
                C8_MIN(vm_config.memory_size - 16, mem_view_offset + 16);
        }

        GuiGroupBox(
            (Rectangle){
                700, uiOffsetY, 100, 599 - uiOffsetY
            }, "Stack"
        );

        for (int i = 0; i < vm_regs->sp; ++i) {
            const uint8_t stack_idx = vm_regs->sp - 1 - i;
            GuiDrawText(
                TextFormat(
                    "STACK %d: %04X", stack_idx, vm_regs->stack[stack_idx]
                ),
                (Rectangle){
                    710,
                    uiOffsetY + 10 + 20 * i,
                    80,
                    20
                },
                TEXT_ALIGN_LEFT,
                WHITE
            );
        }

        if (options_opened) {
            if (GuiWindowBox(
                (Rectangle){
                    40,
                    40,
                    720,
                    520
                },
                "Options"
            )) {
                options_opened = false;
            }

            GuiDrawText(
                TextFormat(
                    "Fill color\t%02X%02X%02X",
                    pixel_color.r,
                    pixel_color.g,
                    pixel_color.b
                ),
                (Rectangle){ 50, 70, 150, 20 },
                TEXT_ALIGN_LEFT,
                WHITE
            );
            GuiColorPicker(
                (Rectangle){
                    50,
                    90,
                    150,
                    140
                },
                nullptr,
                &pixel_color
            );

            GuiDrawText(
                TextFormat(
                    "Background color\t%02X%02X%02X",
                    bg_color.r,
                    bg_color.g,
                    bg_color.b
                ),
                (Rectangle){ 50, 240, 150, 20 },
                TEXT_ALIGN_LEFT,
                WHITE
            );
            GuiColorPicker(
                (Rectangle){
                    50,
                    265,
                    150,
                    140
                },
                nullptr,
                &bg_color
            );

            GuiCheckBox(
                (Rectangle){
                    50,
                    415,
                    20,
                    20
                },
                "Enable sound",
                &enable_sound
            );

            GuiDrawText(
                "Quirks (reset the emulator)",
                (Rectangle){
                    250,
                    70,
                    150,
                    20
                },
                TEXT_ALIGN_LEFT,
                WHITE
            );

            if (GuiCheckBox(
                (Rectangle){
                    250,
                    95,
                    20,
                    20
                },
                "Shift quirk",
                &quirk_shift
            )) {
                vm_config.quirks ^= C8_QUIRK_SHIFT;
                recreate_state();
            }

            if (GuiCheckBox(
                (Rectangle){
                    250,
                    120,
                    20,
                    20
                },
                "Load/Store quirk (increment I by X)",
                &quirk_ls_inc_by_x
            )) {
                vm_config.quirks ^= C8_QUIRK_LOAD_STORE_INC_I_BY_X;
                recreate_state();
            }

            if (GuiCheckBox(
                (Rectangle){
                    250,
                    145,
                    20,
                    20
                },
                "Load/Store quirk (leave I unchanged)",
                &quirk_ls_no_inc_i
            )) {
                vm_config.quirks ^= C8_QUIRK_LOAD_STORE_NO_INC_I;
                recreate_state();
            }

            if (GuiCheckBox(
                (Rectangle){
                    250,
                    170,
                    20,
                    20
                },
                "Wrap sprites",
                &quirk_wrap_sprite
            )) {
                vm_config.quirks ^= C8_QUIRK_WRAP_SPRITES;
                recreate_state();
            }

            if (GuiCheckBox(
                (Rectangle){
                    250,
                    195,
                    20,
                    20
                },
                "Jump quirk",
                &quirk_jump
            )) {
                vm_config.quirks ^= C8_QUIRK_BXNN_JUMP;
                recreate_state();
            }

            if (GuiCheckBox(
                (Rectangle){
                    250,
                    220,
                    20,
                    20
                },
                "VBlank quirk",
                &quirk_vblank
            )) {
                vm_config.quirks ^= C8_QUIRK_VBLANK;
                recreate_state();
            }

            if (GuiCheckBox(
                (Rectangle){
                    250,
                    245,
                    20,
                    20
                },
                "VF reset quirk",
                &quirk_vf_reset
            )) {
                vm_config.quirks ^= C8_QUIRK_VF_RESET;
                recreate_state();
            }
        }

        EndDrawing();

        if (!execution_paused) {
            c8_update_timers(vm, GetFrameTime() * 1000.f);

            update_keys(vm);
        }
    }

    if (file_rom_loaded) {
        UnloadFileData(rom);
    }

    c8_destroy(vm);
    UnloadAudioStream(audio);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}