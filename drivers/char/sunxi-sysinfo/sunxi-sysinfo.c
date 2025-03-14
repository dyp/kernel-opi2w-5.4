/*
 * Based on drivers/char/sunxi-sysinfo/sunxi-sysinfo.c
 *
 * Copyright (C) 2015 Allwinnertech Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/sunxi-sid.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/compat.h>

#include "sunxi-sysinfo-user.h"

static s8 key_name[SUNXI_KEY_NAME_LEN];

static int soc_info_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int soc_info_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long soc_info_ioctl(struct file *file, unsigned int ioctl_num,
		unsigned long ioctl_param)
{
	int ret = 0;
	char id[17] = "";

	memset(id, 0, sizeof(id));

	pr_debug("IOCTRL cmd: %#x, param: %#lx\n", ioctl_num, ioctl_param);
	switch (ioctl_num) {
	case CHECK_SOC_SECURE_ATTR:
		ret = sunxi_soc_is_secure();
		if (ret)
			pr_debug("soc is secure. return value: %d\n", ret);
		else
			pr_debug("soc is normal. return value: %d\n", ret);
		break;
	case CHECK_SOC_VERSION:
		ret = sunxi_get_soc_ver();
		pr_debug("soc version:%x\n", ret);
		break;
	case CHECK_SOC_BONDING:
		sunxi_get_soc_chipid_str(id);
		ret = copy_to_user((void __user *)ioctl_param, id, 8);
		pr_debug("soc id:%s\n", id);
		break;
	case CHECK_SOC_CHIPID:
		sunxi_get_soc_chipid_str(id);
		ret = copy_to_user((void __user *)ioctl_param, id, 16);
		pr_debug("soc chipid:%s\n", id);
		break;
	case CHECK_SOC_FT_ZONE:
		sunxi_get_soc_ft_zone_str(id);
		ret = copy_to_user((void __user *)ioctl_param, id, 8);
		pr_debug("ft zone:%s\n", id);
		break;
	case CHECK_SOC_ROTPK_STATUS:
		sunxi_get_soc_rotpk_status_str(id);
		ret = copy_to_user((void __user *)ioctl_param, id, 8);
		pr_debug("rotpk status:%s\n", id);
		break;
	default:
		pr_err("Unsupported cmd:%d\n", ioctl_num);
		ret = -EINVAL;
		break;
	}
	return ret;
}

#ifdef CONFIG_COMPAT
static long soc_info_compat_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	unsigned long translated_arg = (unsigned long)compat_ptr(arg);

	return soc_info_ioctl(filp, cmd, translated_arg);
}
#endif

static const struct file_operations soc_info_ops = {
	.owner   = THIS_MODULE,
	.open    = soc_info_open,
	.release = soc_info_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = soc_info_compat_ioctl,
#endif
	.unlocked_ioctl = soc_info_ioctl,
};

struct miscdevice soc_info_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "sunxi_soc_info",
	.fops  = &soc_info_ops,
};

static ssize_t sys_info_show(struct class *class,
			     struct class_attribute *attr, char *buf)
{
	int i;
	int databuf[4] = {0};
	char tmpbuf[129] = {0};
	size_t size = 0;

	/* platform */
	sunxi_get_platform(tmpbuf, 129);
	size += sprintf(buf + size, "sunxi_platform    : %s\n", tmpbuf);

	/* secure */
	size += sprintf(buf + size, "sunxi_secure      : ");
	if (sunxi_soc_is_secure()) {
		size += sprintf(buf + size, "%s\n", "secure");
		/* rotpk status */
		memset(tmpbuf, 0x0, sizeof(tmpbuf));
		sunxi_get_soc_rotpk_status_str(tmpbuf);
		size += sprintf(buf + size, "sunxi_rotpk       : %s\n", tmpbuf);
	} else
		size += sprintf(buf + size, "%s\n", "normal");

#ifdef CONFIG_SUNXI_QA_TEST
	/* chipid */
	sunxi_get_soc_chipid((u8 *)databuf);

	for (i = 0; i < 4; i++)
		sprintf(tmpbuf + i*8, "%08x", databuf[i]);
	tmpbuf[128] = 0;
	size += sprintf(buf + size, "sunxi_chipid      : %s\n", tmpbuf);
#endif
	/* serial */
	sunxi_get_serial((u8 *)databuf);
	for (i = 0; i < 4; i++)
		sprintf(tmpbuf + i*8, "%08x", databuf[i]);
	tmpbuf[128] = 0;
	size += sprintf(buf + size, "sunxi_serial      : %s\n", tmpbuf);

	/* chiptype */
	sunxi_get_soc_chipid_str(tmpbuf);
	size += sprintf(buf + size, "sunxi_chiptype    : %s\n", tmpbuf);

	/* socbatch number */
	size += sprintf(buf + size, "sunxi_batchno     : %#x\n",
			sunxi_get_soc_ver());

	return size;
}

static ssize_t key_info_show(struct class *class,
			     struct class_attribute *attr, char *buf)
{
	s32 i;
	u32 *key_data = NULL;
	size_t size = 0;

	key_data = vmalloc(256);
	if (key_data == NULL)
		return -ENOMEM;

	memset(key_data, 0, 256*4);
	sunxi_efuse_readn(key_name, key_data, 256);
	for (i = 0; i < 256; i++) {
		if ((i > 0) && (key_data[i] == 0))
			break;
		if ((i > 0) && (i % 8 == 0))
			size += sprintf(buf + size, "\n");

		size += sprintf(buf + size, "%08x ", key_data[i]);
	}
	size += sprintf(buf + size, "\n");

	vfree(key_data);
	return size;
}

static ssize_t key_info_store(struct class *class, struct class_attribute *attr,
		const char *buf, size_t count)
{
	if (count >= SUNXI_KEY_NAME_LEN)
		return -EINVAL;

	memset(key_name, 0, SUNXI_KEY_NAME_LEN);
	strncpy(key_name, buf, count);
	return count;
}

static struct class_attribute info_class_attrs[] = {
	__ATTR(sys_info, 0644, sys_info_show, NULL),
	__ATTR(key_info, 0644, key_info_show, key_info_store),
};

static struct class info_class = {
	.name           = "sunxi_info",
	.owner          = THIS_MODULE,
};

static int __init sunxi_sys_info_init(void)
{
	s32 ret = 0, i;

	ret = class_register(&info_class);
	if (ret != 0)
		return ret;
	/* need some class specific sysfs attributes */
	for (i = 0; i < ARRAY_SIZE(info_class_attrs); i++) {
		ret = class_create_file(&info_class, &info_class_attrs[i]);
		if (ret)
			goto out_class_create_file_failed;
	}

	ret = misc_register(&soc_info_device);
	if (ret != 0) {
		pr_err("%s: misc_register() failed!(%d)\n", __func__, ret);
		class_unregister(&info_class);
		return ret;
	}
	return ret;
out_class_create_file_failed:
	class_unregister(&info_class);
		return ret;
}

static void __exit sunxi_sys_info_exit(void)
{
	misc_deregister(&soc_info_device);
	class_unregister(&info_class);
}

module_init(sunxi_sys_info_init);
module_exit(sunxi_sys_info_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("xiafeng<xiafeng@allwinnertech.com>");
MODULE_DESCRIPTION("sunxi sys info.");
