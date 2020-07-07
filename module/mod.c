#include "mod.h"
#include "pwm.h"
#include "ioctl.h"

MODULE_LICENSE("GPL");

volatile ulong *TIMER_REG;
volatile ulong *GPIO_REG;
volatile ulong *DMA_REG;
volatile ulong *CLK_REG;
volatile ulong *PWM_REG;
static struct task_struct *task;
static struct kobject *mod_object;
struct ctldata_s *ctl;
static unsigned long ctldatabase;
static struct cdev cdev[2];
static dev_t devnum;
static struct class *my_class;
byte pwm_gpio[] = {4,17,18,27,22,23,24,25};
static unsigned long long _start;
static unsigned long lino,_timer_ticks;
const int NUM_SERVOS=sizeof(pwm_gpio);
PWM pwms[8]={0};

#define _NOW *((ulong64 *)&TIMER_REG[1])

int mod_thread(void *data){
	struct sched_param param = { .sched_priority = 40 };
	unsigned long long _last,_now,_ss;
	unsigned long _period;

	sched_setscheduler(current, SCHED_FIFO, &param);	
	
	_period = 1000000.0 / 15000;
	
	while(1){
	    usleep_range(_period >> 1,_period);
	    _now = _NOW;
	    if((_now - _last) >= _period){
			struct bcm2708_dma_cb *p;
			int i;
			/*,cb = (DMA_REG[DMA_CONBLK_AD] - ((uint32_t)ctl->cb & 0x7fffffff)) / sizeof(ctl->cb[0]);*/
			LPPWM pp;
			
			/*for(pp=pwms,i=0;i<8;i++,pp++){
				if(pp->pulse_width && (_now - pp->start) >= pp->pulse_width){
					ctl->cb[i*3].dst = ((GPIO_BASE + GPSET0*4) & 0x00ffffff) | 0x7e000000;
					pp->start = _now;
					int cb = (DMA_REG[DMA_CONBLK_AD] - ((uint32_t)ctl->cb & 0x7fffffff)) / sizeof(ctl->cb[0]);
					//printk(KERN_INFO "dma %d\n",cb);
				}
			}*/
			lino++;
			_last = _now;
			if((_now - _ss) >= 1000000){
				_timer_ticks = lino;
				lino=0;
				_ss = _now;
			}
		}
	    if(kthread_should_stop()) 
			break;
	}
    return 0;
}

static ssize_t show_mod(struct kobject *kobj,struct kobj_attribute *attrs,char *buf){
    return snprintf(buf, PAGE_SIZE, "%lu\n", _timer_ticks);
}

static ssize_t set_mod(struct kobject *kobj,struct kobj_attribute *attr,const char *buff,size_t count){
	return count;
}

ssize_t timer_read(struct file *filp, char __user * buff, size_t count,loff_t * offset){
    unsigned long long _now;
    unsigned long rval;
	
    if (count < 1) return 0;
    _now = _NOW;
    if(count == 4){
		rval = (unsigned long)_now;
		rval = put_user(rval,buff);
    }
    else{
		if(count > 8) count = 8;
		rval = put_user(_now,buff);
    }
    if (rval < 0)
		return -EFAULT;    
    return count;
}

static long pwm_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	ulong *p,sel;	
	struct private_data* const pdata = file->private_data;
	
	p = (ulong *)arg;		
	get_user(sel,p++);
	switch(cmd){
		case RPIMOD_IOC_GPIO_HIGH:
		    GPIO_REG[GPSET0] |= 1 << sel;
		    return 0;
		case RPIMOD_IOC_GPIO_LOW:
		    GPIO_REG[GPCLR0] |= 1 << sel;
		    return 0;		    
		case RPIMOD_IOC_GPIO_MODE:
			{
				int fnreg = (sel / 10) + GPFSEL0;
				int fnshft = (sel % 10) * 3;				
				GPIO_REG[fnreg] = (GPIO_REG[fnreg] & ~(7 << fnshft)) | (1 << fnshft);	
			}
		    return 0;		    
		case RPIMOD_IOC_SETFREQ:		
			return 0;
		case RPIMOD_IOC_SETDUTY:
			return 0;
		case RPIMOD_IOC_ADD:
		{
		    ulong freq,duty;
		    
		    get_user(freq,p++);
		    get_user(duty,p++);
		    add_pwm(17,10,20);
		}
		    return 0;
	}
	return -EINVAL;
}

static int pwm_open(struct inode *inod, struct file *fil){
	int sz = sizeof(struct private_data) + (sizeof(char)*10*NUM_SERVOS + sizeof(char)*10);
	
	fil->private_data = kmalloc(sz, GFP_KERNEL);
	if (0 == fil->private_data){
		printk(KERN_WARNING "bcm2708: Failed to allocate user data\n");
		return -ENOMEM;
	}
	memset(fil->private_data, 0, sz);
	struct private_data *p = (struct private_data *)fil->private_data;
	p->rd_data = (char *)(p + 1);
	p->partial_cmd = &p->rd_data[NUM_SERVOS*10];	
	return 0;
}

static int pwm_close(struct inode *inod,struct file *fil){
	int ret;
	struct private_data* const pdata = fil->private_data;
	
	ret =0;
	if(pdata){
		if(pdata->partial_len){
			printk(KERN_WARNING "bcm2708: partial command pending on close()\n");
			ret = -EIO;
		}
		kfree(pdata);
	}
	return ret;
}

static ssize_t pwm_write(struct file *filp,const char *user_buf,size_t count,loff_t *f_pos)
{
	struct private_data* const pdata = filp->private_data;
	char buf[128], *p = buf, nl,*pp;
	int len = pdata->partial_len;

	if (0 == pdata)
		return -EFAULT;
	if (pdata->reject_writes)
		return -EINVAL;
	memcpy(buf, pdata->partial_cmd, len);
	pdata->partial_len = 0;
	if (count > sizeof(buf) - len - 1)
		count = sizeof(buf) - len - 1;
	if (raw_copy_from_user(buf+len, user_buf, count))
		return -EFAULT;
	len += count;
	buf[len] = '\0';
	while (p < buf+len) {
		if ((pp = strchr(p, '\n'))) {
			int pwm, freq, duty;			
			
			duty = 50;
			if (sscanf(p,"%d=%d%c", &pwm, &freq, &nl) != 3){
				pdata->reject_writes = 1;
				return -EINVAL;				
			}
			
			if(nl == ','){
				int ii= strchr(p,',');
				
				if(sscanf(p+ii,"%d%c",&duty,&nl) != 2 && nl != '\n'){
					pdata->reject_writes = 1;
					return -EINVAL;				
				}
			}
			set_pwm(pwm,freq,duty);
			p = pp + 1;
		}
		else if (buf+len - p > 10) {
			return -EINVAL;
		}
		else
			break;
	}
	
	pdata->partial_len = buf+len - p;
	memcpy(pdata->partial_cmd, p, pdata->partial_len);

	return count;
}

static struct kobj_attribute mod_attribute=__ATTR(usec,(S_IWUSR|S_IRUSR),show_mod,set_mod);

static struct file_operations fops[] = {
	{.owner = THIS_MODULE,.read = timer_read,.write = NULL,},
    {.owner = THIS_MODULE,.read = NULL,.write = pwm_write,.unlocked_ioctl = pwm_ioctl,.compat_ioctl = pwm_ioctl,.open=pwm_open,.release=pwm_close}
};

static int __init mod_init(void){	
    int err,i,major,s;
	dev_t my_device;

    lino = _timer_ticks = 0;

	ctldatabase = __get_free_pages(GFP_KERNEL, 0);
	if (ctldatabase == 0) {
		printk(KERN_WARNING "bcm2835: alloc_pages failed\n");
		return -EFAULT;
	}
	
	ctl = (struct ctldata_s *)ctldatabase;
	ctl->gpiodata = (ulong *)(ctl+1);
	{
	    ulong64 adr = (ctl->gpiodata + NUM_SERVOS);
	    adr = (adr + 32) & ~0x1f;
	    ctl->cb = (struct bcm2708_dma_cb *)adr;
	}
	
	TIMER_REG = (ulong *)ioremap(TIMER_BASE, TIMER_LEN);    
	GPIO_REG = (ulong *)ioremap(GPIO_BASE, GPIO_LEN);
	DMA_REG  = (ulong *)ioremap(DMA_BASE,  DMA_LEN);
	CLK_REG  = (ulong *)ioremap(CLK_BASE,  CLK_LEN);
	PWM_REG  = (ulong *)ioremap(PWM_BASE,  PWM_LEN);
	    
    _start =_NOW;
	
    err = alloc_chrdev_region(&devnum, 0, 2, "bcm2835");
    if (err)
		printk(KERN_ERR "bcm2835_usec cant create chrdev\n");
    major = MAJOR(devnum);    
    my_class = class_create(THIS_MODULE, "bcm2835");
    for(i=0;i<2;i++){
		my_device = MKDEV(major, i);
		cdev_init(&cdev[i], &fops[i]);
		err = cdev_add(&cdev[i], my_device, 1);
		if (err) 
			pr_info("%s: Failed in adding cdev to subsystem retval:%d\n", __func__, err);
		else
			device_create(my_class, NULL, my_device, NULL, "bcm2835_%s",i==0?"usec":"pwm");
    }

    mod_object = kobject_create_and_add("mod",NULL);
    if(sysfs_create_file(mod_object,&mod_attribute.attr))
	    pr_debug("failed to create mod sysfs\n");	    		  
	pwm_stop();
	
	CLK_REG[PWMCLK_CNTL] = 0x5A000000;
	CLK_REG[PWMCLK_DIV] = 0x5A000000;
	CLK_REG[PWMCLK_CNTL] = 0x5A000001;
	CLK_REG[PWMCLK_DIV] = 0x5A000000 | (32<<12);    // set pwm div to 32 (19.2MHz/32 = 600KHz)
	udelay(500);
	CLK_REG[PWMCLK_CNTL] = 0x5A000011;

	udelay(500);

	PWM_REG[PWM_RNG1] = 6;				// 600KHz/6 = 10us per FIFO write
	udelay(10);
	ctl->pwmdata = 1;
	PWM_REG[PWM_DMAC] = PWMDMAC_ENAB | PWMDMAC_THRSHLD;
	udelay(10);

    //task = kthread_run(mod_thread,NULL,"mod");
    //set_pwm(1,50,50);
    //add_pwm(2,50);
    return 0;
}

static void __exit mod_exit(void){    
    int i,major;
    
    pwm_stop();
	dma_stop(0);
    if(my_class){
	major = MAJOR(devnum);
	for(i =0;i<2;i++){
	    int dev = MKDEV(major,i);
	    cdev_del(&cdev[i]);
	    device_destroy(my_class,dev);
	}
	class_destroy(my_class);
	unregister_chrdev_region(devnum, 2);	
    }
    if(mod_object)
		kobject_put(mod_object);
    if(task)
		kthread_stop(task);
	if(ctldatabase)
		free_pages(ctldatabase, 0);
    iounmap(TIMER_REG);	
	iounmap(GPIO_REG);	 
	iounmap(DMA_REG);	  
	iounmap(CLK_REG);	  
	iounmap(PWM_REG);	      
}

int dma_start(int ch){
    dma_stop(ch);
    DMA_REG[DMA_CS] = BCM2708_DMA_INT | BCM2708_DMA_END;
    DMA_REG[DMA_CONBLK_AD] = (uint32_t)(ctl->cb) & 0x7fffffff;
    DMA_REG[DMA_DEBUG] = 7;
    udelay(10);
    DMA_REG[DMA_CS] = 0x10880000|BCM2708_DMA_ACTIVE;
    udelay(10);
    return 0;
}

int dma_stop(int ch){    
    DMA_REG[DMA_CS] |= BCM2708_DMA_ABORT;
    udelay(100);
    DMA_REG[DMA_CS] &= ~BCM2708_DMA_ACTIVE;
    DMA_REG[DMA_CS] |= BCM2708_DMA_RESET;
    udelay(10);
    return 0;
}

module_init(mod_init);
module_exit(mod_exit);
