/* external_interrupt_handler can be used as portHANDLE_INTERRUPT 
 * to enbale interrupt nesting.
 */

/* control status registers bits */
#define MIE_MTIE                 0x80
#define MSTATUS_MIE              0x8

/* PLIC registers and related helps manipulate registers */
#define PLIC_SOURCE_SHIFT        5
#define PLIC_SOURCE_MASK         0x1f
#define PLIC_PRIORITY_START      0x0c000000UL
#define PLIC_PRIORITY(source)    (*((volatile uint32_t *)(PLIC_PRIORITY_START + (source) * 4)))
#define PLIC_PENDING_START       0x0c001000UL
#define PLIC_PENDING(source)     (*((volatile uint32_t *)(PLIC_PENDING_START + ((source) >> PLIC_SOURCE_SHIFT) * 4)))
#define PLIC_PENDING_BIT(source) (1 << ((source) & PLIC_SOURCE_MASK))
#define PLIC_ENABLE_START        0x0c002000UL
#define PLIC_ENABLE(source)      (*((volatile uint32_t *)(PLIC_ENABLE_START + ((source) >> PLIC_SOURCE_SHIFT) * 4)))
#define PLIC_ENABLE_BIT(source)  (1 << ((source) & PLIC_SOURCE_MASK))
#define PLIC_THRESHOLD        	 (*((volatile uint32_t *)0x0c200000UL))
#define PLIC_CLAIM_COMPLETE      (*((volatile uint32_t *)0x0c200004UL))

volatile unsigned nesting_depth = 0, max_nesting_depth = 0;

extern void pwmx_isr0(unsigned plic_source);

/* if called, found to be external interrupt already for hifive1 revb
 * if software interrupt is not considered */
void external_interrupt_handler( void )
{
	nesting_depth++;
	if (nesting_depth > max_nesting_depth)
		max_nesting_depth = nesting_depth;

	uint32_t plic_source = PLIC_CLAIM_COMPLETE;
	uint32_t plic_source_priority = PLIC_PRIORITY(plic_source);

	uint32_t plic_threshold = PLIC_THRESHOLD;
	
	PLIC_THRESHOLD = plic_source_priority;

	__asm__ volatile("csrc mie, %0" :: "r"(MIE_MTIE));

	__asm__ volatile("csrs mstatus, %0" :: "r"(MSTATUS_MIE));

	/* temporarily set so, should change to application specific function */
	pwmx_isr0(plic_source);

	__asm__ volatile("csrc mstatus, %0" :: "r"(MSTATUS_MIE));
	
	if (nesting_depth == 1)
		__asm__ volatile("csrs mie, %0" :: "r"(MIE_MTIE));

	PLIC_THRESHOLD = plic_threshold;
	
	PLIC_CLAIM_COMPLETE = plic_source;

	nesting_depth--;
	return;
}
