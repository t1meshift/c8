#include "c8.h"
#include <stdlib.h>
#include <memory.h>
#include <assert.h>

/*
 * Sources:
 * http://devernay.free.fr/hacks/chip8/C8TECH10.HTM
 * https://github.com/trapexit/chip-8_documentation
 * https://github.com/edrosten/8bit_rng/blob/master/rng-4294967294.cc
 */

#define C8_MIN(a, b) ((a) < (b) ? (a) : (b))
#define C8_MAX(a, b) ((a) > (b) ? (a) : (b))

enum c8_machine_params
#ifndef C23_COMPAT_NO_ENUM_TYPES
	: uint16_t
#endif
{
    C8_MEM_FONT_OFFSET = 0x50,
    C8_PC_ON_FAULT = 0x0,
};

const uint8_t C8_FAULT_HANDLER[] = { 0x10 | ((C8_PC_ON_FAULT & 0x0F00) >> 8), C8_PC_ON_FAULT & 0xFF };

static const uint8_t C8_FONT[80] = {
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
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

struct c8_state {
    c8_machine_config config;
    c8_registers registers;
    bool pressed_keys[C8_KEY_MAX];
    uint8_t *memory;
    uint8_t *display;
    union {
        uint32_t seed;
        uint8_t b[4];
    } rng;
    float delta_time;
    uint16_t vblank;
};

#pragma region CHIP-8 instructions

/**
 * 0nnn - SYS nnn
 *
 * Call machine language subroutine at address NNN.
 */
static void c8_op_sys(c8_state *state, uint16_t nnn) {
    state->registers.pc += 2;
}

/**
 * 00E0 - CLS
 *
 * Clears the display. Sets all pixels to off.
 */
static void c8_op_cls(c8_state *state) {
    memset(state->display, 0, state->config.screen_width * state->config.screen_height);
    state->registers.pc += 2;
}

/**
 * 00EE - RET
 *
 * Return from subroutine. Set the PC to the address at the top
 * of the stack and subtract 1 from the SP.
 */
static void c8_op_ret(c8_state *state) {
    if (state->registers.sp == 0) {
        state->registers.pc = C8_PC_ON_FAULT;
    }
    state->registers.pc = state->registers.stack[--state->registers.sp] + 2;
}

/**
 * 1nnn - JP nnn
 *
 * Set PC to NNN.
 */
static void c8_op_jp_nnn(c8_state *state, uint16_t nnn) {
    state->registers.pc = nnn;
}

/**
 * 2nnn - CALL nnn
 *
 * Call subroutine a NNN. Increment the SP and put the current PC value
 * on the top of the stack. Then set the PC to NNN. Generally there is
 * a limit of 16 successive calls.
 */
static void c8_op_call(c8_state *state, uint16_t nnn) {
    if (state->registers.sp >= 16) {
        state->registers.pc = C8_PC_ON_FAULT;
    }
    state->registers.stack[state->registers.sp++] = state->registers.pc;
    state->registers.pc = nnn;

}

/**
 * 3xnn - SE Vx, nn
 *
 * Skip the next instruction if register Vx is equal to NN.
 */
static void c8_op_se_vx_nn(c8_state *state, uint8_t x, uint8_t nn) {
    state->registers.pc += state->registers.v[x] == nn ? 4 : 2;
}

/**
 * 4xnn - SNE Vx, nn
 *
 * Skip the next instruction if register Vx is not equal to NN.
 */
static void c8_op_sne_vx_nn(c8_state *state, uint8_t x, uint8_t nn) {
    state->registers.pc += state->registers.v[x] != nn ? 4 : 2;
}

/**
 * 5xy0 - SE Vx, Vy
 *
 * Skip the next instruction if register Vx equals Vy.
 */
static void c8_op_se_vx_vy(c8_state *state, uint8_t x, uint8_t y) {
    state->registers.pc += state->registers.v[x] == state->registers.v[y] ? 4 : 2;
}

/**
 * 6xnn - LD Vx, nn
 *
 * Load immediate value NN into register VX.
 */
static void c8_op_ld_vx_nn(c8_state *state, uint8_t x, uint8_t nn) {
    state->registers.v[x] = nn;
    state->registers.pc += 2;
}

/**
 * 7xnn - ADD Vx, nn
 *
 * Add immediate value NN to register VX. Does **not** effect VF.
 */
static void c8_op_add_vx_nn(c8_state *state, uint8_t x, uint8_t nn) {
    state->registers.v[x] += nn;
    state->registers.pc += 2;
}

/**
 * 8xy0 - LD Vx, Vy
 *
 * Copy the value in register VY into VX
 */
static void c8_op_ld_vx_vy(c8_state *state, uint8_t x, uint8_t y) {
    state->registers.v[x] = state->registers.v[y];
    state->registers.pc += 2;
}

/**
 * 8xy1 - OR Vx, Vy
 *
 * Set VX equal to the bitwise or of the values in VX and VY.
 */
static void c8_op_or(c8_state *state, uint8_t x, uint8_t y) {
    state->registers.v[x] |= state->registers.v[y];

    if ((state->config.quirks & C8_QUIRK_VF_RESET) != 0) {
        state->registers.v[0xF] = 0;
    }

    state->registers.pc += 2;
}

/**
 * 8xy2 - AND Vx, Vy
 *
 * Set VX equal to the bitwise and of the values in VX and VY.
 */
static void c8_op_and(c8_state *state, uint8_t x, uint8_t y) {
    state->registers.v[x] &= state->registers.v[y];

    if ((state->config.quirks & C8_QUIRK_VF_RESET) != 0) {
        state->registers.v[0xF] = 0;
    }

    state->registers.pc += 2;
}

/**
 * 8xy3 - XOR Vx, Vy
 *
 * Set VX equal to the bitwise xor of the values in VX and VY.
 */
static void c8_op_xor(c8_state *state, uint8_t x, uint8_t y) {
    state->registers.v[x] ^= state->registers.v[y];

    if ((state->config.quirks & C8_QUIRK_VF_RESET) != 0) {
        state->registers.v[0xF] = 0;
    }

    state->registers.pc += 2;
}

/**
 * 8xy4 - ADD Vx, Vy
 *
 * Set VX equal to VX plus VY. In the case of an overflow VF is set to 1.
 * Otherwise 0.
 */
static void c8_op_add_vx_vy(c8_state *state, uint8_t x, uint8_t y) {
    state->registers.v[0xF] = state->registers.v[x] + state->registers.v[y] > 0xFF ? 1 : 0;
    state->registers.v[x] += state->registers.v[y];
    state->registers.pc += 2;
}

/**
 * 8xy5 - SUB Vx, Vy
 *
 * Set VX equal to VX minus VY. In the case of an underflow VF is set 0.
 * Otherwise 1. (VF = VX > VY)
 */
static void c8_op_sub(c8_state *state, uint8_t x, uint8_t y) {
    state->registers.v[0xF] = state->registers.v[x] > state->registers.v[y] ? 1 : 0;
    state->registers.v[x] -= state->registers.v[y];
    state->registers.pc += 2;
}

/**
 * 8xy6 - SHR Vx, Vy
 *
 * Set VX equal to VY or VX bitshifted right 1. VF is set to the least
 * significant bit of VX prior to the shift. 
 */
static void c8_op_shr(c8_state *state, uint8_t x, uint8_t y) {
    const bool hasShiftQuirk = (state->config.quirks & C8_QUIRK_SHIFT) != 0;
    const uint8_t value = state->registers.v[hasShiftQuirk ? x : y];
    state->registers.v[x] = value >> 1;
    state->registers.v[0xF] = value & 0x1;
    state->registers.pc += 2;
}

/**
 * 8xy7 - SUBN Vx, Vy
 *
 * Set VX equal to VY minus VX. VF is set to 1 if VY > VX. Otherwise 0.
 */
static void c8_op_subn(c8_state *state, uint8_t x, uint8_t y) {
    state->registers.v[0xF] = state->registers.v[y] > state->registers.v[x] ? 1 : 0;
    state->registers.v[x] = state->registers.v[y] - state->registers.v[x];
    state->registers.pc += 2;
}

/**
 * 8xyE - SHL Vx, Vy
 *
 * Set VX equal to VY or VX bitshifted left 1. VF is set to the most
 * significant bit of VX prior to the shift. 
 */
static void c8_op_shl(c8_state *state, uint8_t x, uint8_t y) {
    const bool hasShiftQuirk = (state->config.quirks & C8_QUIRK_SHIFT) != 0;
    const uint8_t value = state->registers.v[hasShiftQuirk ? x : y];
    state->registers.v[x] = value << 1;
    state->registers.v[0xF] = (value & 0x80) >> 7;
    state->registers.pc += 2;
}

/**
 * 9xy0 - SNE Vx, Vy
 *
 * Skip the next instruction if VX does not equal VY.
 */
static void c8_op_sne_vx_vy(c8_state *state, uint8_t x, uint8_t y) {
    state->registers.pc += state->registers.v[x] != state->registers.v[y] ? 4 : 2;
}

/**
 * Annn - LD I, nnn
 *
 * Set I equal to NNN.
 */
static void c8_op_ld_i_nnn(c8_state *state, uint16_t nnn) {
    state->registers.i = nnn;
    state->registers.pc += 2;
}

/**
 * Bnnn - JP V0, nnn
 *
 * Set the PC to NNN plus the value in V0.
 */
static void c8_op_jp_v0_nnn(c8_state *state, uint16_t nnn) {
    const bool jpXNN = (state->config.quirks & C8_QUIRK_BXNN_JUMP) != 0;
    state->registers.pc = nnn + state->registers.v[jpXNN ? (nnn & 0xF00) >> 8 : 0];
}

/**
 * Cxnn - RND Vx, nn
 *
 * Set VX equal to a random number ranging from 0 to 255 which is logically
 * anded with NN.
 */
static void c8_op_rnd(c8_state *state, uint8_t x, uint8_t nn) {
    /*
     * This code is stolen from
     * https://github.com/edrosten/8bit_rng/blob/master/rng-4294967294.cc
     *
     * Copyright Edward Rosten 2008--2013.
     *
     * Redistribution and use in source and binary forms, with or without
     * modification, are permitted provided that the following conditions
     * are met:
     * 1. Redistributions of source code must retain the above copyright
     * notice, this list of conditions and the following disclaimer.
     * 2. Redistributions in binary form must reproduce the above copyright
     * notice, this list of conditions and the following disclaimer in the
     * documentation and/or other materials provided with the distribution.
     *
     * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND OTHER CONTRIBUTORS ``AS IS''
     * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
     * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
     * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR OTHER CONTRIBUTORS BE
     * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
     * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
     * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
     * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
     * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
     * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
     * POSSIBILITY OF SUCH DAMAGE.
     */
    uint8_t t = state->rng.b[0] ^ (state->rng.b[0] >> 1);
    state->rng.b[0] = state->rng.b[1];
    state->rng.b[1] = state->rng.b[2];
    state->rng.b[2] = state->rng.b[3];
    state->rng.b[3] = state->rng.b[2] ^ t ^ (state->rng.b[2] >> 3) ^ (t << 1);

    state->registers.v[x] = state->rng.b[3] & nn;
    state->registers.pc += 2;
}

/**
 * Dxyn - DRW Vx, Vy, n
 *
 * Display N-byte sprite starting at memory location I at (VX, VY). Each set
 * bit of xored with what's already drawn. VF is set to 1 if a collision
 * occurs. 0 otherwise.
 */
static void c8_op_drw(c8_state *state, uint8_t x, uint8_t y, uint8_t n) {
    const bool hasVblankQuirk = (state->config.quirks & C8_QUIRK_VBLANK) != 0;
    if (hasVblankQuirk) {
	    if (state->vblank == 0) {
		    return;
	    }
        --state->vblank;
    }

    const uint8_t screen_width = state->config.screen_width;
    const uint8_t screen_height = state->config.screen_height;

    uint8_t px0 = state->registers.v[x] % screen_width;
    uint8_t py0 = state->registers.v[y] % screen_height;

    uint8_t* sprite = &state->memory[state->registers.i];

    state->registers.v[0xF] = 0;

    const bool wrap_sprites = (state->config.quirks & C8_QUIRK_WRAP_SPRITES) != 0;
    const uint8_t sprite_width = wrap_sprites ? 8 : C8_MIN(8, screen_width - px0);
    const uint8_t sprite_height = wrap_sprites ? n : C8_MIN(n, screen_height - py0);

    for (uint8_t i = 0; i < sprite_height; ++i) {
        for (uint8_t j = 0; j < sprite_width; ++j) {
            uint8_t dx = (px0 + j) % screen_width;
            uint8_t dy = (py0 + i) % screen_height;

            uint8_t* disp_pixel = &state->display[dy * screen_width + dx];
            uint8_t sprite_pixel = (*sprite >> (7 - j)) & 0x1;

            if (*disp_pixel && sprite_pixel) {
                state->registers.v[0xF] = 1;
            }
            *disp_pixel ^= sprite_pixel;
        }
        ++sprite;
    }

    state->registers.pc += 2;
}

/**
 * Ex9E - SKP Vx
 *
 * Skip the following instruction if the key represented by the value in VX
 * is pressed.
 */
static void c8_op_skp(c8_state *state, uint8_t x) {
    c8_key key = state->registers.v[x];
    state->registers.pc += key <= 0xF && state->pressed_keys[key] ? 4 : 2;
}

/**
 * ExA1 - SKNP Vx
 *
 * Skip the following instruction if the key represented by the value in VX
 * is not pressed.
 */
static void c8_op_sknp(c8_state *state, uint8_t x) {
    c8_key key = state->registers.v[x];
    state->registers.pc += !(key <= 0xF && state->pressed_keys[key]) ? 4 : 2;
}

/**
 * Fx07 - LD Vx, DT
 *
 * Set VX equal to the delay timer.
 */
static void c8_op_ld_vx_dt(c8_state *state, uint8_t x) {
    state->registers.v[x] = state->registers.dt;
    state->registers.pc += 2;
}

/**
 * Fx0A - LD Vx, KEY
 *
 * Wait for a key press and store the value of the key into VX.
 */
static void c8_op_ld_vx_key(c8_state *state, uint8_t x) {
    for (c8_key i = C8_KEY_0; i < C8_KEY_MAX; ++i) {
        if (state->pressed_keys[i]) {
            state->registers.v[x] = i;
            state->registers.pc += 2;
            break;
        }
    }
}

/**
 * Fx15 - LD DT, Vx
 *
 * Set the delay timer DT to VX.
 */
static void c8_op_ld_dt_vx(c8_state *state, uint8_t x) {
    state->registers.dt = state->registers.v[x];
    state->registers.pc += 2;
}

/**
 * Fx18 - LD ST, Vx
 *
 * Set the sound timer ST to VX.
 */
static void c8_op_ld_st_vx(c8_state *state, uint8_t x) {
    state->registers.st = state->registers.v[x];
    state->registers.pc += 2;
}

/**
 * Fx1E - ADD I, Vx
 *
 * Add VX to I. VF is set to 1 if I > 0x0FFF. Otherwise set to 0.
 */
static void c8_op_add_i_vx(c8_state *state, uint8_t x) {
    state->registers.i += state->registers.v[x];
    state->registers.v[0xF] = state->registers.i > 0x0FFF ? 1 : 0;
    state->registers.i &= 0xFFF;
    state->registers.pc += 2;
}

/**
 * Fx29 - LD I, FONT(Vx)
 *
 * Set I to the address of the CHIP-8 8x5 font sprite representing the value
 * in VX.
 */
static void c8_op_ld_i_font_vx(c8_state *state, uint8_t x) {
    state->registers.i = C8_MEM_FONT_OFFSET + (state->registers.v[x] & 0x0F) * 5;
    state->registers.pc += 2;
}

/**
 * Fx33 - BCD Vx
 *
 * Convert that word to BCD and store the 3 digits at memory location I
 * through I+2. I does not change.
 */
static void c8_op_bcd(c8_state *state, uint8_t x) {
    const uint16_t i = state->registers.i;
    const uint16_t vx = state->registers.v[x];

    state->memory[i] = (vx / 100) % 10;
    state->memory[i + 1] = (vx / 10) % 10;
    state->memory[i + 2] = vx % 10;

    state->registers.pc += 2;
}

/**
 * Fx55 - LD [I], Vx
 *
 * Store registers V0 through VX in memory starting at location I.
 */
static void c8_op_ld_i_vx(c8_state *state, uint8_t x) {
    const uint16_t i = state->registers.i;
    const uint16_t mem_size = state->config.memory_size;

    if (i + x >= mem_size) {
        x = mem_size - i - 1;
    }

    memcpy(state->memory + i, state->registers.v, x + 1);

    const bool shouldIncI = (state->config.quirks & C8_QUIRK_LOAD_STORE_NO_INC_I) == 0;
    const bool incByX = (state->config.quirks & C8_QUIRK_LOAD_STORE_INC_I_BY_X) != 0;

    if (shouldIncI) {
        state->registers.i += x + (incByX ? 0 : 1);
    }

    state->registers.pc += 2;
}

/**
 * Fx65 - LD Vx, [I]
 *
 * Copy values from memory location I through I + X into registers V0
 * through VX.
 */
static void c8_op_ld_vx_i(c8_state *state, uint8_t x) {
    const uint16_t i = state->registers.i;
    const uint16_t mem_size = state->config.memory_size;

    if (i + x >= mem_size) {
        x = mem_size - i - 1;
    }

    memcpy(state->registers.v, state->memory + i, x + 1);

    const bool shouldIncI = (state->config.quirks & C8_QUIRK_LOAD_STORE_NO_INC_I) == 0;
    const bool incByX = (state->config.quirks & C8_QUIRK_LOAD_STORE_INC_I_BY_X) != 0;

    if (shouldIncI) {
        state->registers.i += x + (incByX ? 0 : 1);
    }

    state->registers.pc += 2;
}

#pragma endregion

static bool c8_chip8_op_handler(c8_state *state, uint16_t op) {
    bool h = false; // is op handled

    switch (op & 0xF000) {
        case 0x0000:
            switch (op) {
                case 0x00E0:
                    c8_op_cls(state);
                    h = true;
                    break;
                case 0x00EE:
                    c8_op_ret(state);
                    h = true;
                    break;
                default:
                    c8_op_sys(state, op);
                    h = true;
                    break;
            }
            break;
        case 0x1000:
            c8_op_jp_nnn(state, op & 0x0FFF);
            h = true;
            break;
        case 0x2000:
            c8_op_call(state, op & 0x0FFF);
            h = true;
            break;
        case 0x3000:
            c8_op_se_vx_nn(state, (op & 0x0F00) >> 8, op & 0x00FF);
            h = true;
            break;
        case 0x4000:
            c8_op_sne_vx_nn(state, (op & 0x0F00) >> 8, op & 0x00FF);
            h = true;
            break;
        case 0x5000:
            switch (op & 0x000F) {
                case 0:
                    c8_op_se_vx_vy(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                default:
                    break;
            }
            break;
        case 0x6000:
            c8_op_ld_vx_nn(state, (op & 0x0F00) >> 8, op & 0x00FF);
            h = true;
            break;
        case 0x7000:
            c8_op_add_vx_nn(state, (op & 0x0F00) >> 8, op & 0x00FF);
            h = true;
            break;
        case 0x8000:
            switch (op & 0x000F) {
                case 0x0:
                    c8_op_ld_vx_vy(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                case 0x1:
                    c8_op_or(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                case 0x2:
                    c8_op_and(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                case 0x3:
                    c8_op_xor(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                case 0x4:
                    c8_op_add_vx_vy(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                case 0x5:
                    c8_op_sub(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                case 0x6:
                    c8_op_shr(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                case 0x7:
                    c8_op_subn(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                case 0xE:
                    c8_op_shl(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                default:
                    break;
            }
            break;
        case 0x9000:
            switch (op & 0x000F) {
                case 0:
                    c8_op_sne_vx_vy(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4);
                    h = true;
                    break;
                default:
                    break;
            }
            break;
        case 0xA000:
            c8_op_ld_i_nnn(state, op & 0x0FFF);
            h = true;
            break;
        case 0xB000:
            c8_op_jp_v0_nnn(state, op & 0x0FFF);
            h = true;
            break;
        case 0xC000:
            c8_op_rnd(state, (op & 0x0F00) >> 8, op & 0x00FF);
            h = true;
            break;
        case 0xD000:
            c8_op_drw(state, (op & 0x0F00) >> 8, (op & 0x00F0) >> 4, op & 0x000F);
            h = true;
            break;
        case 0xE000:
            switch (op & 0x00FF) {
                case 0x9E:
                    c8_op_skp(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                case 0xA1:
                    c8_op_sknp(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                default:
                    break;
            }
            break;
        case 0xF000:
            switch (op & 0x00FF) {
                case 0x07:
                    c8_op_ld_vx_dt(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                case 0x0A:
                    c8_op_ld_vx_key(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                case 0x15:
                    c8_op_ld_dt_vx(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                case 0x18:
                    c8_op_ld_st_vx(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                case 0x1E:
                    c8_op_add_i_vx(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                case 0x29:
                    c8_op_ld_i_font_vx(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                case 0x33:
                    c8_op_bcd(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                case 0x55:
                    c8_op_ld_i_vx(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                case 0x65:
                    c8_op_ld_vx_i(state, (op & 0x0F00) >> 8);
                    h = true;
                    break;
                default:
                    break;
            }
            break;
        default:
            // this should never happen, but just in case
            break;
    }

    return h;
}

c8_machine_config c8_get_default_machine_config() {
    c8_machine_config config = {
            .op_handlers = {c8_chip8_op_handler,},
            .op_handlers_size = 1,
			.quirks = C8_QUIRK_NONE,
            .memory_size = 4096,
			.cycles_per_frame = 15,
            .screen_width = 64,
            .screen_height = 32
    };
    return config;
}

c8_state *c8_create(c8_machine_config config) {
    c8_state *result = malloc(sizeof(c8_state));
    result->config = config;
    result->memory = nullptr;
    result->display = nullptr;

    c8_reset(result);

    return result;
}

void c8_destroy(c8_state *state) {
    if (state == nullptr) {
        return;
    }

    free(state->display);
    free(state);
}

void c8_set_rng_seed(c8_state *state, uint32_t seed) {
    if (state == nullptr) {
        return;
    }

    state->rng.seed = seed;
}

uint32_t c8_get_rng_seed(c8_state *state) {
    if (state == nullptr) {
        return 0;
    }

    return state->rng.seed;
}

void c8_load_rom(c8_state *state, const uint8_t *rom, uint16_t size) {
    if (state == nullptr || rom == nullptr) {
        return;
    }

    int sz = C8_MIN(size, state->config.memory_size - 0x200);
    memmove(state->memory + 0x200, rom, sz);
}

const c8_machine_config *c8_get_machine_config(c8_state *state) {
    if (state == nullptr) {
        return nullptr;
    }

    return &state->config;
}

const c8_registers *c8_get_registers(const c8_state *state) {
    if (state == nullptr) {
        return nullptr;
    }

    return &state->registers;
}

void c8_set_registers(c8_state *state, const c8_registers *regs) {
    if (state == nullptr || regs == nullptr) {
        return;
    }

    state->registers = *regs;
}

const uint8_t *c8_get_display(const c8_state *state, uint32_t *display_size) {
    if (state == nullptr || display_size == nullptr) {
        return nullptr;
    }

    *display_size = state->config.screen_width * state->config.screen_height;
    return state->display;
}

void c8_set_display(c8_state *state, const uint8_t *display, uint32_t display_size) {
    if (state == nullptr || display == nullptr) {
        return;
    }

    assert(display_size <= state->config.screen_width * state->config.screen_height);

    memmove(state->display, display, display_size);
}

const uint8_t* c8_get_memory(c8_state* state) {
    if (state == nullptr) {
         return nullptr;
    }

    return state->memory;
}

void c8_reset(c8_state *state) {
    if (state == nullptr) {
        return;
    }

    if (state->memory == nullptr) {
        state->memory = calloc(state->config.memory_size, 1);
    } else {
        memset(state->memory, 0, state->config.memory_size);
    }

    memcpy(state->memory + C8_PC_ON_FAULT, C8_FAULT_HANDLER, sizeof(C8_FAULT_HANDLER));
    memcpy(state->memory + C8_MEM_FONT_OFFSET, C8_FONT, 80);

    if (state->display == nullptr) {
        state->display = calloc(state->config.screen_width * state->config.screen_height, 1);
    } else {
        memset(state->display, 0, state->config.screen_width * state->config.screen_height);
    }

    state->delta_time = 0.f;
    memset(state->pressed_keys, 0, C8_KEY_MAX);
    state->registers = (c8_registers) {
            .stack = {0,},
            .v = {0,},
            .pc = 0x200,
            .i = 0,
            .sp = 0,
            .dt = 0,
            .st = 0,
    };
}

void c8_update_timers(c8_state *state, float delta_time) {
    if (state == nullptr) {
        return;
    }

    const float MS_PER_VBLANK = 1000.f / 60.f;

    state->delta_time += delta_time;

    int ticks_elapsed = (int) (state->delta_time / MS_PER_VBLANK);
    int new_dt = state->registers.dt - ticks_elapsed;
    int new_st = state->registers.st - ticks_elapsed;
    state->registers.dt = C8_MAX(new_dt, 0);
    state->registers.st = C8_MAX(new_st, 0);

    state->delta_time -= MS_PER_VBLANK * (float) ticks_elapsed;
    state->vblank = ticks_elapsed;
}

void c8_step(c8_state *state) {
    if (state == nullptr) {
        return;
    }

    uint16_t op = state->memory[state->registers.pc] << 8 | state->memory[state->registers.pc + 1];

    bool opHandled = false;
    for (int i = 0; i < state->config.op_handlers_size; ++i) {
        c8_op_handler h = state->config.op_handlers[i];
        opHandled = h(state, op);
        if (opHandled) {
            break;
        }
    }

    if (state->registers.pc >= state->config.memory_size) {
        state->registers.pc = C8_PC_ON_FAULT;
    }
}

void c8_step_frame(c8_state *state) {
    if (state == nullptr) {
        return;
    }

    for (uint16_t i = 0; i < state->config.cycles_per_frame; ++i) {
        c8_step(state);
    }
}

void c8_press_key(c8_state *state, c8_key key) {
    if (state == nullptr) {
        return;
    }

    state->pressed_keys[key] = true;
}

void c8_release_key(c8_state *state, c8_key key) {
    if (state == nullptr) {
        return;
    }

    state->pressed_keys[key] = false;
}
