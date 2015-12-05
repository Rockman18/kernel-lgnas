/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111−1307 USA
 *
 * Authors:
 * Tuan Phan <tphan@amcc.com>
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/kcpmd.h>
#include <asm/prom.h>
#include <net/sock.h>


#define KCPM_SOCK_GROUP 1

#define TIMER_DELAY			60 * HZ

#define MAX_UIC			4

#define EVENT_APP		0
#define EVENT_SYS		1

/* There is one global netlink socket */
static struct sock *kmessage_sock = NULL;

static unsigned int monitor_irqs[2][MAX_UIC];

struct kcpm {
	unsigned int app_kick;
	unsigned int sys_kick;
	struct timer_list timer;
};

static struct kcpm _kcpm;

static int netlink_send(__u32 groups, const char *buffer, int len)
{
	struct sk_buff *skb;
	char *data_start;

	if (!kmessage_sock)
		return -EIO;

	if (!buffer)
		return -EINVAL;

	if (len > PAGE_SIZE)
		return -EINVAL;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	data_start = skb_put(skb, len);
	memcpy(data_start, buffer, len);
	NETLINK_CB(skb).dst_group = groups;
	return netlink_broadcast(kmessage_sock, skb, 0, groups, GFP_KERNEL);
}

static void kcpm_timer(unsigned long data)
{
	char *buffer;
	int len;
	struct kcpm *pkcpm = (struct kcpm *)data;
	if (pkcpm->app_kick == 0 && pkcpm->sys_kick == 0)
		goto RE_ADD_TIMER;

	buffer = (char *)kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buffer)
		goto RE_ADD_TIMER;

	/* only need to send app_kick or sys_kick */
	if (pkcpm->app_kick != 0) {
		len = sprintf(buffer, "/apm/nas/pwrmgr@");
		len += sprintf(&buffer[len], "apm.nas.pwrmgr@");
		len += sprintf(&buffer[len], "app_kick@");
		pkcpm->app_kick = 0;
	} else if (pkcpm->sys_kick != 0) {
		len = sprintf(buffer, "/apm/nas/pwrmgr@");
		len += sprintf(&buffer[len], "apm.nas.pwrmgr@");
		len += sprintf(&buffer[len], "sys_kick@");
		pkcpm->sys_kick = 0;
	}
	buffer[len++] = '@';
	buffer[len++] = '\0';
	netlink_send(KCPM_SOCK_GROUP, buffer, strlen(buffer));
	kfree(buffer);
RE_ADD_TIMER:
	pkcpm->timer.expires += TIMER_DELAY;
 	add_timer(&pkcpm->timer);
	return;
}

void send_event(unsigned int uic_index, unsigned int irq)
{
	unsigned int src;
	unsigned int mask;
	if (uic_index > MAX_UIC)
		return;
	mask = monitor_irqs[EVENT_APP][uic_index];
	src = 1 << (31 - irq);
	if (mask & src) {
		_kcpm.app_kick = 1;
	}
	mask = monitor_irqs[EVENT_SYS][uic_index];
	if (mask & src)
		_kcpm.sys_kick = 1;
}

EXPORT_SYMBOL_GPL(send_event);

static int __init kcpm_read_device_tree(void)
{
	const unsigned int *app_monitor;
	const unsigned int *sys_monitor;
	int len;
	int i;
	struct device_node *np;
	for_each_compatible_node(np, NULL, "ibm,cpm") {
		app_monitor = of_get_property(np, "pm-monitor-app", &len);
		if (!app_monitor || len > MAX_UIC * sizeof(unsigned int)) {
			printk(KERN_ERR "kcpm: can't read pm-monitor-app\n");
			return -ENODEV;
		}
		memset(&monitor_irqs[EVENT_APP][0], 0, 
					MAX_UIC * sizeof(unsigned int));
		//printk("kcpm:len of pm-monitor-app %d", len);
		for (i = 0; i < len / sizeof(unsigned int); i++) {
			//printk("kcpm:add uic %d with value 0x%x", i, app_monitor[i]);
			monitor_irqs[EVENT_APP][i] = app_monitor[i];
		}
		sys_monitor = of_get_property(np, "pm-monitor-sys", &len);
		if (!sys_monitor || len > MAX_UIC * sizeof(unsigned int)) {
			//printk(KERN_ERR "kcpm: can't read pm-monitor-sys\n");
			return -ENODEV;
		}
		memset(&monitor_irqs[EVENT_SYS][0], 0, 
					MAX_UIC * sizeof(unsigned int));
		//printk("kcpm:len of pm-monitor-sys %d", len);
		for (i = 0; i < len / sizeof(unsigned int); i++) {
			//printk("kcpm:add uic %d with value 0x%x", i, sys_monitor[i]);
			monitor_irqs[EVENT_SYS][i] = sys_monitor[i];
		}
		break;
	}
	return 0;
}

static int __init kcpm_init(void)
{
	kmessage_sock = netlink_kernel_create(&init_net, NETLINK_KCPM, 
	1, NULL, NULL, THIS_MODULE);

	if (!kmessage_sock) {
		printk(KERN_ERR "kcpm: "
		"unable to create netlink socket; aborting\n");
		return -ENODEV;
	}

	if (kcpm_read_device_tree()) {
		printk(KERN_ERR "kcpm: "
		"unable to parse device tree; aborting\n");
		return -ENODEV;
	}

	memset(&_kcpm, 0 , sizeof(struct kcpm));
	
	init_timer(&_kcpm.timer);
	
	_kcpm.timer.function = kcpm_timer;
	_kcpm.timer.data = (unsigned long)&_kcpm;
	_kcpm.timer.expires = jiffies;
	add_timer(&_kcpm.timer);
	return 0;
}

static void __exit kcpm_exit(void)
{
	if (kmessage_sock)
		sock_release(kmessage_sock->sk_socket);
	del_timer(&_kcpm.timer);
}

MODULE_DESCRIPTION("Kernel power management process");
MODULE_AUTHOR("Tuan Phan <tphan@amcc.com>");
MODULE_LICENSE("GPL");

module_init(kcpm_init);
module_exit(kcpm_exit);

