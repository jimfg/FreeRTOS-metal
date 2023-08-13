#include "FreeRTOS.h"
#include "task.h"

#include "IntQueueTimer.h"
#include "IntQueue.h"

#include "metal/machine.h"
#include "metal/machine/platform.h"

/* additional to be add */
#define timerINT_0_FREQUENCY    ( 207UL )
#define timerINT_1_FREQUENCY    ( 213UL )

#define timerLOWER_PRIORITY     ( configMAX_API_CALL_INTERRUPT_PRIORITY - 1 )
#define timerMEDIUM_PRIORITY    ( configMAX_API_CALL_INTERRUPT_PRIORITY     )

void pwmx_isr0(int id, void *data)
{
	metal_pwm_clr_interrupt((struct metal_pwm *)data, 0);
	if (id == 44) {
		portYIELD_FROM_ISR( xFirstTimerHandler() );
	} else if (id == 48) {
		portYIELD_FROM_ISR( xSecondTimerHandler() );
	}
}

void vInitialiseTimerForIntQueueTest( void )
{
	struct metal_interrupt *plic;
	struct metal_pwm *pwm1, *pwm2;
	int pwm1_id0, pwm2_id0;
	
	plic = (struct metal_interrupt *)&__metal_dt_interrupt_controller_c000000;

	pwm1 = metal_pwm_get_device(1);
	pwm1_id0 = metal_pwm_get_interrupt_id(pwm1, 0);		/* source 44 for PLIC */
	metal_interrupt_register_handler(plic, pwm1_id0, pwmx_isr0, pwm1);
	metal_interrupt_set_priority(plic, pwm1_id0, timerLOWER_PRIORITY);

	pwm2 = metal_pwm_get_device(2);
	pwm2_id0 = metal_pwm_get_interrupt_id(pwm2, 0);		/* source 48 for PLIC */
	metal_interrupt_register_handler(plic, pwm2_id0, pwmx_isr0, pwm2);
	metal_interrupt_set_priority(plic, pwm2_id0, timerMEDIUM_PRIORITY);

	metal_pwm_enable(pwm1);
	metal_pwm_set_freq(pwm1, 0, timerINT_0_FREQUENCY);
	metal_pwm_set_duty(pwm1, 1, 0, METAL_PWM_PHASE_CORRECT_DISABLE);
	metal_pwm_set_duty(pwm1, 2, 0, METAL_PWM_PHASE_CORRECT_DISABLE);
	metal_pwm_set_duty(pwm1, 3, 0, METAL_PWM_PHASE_CORRECT_DISABLE);

	metal_pwm_enable(pwm2);
	metal_pwm_set_freq(pwm2, 0, timerINT_1_FREQUENCY);
	metal_pwm_set_duty(pwm2, 1, 0, METAL_PWM_PHASE_CORRECT_DISABLE);
	metal_pwm_set_duty(pwm2, 2, 0, METAL_PWM_PHASE_CORRECT_DISABLE);
	metal_pwm_set_duty(pwm2, 3, 0, METAL_PWM_PHASE_CORRECT_DISABLE);

	metal_pwm_trigger(pwm1, 0, METAL_PWM_CONTINUOUS);
	metal_pwm_cfg_interrupt(pwm1, METAL_PWM_INTERRUPT_ENABLE);
	
	metal_pwm_trigger(pwm2, 0, METAL_PWM_CONTINUOUS);
	metal_pwm_cfg_interrupt(pwm2, METAL_PWM_INTERRUPT_ENABLE);

	metal_interrupt_enable(plic, pwm1_id0);
	metal_interrupt_enable(plic, pwm2_id0);
}
