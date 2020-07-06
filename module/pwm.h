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


#ifndef __PWMH__
#define __PWMH__

#ifdef __cplusplus
extern "C" {
#endif

int add_pwm(int pin,int freq,int duty);
int pwm_stop(void);
int pwm_start(void);

#ifdef __cplusplus
}
#endif

#endif
