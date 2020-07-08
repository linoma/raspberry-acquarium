#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <asm/uaccess.h>
#include <linux/kthread.h>
#include <linux/platform_data/dma-bcm2708.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <uapi/linux/sched/types.h>

#ifndef __MODH__
#define __MODH__

#ifdef __cplusplus
extern "C" {
#endif

#define BCM2708_PERI_BASE 	0x20000000
#define GPIO_BASE   		(BCM2708_PERI_BASE + 0x200000)
#define GPIO_LEN			0xb4
#define DMA_BASE   			(BCM2708_PERI_BASE + 0x7000)
#define DMA_LEN				0x1000

#define PWM_BASE			(BCM2708_PERI_BASE + 0x20C000)
#define PWM_LEN				0x40
#define CLK_BASE			(BCM2708_PERI_BASE + 0x101000)
#define CLK_LEN				0xA8
#define TIMER_BASE 			(BCM2708_PERI_BASE + 0x3000)
#define TIMER_LEN			0x8

#define GPFSEL0				(0x00/4)
#define GPFSEL1				(0x04/4)
#define GPSET0				(0x1c/4)
#define GPCLR0				(0x28/4)

#define PWM_CTL				(0x00/4)
#define PWM_STA				(0x04/4)
#define PWM_DMAC			(0x08/4)
#define PWM_RNG1			(0x10/4)
#define PWM_FIFO			(0x18/4)
#define PWMCLK_CNTL			40
#define PWMCLK_DIV			41
#define PWMCTL_MODE1		(1<<1)
#define PWMCTL_PWEN1		(1<<0)
#define PWMCTL_CLRF			(1<<6)
#define PWMCTL_USEF1		(1<<5)
#define PWMDMAC_ENAB		(1<<31)
#define PWMDMAC_THRSHLD		((15<<8)|(15<<0))
	
#define DMA_CS(a)				((BCM2708_DMA_CS + 0x100*a) /4)
#define DMA_CONBLK_AD(a)	((BCM2708_DMA_ADDR + 0x100*a) /4)
#define DMA_DEBUG(a)			  ((BCM2708_DMA_DEBUG + 0x100*a) /4)

#define BCM2708_DMA_END				(1<<1)	// Why is this not in mach/dma.h ?
#define BCM2708_DMA_NO_WIDE_BURSTS	(1<<26)

#define DMA_CHANNEL 9

typedef unsigned long ULONG;
typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned long ulong;
typedef unsigned long long ulong64;

struct ctldata_s {
  uint32_t pwmdata;
  ulong *gpiodata;	
	struct bcm2708_dma_cb *cb;
};

struct private_data{
	char *rd_data;
	char *partial_cmd;
	int rd_len;
	int partial_len;
	int reject_writes;
};

typedef struct _pwm{
	union{
		struct {
			int enable:1;
			int used:1;
		};
		ulong status;
	};
	byte pin;
	byte duty;
	word freq;
	ulong64 start;
	ulong pulse_width;
} PWM,*LPPWM;

#define NUM_SERVOS 8
extern PWM pwms[NUM_SERVOS];
extern struct ctldata_s *ctl;
extern volatile ulong *TIMER_REG;
extern volatile ulong *GPIO_REG;
extern volatile ulong *DMA_REG;
extern volatile ulong *CLK_REG;
extern volatile ulong *PWM_REG;

int dma_start(int ch);
int dma_stop(int ch);

#ifdef __cplusplus
}
#endif

#endif
