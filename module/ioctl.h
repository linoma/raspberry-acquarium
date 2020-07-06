
#ifndef __IOCTLH
#define __IOCTLH__

#define RPIMOD_MAGIC	('x')

#define RPIMOD_IOC_SETFREQ		_IOW(RPIMOD_MAGIC, 1, ulong *)
#define RPIMOD_IOC_SETDUTY		_IOW(RPIMOD_MAGIC, 2, ulong *)
#define RPIMOD_IOC_ENABLE		_IOW(RPIMOD_MAGIC, 3, ulong *)
#define RPIMOD_IOC_ADD			_IOW(RPIMOD_MAGIC, 4, ulong *)
#define RPIMOD_IOC_GPIO_HIGH	_IOW(RPIMOD_MAGIC, 20, ulong *)
#define RPIMOD_IOC_GPIO_LOW		_IOW(RPIMOD_MAGIC, 21, ulong *)

#endif
