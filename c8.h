#pragma once

#include <stdint.h>

/**
 * An enum for CHIP-8 keys.
 */
typedef enum c8_key : uint8_t {
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
    uint16_t memory_size; ///< CHIP-8 machine's memory size, in bytes.
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
 * Sets display state for a machine.
 *
 * @see c8_get_display
 *
 * @param state CHIP-8 machine state.
 * @param display A new display state.
 * @param display_size Given display's array size.
 */
void c8_set_display(c8_state* state, const uint8_t* display, uint32_t display_size);

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
void c8_update(c8_state* state, float delta_time);

/**
 * Makes a step in code execution.
 *
 * @param state CHIP-8 machine state.
 */
void c8_step(c8_state* state);

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