#pragma once

#include <stdint.h>
#include "c23_compat.h"

/**
 * An enum for CHIP-8 keys.
 */
typedef enum c8_key
#ifndef C23_COMPAT_NO_ENUM_TYPES
    : uint8_t
#endif
{
    C8_KEY_0 = 0,
    C8_KEY_1,
    C8_KEY_2,
    C8_KEY_3,
    C8_KEY_4,
    C8_KEY_5,
    C8_KEY_6,
    C8_KEY_7,
    C8_KEY_8,
    C8_KEY_9,
    C8_KEY_A,
    C8_KEY_B,
    C8_KEY_C,
    C8_KEY_D,
    C8_KEY_E,
    C8_KEY_F,
    C8_KEY_MAX
} c8_key;

/**
 * CHIP-8 quirk flags enum.
 *
 * (stolen from https://github.com/chip-8/chip-8-database/)
 */
typedef enum c8_quirk
#ifndef C23_COMPAT_NO_ENUM_TYPES
    : uint32_t
#endif
{
    /**
     * A value indicating no quirks.
     */
	C8_QUIRK_NONE = 0,

	/**
	 * 
	 * Shift quirk
	 *
	 * On most systems the shift opcodes take `vY` as input and stores the
	 * shifted version of `vY` into `vX`. The interpreters for the HP48 took
	 * `vX` as both the input and the output, introducing the shift quirk.
	 *
	 * Set: Opcodes `8XY6` and `8XYE` take `vX` as both input and output.
	 * 
	 * Unset: Opcodes `8XY6` and `8XYE` take `vY` as input and `vX` as output.
	 */
	C8_QUIRK_SHIFT = 1 << 0,

    /**
     * Load/Store quirk: increment index register by X
     *
     * On most systems storing and retrieving data between registers and memory
     * increments the `i` register with `X + 1` (the number of registers read
     * or written). So for each register read or written, the index register
     * would be incremented. The CHIP-48 interpreter for the HP48 would only
     * increment the `i` register by `X`, introducing the first
     * load/store quirk.
     *
     * Set: `FX55` and `FX65` increment the `i` register with `X`.
     * 
     * Unset: `FX55` and `FX65` increment the `i` register with `X + 1`.
     */
    C8_QUIRK_LOAD_STORE_INC_I_BY_X = 1 << 1,

    /**
     * Load/Store quirk: leave index register unchanged
     *
     * On most systems storing and retrieving data between registers and memory
     * increments the `i` register relative to the number of registers read or
     * written. The Superchip 1.1 interpreter for the HP48 however did not
     * increment the `i` register at all, introducing the second
     * load/store quirk.
     *
     * Set: `FX55` and `FX65` leave the `i` register unchanged.
     *
     * Unset: `FX55` and `FX65` increment the `i` register.
     */
    C8_QUIRK_LOAD_STORE_NO_INC_I = 1 << 2,

    /**
     * Wrap quirk
     *
     * Most systems, when drawing sprites to the screen, will clip sprites at
     * the edges of the screen. The Octo interpreter, which spawned the XO-CHIP
     * variant of CHIP-8, instead wraps the sprite around to the other side of
     * the screen. This introduced the wrap quirk.
     *
     * Set: The `DXYN` opcode wraps around to the other side of the screen when
     * drawing at the edges.
     *
     * Unset: The `DXYN` opcode clips when drawing at the edges of the screen.
     */
    C8_QUIRK_WRAP_SPRITES = 1 << 3,

    /**
     * Jump quirk
     *
     * The jump to `<address> + v0` opcode was wronly implemented on all the
     * HP48 interpreters as jump to `<address> + vX`, introducing the
     * jump quirk.
     *
     * Set: Opcode `BXNN` jumps to address `XNN + vX`.
     *
     * Unset: Opcode `BNNN` jumps to address `NNN + v0`.
     */
    C8_QUIRK_BXNN_JUMP = 1 << 4,

    /**
     * vBlank quirk
     *
     * The original Cosmac VIP interpreter would wait for vertical blank before
     * each sprite draw. This was done to prevent sprite tearing on the
     * display, but it would also act as an accidental limit on the execution
     * speed of the program. Some programs rely on this speed limit to be
     * playable. Vertical blank happens at 60Hz, and as such its logic be
     * combined with the timers.
     *
     * Set: Opcode `DXYN` waits for vertical blank (so max 60 sprites drawn
     * per second.)
     *
     * Unset: Opcode `DXYN` draws immediately (number of sprites drawn per
     * second only limited to number of CPU cycles per frame.)
     */
    C8_QUIRK_VBLANK = 1 << 5,

    /**
     * VF reset quirk
     *
     * On the original Cosmac VIP interpreter, `vF` would be reset after each
     * opcode that would invoke the maths coprocessor. Later interpreters have
     * not copied this behaviour.
     *
     * Set: Opcodes `8XY1`, `8XY2` and `8XY3` (OR, AND and XOR) will set `vF`
     * to zero after execution (even if `vF` is the parameter `X`.)
     *
     * Unset: Opcodes `8XY1`, `8XY2` and `8XY3` (OR, AND and XOR) will leave
     * `vF` unchanged (unless `vF` is the parameter `X`.)
     */
    C8_QUIRK_VF_RESET = 1 << 6,
} c8_quirk;

/**
 * CHIP-8 machine state.
 */
typedef struct c8_state c8_state;

/**
 * A function pointer type for CHIP-8 operation handler.
 * Returns true if matched.
 */
typedef bool (*c8_op_handler)(c8_state* state, uint16_t op);

/**
 * CHIP-8 machine configuration struct.
 */
typedef struct c8_machine_config {
    c8_op_handler op_handlers[8]; ///< Opcode handlers.
    uint32_t op_handlers_size; ///< A size of `op_handlers` array.
    uint32_t quirks; ///< A bitset of CHIP-8 quirks.
    uint16_t memory_size; ///< CHIP-8 machine's memory size, in bytes.
    uint16_t cycles_per_frame; ///< A number of cycles per frame.
    uint8_t screen_width; ///< Screen width, in logical pixels.
    uint8_t screen_height; ///< Screen height, in logical pixels.
} c8_machine_config;

/**
 * Gets CHIP-8 default machine configuration.
 * @see c8_create()
 */

c8_machine_config c8_get_default_machine_config();

/**
 * CHIP-8 registers.
 */
typedef struct c8_registers {
    uint16_t stack[16]; ///< Stack, which can store up to 16 16-bit values.
    uint8_t v[16]; ///< Common 8-bit registers.
    uint16_t pc; ///< Program counter. Start value is 0x200.
    uint16_t i; ///< I 16-bit register.
    uint8_t sp; ///< Stack pointer.
    uint8_t dt; ///< Delay timer.
    uint8_t st; ///< Sound timer.
} c8_registers;

/**
 * Creates a new CHIP-8 machine instance.
 *
 * @param config CHIP-8 machine configuration.
 * @return CHIP-8 machine state or NULL.
 */
c8_state* c8_create(c8_machine_config config);

/**
 * Destroys a CHIP-8 machine instance.
 *
 * @param state CHIP-8 machine state to be destroyed.
 */
void c8_destroy(c8_state* state);

/**
 * Sets a seed for internal PRNG (specifically for RND instruction.)
 * @warning Passing 0 will lead to broken PRNG.
 *
 * @param state CHIP-8 machine state.
 * @param seed 32-bit seed for PRNG.
 */
void c8_set_rng_seed(c8_state *state, uint32_t seed);

/**
 * Gets current seed for internal PRNG.
 *
 * @param state CHIP-8 machine state
 * @return Current seed for internal PRNG.
 */
uint32_t c8_get_rng_seed(c8_state* state);

/**
 * Loads ROM in machine's memory.
 *
 * @param state CHIP-8 machine state.
 * @param rom A pointer to ROM to be loaded.
 * @param size ROM size in bytes.
 */
void c8_load_rom(c8_state* state, const uint8_t* rom, uint16_t size);

/**
 * Gets a machine config which was used on machine state creation.
 * @see c8_machine_config
 *
 * @param state CHIP-8 machine state.
 * @return A machine's config for the given state.
 */
const c8_machine_config* c8_get_machine_config(c8_state* state);

/**
 * Gets a machine's registers.
 * @see c8_registers
 *
 * @param state CHIP-8 machine state.
 * @return CHIP-8 registers.
 */
const c8_registers* c8_get_registers(const c8_state* state);

/**
 * Sets a machine's registers.
 *
 * @param state CHIP-8 machine state.
 * @param regs CHIP-8 new registers state.
 */
void c8_set_registers(c8_state* state, const c8_registers* regs);

/**
 * Gets display state from a machine.
 *
 * @warning You should do boundary check with `display_size` value since
 * display dimensions from `c8_get_machine_config()` are logical,
 * and `display_size` is basically `WIDTH/8 * HEIGHT/8`.
 * 
 * @param state CHIP-8 machine state.
 * @param display_size A pointer to uint32_t where returned display size
 * will be written.
 * @return A machine's display state.
 */
const uint8_t* c8_get_display(const c8_state* state, uint32_t* display_size);

/**
 * Gets machine's memory pointer.
 *
 * @param state CHIP-8 machine state
 * @return A pointer to machine's memory, starting from 0x000.
 */
const uint8_t* c8_get_memory(c8_state* state);

/**
 * Resets a state.
 *
 * @param state CHIP-8 machine state.
 */
void c8_reset(c8_state* state);

/**
 * Updates sound and delay timers.
 *
 * @param state CHIP-8 machine state.
 * @param delta_time Time elapsed since last update call.
 */
void c8_update_timers(c8_state* state, float delta_time);

/**
 * Makes a step in code execution.
 *
 * @param state CHIP-8 machine state.
 */
void c8_step(c8_state* state);

/**
 * Makes `cycles_per_frame` steps in code execution.
 * `cycles_per_frame` is taken from machine's config.
 *
 * @see c8_step()
 *
 * @param state CHIP-8 machine state.
 */
void c8_step_frame(c8_state* state);

/**
 * Passes a key press.
 *
 * @param state CHIP-8 machine state.
 * @param key Pressed key (0-F)
 */
void c8_press_key(c8_state* state, c8_key key);

/**
 * Passes a key release.
 *
 * @param state CHIP-8 machine state.
 * @param key Pressed key (0-F)
 */
void c8_release_key(c8_state* state, c8_key key);