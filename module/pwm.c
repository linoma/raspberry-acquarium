#include "mod.h"
#include "pwm.h"
#include <linux/sort.h>

typedef struct _cb{
	ulong start;
	ulong end;
	ulong idx;
	ulong state;
	ulong pause;
	ulong cycle;
} CB, *LPCB;

struct _tmp{
	ulong idx,period,duty_cycle;
	ulong start;
	union{
		struct{
			unsigned int _state:2;
		};
		ulong _status;
	};
};

#define cb_push(list,a,b,c,d,e){\
		if(list == NULL || list_current >= list_size){\
			CB *pp = (CB*)kmalloc(sizeof(struct _cb)*(list_size+4096),GFP_KERNEL);\
			if(list != NULL){\
				memcpy(pp,list,list_size*sizeof(struct _cb));\
				kfree(list);\
			}\
			list = pp;\
			list_size += 4096;\
		}\
		list[list_current].idx = c;\
		list[list_current].end = b;\
		list[list_current].state = d;\
		list[list_current].pause = e;\
		list[list_current].cycle = ((e ? b: 0) / 10);\
		list[list_current++].start = a;\
	}

static int rearrange_dma_cb(void);
	
static int compare (const void * a, const void * b){
	struct _tmp *aa ,*bb;
  
	aa = (struct _tmp *)a;
	bb = (struct _tmp *)b;
	int i = aa->duty_cycle - bb->duty_cycle;
	if(i == 0)
		i = (aa->period - bb->period) * 1;
	return i*-1;
}

int set_pwm(int pwm,int freq,int duty){
	LPGPIO_PIN p;
	
	p = &gpio_pin[pwm];
	if(p->pwm == 0) {
		if(freq && duty)
			return add_pwm(pwm,freq,duty);
		return -1;
	}
	if(freq == 0 || duty == 0)
		del_pwm(pwm);
	else{
		p = &gpio_pin[pwm];
		p->freq = freq;
		p->duty = duty;
		rearrange_dma_cb();
	}
    printk(KERN_INFO "enable pwm %d f:%d d:%d\n",pwm,freq,duty);
    return 0;
}

int del_pwm(int pwm){
	LPGPIO_PIN p;
	
	p = &gpio_pin[pwm];
	p->used = 0;	
	p->pwm = 0;
	rearrange_dma_cb();	
	//int fnreg = p->pin / 10 + GPFSEL0;
	//int fnshft = (p->pin % 10) * 3;		
	GPIO_REG[GPCLR0] = 1 << p->pin;
	//GPIO_REG[fnreg] = (GPIO_REG[fnreg] & ~(7 << fnshft)) | (1 << fnshft);		
	return 0;
}

int add_pwm(int pin,int freq,int duty){
	LPGPIO_PIN p;
	
	if(gpio_pin[pin].used) 
		return -1;		
	p = gpio_pin + pin;
	p->used = 1;
	p->pin = pin;
    p->freq = freq;
    p->duty = duty;
    p->pwm = 1;
    p->irq = 0;
	ctl->gpiodata[pin] = 1 << p->pin;
	
	int fnreg = p->pin / 10 + GPFSEL0;
	int fnshft = (p->pin % 10) * 3;		
	GPIO_REG[GPCLR0] = 1 << p->pin;
	GPIO_REG[fnreg] = (GPIO_REG[fnreg] & ~(7 << fnshft)) | (1 << fnshft);	
	printk(KERN_INFO "add_pwm p:%d f:%d d:%d\n",p->pin,freq,duty);
	rearrange_dma_cb();
	return pin;
}

static int rearrange_dma_cb(void){
	int i,n,nn,period,step,res;
	ulong list_size,list_current;
	LPGPIO_PIN p;	
	struct _tmp *tmp = NULL;
	CB *cb = NULL;		
	
	res=-1;
	for(p=gpio_pin,nn = i=0;i<GPIO_PINS;i++,p++){
		if(p->used && p->pwm)
			nn++;
	}
	list_size = list_current = 0;
	if(nn == 0){
		res = 1;
		goto _step_1;	
	}
	res--;
	tmp = (struct _tmp*)kmalloc(sizeof(struct _tmp)*nn,GFP_KERNEL);
	if(tmp == NULL)
		goto _exit;	
				
	for(n=i = 0;i<GPIO_PINS;i++){
		if(!gpio_pin[i].used || !gpio_pin[i].pwm) continue;
		tmp[n].idx = i;
		tmp[n].period =  (1000000/gpio_pin[i].freq);
		tmp[n].duty_cycle = tmp[n].period * gpio_pin[i].duty / 100;
		tmp[n]._status = 0;
		n++;
	}
	step = tmp[n-1].duty_cycle;	
	if(nn == 1){
		period=0;
		cb_push(cb,period,tmp[0].duty_cycle,tmp[0].idx,1,1);
		period+=tmp[0].duty_cycle;
		cb_push(cb,period,tmp[0].period - period,tmp[0].idx,0,1);		
		goto _step_1;	
	}
	
	sort(tmp,nn,sizeof(struct _tmp),compare,NULL);		
	nn=n-1;
	for(period=0;period < 1000000;period+=step){
		for(i=0;i<n;i++){			
			switch(tmp[i]._state){
				case 0:
					tmp[i]._state=1;
					tmp[i].start=period;
					cb_push(cb,period,tmp[i].duty_cycle,tmp[i].idx,1,i==nn);
					break;
				case 1:
					if(period >= tmp[i].start+tmp[i].duty_cycle){
						tmp[i]._state=2;
						ulong pause = tmp[i].period-tmp[i].duty_cycle;
						cb_push(cb,period,pause,tmp[i].idx,0,i==n);
					}
					break;
				case 2:
					if(period >= (tmp[i].start + tmp[i].period)){
						period -= step;
						tmp[i]._state=0;
					}
					break;
			}
		}
	}	
_step_1:	
	dma_stop(DMA_CHANNEL);
	pwm_stop();
	if(!list_current)
		goto _exit;
	{
		CB *pcb;
		ulong l;
		struct bcm2708_dma_cb *dma_cb;
		
		dma_cb = ctl->cb;	
		
		for(pcb = cb,l = 0;l<list_current;l++,pcb++){
			//cout << pcb->idx << " " << pcb->start << " " << pcb->end << " " << pcb->state << " " << pcb->pause << " c: "<<pcb->cycle<<endl;
			dma_cb->info = BCM2708_DMA_NO_WIDE_BURSTS | BCM2708_DMA_WAIT_RESP;
			dma_cb->src    = (uint32_t)(&ctl->gpiodata[pcb->idx]) & 0x7fffffff;
			dma_cb->length = sizeof(uint32_t);
			dma_cb->stride = 0;		
			dma_cb->next = (uint32_t)(dma_cb + 1);			
			dma_cb->dst    = ((GPIO_BASE + ((pcb->state ? GPSET0 : GPCLR0) * 4)) & 0x00ffffff) | 0x7e000000;			
			dma_cb++;
			
			dma_cb->info   = BCM2708_DMA_NO_WIDE_BURSTS | BCM2708_DMA_WAIT_RESP | BCM2708_DMA_D_DREQ | BCM2708_DMA_PER_MAP(5);
			dma_cb->src    = (uint32_t)(&ctl->pwmdata) & 0x7fffffff;
			dma_cb->dst    = ((PWM_BASE + PWM_FIFO*4) & 0x00ffffff) | 0x7e000000;
			dma_cb->length = sizeof(uint32_t) * (pcb->cycle);
			dma_cb->stride = 0;
			dma_cb->next = (uint32_t)(dma_cb + 1);
			dma_cb++;
			printk(KERN_INFO "pwm l:%lu %d\n",pcb->cycle,pcb->state);	
		}
		dma_cb--;		
		dma_cb->next = (uint32_t)ctl->cb;
		res = 0;
	}
	
	pwm_start();
	dma_start(DMA_CHANNEL);
_exit:	
	if(tmp)
		kfree(tmp);
	if(cb)
		kfree(cb);
	return res;	
}

int pwm_start(void){
    PWM_REG[PWM_CTL] = PWMCTL_CLRF;
    udelay(10);
    PWM_REG[PWM_CTL] = PWMCTL_USEF1 | PWMCTL_PWEN1;
    udelay(10);
    return 0;
}    

int pwm_stop(void){
	PWM_REG[PWM_CTL] = 0;
	udelay(10);
	PWM_REG[PWM_STA] = PWM_REG[PWM_STA];
	udelay(10);
	return 0;
}
