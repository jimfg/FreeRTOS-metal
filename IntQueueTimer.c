#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "IntQueueTimer.h"
#include "IntQueue.h"

#define timerINT_0_FREQUENCY    ( 157UL )
#define timerINT_1_FREQUENCY    ( 201UL )
#define timerINT_2_FREQUENCY    ( 403UL )

#define timerLOWER_PRIORITY     ( configMAX_API_CALL_INTERRUPT_PRIORITY - 1 )
#define timerMEDIUM_PRIORITY    ( configMAX_API_CALL_INTERRUPT_PRIORITY     )
#define timerHIGHER_PRIORITY    ( configMAX_API_CALL_INTERRUPT_PRIORITY + 1 )

/* PLIC */
/* help manipulating */
#define PLIC_SOURCE_SHIFT        5
#define PLIC_SOURCE_MASK         0x1f
#define PLIC_REG_BIT(source)     ( 1 << ( ( source ) & PLIC_SOURCE_MASK ) )
/* memory mapped registers */
#define PLIC_PRIORITY_START      0x0c000000UL
#define PLIC_PRIORITY(source)    ( * ( ( volatile uint32_t * )( PLIC_PRIORITY_START + ( source ) * 4 ) ) )
#define PLIC_PENDING_START       0x0c001000UL
#define PLIC_PENDING(source)     ( * ( ( volatile uint32_t * )( PLIC_PENDING_START + ( ( source ) >> PLIC_SOURCE_SHIFT ) * 4 ) ) )
#define PLIC_ENABLE_START        0x0c002000UL
#define PLIC_ENABLE(source)      ( * ( ( volatile uint32_t * )( PLIC_ENABLE_START + ( ( source ) >> PLIC_SOURCE_SHIFT ) * 4 ) ) )
#define PLIC_THRESHOLD        	 ( * ( ( volatile uint32_t * ) 0x0c200000UL ) )
#define PLIC_CLAIM_COMPLETE      ( * ( ( volatile uint32_t * ) 0x0c200004UL ) )

/* GPIO */
#define GPIO_IOF_EN              ( * ( ( volatile uint32_t * ) 0x10012038UL ) )
#define GPIO_IOF_SEL             ( * ( ( volatile uint32_t * ) 0x1001203cUL ) )
#define GPIO_REG_BIT(num)        ( 1 << ( num ) )

/* PWM */
/* bits in PWM_x_PWMCFG */
#define PWM_PRESCALE_BITS           0xf
#define PWM_STICKY_BIT              ( 1 << 8 )
#define PWM_ZEROCMP_BIT             ( 1 << 9 )
#define PWM_DEGLITCH_BIT            ( 1 << 10 )
#define PWM_ENALWAYS_BIT            ( 1 << 12 )
#define PWM_PWMCMP0IP_BIT           ( 1 << 28 )
/* memory mapped registers */
#define PWM_1_BASE                  0x10025000UL
#define PWM_1_PWMCFG                ( * ( ( volatile uint32_t * )( PWM_1_BASE ) ) )
#define PWM_1_PWMCMP0               ( * ( ( volatile uint32_t * )( PWM_1_BASE + 0x20 ) ) )
#define PWM_2_BASE                  0x10035000UL
#define PWM_2_PWMCFG                ( * ( ( volatile uint32_t * )( PWM_2_BASE ) ) )
#define PWM_2_PWMCMP0               ( * ( ( volatile uint32_t * )( PWM_2_BASE + 0x20 ) ) )
#define PWM_0_BASE                  0x10015000UL
#define PWM_0_PWMCFG                ( * ( ( volatile uint32_t * )( PWM_0_BASE ) ) )
#define PWM_0_PWMCMP0               ( * ( ( volatile uint32_t * )( PWM_0_BASE + 0x20 ) ) )

#define PWM_1_PWMCMP0IP_PLIC_SOURCE 44
#define PWM_1_PWMCMP0IP_GPIO_NUMBER 20
#define PWM_2_PWMCMP0IP_PLIC_SOURCE 48
#define PWM_2_PWMCMP0IP_GPIO_NUMBER 10
#define PWM_0_PWMCMP0IP_PLIC_SOURCE 40
#define PWM_0_PWMCMP0IP_GPIO_NUMBER 0

/* frequency of high frequency alternative oscillator which is used
 * in default initialization of freedom metal
 */
#define CLOCK_RATE               16000000

void pwmx_isr0( unsigned plic_source )
{
	if ( plic_source == PWM_1_PWMCMP0IP_PLIC_SOURCE ) {
		PWM_1_PWMCFG &= ~PWM_PWMCMP0IP_BIT;
		portYIELD_FROM_ISR( xFirstTimerHandler() );
	} else if ( plic_source == PWM_2_PWMCMP0IP_PLIC_SOURCE ) {
		PWM_2_PWMCFG &= ~PWM_PWMCMP0IP_BIT;
		portYIELD_FROM_ISR( xSecondTimerHandler() );
	} else if ( plic_source == PWM_0_PWMCMP0IP_PLIC_SOURCE ) {
		PWM_0_PWMCFG &= ~PWM_PWMCMP0IP_BIT;
		// just clear this pending, no API call
	}
}

/* given frequency wanted, calculate prescale and count for pwm.
 * return zero for success
 */
int pwm_frequency_to_setting( unsigned frequency, int is_pwm_0, unsigned *prescale_ptr, unsigned *count_ptr )
{
	unsigned prescale = 0, count, max_count;
	if (is_pwm_0)
		max_count = 256UL;
	else
		max_count = 65536UL;

	do {
		count = CLOCK_RATE / ( 1UL << prescale ) / frequency;
	} while ( count > max_count && prescale++ <= 15UL );

	if ( prescale > 15UL )
		return 1;
	*prescale_ptr = prescale;
	*count_ptr = count;
	return 0;
}
/* pwm1 as timer 0, pwm2 as timer 1
 * as these two pwms have 16-bit pwms register which 
 * make them able to stand lower interrupt rate.
 * pwm0 as timer 2.
 */
void vInitialiseTimerForIntQueueTest( void )
{
	PLIC_PRIORITY( PWM_1_PWMCMP0IP_PLIC_SOURCE ) = timerLOWER_PRIORITY;
	PLIC_PRIORITY( PWM_2_PWMCMP0IP_PLIC_SOURCE ) = timerMEDIUM_PRIORITY;
	PLIC_PRIORITY( PWM_0_PWMCMP0IP_PLIC_SOURCE ) = timerHIGHER_PRIORITY;

	GPIO_IOF_EN |= GPIO_REG_BIT( PWM_1_PWMCMP0IP_GPIO_NUMBER ) | 
	               GPIO_REG_BIT( PWM_2_PWMCMP0IP_GPIO_NUMBER ) |
				   GPIO_REG_BIT( PWM_0_PWMCMP0IP_GPIO_NUMBER );
	GPIO_IOF_SEL |= GPIO_REG_BIT( PWM_1_PWMCMP0IP_GPIO_NUMBER ) | 
	                GPIO_REG_BIT( PWM_2_PWMCMP0IP_GPIO_NUMBER ) |
					GPIO_REG_BIT( PWM_0_PWMCMP0IP_GPIO_NUMBER );
	PWM_1_PWMCFG = PWM_2_PWMCFG = PWM_0_PWMCFG = 0;

	unsigned prescale, count;
	if ( pwm_frequency_to_setting( timerINT_0_FREQUENCY, 0, &prescale, &count ) )
		configPRINT_STRING("set freq failed, pwm1\r\n");
	PWM_1_PWMCFG |= prescale;
	PWM_1_PWMCMP0 = --count;
	
	if ( pwm_frequency_to_setting( timerINT_1_FREQUENCY, 0, &prescale, &count ) )
		configPRINT_STRING("set freq failed, pwm 2\r\n");
	PWM_2_PWMCFG |= prescale;
	PWM_2_PWMCMP0 = --count;

	if ( pwm_frequency_to_setting( timerINT_2_FREQUENCY, 1, &prescale, &count ) )
		configPRINT_STRING("set freq failed, pwm 0\r\n");
	PWM_0_PWMCFG |= prescale;
	PWM_0_PWMCMP0 = --count;

	PWM_1_PWMCFG |= PWM_ZEROCMP_BIT | PWM_DEGLITCH_BIT | PWM_ENALWAYS_BIT | PWM_STICKY_BIT;
	PWM_2_PWMCFG |= PWM_ZEROCMP_BIT | PWM_DEGLITCH_BIT | PWM_ENALWAYS_BIT | PWM_STICKY_BIT;
	PWM_0_PWMCFG |= PWM_ZEROCMP_BIT | PWM_DEGLITCH_BIT | PWM_ENALWAYS_BIT | PWM_STICKY_BIT;

	PLIC_ENABLE( PWM_1_PWMCMP0IP_PLIC_SOURCE ) |= PLIC_REG_BIT( PWM_1_PWMCMP0IP_PLIC_SOURCE );
	PLIC_ENABLE( PWM_2_PWMCMP0IP_PLIC_SOURCE ) |= PLIC_REG_BIT( PWM_2_PWMCMP0IP_PLIC_SOURCE );
	PLIC_ENABLE( PWM_0_PWMCMP0IP_PLIC_SOURCE ) |= PLIC_REG_BIT( PWM_0_PWMCMP0IP_PLIC_SOURCE );
}
