#include <stdint.h>
#define configTICK_RATE_HZ 1000U
void test_enter(void);
void test_exit(void);
#define taskENTER_CRITICAL() test_enter()
#define taskEXIT_CRITICAL() test_exit()
