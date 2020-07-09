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
static struct proc_dir_entry *proc_usec_entry;
struct ctldata_s *ctl;
static struct cdev cdev[2];
static dev_t devnum;
static struct class *my_class;
static unsigned long long _start;
static unsigned long lino,_timer_ticks;
static struct private_data *private_data = NULL;
LPGPIO_PIN gpio_pin = NULL;

#define _NOW *((ulong64 *)&TIMER_REG[1])

irqreturn_t myhandler(int irq, void *dev_id){	
	LPGPIO_PIN p = (LPGPIO_PIN)dev_id;
	
	printk(KERN_INFO "IRQ %d\n",p->pin);
	if(private_data && private_data->fa)
		kill_fasync(&(private_data->fa), SIGIO, POLL_IN);
	return IRQ_HANDLED;
}

int mod_thread(void *data){
	struct sched_param param = { .sched_priority = 40 };
	unsigned long long _last,_now,_ss;

	sched_setscheduler(current, SCHED_FIFO, &param);	
	
	while(1){
	    usleep_range(100000,100000);

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
				int reg,sh;
				ulong mode;
				
				reg = (sel / 10) + GPFSEL0;
				sh = (sel % 10) * 3;								
				GPIO_REG[reg] &= ~(7 << sh);
				get_user(mode,p++);
				switch(mode){
					case 0:
						break;
					case 1:
						GPIO_REG[reg] |= 1 << sh;
						break;
					case 0x100:
						gpio_pin[sel].interrupt = gpio_to_irq(sel);
						gpio_pin[sel].used=1;
						gpio_pin[sel].irq = 1;
						gpio_pin[sel].pwm = 0;
						if(request_irq(gpio_pin[sel].interrupt, myhandler, 0, "GPIO_IRQ", &gpio_pin[sel])){
							printk(KERN_INFO "Failed Requesting Interrupt Line...");
							return -1;
						}				
						return 0;
					case 0x101:
						break;
				}
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
	int sz = sizeof(struct private_data) + (sizeof(char)*10*GPIO_PINS + sizeof(char)*10);
	
	if(private_data == NULL){	
		private_data = (struct private_data *)kmalloc(sz, GFP_KERNEL);
		if (0 == private_data){
			printk(KERN_WARNING "bcm2708: Failed to allocate user data\n");
			return -ENOMEM;
		}
		memset(private_data, 0, sz);
		private_data->rd_data = (char *)(private_data + 1);
		private_data->partial_cmd = &private_data->rd_data[GPIO_PINS*10];	
		
	}
	fil->private_data = private_data;
	return 0;
}

static int pwm_close(struct inode *inod,struct file *fil){
	int ret;
	struct private_data* const pdata = fil->private_data;
	
	ret =0;
	if(private_data){
		if(private_data->partial_len){
			printk(KERN_WARNING "bcm2708: partial command pending on close()\n");
			ret = -EIO;
		}
		kfree(private_data);
		private_data= NULL;
	}
	return ret;
}

static ssize_t pwm_write(struct file *filp,const char *user_buf,size_t count,loff_t *f_pos){
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
				p = strchr(p,',');			
				if(sscanf(p+1,"%d%c",&duty,&nl) != 2 && nl != '\n'){
					pdata->reject_writes = 1;
					return -EINVAL;				
				}
			}
			set_pwm(pwm,freq,duty);
			p = pp + 1;
		}
		else if (buf+len - p > 10)
			return -EINVAL;
		else
			break;
	}
	
	pdata->partial_len = buf+len - p;
	memcpy(pdata->partial_cmd, p, pdata->partial_len);

	return count;
}

static int pwm_fasync(int fd, struct file *filp, int mode){

	int ret;
	struct private_data* const pdata = filp->private_data;

	if((ret = fasync_helper(fd, filp, mode, &(pdata->fa))) < 0){
		printk(KERN_INFO "Fasync Failed: %d\n", ret);
		return ret;
	}
	return 0;
}

int usec_read_proc(struct file *filp,char *user_buf,size_t count,loff_t *f_pos){
	int len=0;
//	len = sprintf(buf,"\n %s\n ",proc_data);
	return len;
}

static struct kobj_attribute mod_attribute=__ATTR(usec,(S_IWUSR|S_IRUSR),show_mod,set_mod);

static struct file_operations fops[] = {
	{.owner = THIS_MODULE,.read = timer_read,.write = NULL,},
    {.owner = THIS_MODULE,.read = NULL,.write = pwm_write,.unlocked_ioctl = pwm_ioctl,.compat_ioctl = pwm_ioctl,.open=pwm_open,.release=pwm_close,.fasync=pwm_fasync},
    {.owner = THIS_MODULE,.read = usec_read_proc}
};

static int __init mod_init(void){	
    int err,i,major;
	dev_t my_device;

    lino = _timer_ticks = 0;

	gpio_pin = (LPGPIO_PIN)__get_free_pages(GFP_KERNEL, 1);
	if (gpio_pin == 0) {
		printk(KERN_WARNING "bcm2835: alloc_pages failed\n");
		return -EFAULT;
	}	
	ctl = (struct ctldata_s *)(gpio_pin + GPIO_PINS);
	ctl->gpiodata = (ulong *)(ctl+1);
	{
	    ulong64 adr = (ulong64)(ctl->gpiodata + GPIO_PINS);
	    adr = (adr + 32) & ~0x1f;
	    ctl->cb = (struct bcm2708_dma_cb *)adr;
	}
	
	TIMER_REG = (ulong *)ioremap(TIMER_BASE, TIMER_LEN);    
	GPIO_REG = (ulong *)ioremap(GPIO_BASE, GPIO_LEN);
	DMA_REG  = (ulong *)ioremap(DMA_BASE,  DMA_LEN);
	CLK_REG  = (ulong *)ioremap(CLK_BASE,  CLK_LEN);
	PWM_REG  = (ulong *)ioremap(PWM_BASE,  PWM_LEN);
	
	pwm_stop();
	dma_stop(DMA_CHANNEL);
	    
    err = alloc_chrdev_region(&devnum, 0, 2, "bcm2835");
    if (err)
		printk(KERN_ERR "bcm2835_usec cant create chrdev\n");
    major = MAJOR(devnum);    
    my_class = class_create(THIS_MODULE, "bcm2835");
    for(i=0;i<2;i++){
		my_device = MKDEV(major, i);
		cdev_init(&cdev[i], &fops[i]);
		if(cdev_add(&cdev[i], my_device, 1)){
			pr_info("%s: Failed in adding cdev to subsystem retval:%d\n", __func__, err);
			continue;
		}
	    device_create(my_class, NULL, my_device, NULL, "bcm2835_%s",i==0?"usec":"pwm");
    }

    mod_object = kobject_create_and_add("mod",NULL);
    if(sysfs_create_file(mod_object,&mod_attribute.attr))
	    pr_debug("failed to create mod sysfs\n");	
	if(!(proc_usec_entry = proc_create("usec",0666,0,&fops[2])))
		pr_debug("failed to create usec procfs\n");	
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

	_start =_NOW;	
	
    return 0;
}

static void __exit mod_exit(void){    
    int i,major;
    
    pwm_stop();
	dma_stop(DMA_CHANNEL);
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
    if(proc_usec_entry)
		remove_proc_entry("usec",NULL);
    if(mod_object)
		kobject_put(mod_object);
    if(task)
		kthread_stop(task);
	if(gpio_pin){
		free_pages(gpio_pin, 0);
		gpio_pin = NULL;
	}
    iounmap(TIMER_REG);	
	iounmap(GPIO_REG);	 
	iounmap(DMA_REG);	  
	iounmap(CLK_REG);	  
	iounmap(PWM_REG);	      
}

int dma_start(int ch){
    dma_stop(ch);
    DMA_REG[DMA_CS(ch)] = BCM2708_DMA_INT | BCM2708_DMA_END;
    DMA_REG[DMA_CONBLK_AD(ch)] = (uint32_t)(ctl->cb) & 0x7fffffff;
    DMA_REG[DMA_DEBUG(ch)] = 7;
    udelay(10);
    DMA_REG[DMA_CS(ch)] = 0x10880000|BCM2708_DMA_ACTIVE;
    udelay(10);
    return 0;
}

int dma_stop(int ch){    
    DMA_REG[DMA_CS(ch)] |= BCM2708_DMA_ABORT;
    udelay(100);
    DMA_REG[DMA_CS(ch)] &= ~BCM2708_DMA_ACTIVE;
    DMA_REG[DMA_CS(ch)] |= BCM2708_DMA_RESET;
    udelay(10);
    return 0;
}

module_init(mod_init);
module_exit(mod_exit);
