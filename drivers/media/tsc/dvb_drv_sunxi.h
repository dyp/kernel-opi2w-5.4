/*
 * (C) Copyright 2010-2016
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef DRIVER_INTERFACE_H
#define DRIVER_INTERFACE_H

#include<linux/ioctl.h>
/*****************************************************************************/
/*define iocltl command*/
struct tsc_ionbuf_param {
    int             fd;
    unsigned int    ion_phyaddr;
};

#define TSCDEV_IOC_MAGIC            't'

#define TSCDEV_ENABLE_INT           _IO(TSCDEV_IOC_MAGIC,   0)
#define TSCDEV_DISABLE_INT          _IO(TSCDEV_IOC_MAGIC,   1)
#define TSCDEV_GET_PHYSICS          _IO(TSCDEV_IOC_MAGIC,   2)
#define TSCDEV_RELEASE_SEM          _IO(TSCDEV_IOC_MAGIC,   3)
#define TSCDEV_WAIT_INT             _IOR(TSCDEV_IOC_MAGIC,  4,  unsigned long)
#define TSCDEV_ENABLE_CLK           _IOW(TSCDEV_IOC_MAGIC,  5,  unsigned long)
#define TSCDEV_DISABLE_CLK          _IOW(TSCDEV_IOC_MAGIC,  6,  unsigned long)
#define TSCDEV_PUT_CLK              _IOW(TSCDEV_IOC_MAGIC,  7,  unsigned long)
#define TSCDEV_SET_CLK_FREQ         _IOW(TSCDEV_IOC_MAGIC,  8,  unsigned long)
#define TSCDEV_GET_CLK              _IOWR(TSCDEV_IOC_MAGIC, 9,  unsigned long)
#define TSCDEV_GET_CLK_FREQ         _IOWR(TSCDEV_IOC_MAGIC, 10, unsigned long)
#define TSCDEV_SET_SRC_CLK_FREQ     _IOWR(TSCDEV_IOC_MAGIC, 11, unsigned long)
#define TSCDEV_FOR_TEST             _IO(TSCDEV_IOC_MAGIC,   12)
#define TSCDEV_GET_IONPHYADDR       _IOR(TSCDEV_IOC_MAGIC,  13, unsigned long)
#define TSCDEV_RELEASE_IONPHYADDR   _IOW(TSCDEV_IOC_MAGIC,  14, unsigned long)

#define TSCDEV_IOC_MAXNR            (15)

/*****************************************************************************/
struct intrstatus {
	unsigned int port0chan;
	unsigned int port0pcr;
};

#endif
