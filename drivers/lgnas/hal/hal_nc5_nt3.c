/*
 *  hal.c
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <asm/uaccess.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>


#define CONFIG_MACH_NC5
#define CONFIG_LGNAS_MODEL "nc5"
//#define CONFIG_MACH_NC5

#define CONFIG_MACH_APM_BASE (CONFIG_MACH_NT3 | CONFIG_MACH_NC5 )

#ifdef CONFIG_MACH_APM_BASE
#include <linux/delay.h>
#include <linux/of_i2c.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/reboot.h>
#include <asm/machdep.h>
#include <asm/ppc4xx_cpm.h>
#endif

#define ARCH_NR_GPIOS 32
#define SA_INTERRUPT IRQF_DISABLED
#define CONFIG_LGNAS_HAS_CDROM
#define GPIO_MAX        32
#define GPIO_OR         0x00
#define GPIO_IR         0x1C
#define GPIO_ISR3L      0x40
#define GPIO_VAL(gpio)  (0x80000000 >> (gpio))
//static struct resource res_gpio0;
//void __iomem *gpio0 = NULL;

#ifdef CONFIG_MACH_NT3 //for usb otg 
#define USB_OTG_HOST	1
#define USB_OTG_DEVICE	0
#define USB0_GOTGCTL	0
#define USB0_GINTSTS	0x14
#define DEV_INIT_TRUE  1
#define DEV_INIT_FALSE 0 
volatile static int usb_otg_host_connected = 0;
volatile static int usb_otg_device_connected = 0;
volatile static int curr_usb_port_state;
static int device_init = DEV_INIT_TRUE;
struct resource usb_res;
void __iomem *usb_otg_reg = NULL; // for usb otg
static int usb_otg_intr = NO_IRQ;
static int port_select_gpio;
static int vbus_detect_gpio;
struct micom_work {
	struct work_struct work;
	char   buf[8];
};
static DEFINE_SPINLOCK(usb_switch_lock);
static void set_usb_mode(struct work_struct *work);
static struct micom_work micom_usb_device = {.work = __WORK_INITIALIZER(micom_usb_device.work, 
					     				set_usb_mode),
					     .buf = "\x02\x9b\x00\x00\x00\x00\x00\x00"};
static struct micom_work micom_usb_host = {.work = __WORK_INITIALIZER(micom_usb_host.work, 
					     				set_usb_mode),
					   .buf = "\x02\x9b\x01\x00\x00\x00\x00\x00"};
static void set_usb_mode(struct work_struct *work);
#endif

void gpio_write_bit(int pin, int val);
int gpio_read_in_bit(int pin);
void (*save_power_off)(void);
static int suspend_mode = CPM_PM_DOZE; // for power saving
//

#include "model_info.h"
#include "hal.h"
#define dprintk(fmt, ...) \
	({ if(debug) printk(pr_fmt(fmt), ##__VA_ARGS__); })
static int debug = 1;
module_param(debug, int, S_IRUGO);
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");

/*
 * The MICOM 
 */

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x58, I2C_CLIENT_END };

#define MICOM_ID_GET					0x01
#define MICOM_ID_SET1					0x02
#define 	MICOM_PWM1							0x01 /* pwm(0-255) */
#define 	MICOM_PWM2							0x91 /* pwm(0-255) */
#define 	MICOM_VER								0x0f  
#define 	MICOM_EMRGNC_SHUTDOWN		0x92 /* enabale/disable(1/0) */
#define 	MICOM_POR								0x93 /* enabale/disable(1/0) */
#define 	MICOM_WTDG_VAL					0x94 /* value(0,1,...) */
#define 	MICOM_WTDG_TIMER				0x95 /* enabale/disable(1/0) */
#define		MICOM_LED_BRIGHTNESS		0x81 /* led_id led_brightness */
#define		MICOM_LED_DELAY					0x82 /* led_id led_delay */
#define MICOM_ID_SET2					0x03
#define 	MICOM_ICON							0x30 /* position on/off(0xff/0) blink(1/0) */
#define 	MICOM_BUZZER						0x31 /* fre1 fre2 fre3 fre4 time_play time_wait */
#define MICOM_ID_SET3					0x04
/*
 * The FAN
 */
static const u8 NASHAL_FAN_PWM[2]			= { MICOM_PWM1, MICOM_PWM2 };

/*
 * The BUZZER
 */

/*
 * The LED
 */
#if defined(CONFIG_MACH_NS2)
 #define GPIO_HDD1	  9
 #define GPIO_HDD2		26
 #define GPIO_HDD3	  25	
 #define GPIO_HDD4		15
 #define GPIO_ODD1		57
static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_HDD1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_HDD2,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_HDD3,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_HDD4,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_ODD1,
		.active_low = 0,
	},
};
#endif
#if defined(CONFIG_MACH_NC2) || defined(CONFIG_MACH_NC21)
 #define GPIO_HDD1	  35
 #define GPIO_HDD2		34
 #define GPIO_LAN1		30
 #define GPIO_POWER1	(ARCH_NR_GPIOS + 1)
static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_HDD1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_HDD2,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_LAN1,
		.active_low = 0,
	},
	{
		.state = 1,
		.gpio = GPIO_POWER1,
		.active_low = 0,
	},
};
#endif
#if defined(CONFIG_MACH_NT1)
 #define GPIO_HDD1	  35
 #define GPIO_ODD1		34
 #define GPIO_USB1		30
 #define GPIO_LAN1		ARCH_NR_GPIOS
 #define GPIO_POWER1	(ARCH_NR_GPIOS + 1)
static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_HDD1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_ODD1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_USB1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_LAN1,
		.active_low = 0,
	},
	{
		.state = 1,
		.gpio = GPIO_POWER1,
		.active_low = 0,
	},
};
#endif
#if defined(CONFIG_MACH_NT11)
 #define GPIO_HDD1	  35
 #define GPIO_USB1		34
 #define GPIO_LAN1		ARCH_NR_GPIOS
 #define GPIO_POWER1	(ARCH_NR_GPIOS + 1)
static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_HDD1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_USB1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_LAN1,
		.active_low = 0,
	},
	{
		.state = 1,
		.gpio = GPIO_POWER1,
		.active_low = 0,
	},
};
#endif
#ifdef CONFIG_MACH_MM1
 #define GPIO_ODD1	  4
 #define GPIO_HDD1	  5
 #define GPIO_POWER1	6
 #define GPIO_POWER2	7
 #define GPIO_POWER3	8
static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_HDD1,
		.active_low = 1,
	},
	{
		.state = 0,
		.gpio = GPIO_ODD1,
		.active_low = 1,
	},
	{
		.state = 1,
		.gpio = GPIO_POWER1,
		.active_low = 0,
	},
	{
		.state = 1,
		.gpio = GPIO_POWER2,
		.active_low = 0,
	},
	{
		.state = 1,
		.gpio = GPIO_POWER3,
		.active_low = 0,
	},
};
#endif
#ifdef CONFIG_MACH_NT3
 #define GPIO_HDD1	  224+10
 #define GPIO_ODD1	  224+11	
 #define GPIO_USB1		224+9
 #define GPIO_LAN1		ARCH_NR_GPIOS
 #define GPIO_POWER1	(ARCH_NR_GPIOS + 1)
static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_HDD1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_ODD1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_USB1,
		.active_low = 0,
	},
	/*
	{
		.state = 0,
		.gpio = GPIO_LAN1,
		.active_low = 0,
	},
	{
		.state = 1,
		.gpio = GPIO_POWER1,
		.active_low = 0,
	},
	*/
};

#endif
#ifdef CONFIG_MACH_NC5

#define GPIO_HDD1	  (224+9)
#define GPIO_HDD2	  (224+10)
#define GPIO_ODD1	  (224+11)
#define GPIO_HDD1_ERR (224+17)
#define GPIO_HDD2_ERR (224+18)

static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_HDD1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_HDD2,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_ODD1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_HDD1_ERR,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_HDD2_ERR,
		.active_low = 0,
	},
	/*
	{
		.state = 0,
		.gpio = GPIO_USB1,
		.active_low = 0,
	},
	
	{
		.state = 0,
		.gpio = GPIO_LAN1,
		.active_low = 0,
	},
	{
		.state = 1,
		.gpio = GPIO_POWER1,
		.active_low = 0,
	},
	*/
};
#endif

/*
 * The LCD 
 */

/*
 * The GPIO
 */

/*
 * The BUTTON
 */
#ifdef CONFIG_MACH_NS2
static struct gpio_keys_button buttons[] = {
	{
		.code = KEY_EJECTCD,
		.gpio = 14,
		.desc = "eject",
		.debounce_interval = 0,
	},
	{
		.code = KEY_MODE,
		.gpio = 12,
		.desc = "mode",
		.debounce_interval = 0,
	},
	{
		.code = KEY_SELECT,
		.gpio = 13,
		.desc = "select",
		.debounce_interval = 0,
	},
};
#endif
#if defined(CONFIG_MACH_NC2) || defined(CONFIG_MACH_NC21)
static struct gpio_keys_button buttons[] = {
	{
		.code = KEY_POWER,
		.gpio = 29,
		.desc = "power",
		.debounce_interval = 0,
	},
	{
		.code = KEY_COPY,
		.gpio = 31,
		.desc = "backup",
		.debounce_interval = 0,
	},
	{
		.code = KEY_EJECTCD,
		.gpio = 47,
		.desc = "eject",
		.debounce_interval = 0,
	},
};
#endif
#if defined(CONFIG_MACH_NT1)
static struct gpio_keys_button buttons[] = {
	{
		.code = KEY_POWER,
		.gpio = 29,
		.desc = "power",
		.debounce_interval = 0,
	},
	{
		.code = KEY_COPY,
		.gpio = 31,
		.desc = "backup",
		.debounce_interval = 0,
	},
	{
		.code = KEY_ARCHIVE,
		.gpio = 11,
		.desc = "exthdd",
		.debounce_interval = 1000,
	},
	{
		.code = KEY_CD,
		.gpio = 16,
		.desc = "extodd",
		.debounce_interval = 1000,
	},
};
#endif
#if defined(CONFIG_MACH_NT11)
static struct gpio_keys_button buttons[] = {
	{
		.code = KEY_POWER,
		.gpio = 29,
		.desc = "power",
		.debounce_interval = 0,
	},
	{
		.code = KEY_COPY,
		.gpio = 31,
		.desc = "backup",
		.debounce_interval = 0,
	},
	{
		.code = KEY_ARCHIVE,
		.gpio = 11,
		.desc = "exthdd",
		.debounce_interval = 1000,
	},
	{
		.code = KEY_CD,
		.gpio = 16,
		.desc = "nas",
		.debounce_interval = 1000,
	},
};
#endif
#ifdef CONFIG_MACH_MM1
static struct gpio_keys_button buttons[] = {
	{
		.code = KEY_POWER,
		.gpio = 12,
		.desc = "power1",
		.debounce_interval = 3000,
	},
};
#endif
#ifdef CONFIG_MACH_NT3
static struct gpio_keys_button buttons[] = {
	{
		.code = KEY_POWER,
		.gpio = 21,
		.desc = "power",
		.debounce_interval = 0,
	},
	{
		.code = KEY_COPY,
		.gpio = 23,
		.desc = "backup",
		.debounce_interval = 1000,
	},
	
	{
		.code = KEY_CONNECT,
		.gpio = 8,
		.desc = "vbus_detect",
		.debounce_interval = 0,
	},
	{
		.code = KEY_ARCHIVE,
		.gpio = 0,
		.desc = "usb_otg",
		.debounce_interval = 0,
	},
	/*
	{
		.code = KEY_CD,
		.gpio = 16,
		.desc = "extodd",
		.debounce_interval = 1000,
	},
	{
		.code = KEY_EJECTCD,
		.gpio = 47,
		.desc = "eject",
		.debounce_interval = 0,
	},*/
};

#endif
#ifdef CONFIG_MACH_NC5
#define POWER		(224+19)
#define SELECT	(224+21)
#define CANCEL	(224+23)
#define MODE		(224+20)
static struct gpio_keys_button buttons[] = {
	{
		.code = KEY_POWER,
		.gpio = POWER,
		.desc = "power",
		.debounce_interval = 0,
	},
	{
		.code = KEY_RIGHT,
		.gpio = CANCEL,
		.desc = "right",
		.debounce_interval = 1000,
	},
	
	{
		.code = KEY_LEFT,
		.gpio = MODE,
		.desc = "left",
		.debounce_interval = 1000,
	},
	{
		.code = KEY_UP,
		.gpio = SELECT,
		.desc = "up",
		.debounce_interval = 1000,
	},/*
	{
		.code = KEY_EJECTCD,
		.gpio = 47,
		.desc = "eject",
		.debounce_interval = 0,
	},*/
};
#endif

#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
#define	MAIN_WAIT_MODE	0x02
#define	IP_MODE		0x03
#define IP_SELECT	0x04
#define IP_DHCP		0x05
#define IP_STATIC	0x06
#define IP_IPKEY	0x7
#define IP_MASKSET	0x08
#define IP_MASKKEY	0x09
#define IP_GWSET	0x0a
#define	IP_GWKEY	0x0b
#define	INFO_FAN	0x0c
#define INFO_IP		0x0d
#define INFO_USAGE	0x0e
#define INFO_FWVER	0x0f
#define INFO_MICOM	0x10
#define INFO_TIME	0x11
#define INFO_TEMP	0x12
#define USB_BACKUP	0x13
#define USB_FULL	0x14
#define	USB_INC		0x15
#define	ODD_BACKUP	0x16
#define	ODD_DATA	0x17
#define	ODD_IMAGE	0x18
#define FULL_DONE	0x19
#define	INC_DONE	0x1a
#define	DATA_DONE	0x1b
#define	IMAGE_DONE	0x1c
#define	INFO_FSTAB	0x1d
#define IP_DONE		0x1e
#define USB_ONETOUCH	0x1f
#define ONETOUCH_DONE	0x20
#define MESSAGE_MODE	0x21
#define SYNC_MODE	0x22
#define USB_SYNC	0x23
#define USB_CANCEL	0x24
#define ODD_CANCEL	0x25
#define HIB_MODE	0x26
#define HIB_DONE	0x27
#define CANCEL_ODD	0x28
#define CANCEL_IP	0x29
#define IP_STATIC_MODE	0x2a
#define IP_DHCP_MODE	0x2b
#define INFORM_MODE	0x2c
#define POWER_OFF	0x2d
#define INFO_IPONLY	0x2e
#define HIB_EXIT	0x2f
#define USB_CANCEL_MODE	0x40
#define USB_NOCANCEL	0x41
#define ODD_CANCEL_MODE	0x42
#define ODD_NOCANCEL	0x43

#define	MODES_NUM	42

static struct mode_data modes[MODES_NUM] = {
	{MAIN_WAIT_MODE, 0, 0, {HIB_MODE, INFORM_MODE, INFO_IPONLY}},
	{INFO_IPONLY, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}},
	{INFO_IP, 0, 1, {INFO_FAN, INFO_FWVER, INFORM_MODE}},
	{INFO_FWVER, 0, 1, {INFO_IP, INFO_MICOM, INFORM_MODE}},
	{INFO_MICOM, 0, 1, {INFO_FWVER, INFO_USAGE, INFORM_MODE}},
	{INFO_USAGE, 0, 1, {INFO_MICOM, INFO_TIME, INFORM_MODE}},
	{INFO_TIME, 0, 1, {INFO_USAGE, INFO_FAN, INFORM_MODE}},
	{INFO_FAN, 0, 1, {INFO_TIME, INFO_IP, INFORM_MODE}},
	{IP_MODE, 0, 0, {INFORM_MODE, USB_BACKUP, IP_STATIC_MODE}},	
	{IP_STATIC_MODE, 0, 0, {CANCEL_IP, IP_DHCP_MODE, IP_STATIC}},
	{IP_DHCP_MODE, 0, 0, {IP_STATIC_MODE, CANCEL_IP, IP_DHCP}},
	{CANCEL_IP, 0, 0, {IP_DHCP_MODE, IP_STATIC_MODE, MAIN_WAIT_MODE}},
	{IP_DHCP, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}},
	{IP_STATIC, 0, 1, {IP_MODE, IP_IPKEY, IP_IPKEY}},
	{IP_IPKEY, 1, 0, {MAIN_WAIT_MODE, IP_MASKSET, 0}},
	{IP_MASKSET, 0, 0, {IP_STATIC, IP_MASKKEY, IP_MASKKEY}},		
	{IP_MASKKEY, 1, 0, {IP_MASKSET, IP_GWSET, 0}},
	{IP_GWSET, 0, 1, {IP_MASKSET, IP_GWKEY, IP_GWKEY}},
	{IP_GWKEY, 1, 0, {IP_GWSET, IP_DONE, 0}},
	{IP_DONE, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}},
	{USB_BACKUP, 0, 0, {IP_MODE, ODD_BACKUP, INC_DONE}},
	{INC_DONE, 0, 1, {USB_CANCEL, MAIN_WAIT_MODE, MAIN_WAIT_MODE}},
	{ODD_BACKUP, 0, 0, {USB_BACKUP, HIB_MODE, ODD_DATA}},		
	{ODD_DATA, 0, 0, {CANCEL_ODD, ODD_IMAGE, DATA_DONE}},
	{ODD_IMAGE, 0, 0, {ODD_DATA, CANCEL_ODD, IMAGE_DONE}},		
	{CANCEL_ODD, 0, 0, {ODD_IMAGE, ODD_DATA, MAIN_WAIT_MODE}},		
	{DATA_DONE, 0, 1, {ODD_CANCEL, MAIN_WAIT_MODE, MAIN_WAIT_MODE}},		
	{IMAGE_DONE, 0, 1, {ODD_CANCEL, MAIN_WAIT_MODE, MAIN_WAIT_MODE}},
	{USB_ONETOUCH, 0, 0, {MAIN_WAIT_MODE, USB_ONETOUCH, ONETOUCH_DONE}},	
	{ONETOUCH_DONE, 0, 1, {USB_CANCEL, MAIN_WAIT_MODE, MAIN_WAIT_MODE}},
	{USB_CANCEL, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}},
	{ODD_CANCEL, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}},
	{HIB_MODE, 0, 0, {ODD_BACKUP, MAIN_WAIT_MODE, HIB_DONE}},
	{HIB_DONE, 0, 1, {0, 0, HIB_EXIT}},
	{MESSAGE_MODE, 0, 0, {MAIN_WAIT_MODE, IP_MODE, INFO_FAN}},
	{INFORM_MODE, 0, 0, {MAIN_WAIT_MODE, IP_MODE, INFO_IP}},
	{HIB_EXIT, 0, 1, {0, 0, 0}},
	{USB_CANCEL_MODE, 0, 0, {USB_NOCANCEL, USB_CANCEL_MODE, USB_CANCEL}},
	{USB_NOCANCEL, 0, 1, {USB_CANCEL_MODE, 0, 0}},
	{ODD_CANCEL_MODE, 0, 0, {ODD_NOCANCEL, ODD_CANCEL_MODE, ODD_CANCEL}},
	{ODD_NOCANCEL, 0, 1, {ODD_CANCEL_MODE, 0, 0}},
	{POWER_OFF, 0, 1, {0, 0, 0}}
};

static int key_process(struct nashal_data *data, int button_id)
{
	struct i2c_data *msg = &data->msg_bit8;
	int pos, val;

  dprintk("%s is called\n", __FUNCTION__);
	switch(button_id) {
		case CANCEL:
			if(msg->cur_pos == 1)
				return 1;
			else if(msg->cur_pos % 4 == 1)
				msg->cur_pos -= 2;
			else				
				msg->cur_pos--;
			break;
		case MODE:
			if(msg->cur_pos == 15) { 
				msg->cur_pos = 1;
				if(msg->mode == IP_IPKEY) {
					memcpy(data->address.ipaddr, msg->data, 4);
				}
				if(msg->mode == IP_MASKKEY) {
					memcpy(data->address.netmask, msg->data, 4);
				}
				if(msg->mode == IP_GWKEY) {
					memcpy(data->address.gateway, msg->data, 4);
				}
				return 1;
			}
			else if(msg->cur_pos % 4 == 3)
				msg->cur_pos += 2;
			else
				msg->cur_pos++;
			break;	
		case SELECT:
			pos = msg->cur_pos % 4;
			val = msg->data[msg->cur_pos / 4];
			switch(pos)
			{
				case 1:
					if(val / 100 == 2)
						msg->data[msg->cur_pos / 4] -= 200;
					else if(val > 154)
						msg->data[msg->cur_pos / 4] = 255;
					else
						msg->data[msg->cur_pos / 4] += 100;
					break;
				case 2:
					if(val == 255)
						msg->data[msg->cur_pos / 4] -= 50;
					else if(val % 100 >= 90)
						msg->data[msg->cur_pos / 4] -= 90;
					else if(val > 245)
						msg->data[msg->cur_pos / 4] = 255;
					else
						msg->data[msg->cur_pos / 4] += 10;
					break;
				case 3:
					if(val % 10 == 9)
						msg->data[msg->cur_pos / 4] -= 9;
					else
						msg->data[msg->cur_pos / 4]++;
					break;
				default:
					break;
			}
			break;
	}
	msg->cur_blink = 1;
	return 0; 
}
static void change_fsm(struct nashal_data *data)
{
	struct mode_data *fsm = &data->mode_info;
	int i;

  dprintk("%s is called\n", __FUNCTION__);
	data->msg_bit8.cur_pos = 1;

	if(fsm->cur_mode == IP_IPKEY || fsm->cur_mode == INFO_IP)
		memcpy(data->msg_bit8.data, data->address.ipaddr, 4);
	if(fsm->cur_mode == IP_MASKKEY)
		memcpy(data->msg_bit8.data, data->address.netmask, 4);
	if(fsm->cur_mode == IP_GWKEY)
		memcpy(data->msg_bit8.data, data->address.gateway, 4);

	for(i = 0; i < MODES_NUM; i++) {
		if(fsm->cur_mode == modes[i].cur_mode) {
			fsm->key_mode = modes[i].key_mode;
			fsm->exec_mode = modes[i].exec_mode;
			fsm->next_mode[0]=modes[i].next_mode[0];
			fsm->next_mode[1]=modes[i].next_mode[1];
			fsm->next_mode[2]=modes[i].next_mode[2];
			fsm->next_mode[3]=modes[i].next_mode[3];
		}
	}
}
static void process_mode(struct nashal_data *data, int button_id)
{
	struct mode_data *fsm = &data->mode_info;

  dprintk("%s is called\n", __FUNCTION__);
	switch(button_id) {
		case POWER:
			fsm->cur_mode = POWER_OFF;
			change_fsm(data);
			break;
		case CANCEL:
			if(fsm->next_mode[0] == 0)
				return;
			fsm->cur_mode = fsm->next_mode[0];
			change_fsm(data);
			break;
		case MODE:
			if(fsm->next_mode[1] == 0)
				return;
			fsm->cur_mode = fsm->next_mode[1];
			change_fsm(data);
			break;
		case SELECT:
			if(fsm->next_mode[2] == 0)
				return;
			fsm->cur_mode = fsm->next_mode[2];
			change_fsm(data);
			break;
	}
}

static int fsm_comm_proc(struct nashal_data *data, int button_id)
{
	struct i2c_client *client = data->client;
	
  dprintk("%s is called\n", __FUNCTION__);
	data->msg_bit8.id = MICOM_ID_SET1;

	if(data->mode_info.key_mode) {
		if(key_process(data, button_id) != 0)
			process_mode(data, button_id);
	}
	else {
		nashal_micom_read(client, (u8 *) &data->msg_bit8, sizeof(struct i2c_data));
		data->msg_bit8.id = MICOM_ID_SET1;
		data->mode_info.cur_mode = data->msg_bit8.mode;
		change_fsm(data);
		process_mode(data, button_id);
	}

	data->msg_bit8.mode = data->mode_info.cur_mode;
	nashal_micom_write(client, (u8 *) &data->msg_bit8, sizeof(struct i2c_data));

	if((data->msg_bit8.mode==IP_GWKEY && data->mode_info.cur_mode == IP_DONE) ||
		data->mode_info.exec_mode)
		return 1;
	else
		return 0;
}

static void fsm_button_init(struct nashal_data *data)
{
	data->msg_bit8.id = MICOM_ID_SET1;
	data->msg_bit8.cur_pos = 1;
	data->mode_info = modes[0];
	data->msg_bit8.mode = data->mode_info.cur_mode;
}
#endif
/*
 * The SENSOR(TMP431)
 */
#define SENSOR_I2C_ADDR	 0x4d
static const u8 SENSOR_TEMP_MSB[2]			= { 0x00, 0x01 };
static const u8 SENSOR_TEMP_LSB[2]			= { 0x15, 0x10 };

/* 
 * The TIMER
 */
static void (*_timer_func[])(unsigned long) = {
	nashal_timer_led,
};

/*
 * Driver data (common to all clients)
 */

struct class *nashal_class = 0;
static const struct i2c_device_id nashal_id[] = {
	{ "micom", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nashal_id);

static struct i2c_driver nashal_driver = {
	.driver = {
		.name	= "micom",
	},
	.id_table	= nashal_id,
	.probe		= nashal_probe,
	.remove		= nashal_remove,
};

/*
 * Button Interrupt
 */
static int nashal_fetch_next_event(struct nashal_data *data,
				  struct input_event *event)
{
	int have_event;
	unsigned long flags;
	

  dprintk("%s is called\n", __FUNCTION__);
	spin_lock_irqsave(&data->buffer_lock, flags);

	have_event = data->head != data->tail;
	if (have_event) {
		*event = data->buffer[data->tail++];
		data->tail &= NASHAL_BUFFER_SIZE - 1;
	}

	spin_unlock_irqrestore(&data->buffer_lock, flags);

	return have_event;
}

static void button_event(struct work_struct *work)
{
	struct button_data *bdata =
		container_of(work, struct button_data, work);
	struct nashal_data *data =
		container_of(bdata, struct nashal_data, bdata[bdata->index]);

	struct gpio_keys_button *button = bdata->button;
	unsigned int type = button->type ?: EV_KEY;
	int state = (gpio_get_value(button->gpio) ? 1 : 0) ^ button->active_low;
	struct input_event event;
	unsigned long flags;

  dprintk("%s is called state val: %d \n", __FUNCTION__,state);

#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
  if(state && !fsm_comm_proc(data, button->gpio))
  	return;
#endif
	spin_lock_irqsave(&data->buffer_lock, flags);

	do_gettimeofday(&event.time);
	event.type = type;
#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
	event.code = data->msg_bit8.mode;
#else
	event.code = button->code;
#endif
	event.value = state;
	
	data->buffer[data->head++] = event;
	data->head &= NASHAL_BUFFER_SIZE - 1;

	spin_unlock_irqrestore(&data->buffer_lock, flags);

 	wake_up_interruptible(&data->wait);
}

static void button_timer(unsigned long _data)
{
	struct button_data *bdata = (struct button_data *)_data;

  dprintk("%s is called\n", __FUNCTION__);
	schedule_work(&bdata->work);
}

static irqreturn_t button_isr(int irq, void *dev_id)
{
	struct button_data *bdata = dev_id;
	struct gpio_keys_button *button = bdata->button;
	struct nashal_data *data =
		container_of(bdata, struct nashal_data, bdata[bdata->index]);
	
	if(data->lock.button)
		return IRQ_NONE;
	
  dprintk("%s is called button name:%s \n", __FUNCTION__, button->desc);

	if (button->debounce_interval)
		mod_timer(&bdata->timer,
			jiffies + msecs_to_jiffies(button->debounce_interval));
	else
		schedule_work(&bdata->work);

	return IRQ_HANDLED;
}


/*
 * Sysfs attr show / store functions
 */

static ssize_t show_micom(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int index = to_nashal_dev_attr(devattr)->index;
	u8 in[8];
	
	switch(index)
	{
	case 0:
		if(nashal_micom_read(client, in, 8))
			return sprintf(buf, "%02x %02x %02x %02x %02x %02x %02x %02x\n",
										in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7]);
	case 1:
		if(nashal_micom_reg_read(client, MICOM_VER, in, 8))
			return snprintf(buf, 8, "%s\n", in);
#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
	case 2:
		if(nashal_micom_reg_read(client, 0xff, in, 8))
			return sprintf(buf, "%d\n", in[1]);
		break;
#endif
	default:
		return -EINVAL;
	}

 	return -EFAULT;
}

#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
static ssize_t show_address(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	struct addr_data addr;
	
	mutex_lock(&data->update_lock);
	addr = data->address;
	mutex_unlock(&data->update_lock);

	return sprintf(buf,
					"%d.%d.%d.%d\n%d.%d.%d.%d\n%d.%d.%d.%d\n",
					addr.ipaddr[0], addr.ipaddr[1], addr.ipaddr[2], addr.ipaddr[3],
					addr.netmask[0], addr.netmask[1], addr.netmask[2], addr.netmask[3],
					addr.gateway[0], addr.gateway[1], addr.gateway[2], addr.gateway[3]);
}
#endif

int display_model(char * buf)
{
  int i, count=0;
  count += sprintf(buf+count, "model_name=%s\n", model.name);
  count += sprintf(buf+count, "bay_max=%d\n", model.max.bay);
  count += sprintf(buf+count, "odd_max=%d\n", model.max.odd);
  count += sprintf(buf+count, "usb_max=%d\n", model.max.usb);
  count += sprintf(buf+count, "lan_max=%d\n", model.max.lan);
  count += sprintf(buf+count, "fan_max=%d\n", model.max.fan);
  count += sprintf(buf+count, "lcd_max=%d\n", model.max.lcd);
  count += sprintf(buf+count, "temp_max=%d\n", model.max.temp);
  count += sprintf(buf+count, "led_hdd_max=%d\n", model.max.led_hdd);
  count += sprintf(buf+count, "led_odd_max=%d\n", model.max.led_odd);
  count += sprintf(buf+count, "led_usb_max=%d\n", model.max.led_usb);
  count += sprintf(buf+count, "led_lan_max=%d\n", model.max.led_lan);
  count += sprintf(buf+count, "led_power_max=%d\n", model.max.led_power);
  count += sprintf(buf+count, "led_fail_max=%d\n", model.max.led_fail);
  count += sprintf(buf+count, "wifi_support=");
	if(model.support.wifi) 
	  count += sprintf(buf+count, "yes\n");
	else
	  count += sprintf(buf+count, "no\n");
  count += sprintf(buf+count, "powerloss_support=");
	if(model.support.powerloss) 
	  count += sprintf(buf+count, "yes\n");
	else
	  count += sprintf(buf+count, "no\n");
  count += sprintf(buf+count, "wol_support=");
	if(model.support.wol) 
	  count += sprintf(buf+count, "yes\n");
	else
	  count += sprintf(buf+count, "no\n");
  count += sprintf(buf+count, "scheduled_power=");
	if(model.support.scheduled_power) 
	  count += sprintf(buf+count, "yes\n");
	else
	  count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "external_hdd=");
	if(model.support.external_hdd)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "external_odd=");
	if(model.support.external_odd)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "button_backup=");
	if(model.support.button_backup)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "rtc_battery=");
	if(model.support.battery)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "lcd_icon=");
	if(model.support.lcd_icon)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "memcard=");
	if(model.support.memcard)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
#if (MINFO_MAX_FAN > 0) 
  for(i=0; i<model.max.fan; i++){
	  count += sprintf(buf+count, "fan%d_rpm=",i+1);
	  if(model.support.fan_rpm[i])
		  count += sprintf(buf+count, "yes\n");
	  else
		  count += sprintf(buf+count, "no\n");
	}
#endif

  for(i=0; i<model.max.bay; i++){
    count += sprintf(buf+count, "bay%d_path=%s\n", i+1, model.bay_path[i]);
	}
#if (MINFO_MAX_ODD > 0) 
  for(i=0; i<model.max.odd; i++){
    count += sprintf(buf+count, "odd%d_path=%s\n", i+1, model.odd_path[i]);
	}
#endif
#if (MINFO_MAX_LCD > 0)
	for(i=0; i<model.max.lcd; i++){
		count += sprintf(buf+count, "lcd%d_line=%d\n", i+1, model.lcd_line[i]);
	}
#endif
#if (MINFO_MAX_TEMP > 0)
	for(i=0; i<model.max.temp; i++){
		count += sprintf(buf+count, "temp%d_channel=%d\n", i+1, model.temp_channel[i]);
	}
#endif
	return count;
}

static ssize_t show_sysinfo(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_nashal_dev_attr(devattr)->index;
#if (MINFO_SUPPORT_ACPOW)
	struct i2c_client *client = to_i2c_client(dev);
	u8 in[8];
#endif
	
	switch(index)
	{
	/* model info */
	case 0:
		return display_model( buf );
	/* wol */
	case 1:
		return sprintf(buf, "Todo..\n");
	/* acpowerloss */
	case 2:
#if (MINFO_SUPPORT_ACPOW)
		if(nashal_micom_reg_read(client, MICOM_POR, in, 8))
			return sprintf(buf, "%d\n", in[4]);
		break;
#endif
		return sprintf(buf, "Todo..\n");
	/* scheduled_power */
	case 3:
		return sprintf(buf, "Todo..\n");
	case 4:
		return sprintf(buf, "%d\n", debug);
	default:
		return -EINVAL;
	}

 	return -EFAULT;
}

#if (MINFO_MAX_FAN > 0)	
static ssize_t show_fan(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_nashal_dev_attr(devattr)->index;
	int nr = to_nashal_dev_attr(devattr)->nr;
	//struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	struct nashal_data *data = 	nashal_update_device(dev);
	int val;

	if (index == 0){
	  val = data->fan[nr].pwm;
	}else if (index == 1){
	  val = data->fan[nr].rpm;
	}else{
		val = 0;
 	} 
	return sprintf(buf, "%d\n", val);
}
#endif

/*
 *  0 : led off, 
 *  1 : led on 
 *  2 ~ 255 : timer delay * 100ms timer
 */
static ssize_t show_led(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_nashal_dev_attr(devattr)->index;
	int nr = to_nashal_dev_attr(devattr)->nr;
	int val;
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
dprintk(" %s: is called \n", __FUNCTION__ );

 	switch (index){
	case 0: 
		val = data->leddata[nr].led->state;
		break;
	case 1: 
		val = data->leddata[nr].led->delay;
		break;
	default:
		return -EINVAL;
	}
	return sprintf(buf, "%d\n", val);
}

/*
 *
 */
#if (MINFO_MAX_LCD > 0)
static ssize_t show_lcd(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	int index = to_nashal_dev_attr(devattr)->index;
	int nr = to_nashal_dev_attr(devattr)->nr;
  int status;
  
  mutex_lock(&data->update_lock);
  switch(index)
  {
  case 0:
  	status = data->lcd[nr].brightness;
  	break;
#if (MINFO_MAX_LCD_LINE > 0)
  case 3:
 		status = data->lcd[nr].timeout[0];
  	break;
#endif
#if (MINFO_MAX_LCD_LINE > 1)
  case 5:
  	status = data->lcd[nr].timeout[1];
  	break;
#endif
  default:
  	mutex_unlock(&data->update_lock);
  	return -EINVAL;
  }
  mutex_unlock(&data->update_lock);
  return sprintf(buf, "%d\n", status);
}
#endif

/*
 *
 */
#if (MINFO_MAX_TEMP > 0)
static ssize_t show_temp(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_nashal_dev_attr(devattr)->index;
	struct nashal_data *data = nashal_update_device(dev);

	return sprintf(buf, "%d\n", data->sensor[index].temp);
}
#endif

/*
 *
 */
static ssize_t show_gpio(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	int index = to_nashal_dev_attr(devattr)->index;
	unsigned gpio;
	int val;
	
	mutex_lock(&data->update_lock);
	gpio = data->gpio.gpio;
	mutex_unlock(&data->update_lock);

	switch(index)
	{
	case 0:
		return sprintf(buf, "%d\n", gpio);
	case 1:
		val = gpio_get_value(gpio);
		val = val ? 1 : val;
		return sprintf(buf, "%d\n", val);
	default:
		return -EINVAL;
	}
}

/*
 *
 */
static ssize_t show_button(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
	struct input_event event;
	int index = to_nashal_dev_attr(devattr)->index;
  int ret;
  
dprintk(" %s: is called \n", __FUNCTION__ );
  switch(index)
  {
  case 0:

dprintk(" %s: is called index:%d \n", __FUNCTION__,index );
  	ret = wait_event_interruptible(data->wait, data->head != data->tail);
  	if (ret)
  		return ret;
  	
  	while ( nashal_fetch_next_event(data, &event)) {
  	
  		memcpy(buf + ret, &event, sizeof(struct input_event));
  		
  		ret += sizeof(struct input_event);
  	}
  	
  	//return sprintf(buf, "code = %d, size = %d byte\n", event.code, ret);
	//return sprintf(buf,"%d",event.code);
  	return ret;
  case 1:
dprintk(" %s: is called case 1 index:%d \n", __FUNCTION__,index );
  	mutex_lock(&data->update_lock);
  	ret = data->lock.button;
  	mutex_unlock(&data->update_lock);
  	return sprintf(buf, "%d\n", ret);
  default:
  	return -EINVAL;
  }
}

static ssize_t show_lock(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	int index = to_nashal_dev_attr(devattr)->index;
  int status;
  
  mutex_lock(&data->update_lock);
  
  switch(index)
  {
  case 0:
  	status = data->lock.led;
  	break;
  case 1:
  	status = data->lock.button;
  	break;
  case 2:
  	status = data->lock.buzzer;
  	break;
  default:
  	mutex_unlock(&data->update_lock);
  	return -EINVAL;
  }
  
  mutex_unlock(&data->update_lock);
  return sprintf(buf, "%d\n", status);
}

#if (MINFO_SUPPORT_EXHDD)
static ssize_t show_switch(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	struct button_data *bdata;
	int i, status[2] = {-1, -1};
	
	for(i = 0; i < ARRAY_SIZE(buttons); i++) {
		bdata = &data->bdata[i];
		switch(bdata->button->code) {
			case KEY_ARCHIVE:
				status[0] = (gpio_get_value(bdata->button->gpio)? 1 : 0) ^
					bdata->button->active_low;
				break;
			case KEY_CD:
				status[1] = (gpio_get_value(bdata->button->gpio)? 1 : 0) ^
					bdata->button->active_low;
				break;
		}
	}
	
	if(status[0] > 0 && status[1] > 0)
		return sprintf(buf, "%s\n", "nas");
	if(status[0] == 0 && status[1] > 0)
		return sprintf(buf, "%s\n", "xhdd");
	if(status[0] > 0 && status[1] == 0)
		return sprintf(buf, "%s\n", "xodd");
	if(status[0] == 0 && status[1] == 0)
		return sprintf(buf, "%s\n", "xhdd");
	
	return -EFAULT;
}
#endif

static ssize_t store_micom(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
	struct nashal_data *data = i2c_get_clientdata(client);
#endif
	int index = to_nashal_dev_attr(devattr)->index;
	u8 out[19];
	char *p;
	long val, idx = 0;
	
	switch(index) {
	case 0:
		while((p = strsep((char **) &buf, " ")) != NULL) {
			if(strict_strtol(p, 16, &val))
				return -EINVAL;
			if(idx < 19) {
				out[idx] = val;
				idx++;
			}
		}
		if(nashal_micom_write(client, out, idx))
			return count;
		break;
#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
	case 2:
		if(strict_strtol(buf, 10, &val))
			return -EINVAL;
		mutex_lock(&data->update_lock);
		data->mode_info.cur_mode = val;
		change_fsm(data);
		data->msg_bit8.id = MICOM_ID_SET1;
		data->msg_bit8.mode = data->mode_info.cur_mode;
		mutex_unlock(&data->update_lock);
		if(nashal_micom_write(client,
			(u8 *) &data->msg_bit8, sizeof(struct i2c_data)))
			return count;
		break;
#endif
	}
		
	return -EFAULT;
}

#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
static int net_addr_set(char *dest, char *src)
{
	char *p;
	long val;
	int ret, idx = 0;

	while((p = strsep((char **) &src, ".")) != NULL)
	{
		ret = strict_strtol(p, 10, &val);
		if(!ret)
			dest[idx++] = val;
		else
			break;
	}
	return ret;
}

static ssize_t store_address(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	char *p;
	int idx = 0;
	
	while((p = strsep((char **) &buf, " ")) != NULL)
	{
		mutex_lock(&data->update_lock);
		if(net_addr_set(data->address.ipaddr + idx, p))
		{
			mutex_unlock(&data->update_lock);
			return -EINVAL;
		}
		mutex_unlock(&data->update_lock);
		idx += 4;
		if(idx >= 12)
			break;
	}
	return count;
}
#endif

static ssize_t store_sysinfo(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
	int index = to_nashal_dev_attr(devattr)->index;
#if (MINFO_SUPPORT_ACPOW)
	u8 out[8] = {MICOM_ID_SET1, MICOM_POR, 0, 0, 0, 0, 0, 0};
#endif
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	switch(index)
	{
	case 2:
#if (MINFO_SUPPORT_ACPOW)
		out[3] = (u8) val;
		if(nashal_micom_write(client, out, 8))
		{
			mutex_lock(&data->update_lock);
			data->sysinfo.powerloss = val;
			mutex_unlock(&data->update_lock);
		}
		else
			return -EFAULT;
		break;
#endif
		break;
	case 4:
		debug = val;
		break;
	default:
		return -EINVAL;
	}
	return count;
}

#if (MINFO_MAX_FAN > 0)	
static ssize_t store_fan(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	int nr = to_nashal_dev_attr(devattr)->nr;
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	//struct nashal_data *data = nashal_update_device(dev);
	long val;
	int ret;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;
	
	mutex_lock(&data->update_lock);
	data->buf[0] = MICOM_ID_SET1;
	switch(nr)
	{
	case 0: data->buf[1] = MICOM_PWM1; break;
	case 1: data->buf[1] = MICOM_PWM2; break;
	default: return -EINVAL;
	}
	data->buf[3] = val;
  /* i2c transfer */
  dprintk("micom write is called [0]:%x [1]:%x [2]:%x [3]:%x [4]:%x\n", data->buf[0], data->buf[1], data->buf[2], data->buf[3], data->buf[4]);
  ret = nashal_micom_write(client, data->buf, 8);
	/* data update */
	if(ret) data->fan[nr].pwm = val;
	mutex_unlock(&data->update_lock);
	
	if(ret)
		return count;
	
	return -EFAULT;
}
#endif


static void micom_led_brightness_set(struct i2c_client *client, unsigned value)
{
	u8 out[8] = {MICOM_ID_SET1, MICOM_LED_BRIGHTNESS, 1, 0, 0, 0, 0, 0};
	out[3] = (u8) value;
	nashal_micom_write(client, out, 8);
}

void (*link_led_brightness_set)(unsigned value);
static void led_set_value(struct i2c_client *client, unsigned gpio, int value)
{
	switch(gpio)
	{
	case ARCH_NR_GPIOS:
		if(link_led_brightness_set)
			link_led_brightness_set(value);
		break;
	case ARCH_NR_GPIOS + 1:
		micom_led_brightness_set(client, value);
		break;
	}
}

/*
 *  0 : led off, 
 *  1 : led on 
 *  2 ~ 255 : timer delay * 100ms timer
 */
static ssize_t store_led(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	int index = to_nashal_dev_attr(devattr)->index;
	int nr = to_nashal_dev_attr(devattr)->nr;
	long val;
	int lock;
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
	struct gpio_leds *led = data->leddata[nr].led;
dprintk(" %s: store_led is called, nr :%d \n", __FUNCTION__, nr );

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	lock = data->lock.led;
	if(lock)
	{
		mutex_unlock(&data->update_lock);
		return count;
	}
	
dprintk(" %s: store_led is called, nr :%d \n", __FUNCTION__, nr );
 	switch (index){
	case 0: 
		led->state = val ? 1: 0;
		if(gpio_is_valid(led->gpio)){
			dprintk(" %s : gpio_is_valid is true gpio_val:%d, led->state:%d led->active_low:%d \n", __FUNCTION__, led->gpio, led->state, led->active_low );
			gpio_set_value(led->gpio, (int)(led->state ^ led->active_low));
		}
		else{
			dprintk(" %s : gpio_is_valid is true else", __FUNCTION__ );
			led_set_value(client, led->gpio, (int)(led->state ^ led->active_low));
		}
		break;
	case 1: 
		if(!val)
		{
			led->state = val;
			if(gpio_is_valid(led->gpio))
				gpio_set_value(led->gpio, (int)(led->state ^ led->active_low));
			else
				led_set_value(client, led->gpio, (int)(led->state ^ led->active_low));
		}
		led->delay = val;
		mod_timer(&data->timer_100ms,
			jiffies + msecs_to_jiffies(100));
		break;
	default:
		return -EINVAL;
	}
#ifdef CONFIG_MACH_LGAMS
	if(!data->boot_complete)
	{
		data->boot_complete = 1;
		ata_led_act_set(ata_led_act_func);
	}
#endif
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t store_buzzer(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
	int index = to_nashal_dev_attr(devattr)->index;
	u8 out[8] = {MICOM_ID_SET2, MICOM_BUZZER, 0, 0, 0, 0, 0, 0};
	long val;
	int lock, idx = 0;
	char *p;
	
	switch(index)
	{
	case 0:
		mutex_lock(&data->update_lock);
		lock = data->lock.buzzer;
		mutex_unlock(&data->update_lock);
		if(lock)
			return count;
		while((p = strsep((char **) &buf, " ")) != NULL)
		{
			if(strict_strtol(p, 10, &val))
				return -EINVAL;
			if(idx < 6)
				out[idx + 2] = val;
			idx++;
		}
		if(nashal_micom_write(client, out, 8))
			return count;
		break;
	case 1:
		if(strict_strtol(buf, 10, &val))
			return -EINVAL;
		mutex_lock(&data->update_lock);
		data->lock.buzzer = val;
		mutex_unlock(&data->update_lock);
		return count;
	default:
		return -EINVAL;
	}

	return -EFAULT;
}

#if (MINFO_MAX_LCD > 0)
static ssize_t store_lcd(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
	int index = to_nashal_dev_attr(devattr)->index;
	int nr = to_nashal_dev_attr(devattr)->nr;
	u8 out[19];
	long val;
	int lock, len, ret = count;
	char *p;

  dprintk("%s is called\n", __FUNCTION__);
	mutex_lock(&data->update_lock);
	lock = data->lock.lcd;

	if(lock)
		goto __exit__;

  /* i2c transfer */
	switch(index) {
	case 0:
		if(strict_strtol(buf, 10, &val)) {
			ret = -EINVAL;
			goto __exit__;
		}
		data->lcd[nr].brightness = val;
		break;
	case 1:
		out[0] = MICOM_ID_SET2;
		out[1] = MICOM_ICON;
		while((p = strsep((char **) &buf, " ")) != NULL) {
			if(!strncmp(p, "left", 4)) out[2] = 1;
			else if(!strncmp(p, "net", 3)) out[2] = 2;
			else if(!strncmp(p, "user", 4)) out[2] = 3;
			else if(!strncmp(p, "usb", 3)) out[2] = 4;
			else if(!strncmp(p, "disc", 4)) out[2] = 5;
			else if(!strncmp(p, "hdd", 3)) out[2] = 6;
			else if(!strncmp(p, "right", 5)) out[2] = 7;
			else if(!strncmp(p, "on", 2)) out[3] = 255;
			else if(!strncmp(p, "off", 3)) out[3] = 0;
			else if(!strncmp(p, "blink", 5)) out[4] = 1;
			else { ret = -EINVAL; goto __exit__; }
		}
		if(!nashal_micom_write(client, out, 8))
			ret = -EFAULT;
		break;
	case 2:
		memset(out, ' ', 19);
		out[0] = MICOM_ID_SET3;
		p = memchr(buf, '\n', count);
		len = p? p - buf : count;
		len = (len > 16)? 16 : len;
		memcpy(out + 1, buf, len);
		out[18] = 0x0;
#if (MINFO_MAX_LCD_LINE > 0)
		out[18] = data->lcd[nr].timeout[0];
#endif
		if(!nashal_micom_write(client, out, 19))
			ret = -EFAULT;
		break;
	case 3:
		if(strict_strtol(buf, 10, &val)) {
			ret = -EINVAL;
			goto __exit__;
		}
#if (MINFO_MAX_LCD_LINE > 0)
		data->lcd[nr].timeout[0] = val;
#endif
		break;
	case 4:
		break;
	case 5:
		break;
	}
__exit__:
	mutex_unlock(&data->update_lock);
	return ret;
}
#endif
static ssize_t store_gpio(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	int index = to_nashal_dev_attr(devattr)->index;
	long val;
	unsigned gpio;
	int status;

	if(strict_strtol(buf, 10, &val))
		return -EINVAL;
	
	switch(index)
	{
	case 0:
		status = gpio_request(val, "hal");
		if(status < 0 && status != -EBUSY)
			return -EINVAL;
		if(status != -EBUSY)
			gpio_free(val);
		mutex_lock(&data->update_lock);
		data->gpio.gpio = val;
		mutex_unlock(&data->update_lock);
		return count;
	case 1:
		mutex_lock(&data->update_lock);
		gpio = data->gpio.gpio;
		mutex_unlock(&data->update_lock);
		gpio_set_value(gpio, (int)(val & 0xff));
		return count;
	default:
		return -EINVAL;
	}
}

static ssize_t store_lock(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
	int i, index = to_nashal_dev_attr(devattr)->index;
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->update_lock);
 	switch(index)
 	{
 	case 0:
 		for(i = 0; i < ARRAY_SIZE(leds); i++)
 		{
 			struct led_data *leddata = &data->leddata[i];
 			if(val)
 			{
#ifdef CONFIG_MACH_LGAMS
 				ata_led_act_set(NULL);
#endif
 				leddata->led->delay = 0;
 				leddata->count = 0;
 				leddata->led->state = 0;
 				if(gpio_is_valid(leddata->led->gpio))
 				{
 					gpio_set_value(leddata->led->gpio,
 						(int) (leddata->led->state ^ leddata->led->active_low));
 					gpio_direction_input(leddata->led->gpio);
 				}
 				else
 				{
 					led_set_value(client, leddata->led->gpio,
 						leddata->led->state ^ leddata->led->active_low);
 				}
 			}
#ifdef CONFIG_MACH_LGAMS
 			else
 				ata_led_act_set(ata_led_act_func);
#endif
 		}
 		data->lock.led = val;
 		break;
	case 1:
		data->lock.button = val;
		break;
	case 2:
		data->lock.buzzer = val;
		break;
	case 3:
		data->lock.lcd = val;
		break;
	default:
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_psm(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
/*	if( CPM_PM_DOZE == suspend_mode ){
		return "doze";
	}else if( CPM_PM_NAP == suspend_mode ){
		return "sleep";
	}*/
	return 1;
}
static ssize_t store_psm(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
/*
	int i;
	char *p;
	int len;

	p = memchr(buf, '\n', 10);
	len = p ? p - buf : 10;

	for (i = 0; i < CPM_PM_MODES_MAX; i++) {
		if (strncmp(buf, cpm_mode_name(i), len) == 0) {
			suspend_mode = i;
			if( CPM_PM_DOZE == suspend_mode ){
				cpm_pm_suspend(PM_SUSPEND_STANDBY, suspend_mode);
			}else if( CPM_PM_NAP == suspend_mode ){
				cpm_pm_suspend(PM_SUSPEND_MEM, suspend_mode);
			}
			return 10;
		}
	}
	*/
	return -EINVAL;
}

static struct nashal_device_attribute nashal_attr[] = {
                                             /*nr, index */
	/* micom */
	NASHAL_ATTR(micom,  	   0644, show_micom, store_micom, 0, 0),
	NASHAL_ATTR(micom_version, 0444, show_micom, NULL, 0, 1),
#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
	NASHAL_ATTR(micom_mode, 0644, show_micom, store_micom, 0, 2),
	/* address */
	NASHAL_ATTR(address, 0644, show_address, store_address, 0, 0),
#endif
	/* buzzer */
	NASHAL_ATTR(buzzer,      0200, NULL, store_buzzer, 0, 0),
	/* button */
	NASHAL_ATTR(button,      0444, show_button, NULL, 0, 0),
	/* gpio */
	NASHAL_ATTR(gpio,        0644, show_gpio, store_gpio, 0, 0),
	NASHAL_ATTR(gpio_value,  0644, show_gpio, store_gpio, 0, 1),
	/* lock */
	NASHAL_ATTR(led_lock,    0644, show_lock, store_lock, 0, 0),
	NASHAL_ATTR(button_lock, 0644, show_lock, store_lock, 0, 1),
	NASHAL_ATTR(buzzer_lock, 0644, show_lock, store_lock, 0, 2),
#if (MINFO_MAX_LCD > 0)
	NASHAL_ATTR(lcd_lock, 0644, show_lock, store_lock, 0, 3),
#endif
	/* sys info */
	NASHAL_ATTR(model_info,  0444, show_sysinfo, 0, 0, 0),
#if (MINFO_SUPPORT_WOL)
	NASHAL_ATTR(wol, 				 0644, show_sysinfo, store_sysinfo, 0, 1),
#endif
#if (MINFO_SUPPORT_ACPOW)
	NASHAL_ATTR(ac_powerloss,0644, show_sysinfo, store_sysinfo, 0, 2),
#endif
#if (MINFO_SUPPORT_SCHPOW)
	NASHAL_ATTR(scheduled_power,0644, show_sysinfo, store_sysinfo, 0, 3),
#endif
	NASHAL_ATTR(debug, 	     0644, show_sysinfo, store_sysinfo, 0, 4),
	/* fan */
#if (MINFO_MAX_FAN > 0)
	NASHAL_ATTR(fan1_pwm, 0644, show_fan, store_fan, 0, 0),
	NASHAL_ATTR(fan1_rpm, 0444, show_fan, NULL,      0, 1),
#endif
#if (MINFO_MAX_FAN > 1)
	NASHAL_ATTR(fan2_pwm, 0644, show_fan, store_fan, 1, 0),
	NASHAL_ATTR(fan2_rpm, 0444, show_fan, NULL,      1, 1),
#endif
	/* led */
#if (MINFO_MAX_LED_HDD > 0)
	NASHAL_ATTR(led_hdd1,     		0644, show_led, store_led, LED_HDD_INDEX, 0),
	NASHAL_ATTR(led_hdd1_delay,   0644, show_led, store_led, LED_HDD_INDEX, 1),
#endif
#if (MINFO_MAX_LED_HDD > 1)
	NASHAL_ATTR(led_hdd2,   			0644, show_led, store_led, LED_HDD_INDEX+1, 0),
	NASHAL_ATTR(led_hdd2_delay,   0644, show_led, store_led, LED_HDD_INDEX+1, 1),
#endif
#if (MINFO_MAX_LED_HDD > 2)
	NASHAL_ATTR(led_hdd3,   			0644, show_led, store_led, LED_HDD_INDEX+2, 0),
	NASHAL_ATTR(led_hdd3_delay,   0644, show_led, store_led, LED_HDD_INDEX+2, 1),
#endif
#if (MINFO_MAX_LED_HDD > 3)
	NASHAL_ATTR(led_hdd4,   			0644, show_led, store_led, LED_HDD_INDEX+3, 0),
	NASHAL_ATTR(led_hdd4_delay,   0644, show_led, store_led, LED_HDD_INDEX+3, 1),
#endif
#if (MINFO_MAX_LED_ODD > 0)
	NASHAL_ATTR(led_odd1,   			0644, show_led, store_led, LED_ODD_INDEX, 0),
	NASHAL_ATTR(led_odd1_delay,   0644, show_led, store_led, LED_ODD_INDEX, 1),
#endif
#if (MINFO_MAX_LED_USB > 0)
	NASHAL_ATTR(led_usb1,   			0644, show_led, store_led, LED_USB_INDEX, 0),
	NASHAL_ATTR(led_usb1_delay,   0644, show_led, store_led, LED_USB_INDEX, 1),
#endif
#if (MINFO_MAX_LED_LAN > 0)
	NASHAL_ATTR(led_lan1,   		0644, show_led, store_led, LED_LAN_INDEX, 0),
	NASHAL_ATTR(led_lan1_delay, 0644, show_led, store_led, LED_LAN_INDEX, 1),
#endif
#if (MINFO_MAX_LED_POW > 0)
	NASHAL_ATTR(led_power1,   		0644, show_led, store_led, LED_POW_INDEX, 0),
	NASHAL_ATTR(led_power1_delay, 0644, show_led, store_led, LED_POW_INDEX, 1),
#endif
#if (MINFO_MAX_LED_POW > 1)
	NASHAL_ATTR(led_power2,   		0644, show_led, store_led, LED_POW_INDEX+1, 0),
	NASHAL_ATTR(led_power2_delay, 0644, show_led, store_led, LED_POW_INDEX+1, 1),
#endif
#if (MINFO_MAX_LED_POW > 2)
	NASHAL_ATTR(led_power3,   		0644, show_led, store_led, LED_POW_INDEX+2, 0),
	NASHAL_ATTR(led_power3_delay, 0644, show_led, store_led, LED_POW_INDEX+2, 1),
#endif
#if (MINFO_MAX_LED_FAIL > 0)
	NASHAL_ATTR(led_fail1,				0644, show_led, store_led, LED_FAIL_INDEX, 0),
	NASHAL_ATTR(led_fail1_delay,	0644, show_led, store_led, LED_FAIL_INDEX, 1),
#endif
#if (MINFO_MAX_LED_FAIL > 1)
	NASHAL_ATTR(led_fail2,				0644, show_led, store_led, LED_FAIL_INDEX+1, 0),
	NASHAL_ATTR(led_fail2_delay,	0644, show_led, store_led, LED_FAIL_INDEX+1, 1),
#endif
#if (MINFO_MAX_LED_FAIL > 2)
	NASHAL_ATTR(led_fail3,				0644, show_led, store_led, LED_FAIL_INDEX+2, 0),
	NASHAL_ATTR(led_fail3_delay,	0644, show_led, store_led, LED_FAIL_INDEX+2, 1),
#endif
#if (MINFO_MAX_LED_FAIL > 3)
	NASHAL_ATTR(led_fail4,				0644, show_led, store_led, LED_FAIL_INDEX+3, 0),
	NASHAL_ATTR(led_fail4_delay,	0644, show_led, store_led, LED_FAIL_INDEX+4, 1),
#endif
	/* lcd */
#if (MINFO_MAX_LCD > 0)
	NASHAL_ATTR(lcd1_brightness, 	0644, show_lcd, store_lcd, 0, 0),
#if (MINFO_SUPPORT_LCDICON)
	NASHAL_ATTR(lcd1_icon, 0200, NULL, store_lcd, 0, 1),
#endif
#if (MINFO_LCD1_LINE > 0)
	NASHAL_ATTR(lcd1_str1, 0200, NULL, store_lcd, 0, 2),
	NASHAL_ATTR(lcd1_str1_timeout, 0644, show_lcd, store_lcd, 0, 3),
#endif
#if (MINFO_LCD1_LINE > 1)
	NASHAL_ATTR(lcd1_str2, 0200, NULL, store_lcd, 0, 4),
	NASHAL_ATTR(lcd1_str2_timeout, 0644, show_lcd, store_lcd, 0, 5),
#endif
#endif
	/* sensor temp */
#if (MINFO_MAX_TEMP > 0)
	NASHAL_ATTR(temp1_1,  0444, show_temp, NULL, 0, 0),
#endif
#if (MINFO_MAX_TEMP > 1)
	NASHAL_ATTR(temp1_2,  0444, show_temp, NULL, 0, 1),
#endif
#if (MINFO_SUPPORT_EXHDD)
	NASHAL_ATTR(switch_mode, 0444, show_switch, NULL, 0, 0),
#endif
};

/*
 * Begin non sysfs callback code (NAS HAL Real code)
 */

/* Timer functions */
static void nashal_timer_led(unsigned long __data){
	struct i2c_client *client = (struct i2c_client *)__data;
	struct nashal_data *data = i2c_get_clientdata(client);
  int i;

  dprintk("%s is called\n", __FUNCTION__);

	for ( i=0; i<ARRAY_SIZE(leds); i++)
	{
		struct led_data *leddata = &data->leddata[i]; 

		if( leddata->led->delay ){
			if( leddata->count >= leddata->led->delay ){
				leddata->count = 0;
				leddata->led->state = ~leddata->led->state;
				if(gpio_is_valid(leddata->led->gpio))
					gpio_set_value(leddata->led->gpio,
						(int)(leddata->led->state ^ leddata->led->active_low));
				else{
					led_set_value(client, leddata->led->gpio,
						leddata->led->state ^ leddata->led->active_low);
					
				}
			}else{
				leddata->count++;
			}
		}	
	}
 	mod_timer(&data->timer_100ms, jiffies + msecs_to_jiffies(100));
}

/* Micom I/F functions */
static int nashal_micom_read(struct i2c_client *client, u8 *buf, u8 len )
{
	struct i2c_msg msgs[] = {
		{.addr = client->addr,.flags = I2C_M_RD,.buf = buf,.len = len }
	};

  dprintk("%s is called\n", __FUNCTION__);

	if( i2c_transfer( client->adapter, msgs, 1) != 1 )
	  return false;

	return true;
}

static int nashal_micom_write(struct i2c_client *client, u8 *buf, u8 len )
{
	struct i2c_msg msgs[] = {
		{.addr = client->addr,.flags = 0,       .buf = buf,.len = len },
	};

  dprintk("%s is called\n", __FUNCTION__);

	if( i2c_transfer( client->adapter, msgs, 1) != 1 )
	  return false;

	return true;
}

static int nashal_micom_reg_read(struct i2c_client *client, u8 reg, u8 *buf, u8 len )
{
  u8 out[8] = { MICOM_ID_GET, reg, 0, 0, 0, 0, 0, MICOM_ID_GET ^ reg };

	struct i2c_msg msgs[] = {
		{.addr = client->addr,.flags = 0,       .buf = out,.len = 8  },
		{.addr = client->addr,.flags = I2C_M_RD,.buf = buf,.len = len}
	};

	memset(buf, 0x00, 8);	
	dprintk("out:0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
	out[0],out[1],out[2],out[3],out[4],out[5],out[6],out[7]);

	if( i2c_transfer( client->adapter, msgs, 2) != 2 ){
		udelay(100);	//omw retry
		if( i2c_transfer( client->adapter, msgs, 2) != 2 ){
		  return false;
		}
	}

	dprintk(" in:0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
	buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);

	return true;
}

#if (MINFO_MAX_TEMP > 0)
/* Sensor I/F functions */
static int nashal_temp_read(struct i2c_client *client, u8 reg1, u8 reg2 )
{
	union i2c_smbus_data data;
	int temp;

	if ( i2c_smbus_xfer(client->adapter, SENSOR_I2C_ADDR, client->flags,
				I2C_SMBUS_READ, reg1,
				I2C_SMBUS_BYTE_DATA, &data) < 0 )
		return -EINVAL;
	temp = data.byte << 8;

	if ( i2c_smbus_xfer(client->adapter, SENSOR_I2C_ADDR, client->flags,
				I2C_SMBUS_READ, reg2,
				I2C_SMBUS_BYTE_DATA, &data) < 0 )
		return -EINVAL;

	return  ((( temp | data.byte) * 625 + 80 ) / 160) / 1000;
}
#endif

#if (MINFO_MAX_FAN > 0)
/* Update functions of hal data from i2c devices( micom, sensor, ... ) */
static struct nashal_data *nashal_update_device_fan(
	struct i2c_client *client, struct nashal_data *data)
{
	int i;

  dprintk("%s is called\n", __FUNCTION__);

	for (i = 0; i < MINFO_MAX_FAN; i++) {
	  if(nashal_micom_reg_read( client, NASHAL_FAN_PWM[i], data->buf, 8))
			data->fan[i].rpm = (u16)(data->buf[3] << 8) + data->buf[4];
	}
	return data;
}
#endif

#if (MINFO_MAX_TEMP > 0)
static struct nashal_data *nashal_update_device_temp(
	struct i2c_client *client, struct nashal_data *data)
{
	int i;
	int val;

  dprintk("%s is called\n", __FUNCTION__);

	for ( i = 0; i < MINFO_MAX_TEMP; i++ ) {
	  val = nashal_temp_read(client, SENSOR_TEMP_MSB[i], SENSOR_TEMP_LSB[i]);
		if ( val < 0 ) 
		  return data;
	  data->sensor[i].temp = val;
	}
	return data;
}
#endif

static struct nashal_data *nashal_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);

  dprintk("%s is called\n", __FUNCTION__);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
#if (MINFO_MAX_FAN > 0)
		nashal_update_device_fan(client, data);
#endif		
#if (MINFO_MAX_TEMP > 0)
		nashal_update_device_temp(client, data);
#endif		

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}
static int nashal_led_init(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
	struct gpio_leds *led;
	int i, ret;
dprintk("%s: is called\n", __FUNCTION__ );

	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		data->leddata[i].led = &leds[i];
		led = data->leddata[i].led;

		if(gpio_is_valid(led->gpio))
		{
			ret = gpio_request(led->gpio, "gpio_leds");
			if(ret < 0)
				return ret;
			ret = gpio_direction_output(led->gpio, led->state ^ led->active_low);
			if(ret < 0)
				goto err;
		}
		else{
dprintk("%s: is called led_set_value\n", __FUNCTION__ );
			led_set_value(client, led->gpio, led->state ^ led->active_low);
		}
	}

	setup_timer(&data->timer_100ms, nashal_timer_led, (unsigned long)client);

	return 0;

err:
	gpio_free(led->gpio);
	return ret;
}



#define INSERT 0
#define DELETE 1
static irqreturn_t power_isr(int irq, void *dev_id)
{
	struct device_node* np;
	struct i2c_client* micom;
	np = of_find_compatible_node(NULL, NULL, "misc,micom");
	micom = of_find_i2c_device_by_node(np);

 dprintk("%s: Power off the board\n", __FUNCTION__);
 	//kernel_power_off();
 /*
	//u8 out[8] = {MICOM_ID_SET2, MICOM_BUZZER, 0, 0, 0, 0, 0, 0};
	u8 out[8]={ 0x02, 0x97, 0x00, 0x0, 0x0, 0x0, 0x0, 0x0};
	// busswitch on -> detach_nor
	nashal_micom_write( micom, out, 8);
	out[0]=MICOM_ID_SET2; out[1]=MICOM_BUZZER; out[2]=1; out[3]=1;
	out[4]=1; out[5]=1; out[6]=1; out[7]=1;
	nashal_micom_write( micom, out, 8);
	udelay(1000);
	gpio_set_value( 224+15, 1);
	udelay(1000);
	gpio_set_value( 224+15, 0);

	*/
	return IRQ_HANDLED;
}
#ifdef CONFIG_MACH_NC5
static int nc5_button_control(struct button_data *bdata, int opt)
{

	struct device_node *np;
	int err;
	for_each_compatible_node(np, NULL, "amcc,ext_int-460ex")
	{
	
		int irq_num = NO_IRQ;
		if( !strcmp( bdata->button->desc, "power" )){
			irq_num = irq_of_parse_and_map(np, 0);
			if ( irq_num == NO_IRQ ){
					printk(KERN_INFO  "POWER_INTR:irq_of_parse_and_map failed\n");
					err = -EINVAL;
					goto fail1;
			}
			if( INSERT == opt ){
				if ((err = request_irq(irq_num,power_isr,
						 SA_INTERRUPT 
						 //IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
						 ,"POWER",bdata) ))
				{
						printk(KERN_INFO "POWER_INTR:Request interrupt failed");
						goto fail1;
				}
			}else if( DELETE == opt ){
				free_irq( irq_num, bdata);
			}
			return 1;
			
		}else if( !strcmp( bdata->button->desc, "up" )){
		
			irq_num = irq_of_parse_and_map(np, 1);
			if ( irq_num == NO_IRQ ){
					printk(KERN_INFO  "BACKUP_BTN:irq_of_parse_and_map failed\n");
					err = -EINVAL;
					//free_irq( power_intr, NULL );
					goto fail1;
			}
		}else if( !strcmp( bdata->button->desc, "right") ){
			irq_num = irq_of_parse_and_map(np, 3);
			if ( irq_num == NO_IRQ ){
					printk(KERN_INFO  "BACKUP_BTN:irq_of_parse_and_map failed\n");
					err = -EINVAL;
					//free_irq( power_intr, NULL );
					goto fail1;
			}
		}else if( !strcmp( bdata->button->desc, "left")) {
			irq_num = NO_IRQ;
			irq_num = irq_of_parse_and_map(np, 2);
			if ( irq_num == NO_IRQ ){
					printk(KERN_INFO  "BACKUP_BTN:irq_of_parse_and_map failed\n");
					err = -EINVAL;
					//free_irq( power_intr, NULL );
					goto fail1;
			}
		}
		if( INSERT == opt ){
			if ((err = request_irq(irq_num,button_isr,
					 SA_INTERRUPT 
					 //IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
					 ,"POWER",bdata) ))
			{
					printk(KERN_INFO "POWER_INTR:Request interrupt failed");
					goto fail1;
			}
		}else if( DELETE == opt ){
			free_irq( irq_num, bdata);
		}
		return 1;
fail1:
		return 0;
	}	
	return 0;
}
#endif

#ifdef CONFIG_MACH_NT3
static irqreturn_t hlds_usb_otg_hdlr(int irq, void *dev_id)
{
	volatile int vbus_state = 0;
	volatile u32 usb_global_intr_stat = 0;

	if (device_init == DEV_INIT_TRUE){
		return IRQ_HANDLED;
	}
	
	//printk(" usb_otg handler\n");
	if (usb_otg_reg) {	
		usb_global_intr_stat = in_le32((u32*)(usb_otg_reg + USB0_GINTSTS));
		if (usb_global_intr_stat & 0x20000000) {
			dprintk("USB host disconnected interrupt detected\n");
			usb_otg_host_connected = 0;		
			/* Start processing USB device port event */
			if (gpio_request(vbus_detect_gpio, NULL) >= 0) {
				vbus_state = gpio_get_value(vbus_detect_gpio);
				gpio_free(vbus_detect_gpio);
				printk("USB OTG HDLR: vbus state = %d\n", vbus_state);
			} 
			if (vbus_state) {
				/* Device port is connected so switch to device mode */
				curr_usb_port_state = USB_OTG_DEVICE;
				schedule_work((struct work_struct *)&micom_usb_device);
			} 
		} else if (usb_global_intr_stat & 0x01000000) {
			printk(KERN_INFO "USB device connected to host port\n");
			usb_otg_host_connected = 1;
			dprintk("USB Device port Dis-connected\n");
			/* and stay in current mode, do nothing */
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t hlds_vbus_detect_hdlr(int irq, void *dev_id) 
{
	volatile int vbus_state = 1;
	unsigned long flags;

           
	printk("VBUS detect handler\n");
	/*
	if (device_init == DEV_INIT_TRUE){
		if (gpio_request(vbus_detect_gpio, NULL) >= 0) {
			vbus_state = gpio_get_value(vbus_detect_gpio);
			gpio_free(vbus_detect_gpio);
			printk("VBUS detect HDLR: vbus state = %d\n", vbus_state);
		}
		if ((curr_usb_port_state == USB_OTG_DEVICE) && vbus_state) {
				// then change type of interrupt to falling 
				set_irq_type(irq, IRQ_TYPE_EDGE_FALLING);
		}
		return IRQ_HANDLED;
	}*/

	if (device_init == DEV_INIT_TRUE){
		return IRQ_HANDLED;
	}
	if (gpio_request(vbus_detect_gpio, NULL) >= 0) {
		vbus_state = gpio_get_value(vbus_detect_gpio);
		gpio_free(vbus_detect_gpio);
		printk("VBUS detect HDLR: vbus state = %d\n", vbus_state);
	}
	/*
	device_init = DEV_INIT_FALSE;
	if (gpio_get_value( vbus_detect_gpio ) != 0 ) { //device mode
		set_irq_type( irq, IRQ_TYPE_EDGE_FALLING );
	}else {
		set_irq_type( irq, IRQ_TYPE_EDGE_RISING );
	}*/

	if (vbus_state) {
		printk(KERN_INFO "VBUS detected - Device port is connected\n");
		dprintk("usb_otg_host_connected = %d\n", usb_otg_host_connected);
		if (curr_usb_port_state == USB_OTG_HOST) {
			if (usb_otg_host_connected == 0) {/* There is no device in host port */
				printk(KERN_INFO "USB OTG port now will switch to Device mode\n");
				curr_usb_port_state = USB_OTG_DEVICE;
				schedule_work((struct work_struct *)&micom_usb_device);
				//set_irq_type( irq, IRQ_TYPE_EDGE_FALLING );
				/* Soft disconnect */
				spin_lock_irqsave(&usb_switch_lock, flags);
				if (usb_otg_reg) {
					out_le32((u32*)(usb_otg_reg + 0x804), 0x2);	
					mdelay(1000);
					out_le32((u32*)(usb_otg_reg + 0x804), 0x0);
				}
				spin_unlock_irqrestore(&usb_switch_lock, flags);
			}else{
				printk(KERN_INFO "USB OTG port now not switch to Device mode\n");
			}
		}else {
			/* unexpected */
			dprintk("curr_usb_port_state != USB_OTG_HOST\n");
		}
		set_irq_type( irq, IRQ_TYPE_EDGE_FALLING );
	}else {
		printk(KERN_INFO "VBUS value is 0\n");
		if (curr_usb_port_state == USB_OTG_DEVICE) {
			dprintk("USB Device port Dis-connected\n");
			dprintk("USB OTG port now will switch to Host mode\n");
			curr_usb_port_state = USB_OTG_HOST;
			usb_otg_host_connected = 0;
			schedule_work((struct work_struct *)&micom_usb_host);
			//set_irq_type( irq, IRQ_TYPE_EDGE_RISING );
		} else {
			/* Nothing to do in this case */
			dprintk("curr_usb_port_state != USB_OTG_DEVICE\n");
		}
		set_irq_type( irq, IRQ_TYPE_EDGE_RISING );
	}
	return IRQ_HANDLED;
}     
static void set_usb_mode(struct work_struct *work)
{
	struct device_node* np;
	struct micom_work *worker = container_of(work, struct micom_work, work);
	struct i2c_client* client;

	if (gpio_request(port_select_gpio, NULL) >= 0) {
		int port_sel = worker->buf[2];
		gpio_set_value(port_select_gpio, port_sel);
		gpio_free(port_select_gpio);
		dprintk("%s: USB OTG HDLR: port_sel = %d\n",__FUNCTION__, port_sel);
	}

	//if (micom == NULL) {
	np = of_find_compatible_node(NULL, NULL, "misc,micom");
	client = of_find_i2c_device_by_node(np);
	//}

	if (client) 
		nashal_micom_write( client, worker->buf, 8);
}
static int nt3_button_control(struct button_data *bdata, int opt)
{

	struct device_node *np,*child;
	enum of_gpio_flags flags;
	int err,intr_num=NO_IRQ;
	
	for_each_compatible_node(np, NULL, "amcc,ext_int-460ex")
	{
		
		if( !strcmp( bdata->button->desc, "power" )){
			
	//		child = of_get_next_child( np, NULL);
	//		for( i=0; i<2; i++){	// 0:vbus_detect 1:power 2:backup by omw 
	//			child = of_get_next_child(np, child);
	//		}
			child = of_find_node_by_name( NULL, "power_button" );
			intr_num = irq_of_parse_and_map( child, 0);

			if( INSERT == opt ){
				bdata->button->gpio = of_get_gpio_flags(child, 0, &flags);
				if ((err = request_irq(intr_num,button_isr,
										 SA_INTERRUPT 
										 ,"POWER",bdata) ))
				{
						printk(KERN_INFO "POWER_INTR:Request interrupt failed");
						goto fail1;
				}
			}else if( DELETE == opt ){
				free_irq( intr_num, bdata);
			}

		}else if( !strcmp( bdata->button->desc, "backup" )){

			child = of_find_node_by_name( NULL, "backup_button" );
			intr_num = irq_of_parse_and_map( child, 0);

			if( INSERT == opt ){
				bdata->button->gpio = of_get_gpio_flags(child, 0, &flags);
				if ((err = request_irq(intr_num, button_isr,
						 SA_INTERRUPT 
						 ,"BACKUP BUTTON",bdata) ))
				{
						printk(KERN_INFO "BACKUP_BTN:Request interrupt failed");
						//free_irq( power_intr, NULL );
						goto fail1;
		 		}		
			}else if( DELETE == opt ){
				free_irq( intr_num, bdata);
			}

		}else if( !strcmp( bdata->button->desc, "vbus_detect" )){
			
			child = of_find_node_by_name( NULL, "vbus_detect_intr" );
			intr_num = irq_of_parse_and_map( child, 0);
			printk( " vbus_intr num :%d\n ", intr_num );
			if( INSERT == opt ){
				bdata->button->gpio = vbus_detect_gpio = of_get_gpio_flags(child, 0, &flags);
				port_select_gpio = of_get_gpio_flags(child, 1, &flags);
				if ((err = request_irq(intr_num,hlds_vbus_detect_hdlr,
										 IRQF_SHARED
										 ,"vbus",bdata) ))
				{
						printk(KERN_INFO "VBUS_INTR:Request interrupt failed");
						goto fail1;
				}
			}else if( DELETE == opt ){
				free_irq( intr_num, bdata);
			}

		}else if( !strcmp( bdata->button->desc, "usb_otg" )){
			
			//bdata->button->gpio = of_get_gpio_flags(child, 0, &flags);
			child = of_get_next_child( np, NULL);
			usb_otg_intr = irq_of_parse_and_map( child, 0);
			if( INSERT == opt ){
				bdata->button->gpio = of_get_gpio_flags(child, 0, &flags);
				if ((err = request_irq( usb_otg_intr,hlds_usb_otg_hdlr,
										 IRQF_SHARED 
										 ,"usb_otg",bdata) ))
				{
						printk(KERN_INFO "USB_OTG_INTR:Request interrupt failed");
						goto fail1;
				}
			}else if( DELETE == opt ){
				free_irq( usb_otg_intr, bdata);
			}
		}
	}
	return 1;
fail1:
	return 0;
}
#endif
static int nashal_button_init(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
	int i, err;

	spin_lock_init(&data->buffer_lock);
	init_waitqueue_head(&data->wait);

	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		struct button_data *bdata = &data->bdata[i];
		int irq;

		bdata->index = i;
		bdata->button = &buttons[i];
		setup_timer(&bdata->timer, button_timer, (unsigned long)bdata);
		INIT_WORK(&bdata->work, button_event);
#ifdef CONFIG_MACH_NT3 
		if( !nt3_button_control( bdata, INSERT ) ){
			goto fail2;
		}
#elif defined(CONFIG_MACH_NC5)
		if( !nc5_button_control( bdata, INSERT ) ){
			goto fail2;
		}
#else
		err = gpio_request(bdata->button->gpio, bdata->button->desc ?: "gpio_keys");
		if (err < 0) {
			pr_err("gpio-keys: failed to request GPIO %d,"
				" error %d\n", bdata->button->gpio, err);
			goto fail2;
		}
		err = gpio_direction_input(bdata->button->gpio);
		if (err < 0) {
			pr_err("gpio-keys: failed to configure input"
				" direction for GPIO %d, error %d\n",
				bdata->button->gpio, err);
			gpio_free(bdata->button->gpio);
			goto fail2;
		}

		irq = gpio_to_irq(bdata->button->gpio);
		if (irq < 0) {
			err = irq;
			pr_err("gpio-keys: Unable to get irq number"
				" for GPIO %d, err %d\n",
				bdata->button->gpio, err);
			gpio_free(bdata->button->gpio);
			goto fail2;
		}

		err = request_irq(irq, button_isr,
				    IRQF_SHARED |
				    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				    bdata->button->desc ? bdata->button->desc : "gpio_keys",
				    bdata);
		if (err) {
			pr_err("gpio-keys: Unable to claim irq %d; err %d\n",
				irq, err);
			gpio_free(bdata->button->gpio);
			goto fail2;
		}
#endif
	}
#if defined(CONFIG_MACH_NC1) || defined(CONFIG_MACH_NC5)
	fsm_button_init(data);
#endif
	return 0;

 fail2:
	while (--i >= 0) {
		if (data->bdata[i].button->debounce_interval)
			del_timer_sync(&data->bdata[i].timer);
		cancel_work_sync(&data->bdata[i].work);
#ifdef CONFIG_MACH_NC5
		nc5_button_control( &data->bdata[i] , DELETE );
#elif defined CONFIG_MACH_NT3
		nt3_button_control( &data->bdata[i] , DELETE );
#else
		free_irq(gpio_to_irq(data->bdata[i].button->gpio), &data->bdata[i]);
		gpio_free(data->bdata[i].button->gpio);
#endif
	}
  return -EPERM;
}

#ifdef CONFIG_MACH_LGAMS
static void nashal_model_init(struct i2c_client *client)
{
	u8 in[8];
	
	if(nashal_micom_reg_read(client, MICOM_VER, in, 8)) {
		if(strncmp(in, "100909", 6) < 0) {
			model.support.powerloss = false;
		}
	}
}
#endif

static int nashal_init_client(struct i2c_client *client)
{
  int ret;
#ifdef CONFIG_MACH_NT3 // for usb otg by omw
	struct device_node *usb_np;
	for_each_compatible_node(usb_np, NULL, "amcc,usb-otg-405ex") {
		if (of_address_to_resource(usb_np, 0, &usb_res)) {
			printk(KERN_ERR "Can't get USB-OTG register address\n");
			return -ENOMEM;
		}
		usb_otg_reg = ioremap(usb_res.start, usb_res.end - usb_res.start + 1);
		if (!usb_otg_reg) {
			printk(KERN_ERR "Failed to map IO register of USB OTG\n");
			return -EIO;
		}
	}
#endif
	
	/* Add code to here about nashal initialization */
  dprintk("%s is called\n", __FUNCTION__);
	ret = nashal_led_init(client);
	if(ret)
		dprintk("%s is led init error\n", __FUNCTION__);
	ret = nashal_button_init(client);
  if(ret)
  	dprintk("%s is button init error\n", __FUNCTION__);
	

	return ret;
}

/*
 * driver function ( probe, remove, init, exit )
 */
static int nashal_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int i, err = 0;
	struct i2c_adapter *adapter = client->adapter;
	struct nashal_data *data;
#ifdef CONFIG_MACH_APM_BASE
	struct device_node *np;
#endif

#ifdef CONFIG_MACH_NT3
	int vbus_state = 0;
	volatile u32 usb_global_intr_stat = 0;
#endif

  dprintk("%s is called\n", __FUNCTION__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;

	data = kzalloc(sizeof(struct nashal_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);

#ifdef CONFIG_MACH_NC5
	/*for_each_compatible_node(np, NULL, "amcc,ext_int-460ex")
	{
		if (of_address_to_resource(np, 0, &res_gpio0)) {
				printk(KERN_ERR "%s: Can't get GPIO0 register address\n", __func__);
				return -ENOMEM;
		}
		if (request_mem_region(res_gpio0.start, res_gpio0.end + 1 - res_gpio0.start,"AMCC-GPIO0") == NULL) {
				printk(KERN_ERR "%s: GPIO0 Memory resource is busy\n", __func__);
				return -ENOMEM;
		}

		gpio0 = ioremap(res_gpio0.start, res_gpio0.end + 1 - res_gpio0.start);
		if (gpio0 == NULL)
		{
				printk(KERN_ERR "%s: Error when mapping GPIO0 space\n", __func__);
				return -ENOMEM;
		}
	}*/

#endif
	/* Initialize the NASHAL */
	err = nashal_init_client(client);
	if(err){
		goto exit_free;
  }
	mutex_init(&data->update_lock);

	/* Register sysfs hooks */
	for (i = 0; i < ARRAY_SIZE(nashal_attr); i++) {
		err = device_create_file(&client->dev,
					 &nashal_attr[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	data->nashal_dev = device_create(nashal_class, &client->dev, 
	              		MKDEV(0, 0), NULL,
	              		"hal" );

	if (IS_ERR(data->nashal_dev)) {
		err = PTR_ERR(data->nashal_dev);
		data->nashal_dev = NULL;
		goto exit_remove;
	}

	dev_info(&client->dev, "NAS HAL Detected %s chip at 0x%02x\n",
		client->name, client->addr);
#ifdef CONFIG_MACH_NT3
	if (gpio_request(vbus_detect_gpio, NULL) >= 0) {
		vbus_state = gpio_get_value(vbus_detect_gpio);
		gpio_free(vbus_detect_gpio);
		dprintk("USB host/device init: vbus state = %d\n", vbus_state);
		if (vbus_state) {
			dprintk("USB VBUS Detected - USB OTG currently works in device mode\n");
			schedule_work((struct work_struct *)&micom_usb_device);
			curr_usb_port_state = USB_OTG_DEVICE;	
			usb_otg_host_connected = 0; /* Make sure USB device in host port is disconnected */
		} else {
			dprintk("USB OTG currently works in host mode\n");
			schedule_work((struct work_struct *)&micom_usb_host);
			curr_usb_port_state = USB_OTG_HOST;

		usb_global_intr_stat = in_le32((u32*)(usb_otg_reg + USB0_GINTSTS));

		if (usb_global_intr_stat & 0x20000000) {
			usb_otg_host_connected = 0;		
		} else if (usb_global_intr_stat & 0x01000000) {
			printk(KERN_INFO "USB device connected to host port\n");
			usb_otg_host_connected = 1;
		}
		
			if (usb_otg_host_connected)
				printk(KERN_INFO "There is device connected to host port\n");
		}   
	}
	device_init = DEV_INIT_FALSE;
#endif 

	return 0;

exit_remove:
	nashal_remove(client); /* will also free data for us */

exit_free:
	i2c_set_clientdata(client, NULL);
	kfree(data);

	return err;
}

static int nashal_remove(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
	int i;

  dprintk("%s is called\n", __FUNCTION__);

	del_timer_sync(&data->timer_100ms);
#ifndef CONFIG_MACH_APM_BASE
	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		if(gpio_is_valid(data->leddata[i].led->gpio))
			gpio_free(data->leddata[i].led->gpio);
	}
#endif
	for (i = 0; i < ARRAY_SIZE(buttons); i++){
		if (data->bdata[i].button->debounce_interval){
			del_timer_sync(&data->bdata[i].timer);
		}
		cancel_work_sync(&data->bdata[i].work);
#ifdef CONFIG_MACH_NC5
		nc5_button_control( &data->bdata[i] , DELETE );
#elif defined CONFIG_MACH_NT3
		nt3_button_control( &data->bdata[i] , DELETE );
#else
		free_irq(gpio_to_irq(data->bdata[i].button->gpio), &data->bdata[i]);
		gpio_free(data->bdata[i].button->gpio);
#endif
	}

	if (data->nashal_dev)
		device_unregister(data->nashal_dev);

	for (i = 0; i < ARRAY_SIZE(nashal_attr); i++)
		device_remove_file(&client->dev, &nashal_attr[i].dev_attr);

	i2c_set_clientdata(client, NULL);
	kfree(data);
	return 0;
}

void gpio_write_bit(int pin, int val)
{
/*        void __iomem* gpio = gpio0;

        if (val)
                out_be32((void *)(gpio + GPIO_OR),
                         in_be32((void *)(gpio + GPIO_OR)) | GPIO_VAL(pin));
        else
                out_be32((void *)(gpio + GPIO_OR),
                         in_be32((void *)(gpio + GPIO_OR)) & ~GPIO_VAL(pin));*/
}
int gpio_read_in_bit(int pin)
{
/*        int val = 0;
        void __iomem* gpio = gpio0;

        val = in_be32((void *)(gpio + GPIO_IR)) & GPIO_VAL(pin) ? 1 : 0;

        return val;*/
	return 1;
}


#ifdef CONFIG_MACH_APM_BASE
static void nashal_power_off(void)
{
	struct device_node* np;
	struct i2c_client* micom;
	np = of_find_compatible_node(NULL, NULL, "misc,micom");
	micom = of_find_i2c_device_by_node(np);

 dprintk("hal: Power off the board\n");
	//u8 out[8] = {MICOM_ID_SET2, MICOM_BUZZER, 0, 0, 0, 0, 0, 0};
	u8 out[8]={ 0x02, 0x97, 0x00, 0x0, 0x0, 0x0, 0x0, 0x0};
	// busswitch on -> detach_nor
	nashal_micom_write( micom, out, 8);
	out[0]=MICOM_ID_SET2; out[1]=MICOM_BUZZER; out[2]=1; out[3]=1;
	out[4]=4; out[5]=3; out[6]=2; out[7]=1;
	nashal_micom_write( micom, out, 8);
	udelay(1000);
	gpio_set_value( 224+15, 1);
	udelay(1000);
	gpio_set_value( 224+15, 0);

}
#endif
static int __init nashal_init(void)
{
  dprintk("%s is called\n", __FUNCTION__);

    int ret;
	struct device_node *np;
	nashal_class = class_create(THIS_MODULE, "nas");

	if (IS_ERR(nashal_class)) {
		dprintk(KERN_ERR "hal.c: couldn't create sysfs class\n");
		return PTR_ERR(nashal_class);
	}
	
#ifdef CONFIG_MACH_APM_BASE
	for_each_compatible_node(np, NULL, "amcc,hlds")
	{
			ret = 0;
	}

	if (ret != 0){
		return ret;
	}

	save_power_off = ppc_md.power_off; //omw
	ppc_md.power_off = nashal_power_off;
#endif

	return i2c_add_driver(&nashal_driver);
}

static void __exit nashal_exit(void)
{
  dprintk("%s is called\n", __FUNCTION__);


#ifdef CONFIG_MACH_APM_BASE
/*	if (gpio0)
	{
			iounmap(gpio0);
			release_mem_region(res_gpio0.start, res_gpio0.end + 1 - res_gpio0.start);
	}*/
#ifdef CONFIG_MACH_NT3
	if( usb_otg_reg ){

			iounmap( usb_otg_reg);
			release_mem_region(usb_res.start, usb_res.end + 1 - usb_res.start);
	}
#endif
	ppc_md.power_off = save_power_off; //omw
#endif
	i2c_del_driver(&nashal_driver);
	class_destroy(nashal_class);	
}

MODULE_AUTHOR("Wonbae, Joo <wonbae.joo@lge.com>");
MODULE_DESCRIPTION("LG Electronics LGNAS HAL driver");
MODULE_LICENSE("GPL");

module_init(nashal_init);
module_exit(nashal_exit);
