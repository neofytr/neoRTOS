#include <stdint.h>
#include <stdbool.h>

#define SET_BIT(reg, bit) ((reg) |= ((1UL) << (bit)))
#define CLEAR_BIT(reg, bit) ((reg) &= ~((1UL) << (bit)))
#define TOGGLE_BIT(reg, bit) ((reg) ^= ((1UL) << (bit)))

#define COUNTER_ENABLE (0)
#define CLOCK_SOURCE (2)
#define SYSTICK_INTERRUPT (1)

#define SYS_CLOCK 16000000U // 16MHz is the default system clock without any configuration

static volatile uint32_t tick_count = 0; // global tick counter; each tick measures 1 milisecond passed

static inline void setup_systick(void);
static inline uint32_t get_tick_count(void);
void SysTick_handler(void);
static inline void LED_setup(void);
static inline bool has_time_passed(uint32_t time, uint32_t start_tick_count);