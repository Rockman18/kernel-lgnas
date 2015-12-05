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
#include <linux/io.h>
#include <linux/delay.h>
#include <asm/uaccess.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>

#include "model_info.h"
#include "hal.h"
#include "fsm.h"
#include "opcode.h"

#ifdef CONFIG_LGNAS_HAS_NO_MCU
#include "../mcu_emul/lgnas_if.h"
#include "../mcu_emul/lgnas_sym.h"
#define EMCU_VERSION "emcu v0.01"
extern void task_1000ms(struct nashal_data *data);
extern void task_200ms(struct nashal_data *data);
#endif

int lgnas_debug = 0;
module_param(lgnas_debug, int, S_IRUGO);
MODULE_PARM_DESC(lgnas_debug, "Debugging mode enabled or not");

/*
 * Functions declarations
 */
static int nashal_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int nashal_remove(struct i2c_client *client);

static void nashal_timer_led(unsigned long __data);

static int nashal_init_client(struct i2c_client *client);
#if (MINFO_MAX_TEMP > 0)
static int nashal_temp_read(struct i2c_client *client, u8 reg1, u8 reg2 );
#endif
#if (MINFO_MAX_FAN > 0)
static struct nashal_data *nashal_update_device_fan(struct i2c_client *client, 
								struct nashal_data *data);
#endif
#if (MINFO_MAX_TEMP > 0)
static struct nashal_data *nashal_update_device_temp(struct i2c_client *client, 
								struct nashal_data *data);
#endif
static struct nashal_data *nashal_update_device(struct device *dev);

/*
 * The Model Information 
 */
static char model_class[6];
static u32  model_type;
static struct model_info model = {
  .name = CONFIG_LGNAS_MODEL,
	.max = { 
	  MINFO_MAX_BAY,
	  MINFO_MAX_ODD,
	  MINFO_MAX_ESATA,
	  MINFO_MAX_LAN,
  	MINFO_MAX_FAN,
  	MINFO_MAX_LCD,
  	MINFO_MAX_LCD_STR,
  	MINFO_MAX_USB,
  	MINFO_MAX_TEMP,
  	MINFO_MAX_LED_HDD,
  	MINFO_MAX_LED_ODD,
  	MINFO_MAX_LED_USB,
  	MINFO_MAX_LED_POW,
  	MINFO_MAX_LED_LAN,
  	MINFO_MAX_LED_FAIL
	},
	.support = {
	  MINFO_SUPPORT_WIFI,
	  MINFO_SUPPORT_ACPOW,
	  MINFO_SUPPORT_WOL,
	  /*MINFO_SUPPORT_SCHPOW,*/
    false,
	  MINFO_SUPPORT_EXHDD,
	  MINFO_SUPPORT_EXODD,
	  MINFO_SUPPORT_BTNBACKUP,
    MINFO_SUPPORT_BATTERY,
    MINFO_SUPPORT_LCDICON,
    MINFO_SUPPORT_MEMCARD,
#if (MINFO_MAX_FAN > 0) 
    MINFO_SUPPORT_FANRPM,
#endif
		MINFO_SUPPORT_STREAMING,
		MINFO_SUPPORT_FAMILYCAST,
		MINFO_SUPPORT_TRANSCODE,
		MINFO_SUPPORT_DTCPIP,
		MINFO_SUPPORT_BUTTON_BURN,
		MINFO_SUPPORT_FANINFO,
	},
	.bay_path =  MINFO_PATH_BAY,
#if (MINFO_MAX_ODD > 0)
	.odd_path =  MINFO_PATH_ODD,
#endif
#if (MINFO_MAX_ESATA > 0)
	.esata_path =  MINFO_PATH_ESATA,
#endif
#if (MINFO_MAX_LCD > 0)
	.lcd_line =  MINFO_LCD_LINE,
#endif
#if (MINFO_MAX_TEMP > 0)
	.temp_channel =  MINFO_CHANNEL_TEMP,
#endif
	.fs = {
		MINFO_FS_DEFAULT,
		MINFO_FS_BLOCK_SIZE,
	},
	.mtu = {
		MINFO_MTU_MAX,
		MINFO_MTU_MIN,
	},
#if (MINFO_SUPPORT_EXHDD)
	.usb = {
		MINFO_USB_HOST_MOD_NAME,
		MINFO_USB_DEVICE_MOD_NAME,
	},
#endif
	.smb = {
		MINFO_SMB_SO_RCVBUF,
		MINFO_SMB_SO_SNDBUF,
		MINFO_SMB_NR_REQUESTS,
		MINFO_SMB_READ_AHEAD_KB,
	},
  .misc = {
    MINFO_BOOTLOADER, 
    MINFO_MICOM, 
    MINFO_BUZZER, 
  },
  .swap = {
  	MINFO_SWAP_MODE,
  },
};
/*
 * Model Type Function 
 */
#ifdef CONFIG_MACH_NS2
#define REG_EEPROM_CTRL			0x12C0
#define EEPROM_CTRL_DATA_HI_MASK	0xFFFF
#define EEPROM_CTRL_DATA_HI_SHIFT	0
#define EEPROM_CTRL_ADDR_MASK		0x3FF
#define EEPROM_CTRL_ADDR_SHIFT		16
#define EEPROM_CTRL_ACK			0x40000000
#define EEPROM_CTRL_RW			0x80000000
#define REG_EEPROM_DATA_LO		0x12C4
#define REG_OTP_CTRL			0x12F0
#define OTP_CTRL_CLK_EN			0x0002
#define AT_HW_ADDR (u8 __iomem *)(0xf8900000)
#define AT_READ_REG(reg,pdata) \
  readl(AT_HW_ADDR + reg); \
  *(u32 *)pdata = readl(AT_HW_ADDR + reg)

#define AT_WRITE_REG( reg, value) ( \
		writel((value), (AT_HW_ADDR + reg)))
                    
static bool get_eeprom(u32 offset,u32 *p_value)
{
	int i;
	int ret = false;
	u32 otp_ctrl_data;
	u32 control;
	u32 data;

	if (offset & 3)
		return ret; /* address do not align */

	AT_READ_REG( REG_OTP_CTRL, &otp_ctrl_data);
	if (!(otp_ctrl_data & OTP_CTRL_CLK_EN))
		AT_WRITE_REG( REG_OTP_CTRL,
				(otp_ctrl_data | OTP_CTRL_CLK_EN));

	AT_WRITE_REG( REG_EEPROM_DATA_LO, 0);
	control = (offset & EEPROM_CTRL_ADDR_MASK) << EEPROM_CTRL_ADDR_SHIFT;
	AT_WRITE_REG( REG_EEPROM_CTRL, control);

	for (i = 0; i < 30; i++) {
		udelay(2000);
		AT_READ_REG( REG_EEPROM_CTRL, &control);
		if (control & EEPROM_CTRL_RW)
			break;
	}
	if (control & EEPROM_CTRL_RW) {
		AT_READ_REG( REG_EEPROM_CTRL, &data);
		AT_READ_REG( REG_EEPROM_DATA_LO, p_value);
		data = data & 0xFFFF;
		*p_value = swab32((data << 16) | (*p_value >> 16));
		ret = true;
	}
	if (!(otp_ctrl_data & OTP_CTRL_CLK_EN))
		AT_WRITE_REG( REG_OTP_CTRL, otp_ctrl_data);

	return ret;
}

static bool set_eeprom( u32 offset, u32 value, u32 value1)
{
	int i;
	int ret = false;
	u32 otp_ctrl_data;
	u32 control;
	u32 data;

	if (offset & 3)
		return ret; /* address do not align */

	AT_READ_REG(REG_OTP_CTRL, &otp_ctrl_data);
	if (!(otp_ctrl_data & OTP_CTRL_CLK_EN))
		AT_WRITE_REG(REG_OTP_CTRL,
				(otp_ctrl_data | OTP_CTRL_CLK_EN));

  /* write lo data */
	AT_WRITE_REG(REG_EEPROM_DATA_LO, ((swab32(value)<<16)|(swab32(value1)>>16)));
  /* write high data and offset */
	control = (offset & EEPROM_CTRL_ADDR_MASK) << EEPROM_CTRL_ADDR_SHIFT;
  control |= (swab32(value)>>16);
  control |= EEPROM_CTRL_RW;
	AT_WRITE_REG(REG_EEPROM_CTRL, control);

	for (i = 0; i < 30; i++) {
		udelay(2000);
		AT_READ_REG(REG_EEPROM_CTRL, &control);
		if (!(control & EEPROM_CTRL_RW)){
      //printk("%s_w(i:0x%x)\n",__FUNCTION__,i);
			break;
    }
	}

	if (!(otp_ctrl_data & OTP_CTRL_CLK_EN))
		AT_WRITE_REG(REG_OTP_CTRL, otp_ctrl_data);

	udelay(2000);

  /* verification */
  if (   (get_eeprom(offset, &data)) 
      && (value == data) ){ 
    ret = true;
  }

	return ret;
}
static int read_eeprom(u32 offset, u8 *bytes, u8 len)
{
	u32 *eeprom_buff;
	int first_dword, last_dword;
	int ret_val = 0;
	int i;

	if (len == 0)
		return -EINVAL;

	first_dword = offset >> 2;
	last_dword = (offset + len - 1) >> 2;

	eeprom_buff = kmalloc(sizeof(u32) *
			(last_dword - first_dword + 1), GFP_KERNEL);
	if (eeprom_buff == NULL)
		return -ENOMEM;

	for (i = first_dword; i <= last_dword; i++) {
		if (!get_eeprom( i * 4, &(eeprom_buff[i-first_dword]))) {
			kfree(eeprom_buff);
			return -EIO;
		}
	}

	memcpy(bytes, (u8 *)eeprom_buff + (offset & 3), len);
	kfree(eeprom_buff);

	return ret_val;
}
#define AT_EEPROM_LEN 256
static int write_eeprom(u32 offset, u8 *bytes, u8 len)
{
	u32 *eeprom_buff;
	u32 *ptr;
	int first_dword, last_dword;
	int ret_val = 0;
	int i;

	if (len == 0)
		return -EOPNOTSUPP;

	first_dword = offset >> 2;
	last_dword = (offset + len - 1) >> 2;
	eeprom_buff = kmalloc(AT_EEPROM_LEN, GFP_KERNEL);
	if (eeprom_buff == NULL)
		return -ENOMEM;

	ptr = (u32 *)eeprom_buff;

	if (offset & 3) 
		ptr = (u32*)((u8 *)ptr + (offset & 3));
  /*
   * atheros eeprom hw can write 6byte at one time, so 
   * we have to prepare next dword
   */
	for (i = 0; i < last_dword - first_dword + 2; i++) {
		if (!get_eeprom((first_dword + i)* 4,&(eeprom_buff[i]))) {
			ret_val = -EIO;
			goto out;
		}
	}
	/* Device's eeprom is always little-endian, word addressable */
	memcpy((u8 *)ptr, bytes, len);

	for (i = 0; i < last_dword - first_dword + 1; i++) {
		if (!set_eeprom(((first_dword + i) * 4),
				  eeprom_buff[i],eeprom_buff[i+1])) {
			ret_val = -EIO;
			goto out;
		}
	}
out:
	kfree(eeprom_buff);
	return ret_val;
}
#endif
static int get_nas_class(char *str)
{
  unsigned int nas_class=0;

#ifndef CONFIG_MACH_NS2
  if(!get_option(&str, &model_type))
#else
  if(!get_eeprom(0x20, &model_type))
#endif
  {
    model_type=0;
    return 0;
  }

#ifdef CONFIG_MACH_NS2
  /*
   * old ns2 don't have a type code in eeprom
   */
  if(( model_type & 0xffff) == 0xffff){
    model_type &= 0xffff0000;
    model_type |= 0x0020;
  }
#endif

  dprintk ("%s() model_type:%x\n",__FUNCTION__,model_type);

  nas_class = model_type >> 16; 
  if(   ( nas_class & 0xff00)
     && ( nas_class & 0x00ff)
     && ((nas_class & 0xff00) != 0xff00)
     && ((nas_class & 0x00ff) != 0x00ff) ){
    sprintf(model_class, "_%04x",nas_class);
  }else{
	  memset(model_class, 0x00, 6);	
    model_type &= 0xffff;
  }
  return 0;
}
#ifndef CONFIG_MACH_NS2
early_param("nas_type", get_nas_class);
#endif

/*
 * The MICOM 
 */

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x58, I2C_CLIENT_END };

/*
 * The FAN
 */
#ifdef CONFIG_LGNAS_HAS_NO_MCU
/*****************************************************************************
 * LG NAS Proc FS
 ****************************************************************************/
static const u8 NASHAL_FAN_PWM[2]			= { 0xb0, 0xa0 };
#else
static const u8 NASHAL_FAN_PWM[2]			= { MICOM_PWM1, MICOM_PWM2 };
#endif
/*
 * The BUZZER
 */

/*
 * The LED
 */
#ifdef CONFIG_MACH_NS2
 #define GPIO_ICH9R_NS2_BASE		192
 #define GPIO_SIO_NS2_BASE			164
 #define GPIO_ODD1			GPIO_ICH9R_NS2_BASE + 57
 #define GPIO_HDD1			GPIO_ICH9R_NS2_BASE + 15
 #define GPIO_HDD2			GPIO_ICH9R_NS2_BASE + 25	
 #define GPIO_HDD3			GPIO_ICH9R_NS2_BASE + 26
 #define GPIO_HDD4			GPIO_ICH9R_NS2_BASE + 9
 #define GPIO_FAIL1	    GPIO_SIO_NS2_BASE + 13
 #define GPIO_FAIL2	    GPIO_SIO_NS2_BASE + 12	
 #define GPIO_FAIL3	    GPIO_SIO_NS2_BASE + 11
 #define GPIO_FAIL4	    GPIO_SIO_NS2_BASE + 10
#ifdef NS2EVT_BOARD
   #define ACTIVE_LOW 1
 #else
   #define ACTIVE_LOW 0
 #endif
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
		.active_low = ACTIVE_LOW,
	},
	{
		.state = 0,
		.gpio = GPIO_ODD1,
		.active_low = ACTIVE_LOW,
	},
	{
		.state = 0,
		.gpio = GPIO_FAIL1,
		.active_low = ACTIVE_LOW,
	},
	{
		.state = 0,
		.gpio = GPIO_FAIL2,
		.active_low = ACTIVE_LOW,
	},
	{
		.state = 0,
		.gpio = GPIO_FAIL3,
		.active_low = ACTIVE_LOW,
	},
	{
		.state = 0,
		.gpio = GPIO_FAIL4,
		.active_low = ACTIVE_LOW,
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
#if defined(CONFIG_MACH_NC1)
 #define GPIO_FAIL1		20
 #define GPIO_FAIL2		22
static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_FAIL1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_FAIL2,
		.active_low = 0,
	},
};
#endif
#ifdef CONFIG_MACH_NT3
 #define GPIO_ODD1	  11	
 #define GPIO_HDD1	  10
 #define GPIO_USB1		9
 #define GPIO_LAN1		ARCH_NR_GPIOS
 #define GPIO_POWER1	(ARCH_NR_GPIOS + 1)
static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_ODD1,
		.active_low = 0,
	},
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
#endif /* NT3 */
#if defined(CONFIG_MACH_NC5)
 #define GPIO_HDD1		10	
 #define GPIO_HDD2		9
 #define GPIO_ODD1		11
 #define GPIO_LAN1		ARCH_NR_GPIOS
 #define GPIO_FAIL1		17
 #define GPIO_FAIL2		18
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
		.gpio = GPIO_HDD2,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_LAN1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_FAIL1,
		.active_low = 0,
	},
	{
		.state = 0,
		.gpio = GPIO_FAIL2,
		.active_low = 0,
	},
};
#endif /* NC5 */
#ifdef CONFIG_MACH_NC3
 #define GPIO_HDD1	  10	
 #define GPIO_HDD2	  11
 #define GPIO_ODD1		9
 #define GPIO_POWER1	(ARCH_NR_GPIOS + 1)
static struct gpio_leds leds[] = {
	{
		.state = 0,
		.gpio = GPIO_HDD1,
		.active_low = 1,
	},
	{
		.state = 0,
		.gpio = GPIO_HDD2,
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

/*
 * The LCD 
 */

/*
 * The GPIO
 */

/*
 * The BUTTON
 */
#ifdef CONFIG_LGNAS_HAS_NO_MCU
struct nashal_data *ns2data;
static struct gpio_button buttons[] = {
	{
		.code = NS2_KEY_ODD_BACKUP_DATA,
		.desc = "backup data",
	},
	{
		.code = NS2_KEY_ODD_BACKUP_ISO,
		.desc = "backup iso",
	},
	{
		.code = NS2_KEY_ODD_BACKUP_CANCEL,
		.desc = "odd backup cancel",
	},
	{
		.code = NS2_KEY_ODD_BURN,
		.desc = "odd burn",
	},
	{
		.code = NS2_KEY_ODD_BURN_CANCEL,
		.desc = "odd burn cancel",
	},
	{
		.code = NS2_KEY_USB_BACKUP,
		.desc = "usb-onetoouch",
	},
	{
		.code = NS2_KEY_USB_BACKUP_CANCEL,
		.desc = "usb-onetoouch",
	},
	{
		.code = NS2_KEY_DHCP,
		.desc = "dhcp",
	},
	{
		.code = NS2_KEY_STATIC,
		.desc = "static",
	},
	{
		.code = NS2_KEY_EJECT,
		.desc = "eject",
	},
	{
		.code = NS2_KEY_ODD_IMAGE_GET,
		.desc = "odd image",
	},
	{
		.code = NS2_INFO_IP,
		.desc = "ip",
	},
	{
		.code = NS2_INFO_TIME,
		.desc = "time",
	},
	{
		.code = NS2_INFO_CAPA,
		.desc = "capa",
	},
	{
		.code = NS2_INFO_SYNC,
		.desc = "sync",
	},
	{
		.code = NS2_INFO_SVCCODE,
		.desc = "svccode",
	},
	{
		.code = NS2_INFO_PWDINIT,
		.desc = "pwdinit",
	},
	{
		.code = NS2_INFO_RSV,
		.desc = "reserve",
	},
};
#endif
#if defined(CONFIG_MACH_NC2) || defined(CONFIG_MACH_NC21)
static struct gpio_button buttons[] = {
	{
		.code = POWER_OFF,
		.gpio = 29,
		.desc = "power",
		.debounce_interval = 0,
		.type = PRIO_POWEROFF,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = BOTH_BACKUP,
		.gpio = 31,
		.desc = "backup",
		.debounce_interval = 0,
		.type = PRIO_DEFAULT,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = ODD_EJECT,
		.gpio = 47,
		.desc = "eject",
		.debounce_interval = 0,
		.type = PRIO_EJECT,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
};
#endif
#if defined(CONFIG_MACH_NT1)
static struct gpio_button buttons[] = {
	{
		.code = POWER_OFF,
		.gpio = 29,
		.desc = "power",
		.debounce_interval = 0,
		.type = PRIO_POWEROFF,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = BOTH_BACKUP,
		.gpio = 31,
		.desc = "backup",
		.debounce_interval = 0,
		.type = PRIO_DEFAULT,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = EXT_HDD,
		.gpio = 11,
		.desc = "exthdd",
		.debounce_interval = 1000,
		.type = PRIO_SWITCH,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = EXT_ODD,
		.gpio = 16,
		.desc = "extodd",
		.debounce_interval = 1000,
		.type = PRIO_SWITCH,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
};
#endif
#if defined(CONFIG_MACH_NT11)
static struct gpio_button buttons[] = {
	{
		.code = POWER_OFF,
		.gpio = 29,
		.desc = "power",
		.debounce_interval = 0,
		.type = PRIO_POWEROFF,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = BOTH_BACKUP,
		.gpio = 31,
		.desc = "backup",
		.debounce_interval = 0,
		.type = PRIO_DEFAULT,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = EXT_HDD,
		.gpio = 11,
		.desc = "exthdd",
		.debounce_interval = 1000,
		.type = PRIO_SWITCH,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = EXT_ODD,
		.gpio = 16,
		.desc = "nas",
		.debounce_interval = 1000,
		.active_low = 1,
		.type = PRIO_SWITCH,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
};
#endif
#if defined(CONFIG_MACH_NC1)
#define POWER		29
#define SELECT	30
#define CANCEL	31
#define MODE		32
static struct gpio_button buttons[] = {
	{
		.code = KEY_POWER,
		.gpio = POWER,
		.desc = "power",
		.debounce_interval = 0,
		.type = PRIO_HIGHEST,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = KEY_SETUP,
		.gpio = SELECT,
		.desc = "setup",
		.debounce_interval = 10,
		.type = PRIO_HIGHEST,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = KEY_RIGHT,
		.gpio = MODE,
		.desc = "right",
		.debounce_interval = 10,
		.type = PRIO_HIGHEST,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
	{
		.code = KEY_LEFT,
		.gpio = CANCEL,
		.desc = "left",
		.debounce_interval = 10,
		.type = PRIO_HIGHEST,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
};
#endif
#ifdef CONFIG_MACH_MM1
static struct gpio_button buttons[] = {
	{
		.code = POWER_OFF,
		.gpio = 12,
		.desc = "power1",
		.debounce_interval = 3000,
		.type = PRIO_POWEROFF,
		.trigger = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	},
};
#endif
#ifdef CONFIG_MACH_NT3
static struct gpio_button buttons[] = {
	{
		.code = POWER_OFF,
		.gpio = 21,
		.desc = "power",
		.debounce_interval = 0,
		.type = PRIO_POWEROFF,
		.trigger = IRQF_TRIGGER_FALLING,
	},
	{
		.code = BOTH_BACKUP,
		.gpio = 23,
		.desc = "backup",
		.debounce_interval = 0,
		.type = PRIO_DEFAULT,
		.trigger = IRQF_TRIGGER_FALLING,
	},
	{
		.code = EXT_HDD,
		.gpio = 20,
		.desc = "exthdd",
		.debounce_interval = 1000,
		.active_low = 1,
		.type = PRIO_SWITCH,
		.trigger = IRQF_TRIGGER_RISING,
	},
};
#endif
#ifdef CONFIG_MACH_NC5
#define POWER		19
#define SELECT	21
#define CANCEL	23
#define MODE		20
static struct gpio_button buttons[] = {
	{
		.code = KEY_POWER,
		.gpio = POWER,
		.desc = "power",
		.debounce_interval = 0,
		.type = PRIO_HIGHEST,
		.trigger = IRQF_TRIGGER_FALLING,
	},
	{
		.code = KEY_SETUP,
		.gpio = SELECT,
		.desc = "setup",
		.debounce_interval = 10,
		.type = PRIO_HIGHEST,
		.trigger = IRQF_TRIGGER_FALLING,
	},
	{
		.code = KEY_RIGHT,
		.gpio = MODE,
		.desc = "right",
		.debounce_interval = 10,
		.type = PRIO_HIGHEST,
		.trigger = IRQF_TRIGGER_FALLING,
	},
	{
		.code = KEY_LEFT,
		.gpio = CANCEL,
		.desc = "left",
		.debounce_interval = 10,
		.type = PRIO_HIGHEST,
		.trigger = IRQF_TRIGGER_FALLING,
	},
};
#endif
#ifdef CONFIG_MACH_NC3
static struct gpio_button buttons[] = {
	{
		.code = POWER_OFF,
		.gpio = 21,
		.desc = "power",
		.debounce_interval = 0,
		.type = PRIO_POWEROFF,
		.trigger = IRQF_TRIGGER_FALLING,
	},
	{
		.code = BOTH_BACKUP,
		.gpio = 23,
		.desc = "backup",
		.debounce_interval = 0,
		.type = PRIO_DEFAULT,
		.trigger = IRQF_TRIGGER_FALLING,
	},
};
#endif
/*
 * LED Function
 */
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
  return;
}

void (*gpio_set_blink)(unsigned pin, int blink);
static void scsi_led_act_func(unsigned int dev_no, int act)
{
	if(dev_no < MINFO_MAX_LED_HDD + MINFO_MAX_LED_ODD) {
		struct gpio_leds *led = &leds[dev_no];
		gpio_set_value(led->gpio, led->state ^ led->active_low ^ act);
	}
}
static void usb_led_act_func(int act)
{
#if (MINFO_MAX_LED_USB > 0)
	struct gpio_leds *led = &leds[LED_USB1_INDEX];
	gpio_set_value(led->gpio, led->state ^ led->active_low ^ act);
#endif
}
extern void (*scsi_led_act)(unsigned int dev_no, int act);
static void scsi_led_act_set(void (*scsi_led_act_fn)(unsigned int dev_no, int act))
{
	scsi_led_act = scsi_led_act_fn;
}
extern void (*usb_led_act)(int act);
static void usb_led_act_set(void (*usb_led_act_fn)(int act))
{
	usb_led_act = usb_led_act_fn;
}
static void scsi_led_blink_set(struct nashal_data *data, u8 delay)
{
#if (MINFO_MAX_LED_HDD > 0)
	data->leddata[LED_HDD1_INDEX].led->delay = delay;
#endif
#if (MINFO_MAX_LED_HDD > 1)
	data->leddata[LED_HDD2_INDEX].led->delay = delay;
#endif
#if (MINFO_MAX_LED_HDD > 2)
	data->leddata[LED_HDD3_INDEX].led->delay = delay;
#endif
#if (MINFO_MAX_LED_HDD > 3)
	data->leddata[LED_HDD4_INDEX].led->delay = delay;
#endif
#if (MINFO_MAX_LED_HDD > 0)
	mod_timer(&data->timer_100ms, jiffies + msecs_to_jiffies(100));
#endif
}

static void __led_lock(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
  int i;
  
  del_timer_sync(&data->timer_100ms);

	scsi_led_act_set(NULL);
	usb_led_act_set(NULL);

	for(i = 0; i < ARRAY_SIZE(leds); i++) {
		struct led_data *leddata = &data->leddata[i];
		if(gpio_is_valid(leddata->led->gpio))
			gpio_set_value(leddata->led->gpio, 0 ^ leddata->led->active_low);
		else
			led_set_value(client, leddata->led->gpio, 0 ^ leddata->led->active_low);
	}
}

static void __led_unlock(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
  int i;

	for(i = 0; i < ARRAY_SIZE(leds); i++) {
		struct led_data *leddata = &data->leddata[i];
		if(gpio_is_valid(leddata->led->gpio))
			gpio_set_value(leddata->led->gpio,
				leddata->led->state ^ leddata->led->active_low);
		else
			led_set_value(client, leddata->led->gpio,
				leddata->led->state ^ leddata->led->active_low);
	}
	
	scsi_led_act_set(scsi_led_act_func);
	usb_led_act_set(usb_led_act_func);
	
	mod_timer(&data->timer_100ms, jiffies + msecs_to_jiffies(100));
}

#ifdef CONFIG_PM
static void __led_suspend(struct i2c_client *client)
{
	__led_lock(client);
}

static void __led_resume(struct i2c_client *client)
{
	__led_unlock(client);
}

#if (MINFO_MAX_FAN > 0)
static void __fan_suspend(struct i2c_client *client)
{
	u8 out[8] = {0};
	int i;
	
	out[0] = MICOM_ID_SET1;
	for(i = 0; i < MINFO_MAX_FAN; i++) {
		switch(i) {
			case 0: out[1] = MICOM_PWM1; break;
			case 1: out[1] = MICOM_PWM2; break;
			default: return;
		}
		out[3] = 0;
		nashal_micom_write(client, out, 8);
	}
}

static void __fan_resume(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
	u8 out[8] = {0};
	int i;
	
	out[0] = MICOM_ID_SET1;
	for(i = 0; i < MINFO_MAX_FAN; i++) {
		switch(i) {
			case 0: out[1] = MICOM_PWM1; break;
			case 1: out[1] = MICOM_PWM2; break;
			default: return;
		}
		out[3] = data->fan[i].pwm;
		nashal_micom_write(client, out, 8);
	}
}
#endif

static int nashal_suspend(struct i2c_client *client, pm_message_t state)
{
	__led_suspend(client);
#if (MINFO_MAX_FAN > 0)
	__fan_suspend(client);
#endif
	return 0;
}

static int nashal_resume(struct i2c_client *client)
{
	__led_resume(client);
#if (MINFO_MAX_FAN > 0)
	__fan_resume(client);
#endif
	return 0;
}
#else
#define nashal_suspend NULL
#define nashal_resume NULL
#endif

/*
 * The SENSOR(TMP431)
 */
#ifdef CONFIG_MACH_NS2
static const u8 SENSOR_TEMP_MSB[2]			= { 0x72, 0x74 };
static const u8 SENSOR_TEMP_LSB[2]			= { 0x00, 0x00 };
#else
#define SENSOR_I2C_ADDR	 0x4d
static const u8 SENSOR_TEMP_MSB[2]			= { 0x00, 0x01 };
static const u8 SENSOR_TEMP_LSB[2]			= { 0x15, 0x10 };
#endif

/*
 * Driver data (common to all clients)
 */

struct class *nashal_class = 0;
static const struct i2c_device_id nashal_id[] = {
	{ "io-micom", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nashal_id);

static struct i2c_driver nashal_driver = {
	.driver = {
		.name	= "iomicom",
	},
	.id_table	= nashal_id,
	.probe		= nashal_probe,
	.remove		= nashal_remove,
	.suspend	= nashal_suspend,
	.resume		= nashal_resume,
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

void button_event(struct work_struct *work)
{
	struct button_data *bdata =
		container_of(work, struct button_data, work);
	struct nashal_data *data =
		container_of(bdata, struct nashal_data, bdata[bdata->index]);

	struct gpio_button *button = bdata->button;
	unsigned int type = button->type ?: EV_KEY;
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	int state = 0;
#else
	int state = (gpio_get_value(button->gpio) ? 1 : 0) ^ button->active_low;
#endif

	struct input_event event;
	unsigned long flags;
#if (MINFO_MAX_LCD > 0)
#ifndef CONFIG_LGNAS_HAS_NO_MCU
	int disable_fsm = data->lock.micom;
#endif
#endif

  dprintk("%s is called\n", __FUNCTION__);

	if(data->lock.button)
		return;

  if(bdata->priority < data->priority.button)
  	return;

#if (MINFO_MAX_LCD > 0)
#ifndef CONFIG_LGNAS_HAS_NO_MCU
  if(!state &&
  	 !disable_fsm &&
  	 !fsm_comm_proc(data->client, button->code, data->priority.button))
  	return;
#endif
#endif

	spin_lock_irqsave(&data->buffer_lock, flags);

	do_gettimeofday(&event.time);
	event.type = type;
	event.code = button->code;
#if (MINFO_MAX_LCD > 0)
#ifndef CONFIG_LGNAS_HAS_NO_MCU
	if(!disable_fsm)
		event.code = fsm_get_mode();
#endif
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
#ifndef CONFIG_LGNAS_HAS_NO_MCU
static irqreturn_t button_isr(int irq, void *dev_id)
{
	struct button_data *bdata = dev_id;
	struct gpio_button *button = bdata->button;
	struct nashal_data *data =
		container_of(bdata, struct nashal_data, bdata[bdata->index]);

	if(data->lock.button)
		return IRQ_NONE;
	
  dprintk("%s is called\n", __FUNCTION__);
	if( irq != gpio_to_irq(button->gpio))
		return IRQ_NONE;

	if (button->debounce_interval)
		mod_timer(&bdata->timer,
			jiffies + msecs_to_jiffies(button->debounce_interval));
	else
		schedule_work(&bdata->work);

	return IRQ_HANDLED;
}
#endif

/*
 * Sysfs attr show / store functions
 */

static ssize_t show_micom(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int index = to_nashal_dev_attr(devattr)->index;
#if (MINFO_MAX_LCD > 0)
	struct nashal_data *data = i2c_get_clientdata(client);
  int i, j=0;
#endif
	u8 in[8];
  u32 temp;
	
	switch(index)
	{
	case 0:
		if(nashal_micom_read(client, in, 8))
			return sprintf(buf, "%02x %02x %02x %02x %02x %02x %02x %02x\n",
										in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7]);
		break;
	case 1:
#ifdef CONFIG_LGNAS_HAS_NO_MCU
		return sprintf(buf, "%s\n", EMCU_VERSION);
#else
		if(nashal_micom_reg_read(client, MICOM_VER, in, 8))
			return snprintf(buf, 8, "%s\n", in);
#endif
		break;
#if (MINFO_MAX_LCD > 0)
	case 2:
		if(nashal_micom_reg_read(client, 0xff, in, 8))
			return sprintf(buf, "%d\n", in[1]);
		break;
	case 3:
		i = sprintf(buf, "%x %x ",data->mdata.file_num, data->mdata.file_total);

    for(j=0; j<2048; j++){
      if((*(data->mdata.filename+j) == '\0') || (*(data->mdata.filename+j) == '\n'))
        break;
      *(buf+i+j) = *(data->mdata.filename+j); 
    }
    i += j;
    i += sprintf(buf+i, "\n");
    return i;
		break;
	case 4:
		return sprintf(buf, "%s\n", data->mdata.hostname);
		break;
	case 5:
		return sprintf(buf, "%s\n", data->mdata.fwversion);
		break;
#endif
#ifdef CONFIG_LGNAS_HAS_NO_MCU
  /* nas_type */
	case 6:
    if(!get_eeprom(0x20, &temp))
		  return -EINVAL;
		return sprintf(buf, "0x%08x\n", temp);
		break;
  /* mac address */
	case 7:
    if( (read_eeprom(0xa, &in[0],2) < 0) ||
        (read_eeprom(0x2, &in[2],4) < 0))
		  return -EFAULT;
    return sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n", 
             in[0],in[1],in[2],in[3],in[4],in[5]);
		break;
#endif
	default:
		return -EINVAL;
	}

 	return -EFAULT;
}

#if (MINFO_MAX_LCD > 0)
static ssize_t show_address(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	return fsm_show_address(buf);
}
#endif

#if 0
#include <scsi/scsi_transport.h>

struct odd_path {
	int len;
	int num;
	char *buf;
};

extern struct bus_type scsi_bus_type;

static int print_odd_path(struct device *dev, void *data)
{
	struct scsi_device *sdev;
	struct odd_path *op = (struct odd_path *) data;

	if (!scsi_is_sdev_device(dev))
		goto out;

	sdev = to_scsi_device(dev);
	if(!strncmp(scsi_device_type(sdev->type), "CD-ROM", 6)) {
		op->len += sprintf(op->buf + op->len, "odd%d_path=%d:%d:%d:%d\n",
			op->num + 1, sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
		op->num++;
	}
	
out:
	return 0;
}

static int odd_path_show(char *p)
{
	struct odd_path op;
	
	op.len = 0;
	op.num = 0;
	op.buf = p;

	bus_for_each_dev(&scsi_bus_type, NULL, &op, print_odd_path);

	return op.len;
}
#endif
#include <linux/mtd/mtd.h>
#include "../../mtd/mtdcore.h"

static int get_dtcpip_partition(void)
{
  int index = -1;
#ifndef CONFIG_MACH_NS2
  struct mtd_info *mtd;
  mtd_for_each_device(mtd) {
    if(!strncmp(mtd->name, "extra", 5) || !strncmp(mtd->name, "back-up", 7)) {
      index = mtd->size > 0? mtd->index : -1;
      break;
    }
  }
#endif
  return index;
}
int display_model(char * buf)
{
  int i, count=0;
  count += sprintf(buf+count, "model_type=0x%08x\n", model_type);
  count += sprintf(buf+count, "model_name=%s%s\n", model.name,model_class);
  count += sprintf(buf+count, "bay_max=%d\n", model.max.bay);
  count += sprintf(buf+count, "odd_max=%d\n", model.max.odd);
  count += sprintf(buf+count, "esata_max=%d\n", model.max.esata);
  count += sprintf(buf+count, "usb_max=%d\n", model.max.usb);
  count += sprintf(buf+count, "lan_max=%d\n", model.max.lan);
  count += sprintf(buf+count, "fan_max=%d\n", model.max.fan);
  count += sprintf(buf+count, "lcd_max=%d\n", model.max.lcd);
  count += sprintf(buf+count, "lcd_str_max=%d\n", model.max.lcd_str);
  count += sprintf(buf+count, "temp_max=%d\n", model.max.temp);
  count += sprintf(buf+count, "led_hdd_max=%d\n", model.max.led_hdd);
  count += sprintf(buf+count, "led_odd_max=%d\n", model.max.led_odd);
  count += sprintf(buf+count, "led_usb_max=%d\n", model.max.led_usb);
  count += sprintf(buf+count, "led_lan_max=%d\n", model.max.led_lan);
  count += sprintf(buf+count, "led_power_max=%d\n", model.max.led_power);
  count += sprintf(buf+count, "led_fail_max=%d\n", model.max.led_fail);
  count += sprintf(buf+count, "bootloader=%s\n", model.misc.bootloader);
  count += sprintf(buf+count, "micom=%s\n", model.misc.micom);
  count += sprintf(buf+count, "buzzer=%s\n", model.misc.buzzer);
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
	count += sprintf(buf+count, "title_streaming=");
	if(model.support.streaming)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "familycast_support=");
	if(model.support.familycast)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "transcode_support=");
	if(model.support.transcode)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "dtcpip_support=");
	if(model.support.dtcpip)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "button_burn=");
	if(model.support.button_burn)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");
	count += sprintf(buf+count, "faninfo_support=");
	if(model.support.faninfo)
		count += sprintf(buf+count, "yes\n");
	else
		count += sprintf(buf+count, "no\n");

  for(i=0; i<model.max.bay; i++){
    count += sprintf(buf+count, "bay%d_path=%s\n", i+1, model.bay_path[i]);
	}
#if 1
#if (MINFO_MAX_ODD > 0)
  for(i=0; i<model.max.odd; i++){
    count += sprintf(buf+count, "odd%d_path=%s\n", i+1, model.odd_path[i]);
	}
#endif
#else
	count += odd_path_show(buf + count);
#endif
#if (MINFO_MAX_ESATA > 0)
  for(i=0; i<model.max.esata; i++){
    count += sprintf(buf+count, "esata%d_path=%s\n", i+1, model.esata_path[i]);
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
	count += sprintf(buf+count, "fs_default=%s\n", model.fs.dflt);
	count += sprintf(buf+count, "fs_block_size=%d\n", model.fs.block_size);
	count += sprintf(buf+count, "mtu_max=%d\n", model.mtu.max);
	count += sprintf(buf+count, "mtu_min=%d\n", model.mtu.min);
#if (MINFO_SUPPORT_EXHDD)
	count += sprintf(buf+count, "usb_host_mod_name=%s\n", model.usb.host_mod_name);
	count += sprintf(buf+count, "usb_device_mod_name=%s\n", model.usb.device_mod_name);
#endif
	if(model.smb.so_rcvbuf)
		count += sprintf(buf+count, "SO_RCVBUF=%d\n", model.smb.so_rcvbuf);
	if(model.smb.so_sndbuf)
		count += sprintf(buf+count, "SO_SNDBUF=%d\n", model.smb.so_sndbuf);
	if(model.smb.nr_requests)
		count += sprintf(buf+count, "nr_requests=%d\n", model.smb.nr_requests);
	if(model.smb.read_ahead_kb)
		count += sprintf(buf+count, "read_ahead_kb=%d\n", model.smb.read_ahead_kb);
	count += sprintf(buf+count, "swap_mode=%s\n", model.swap.mode);
	if(model.support.dtcpip && get_dtcpip_partition() >= 0)
		count += sprintf(buf+count, "dtcpip_partition=%d\n", get_dtcpip_partition());
	return count;
}

static ssize_t show_sysinfo(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
	int index = to_nashal_dev_attr(devattr)->index;
#ifndef CONFIG_LGNAS_HAS_NO_MCU
	u8 in[8];
#endif

	switch(index)
	{
	/* model info */
	case 0:
		return display_model( buf );
	/* wol */
	case 1:
#if (MINFO_SUPPORT_WOL)
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    data->sysinfo.wol = (cmos_control(CMOS_ADD_WOL, 0, 0) == 1)? 1:0;
#endif
    return sprintf(buf, "%d\n",(data->sysinfo.wol) ?  1:0);
#else
		return sprintf(buf, "Not supported\n");
#endif
    break;
	/* acpowerloss */
	case 2:
#if (MINFO_SUPPORT_ACPOW)
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    data->sysinfo.powerloss = (cmos_control(CMOS_ADD_POWER, 0, 0) == 1)? 1:0;
#else
		if(nashal_micom_reg_read(client, MICOM_POR, in, 8))
      data->sysinfo.powerloss = (in[4]) ? 1:0;
#endif
		return sprintf(buf, "%d\n",(data->sysinfo.powerloss) ? 1:0);
#endif
		return sprintf(buf, "Not supported\n");
	/* scheduled_power */
	case 3:
#if (MINFO_SUPPORT_SCHPOW)
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    data->sysinfo.sch_power = (cmos_control(CMOS_ADD_RTC, 0, 0) == 1)? 1:0;
		return sprintf(buf, "%d\n",(data->sysinfo.sch_power)? 1:0);
#endif
#else
		return sprintf(buf, "Not supported\n");
#endif
		break;
	case 5:
#if (MINFO_SUPPORT_SCHPOW)
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    data->sysinfo.spdata.date=cmos_read_byte(CMOS_ADD_RTC_DATE);
    data->sysinfo.spdata.hour=cmos_read_byte(CMOS_ADD_RTC_HOUR);
    data->sysinfo.spdata.minute=cmos_read_byte(CMOS_ADD_RTC_MIN);
    data->sysinfo.spdata.second=cmos_read_byte(CMOS_ADD_RTC_SEC);
		return sprintf(buf, "%d %d:%d:%d\n",
      data->sysinfo.spdata.date,
      data->sysinfo.spdata.hour,
      data->sysinfo.spdata.minute,
      data->sysinfo.spdata.second);
#endif
#else
		return sprintf(buf, "Not supported\n");
#endif
		break;
	case 4:
		return sprintf(buf, "%d\n", lgnas_debug);
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
	struct nashal_data *data = nashal_update_device(dev);
	int val;

	switch(index){
	case 0:
	  val = data->fan[nr].pwm;
    break;
	case 1:
	  val = data->fan[nr].rpm;
    break;
	case 2:
	  val = data->fan[nr].pwm_max;
    break;
	case 3:
	  val = data->fan[nr].pwm_mid;
    break;
	case 4:
	  val = data->fan[nr].pwm_min;
    break;
	case 5:
	  val = data->fan[nr].temp_high;
    break;
	case 6:
	  val = data->fan[nr].temp_low;
    break;
  default:
		val = 0;
    break;
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
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	int index = to_nashal_dev_attr(devattr)->index;
	int nr = to_nashal_dev_attr(devattr)->nr;
	int val;

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
  	status = sprintf(buf, "%d %d\n",
  		data->lcd[nr].brightness[0], data->lcd[nr].brightness[1]);
		mutex_unlock(&data->update_lock);
		return status;
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
	case 6:
		status = -EINVAL;
#if (MINFO_MAX_LCD_LINE > 0)
		status = sprintf(buf, "%s\n", data->lcd[nr].greet_msg);
#endif
		mutex_unlock(&data->update_lock);
		return status;
	case 7:
		status = -EINVAL;
#if (MINFO_MAX_LCD_LINE > 1)
		status = sprintf(buf, "%s\n", data->lcd[nr].str[1]);
#endif
		mutex_unlock(&data->update_lock);
		return status;
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
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	struct nashal_data *data = ns2data;
#else
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
#endif
	struct input_event event;
	int index = to_nashal_dev_attr(devattr)->index;
  int ret;
  
  switch(index)
  {
  case 0:
  	ret = wait_event_interruptible(data->wait, data->head != data->tail);
  	if (ret)
  		return ret;
  	
  	while ( nashal_fetch_next_event(data, &event)) {
  		memcpy(buf + ret, &event, sizeof(struct input_event));
  		ret += sizeof(struct input_event);
  	}
  	//return sprintf(buf, "code = %d, size = %d byte\n", event.code, ret);
  	return ret;
  case 1:
  	mutex_lock(&data->update_lock);
  	ret = data->priority.button;
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
  case 3:
  	status = data->lock.lcd;
  	break;
  case 4:
  	status = data->lock.activity;
  	break;
  case 5:
  	status = data->lock.micom;
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
			case EXT_HDD:
				status[0] = (gpio_get_value(bdata->button->gpio)? 1 : 0) ^
					bdata->button->active_low;
				break;
			case EXT_ODD:
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
	if(status[0] > 0 && status[1] < 0)
		return sprintf(buf, "%s\n", "nas");
	if(status[0] == 0 && status[1] < 0)
		return sprintf(buf, "%s\n", "xhdd");
	
	return -EFAULT;
}
#endif

static ssize_t store_micom(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	struct nashal_data *data = i2c_get_clientdata(client);
#endif
	int index = to_nashal_dev_attr(devattr)->index;
	u8 out[19];
	char *p;
	long val, idx = 0;
	
	memset(out, 0x00, 19);	
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
#if (MINFO_MAX_LCD > 0)
	case 2:
		if(strict_strtol(buf, 10, &val))
			return -EINVAL;
		if(fsm_set_mode(client, val))
			return count;
		break;
  /* filename */
	case 3:
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    set_message1(data, 0xd0, buf, count);
    return count;
#endif
		break;
  /* hostname */
	case 4:
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    set_hostname(data, buf, count);
    return count;
#endif
		break;
  /* fwversion */
	case 5:
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    set_fwversion(data, buf, count);
    return count;
#endif
		break;
#endif
#ifdef CONFIG_LGNAS_HAS_NO_MCU
  /* nastype */
	case 6:
	  if (strict_strtol(buf, 16, &val))
		  return -EINVAL;
    out[0] = (u8)((val >>  0) & 0xff);
    out[1] = (u8)((val >>  8) & 0xff);
    out[2] = (u8)((val >> 16) & 0xff);
    out[3] = (u8)((val >> 24) & 0xff);
    if(write_eeprom(0x20, out, 4) < 0)
	    return -EFAULT;
    (void)get_nas_class(NULL);
    return count;
		break;
  /* mac address*/
	case 7:
		while((p = strsep((char **) &buf, ":")) != NULL) {
			if(strict_strtol(p, 16, &val))
				return -EINVAL;
			if(idx < 19) {
		    out[idx] = val;
				idx++;
			}
		}
    if( (write_eeprom(0xa, &out[0], 2) < 0) ||
        (write_eeprom(0x2, &out[2], 4) < 0))
	    return -EFAULT;
    return count;
		break;
#endif
	default:
		return -EINVAL;
	}

	return -EFAULT;
}

#if (MINFO_MAX_LCD > 0)
static ssize_t store_address(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	if(fsm_store_address(buf))
		return count;
	return -EINVAL;
}
#endif

static ssize_t store_sysinfo(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
	int index = to_nashal_dev_attr(devattr)->index;
	long val;

	if (strict_strtol(buf, 10, &val) && (index != 5))
		return -EINVAL;

	switch(index)
	{
	case 1:
    {
#if (MINFO_SUPPORT_WOL)
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    if(cmos_control(CMOS_ADD_WOL, 1, (val)? 1:0))
#else
	  u8 out[8] = {MICOM_ID_SET1, 0x53, (u8)val, 0, 0, 0, 0, 0};
		if (!nashal_micom_write(client, out, 8))
#endif
			return -EFAULT;
    printk("Setting WOL config of MICOM or BIOS is ok! But you must set NIC tool. with ethtool\n");
		mutex_lock(&data->update_lock);
    data->sysinfo.wol= (val)? 1:0;
		mutex_unlock(&data->update_lock);
#else
		return -EFAULT;
#endif
    }
		break;
	case 2:
    {
#if (MINFO_SUPPORT_ACPOW)
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    if(cmos_control(CMOS_ADD_POWER, 1, (val)? 2:0))
#else
	  u8 out[8] = {MICOM_ID_SET1, MICOM_POR, 0, (u8)val, 0, 0, 0, 0};
		if(!nashal_micom_write(client, out, 8))
#endif
			return -EFAULT;
		mutex_lock(&data->update_lock);
    data->sysinfo.powerloss= (val)? 1:0;
		mutex_unlock(&data->update_lock);
#else
		return -EFAULT;
#endif
    }
		break;
	case 3:
#if (MINFO_SUPPORT_SCHPOW)
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    if(cmos_control(CMOS_ADD_RTC, 1, (val)? 1:0))
      return -EFAULT;
		mutex_lock(&data->update_lock);
    data->sysinfo.sch_power= (val)? 1:0;
		mutex_unlock(&data->update_lock);
#endif
#else
		return -EFAULT;
#endif
		break;
	case 5:
    {
#if (MINFO_SUPPORT_SCHPOW)
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    int date,hour,minute,second;
    sscanf(buf, "%d %d:%d:%d\n",&date,&hour,&minute,&second);
    if(cmos_control_rtc(1,date,hour,minute,second))
		  return -EFAULT;
#endif
#else
		return -EFAULT;
#endif
    }
		break;
	case 4:
		lgnas_debug = val;
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
	struct nashal_data *data = i2c_get_clientdata(client);
	int nr = to_nashal_dev_attr(devattr)->nr;
	int index = to_nashal_dev_attr(devattr)->index;
	long val;
	int ret = 1;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;
	
  mutex_lock(&data->update_lock);
	switch(index){
	case 0:
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    SHWMOutB((u8)val, NASHAL_FAN_PWM[nr] + 3);
#else
	  data->buf[0] = MICOM_ID_SET1;
    data->buf[1] = NASHAL_FAN_PWM[nr];
	  data->buf[3] = val;
    /* i2c transfer */
    ret = nashal_micom_write(client, data->buf, 8);
#endif
	  if(!ret) 
      ret = -EFAULT;
    else
      data->fan[nr].pwm = val;
    break;
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	case 2:
    SHWMOutB((u8)val, 0xaa); 
    data->fan[nr].pwm_max = val;
    break;
	case 3:
    SHWMOutB((u8)val, 0xab); 
    data->fan[nr].pwm_mid = val;
    break;
	case 4:
    SHWMOutB((u8)val, 0xae); 
    data->fan[nr].pwm_min = val;
    break;
	case 5:
    SHWMOutB((u8)val, 0xa6); 
    data->fan[nr].temp_high = val;
    break;
	case 6:
    SHWMOutB((u8)val, 0xa9); 
    data->fan[nr].temp_low = val;
    break;
#endif
	default:
		ret =  -EINVAL;
    break;
	}
	mutex_unlock(&data->update_lock);
	return (ret < 0)? ret : count;
}
#endif

/*
 *  0 : led off, 
 *  1 : led on 
 *  2 ~ 255 : timer delay * 100ms timer
 */
static ssize_t store_led(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nashal_data *data = i2c_get_clientdata(client);
	int index = to_nashal_dev_attr(devattr)->index;
	int nr = to_nashal_dev_attr(devattr)->nr;
	struct gpio_leds *led = data->leddata[nr].led;
	long val;
	int lock;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	
 	switch (index){
	case 0: 
		led->state = val ? 1: 0;
		lock = data->lock.led;
		if(lock) {
			mutex_unlock(&data->update_lock);
			return count;
		}
		if(gpio_is_valid(led->gpio))
			gpio_set_value(led->gpio, (int)(led->state ^ led->active_low));
		else
			led_set_value(client, led->gpio, (int)(led->state ^ led->active_low));
		break;
	case 1: 
		led->delay = val;
		lock = data->lock.led;
		if(lock) {
			mutex_unlock(&data->update_lock);
			return count;
		}
		if(!val) {
			if(gpio_is_valid(led->gpio))
				gpio_set_value(led->gpio, (int)(led->state ^ led->active_low));
			else
				led_set_value(client, led->gpio, (int)(led->state ^ led->active_low));
		}
		mod_timer(&data->timer_100ms, jiffies + msecs_to_jiffies(100));
		break;
	default:
		return -EINVAL;
	}

	if(!data->boot_complete)
	{
		data->boot_complete = 1;
		scsi_led_act_set(scsi_led_act_func);
	}

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
		if(nashal_micom_write(client, out, 8)) {
			msleep(MICOM_WAIT_TIME);
			return count;
		}
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
	u8 out[MINFO_MAX_LCD_STR + 6] = {0};
	long val;
	int lock, len, ret = count;
	char *p;

	mutex_lock(&data->update_lock);
	lock = data->lock.lcd;
	if(lock)
		goto __exit__;

	switch(index) {
	case 0:
		p = (char *) buf;
    out[2] = simple_strtoul(p, &p, 0);
    out[3] = simple_strtoul(p + 1 , &p, 0);
#ifndef CONFIG_LGNAS_HAS_NO_MCU
		out[0] = MICOM_ID_SET1;
		out[1] = MICOM_LCD_BRIGHTNESS;
		if(!nashal_micom_write(client, out, 8)){
			ret = -EFAULT;
      break;
    } 
#endif
		data->lcd[nr].brightness[LCD_BRIGHT_FULL] = out[2];
		data->lcd[nr].brightness[LCD_BRIGHT_HALF] = out[3];
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
		memset(out, ' ', MINFO_MAX_LCD_STR + 6);
		out[0] = MICOM_ID_SET3;
		p = memchr(buf, '\n', count);
		len = p? p - buf : count;
		len = (len > MINFO_MAX_LCD_STR)? MINFO_MAX_LCD_STR : len;
		memcpy(out + 1, buf, len);
		out[MINFO_MAX_LCD_STR + 2] = 0x0;
#if (MINFO_MAX_LCD_LINE > 0)
		out[MINFO_MAX_LCD_STR + 2] = data->lcd[nr].timeout[0];
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    if(data->lock.micom){
      lcd_backlight(data->lcd[0].brightness[LCD_BRIGHT_FULL]);
		  lcd_string(0xc0, buf);
    }else{
      if(data->lcd[0].timeout[0])
		    set_message(0xd1, (char *)&out[1], len);
      else /*persist */
		    set_message(0xd2, (char *)&out[1], len);
    }
#else
		if(!nashal_micom_write(client, out, 19))
			ret = -EFAULT;
#endif
#endif
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
	case 6:
		memset(out, ' ', MINFO_MAX_LCD_STR + 6);
    memset(data->lcd[0].greet_msg, 0,MINFO_MAX_LCD_STR+1);
		out[0] = MICOM_ID_SET4;
		p = memchr(buf, '\n', count);
		len = p? p - buf : count;
		len = (len > MINFO_MAX_LCD_STR)? MINFO_MAX_LCD_STR : len;
		memcpy(out + 1, buf, len);
    memcpy(data->lcd[0].greet_msg, buf, len);
#ifdef CONFIG_LGNAS_HAS_NO_MCU
		if(set_message(0xd3, (char *)&out[1], len))
#else
		if(!nashal_micom_write(client, out, MINFO_MAX_LCD_STR+3))
#endif
			ret = -EFAULT;
#if (MINFO_MAX_LCD_LINE > 0)
		else
			memcpy(data->lcd[nr].str[0], buf, len);
#endif
		break;
	case 7:
		p = memchr(buf, '\n', count);
		len = p? p - buf : count;
		len = (len > 16)? 16 : len;
#if (MINFO_MAX_LCD_LINE > 1)
		  memcpy(data->lcd[nr].str[1], buf, len);
#endif
		break;
	case 12:
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    lcd_backlight(data->lcd[0].brightness[LCD_BRIGHT_FULL]);
		lcd_string(0x80,buf);
#endif
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

static ssize_t store_button(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct nashal_data *data = i2c_get_clientdata(to_i2c_client(dev));
	int index = to_nashal_dev_attr(devattr)->index;
	long val;

	if(strict_strtol(buf, 10, &val))
		return -EINVAL;
	
	switch(index) {
		case 1:
			mutex_lock(&data->update_lock);
			data->priority.button = val;
			mutex_unlock(&data->update_lock);
			return count;
		default:
			return -EINVAL;
	}
}

static ssize_t store_lock(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_adapter *adap = client->adapter;
	struct nashal_data *data = i2c_get_clientdata(client);
	int index = to_nashal_dev_attr(devattr)->index;
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->update_lock);
 	switch(index)
 	{
 	case 0:
 		if(val)
 			__led_lock(client);
 		else
 			__led_unlock(client);
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
	case 4:
		scsi_led_act_set(val? NULL : scsi_led_act_func);
		usb_led_act_set(val? NULL : usb_led_act_func);
		data->lock.activity = val;
		break;
	case 5:
		i2c_lock_adapter(adap);
		data->lock.micom = val;
		i2c_unlock_adapter(adap);
		break;
	default:
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}
	mutex_unlock(&data->update_lock);
	return count;
}

#ifdef CONFIG_MACH_APM
#define mtdcr(rn, val) \
	asm volatile("mtdcr %0,%1" : : "i"(rn), "r"(val))

#define DCRN_SDR0_CONFIG_ADDR	0xe
#define DCRN_SDR0_CONFIG_DATA	0xf

#define SDR0_WRITE(offset, data) ({\
	mtdcr(DCRN_SDR0_CONFIG_ADDR, offset); \
	mtdcr(DCRN_SDR0_CONFIG_DATA, data); })

#define DCRN_SDR0_PFC0	0x4100

static void __gpio_output_enable(void)
{
	SDR0_WRITE(DCRN_SDR0_PFC0, 0xffffffff);
}

static void __gpio_output_disable(void)
{
	SDR0_WRITE(DCRN_SDR0_PFC0, 0x0);
}

static void __button_enable(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
	int i;

	for(i = 0; i < ARRAY_SIZE(buttons); i++) {
		struct button_data *bdata = &data->bdata[i];
		if(bdata->disabled) {
			enable_irq(gpio_to_irq(bdata->button->gpio));
			bdata->disabled = 0;
		}
	}
}

static void __button_disable(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
	int i;

	for(i = 0; i < ARRAY_SIZE(buttons); i++) {
		struct button_data *bdata = &data->bdata[i];
		if(!bdata->disabled) {
			disable_irq(gpio_to_irq(bdata->button->gpio));
			if(bdata->button->debounce_interval)
				del_timer_sync(&bdata->timer);
			bdata->disabled = 1;
		}
	}
}

static ssize_t store_flash(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 out[8] = {MICOM_ID_SET1, MICOM_FLASH_ATTACH, 0, 0, 0, 0, 0, 0};
	char *p;
	int len;

	p = memchr(buf, '\n', count);
	len = p? p - buf : count;

	if(len == 6 && !strncmp(buf, "attach", len)) {
		__button_disable(client);
		__gpio_output_disable();
		out[2] = 0x1;
	}
	else if(len == 6 && !strncmp(buf, "detach", len)) {
		__button_enable(client);
		__gpio_output_enable();
		out[2] = 0x0;
	}
	else
		return -EINVAL;
	
	if(nashal_micom_write(client, out, 8))
		return count;
	
	return -EFAULT;
}
#endif

static struct nashal_device_attribute nashal_attr[] = {
                                             /*nr, index */
	/* micom */
	NASHAL_ATTR(micom,  	      0644, show_micom, store_micom, 0, 0),
	NASHAL_ATTR(micom_version,  0444, show_micom, NULL, 0, 1),
#if (MINFO_MAX_LCD > 0)
	NASHAL_ATTR(micom_mode,     0644, show_micom, store_micom, 0, 2),
	NASHAL_ATTR(micom_filename, 0644, show_micom, store_micom, 0, 3),
	NASHAL_ATTR(micom_hostname, 0644, show_micom, store_micom, 0, 4),
	NASHAL_ATTR(micom_fwversion,0644, show_micom, store_micom, 0, 5),
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	NASHAL_ATTR(nas_type,0644, show_micom, store_micom, 0, 6),
	NASHAL_ATTR(mac,0644, show_micom, store_micom, 0, 7),
#endif
	/* address */
	NASHAL_ATTR(address, 0644, show_address, store_address, 0, 0),
#endif
	/* buzzer */
	NASHAL_ATTR(buzzer,      0200, NULL, store_buzzer, 0, 0),
	/* button */
	NASHAL_ATTR(button,      0444, show_button, NULL, 0, 0),
	NASHAL_ATTR(button_priority, 0644, show_button, store_button, 0, 1),
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
	NASHAL_ATTR(led_activity_lock, 0644, show_lock, store_lock, 0, 4),
	NASHAL_ATTR(micom_lock, 0644, show_lock, store_lock, 0, 5),
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
	NASHAL_ATTR(scheduled_power_time,0644, show_sysinfo, store_sysinfo, 0, 5),
#endif
	NASHAL_ATTR(debug, 	     0644, show_sysinfo, store_sysinfo, 0, 4),
	/* fan */
#if (MINFO_MAX_FAN > 0)
	NASHAL_ATTR(fan1_pwm, 0644, show_fan, store_fan, 0, 0),
#if (MINFO_SUPPORT_FAN1RPM)
	NASHAL_ATTR(fan1_rpm, 0444, show_fan, NULL,      0, 1),
#endif
#endif
#if (MINFO_MAX_FAN > 1)
#if (MINFO_SUPPORT_FAN2AUTO)
	NASHAL_ATTR(fan2_pwm_max,  0644, show_fan, store_fan, 1, 2),
	NASHAL_ATTR(fan2_pwm_mid,  0644, show_fan, store_fan, 1, 3),
	NASHAL_ATTR(fan2_pwm_min,  0644, show_fan, store_fan, 1, 4),
	NASHAL_ATTR(fan2_temp_high,0644, show_fan, store_fan, 1, 5),
	NASHAL_ATTR(fan2_temp_low, 0644, show_fan, store_fan, 1, 6),
#else
	NASHAL_ATTR(fan2_pwm, 		 0644, show_fan, store_fan, 1, 0),
#endif
#if (MINFO_SUPPORT_FAN2RPM)
	NASHAL_ATTR(fan2_rpm, 0444, show_fan, NULL,      1, 1),
#endif
#endif
	/* led */
#if (MINFO_MAX_LED_HDD > 0)
	NASHAL_ATTR(led_hdd1,     		0644, show_led, store_led, LED_HDD1_INDEX, 0),
	NASHAL_ATTR(led_hdd1_delay,   0644, show_led, store_led, LED_HDD1_INDEX, 1),
#endif
#if (MINFO_MAX_LED_HDD > 1)
	NASHAL_ATTR(led_hdd2,   			0644, show_led, store_led, LED_HDD2_INDEX, 0),
	NASHAL_ATTR(led_hdd2_delay,   0644, show_led, store_led, LED_HDD2_INDEX, 1),
#endif
#if (MINFO_MAX_LED_HDD > 2)
	NASHAL_ATTR(led_hdd3,   			0644, show_led, store_led, LED_HDD3_INDEX, 0),
	NASHAL_ATTR(led_hdd3_delay,   0644, show_led, store_led, LED_HDD3_INDEX, 1),
#endif
#if (MINFO_MAX_LED_HDD > 3)
	NASHAL_ATTR(led_hdd4,   			0644, show_led, store_led, LED_HDD4_INDEX, 0),
	NASHAL_ATTR(led_hdd4_delay,   0644, show_led, store_led, LED_HDD4_INDEX, 1),
#endif
#if (MINFO_MAX_LED_ODD > 0)
	NASHAL_ATTR(led_odd1,   			0644, show_led, store_led, LED_ODD1_INDEX, 0),
	NASHAL_ATTR(led_odd1_delay,   0644, show_led, store_led, LED_ODD1_INDEX, 1),
#endif
#if (MINFO_MAX_LED_USB > 0)
	NASHAL_ATTR(led_usb1,   			0644, show_led, store_led, LED_USB1_INDEX, 0),
	NASHAL_ATTR(led_usb1_delay,   0644, show_led, store_led, LED_USB1_INDEX, 1),
#endif
#if (MINFO_MAX_LED_LAN > 0)
	NASHAL_ATTR(led_lan1,   		0644, show_led, store_led, LED_LAN1_INDEX, 0),
	NASHAL_ATTR(led_lan1_delay, 0644, show_led, store_led, LED_LAN1_INDEX, 1),
#endif
#if (MINFO_MAX_LED_POW > 0)
	NASHAL_ATTR(led_power1,   		0644, show_led, store_led, LED_POW1_INDEX, 0),
	NASHAL_ATTR(led_power1_delay, 0644, show_led, store_led, LED_POW1_INDEX, 1),
#endif
#if (MINFO_MAX_LED_POW > 1)
	NASHAL_ATTR(led_power2,   		0644, show_led, store_led, LED_POW2_INDEX, 0),
	NASHAL_ATTR(led_power2_delay, 0644, show_led, store_led, LED_POW2_INDEX, 1),
#endif
#if (MINFO_MAX_LED_POW > 2)
	NASHAL_ATTR(led_power3,   		0644, show_led, store_led, LED_POW3_INDEX, 0),
	NASHAL_ATTR(led_power3_delay, 0644, show_led, store_led, LED_POW3_INDEX, 1),
#endif
#if (MINFO_MAX_LED_FAIL > 0)
	NASHAL_ATTR(led_fail1,				0644, show_led, store_led, LED_FAIL1_INDEX, 0),
	NASHAL_ATTR(led_fail1_delay,	0644, show_led, store_led, LED_FAIL1_INDEX, 1),
#endif
#if (MINFO_MAX_LED_FAIL > 1)
	NASHAL_ATTR(led_fail2,				0644, show_led, store_led, LED_FAIL2_INDEX, 0),
	NASHAL_ATTR(led_fail2_delay,	0644, show_led, store_led, LED_FAIL2_INDEX, 1),
#endif
#if (MINFO_MAX_LED_FAIL > 2)
	NASHAL_ATTR(led_fail3,				0644, show_led, store_led, LED_FAIL3_INDEX, 0),
	NASHAL_ATTR(led_fail3_delay,	0644, show_led, store_led, LED_FAIL3_INDEX, 1),
#endif
#if (MINFO_MAX_LED_FAIL > 3)
	NASHAL_ATTR(led_fail4,				0644, show_led, store_led, LED_FAIL4_INDEX, 0),
	NASHAL_ATTR(led_fail4_delay,	0644, show_led, store_led, LED_FAIL4_INDEX, 1),
#endif
	/* lcd */
#if (MINFO_MAX_LCD > 0)
	NASHAL_ATTR(lcd1_brightness, 0644, show_lcd, store_lcd, 0, 0),
#if (MINFO_SUPPORT_LCDICON)
	NASHAL_ATTR(lcd1_icon, 0200, NULL, store_lcd, 0, 1),
#endif
#if (MINFO_LCD1_LINE > 0)
	NASHAL_ATTR(lcd1_str1, 0200, NULL, store_lcd, 0, 2),
	NASHAL_ATTR(lcd1_str1_timeout, 0644, show_lcd, store_lcd, 0, 3),
	NASHAL_ATTR(lcd1_str1_default, 0644, show_lcd, store_lcd, 0, 6),
#endif
#if (MINFO_LCD1_LINE > 1) 
	NASHAL_ATTR(lcd1_str0, 0200, NULL, store_lcd, 0, 12),
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
#ifdef CONFIG_MACH_APM
	NASHAL_ATTR(flash, 0200, NULL, store_flash, 0, 0),
#endif
};

static ssize_t read_micom(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct i2c_client *client =
		to_i2c_client(container_of(kobj, struct device, kobj));
	char *tmp;
	int ret;
	
	if(count > 8192)
		count = 8192;
	
	tmp = kmalloc(count, GFP_KERNEL);
	if(tmp == NULL)
		return -ENOMEM;
	
	dprintk("%s: reading %zu bytes.\n", __func__, count);
	
	ret = i2c_master_recv(client, tmp, count);
	if(ret >= 0)
		memcpy(buf, tmp, count);
	kfree(tmp);
	return ret;
}

static ssize_t write_micom(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct i2c_client *client =
		to_i2c_client(container_of(kobj, struct device, kobj));
	char *tmp;
	int ret;
	
	if(count > 8192)
		count = 8192;
	
	tmp = kmalloc(count, GFP_KERNEL);
	if(tmp == NULL)
		return -ENOMEM;
	
	memcpy(tmp, buf, count);

	dprintk("%s: writing %zu bytes.\n", __func__, count);
	
	ret = i2c_master_send(client, tmp, count);
	kfree(tmp);
	return ret;
}

static struct bin_attribute nashal_binattr = {
	.attr = {
		.name = "micom_bio",
		.mode = S_IRUSR | S_IWUSR,
	},
	.read = read_micom,
	.write = write_micom,
};

/*
 * Begin non sysfs callback code (NAS HAL Real code)
 */

/* Timer functions */
static void nashal_work_led(struct work_struct *work)
{
	struct work_data *wdata =
		container_of(work, struct work_data, led);
	struct nashal_data *data =
		container_of(wdata, struct nashal_data, work);
	struct i2c_client *client = data->client;
	int i;

	for(i = 0; i < ARRAY_SIZE(leds); i++) {
		struct led_data *leddata = &data->leddata[i]; 
		if(leddata->led->delay) {
			if(leddata->count >= leddata->led->delay) {
				leddata->count = 0;
				leddata->led->blink = ~leddata->led->blink;
				if(gpio_is_valid(leddata->led->gpio))
					gpio_set_value(leddata->led->gpio,
						leddata->led->state^leddata->led->active_low^leddata->led->blink);
				else
					led_set_value(client, leddata->led->gpio,
						leddata->led->state^leddata->led->active_low^leddata->led->blink);
			}
			else {
				leddata->count++;
			}
		}	
	}
 	mod_timer(&data->timer_100ms, jiffies + msecs_to_jiffies(100));
}

static void nashal_timer_led(unsigned long __data){
	struct i2c_client *client = (struct i2c_client *)__data;
	struct nashal_data *data = i2c_get_clientdata(client);

  //dprintk("%s is called\n", __FUNCTION__);
  schedule_work(&data->work.led);
}

static void __micom_wait(void)
{
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	return;
#else
	msleep(200);
#endif
}

/* Micom I/F functions */
int nashal_micom_read(struct i2c_client *client, u8 *buf, u8 len )
{
	struct i2c_msg msgs[] = {
		{.addr = client->addr,.flags = I2C_M_RD,.buf = buf,.len = len }
	};
	struct nashal_data *data = i2c_get_clientdata(client);
	
	if(data->lock.micom)
		return false;

#ifdef CONFIG_LGNAS_HAS_NO_MCU
  if( front_read( msgs[0].buf, msgs[0].len ) != 0 )
#else
	if( i2c_transfer( client->adapter, msgs, 1) != 1 )
#endif
	  return false;

	dprintk(" in:0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
	buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);

	__micom_wait();
	return true;
}
int nashal_micom_write(struct i2c_client *client, u8 *buf, u8 len )
{
	struct i2c_msg msgs[] = {
		{.addr = client->addr,.flags = 0,       .buf = buf,.len = len },
	};
	struct nashal_data *data = i2c_get_clientdata(client);
	
	if(data->lock.micom)
		return false;

  dprintk("%s is called\n", __FUNCTION__);
	dprintk("out:0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
	buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);
#ifdef CONFIG_LGNAS_HAS_NO_MCU
     *(msgs[0].buf+7) = buf[0]^buf[1]^buf[2]^buf[3]^buf[4]^buf[5]^buf[6];
  if( front_write(msgs[0].buf, msgs[0].len) )
#else
	if( i2c_transfer( client->adapter, msgs, 1) != 1 )
#endif
	  return false;

	__micom_wait();
	return true;
}

int nashal_micom_reg_read(struct i2c_client *client, u8 reg, u8 *buf, u8 len )
{
  u8 out[8] = { MICOM_ID_GET, reg, 0, 0, 0, 0, 0, MICOM_ID_GET ^ reg };

	struct i2c_msg msgs[] = {
		{.addr = client->addr,.flags = 0,       .buf = out,.len = 8  },
		{.addr = client->addr,.flags = I2C_M_RD,.buf = buf,.len = len}
	};
	struct nashal_data *data = i2c_get_clientdata(client);
	
	if(data->lock.micom)
		return false;

	memset(buf, 0x00, 8);	
	dprintk("out:0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
	out[0],out[1],out[2],out[3],out[4],out[5],out[6],out[7]);

#ifdef CONFIG_LGNAS_HAS_NO_MCU
  if( front_write(msgs[0].buf, msgs[0].len) && front_read(msgs[1].buf, msgs[1].len) )
#else
	if( i2c_transfer( client->adapter, msgs, 2) != 2 )
#endif
	  return false;

	dprintk(" in:0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
	buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);

	__micom_wait();
	return true;
}

#if (MINFO_MAX_TEMP > 0)
/* Sensor I/F functions */
static int nashal_temp_read(struct i2c_client *client, u8 reg1, u8 reg2 )
{
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	  return (int)SHWMInB(reg1);
#else
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
#endif
}
#endif

#if (MINFO_MAX_FAN > 0)
/* Update functions of hal data from i2c devices( micom, sensor, ... ) */
static struct nashal_data *nashal_update_device_fan(
	struct i2c_client *client, struct nashal_data *data)
{
	int i;
#ifdef CONFIG_LGNAS_HAS_NO_MCU
  u16 rpm;
#endif

  dprintk("%s is called\n", __FUNCTION__);

	for (i = 0; i < MINFO_MAX_FAN; i++) {
#ifdef CONFIG_LGNAS_HAS_NO_MCU
    rpm = SHWMInW(NASHAL_FAN_PWM[i]);
    rpm = rpm ? (1500000 / rpm) : 0;
    data->fan[i].rpm = (rpm < 500)? 0: rpm;
    if (i == 1) {
      data->fan[i].pwm_max = (int)SHWMInB(0xaa); 
      data->fan[i].pwm_mid = (int)SHWMInB(0xab); 
      data->fan[i].pwm_min = (int)SHWMInB(0xae); 
      data->fan[i].temp_high = (int)SHWMInB(0xa6); 
      data->fan[i].temp_low  = (int)SHWMInB(0xa9); 
    }else{
      data->fan[i].pwm = (int)SHWMInB(0xb3); 
    }
    
#else
	  if(nashal_micom_reg_read( client, NASHAL_FAN_PWM[i], data->buf, 8))
			data->fan[i].rpm = (u16)(data->buf[3] << 8) + data->buf[4];
#endif
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

  dprintk("%s is called\n", __FUNCTION__);
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
		else
			led_set_value(client, led->gpio, led->state ^ led->active_low);
	}

	setup_timer(&data->timer_100ms, nashal_timer_led, (unsigned long)client);
	INIT_WORK(&data->work.led, nashal_work_led);

	usb_led_act_set(usb_led_act_func);
	scsi_led_blink_set(data, 10);

	return 0;

err:
	gpio_free(led->gpio);
	return ret;
}
static int nashal_button_init(struct i2c_client *client)
{
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	struct nashal_data *data = ns2data;
#else
	struct nashal_data *data = i2c_get_clientdata(client);
  int err;
#endif
	int i;

	spin_lock_init(&data->buffer_lock);
	init_waitqueue_head(&data->wait);

	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		struct button_data *bdata = &data->bdata[i];
#ifndef CONFIG_LGNAS_HAS_NO_MCU
		int irq;
#endif

		bdata->index = i;
		bdata->button = &buttons[i];
		setup_timer(&bdata->timer, button_timer, (unsigned long)bdata);
		INIT_WORK(&bdata->work, button_event);

		bdata->priority = bdata->button->type;
		bdata->button->type = 0;

#ifndef CONFIG_LGNAS_HAS_NO_MCU
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
				    bdata->button->trigger,
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

#ifndef CONFIG_LGNAS_HAS_NO_MCU
#if (MINFO_MAX_LCD > 0)
	fsm_init();
#endif
#endif
	return 0;

#ifndef CONFIG_LGNAS_HAS_NO_MCU
 fail2:
	while (--i >= 0) {
		free_irq(gpio_to_irq(data->bdata[i].button->gpio), &data->bdata[i]);
		if (data->bdata[i].button->debounce_interval)
			del_timer_sync(&data->bdata[i].timer);
		cancel_work_sync(&data->bdata[i].work);
		gpio_free(data->bdata[i].button->gpio);
	}
  return -EPERM;
#endif
}
#ifdef CONFIG_LGNAS_HAS_NO_MCU
static void emcu_timer1(unsigned long _data)
{
	struct emcu_data *edata = (struct emcu_data *)_data;
	struct nashal_data *data =
		container_of(edata, struct nashal_data, edata[0]);
  dprintk("%s is called\n", __FUNCTION__);
  task_200ms(data);	
  mod_timer(&edata->timer, jiffies + msecs_to_jiffies(edata->timer_delay));
}
static void emcu_timer2(unsigned long _data)
{
	struct emcu_data *edata = (struct emcu_data *)_data;
	struct nashal_data *data =
		container_of(edata, struct nashal_data, edata[1]);
  dprintk("%s is called\n", __FUNCTION__);
  task_1000ms(data);	
  mod_timer(&edata->timer, jiffies + msecs_to_jiffies(edata->timer_delay));
}

static const struct emcu_data nashal_edata[] = {
  { .timer_func = emcu_timer1, 
    .timer_delay = 200,
  },
  { .timer_func = emcu_timer2, 
    .timer_delay = 1000,
  },
};

static void nashal_emcu_init(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
  int i;
 
  /* nas model name init */
  (void)get_nas_class(NULL);

  /* system fan init */
  SHWMOutB(NAS_INIT_FAN_PWM, NASHAL_FAN_PWM[0] + 3);
  data->fan[0].pwm = NAS_INIT_FAN_PWM;


	for (i = 0; i < ARRAY_SIZE(nashal_edata); i++) {
		struct emcu_data *edata = &data->edata[i];

    edata->timer_delay = nashal_edata[i].timer_delay;
    edata->timer_func = nashal_edata[i].timer_func;
	  setup_timer(&edata->timer, edata->timer_func, (unsigned long)edata);
		mod_timer(&edata->timer,
			jiffies + msecs_to_jiffies(edata->timer_delay));
  }
}
static void nashal_emcu_exit(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
  int i;

	for (i = 0; i < ARRAY_SIZE(nashal_edata); i++) {
		struct emcu_data *edata = &data->edata[i];
	  del_timer_sync(&edata->timer);
  }
}
#endif
#ifndef CONFIG_LGNAS_HAS_NO_MCU
static void nashal_model_init(struct i2c_client *client)
{
	u8 in[8];
	int i;
	
	if(nashal_micom_reg_read(client, MICOM_VER, in, 8)) {
		if(strncmp(in, "100909", 6) < 0) {
			model.support.powerloss = false;
			for(i = 0; i < ARRAY_SIZE(nashal_attr); i++)
				if(!strncmp(nashal_attr[i].dev_attr.attr.name, "ac_powerloss", 12))
					nashal_attr[i].skip = 1;
		}
	}
	else
		printk("%s: failed\n", __func__);
}
#endif
static void nashal_data_init(struct i2c_client *client)
{
	struct nashal_data *data = i2c_get_clientdata(client);
	
	data->client = client;
#if (MINFO_MAX_LCD > 0)
  /* greeting message initialize */
  sprintf(data->lcd[0].greet_msg, "%s", NAS_INIT_GREET_MSG);
	data->lcd[0].brightness[LCD_BRIGHT_FULL] = NAS_INIT_LCD_BRIGHT_FULL;
	data->lcd[0].brightness[LCD_BRIGHT_HALF] = NAS_INIT_LCD_BRIGHT_HALF;
	data->lcd[0].timeout[0] = NAS_INIT_LCD_TO_DEFAULT_CNT;
#endif
}

static int nashal_init_client(struct i2c_client *client)
{
  int ret;
	
	/* Add code to here about nashal initialization */
  dprintk("%s is called\n", __FUNCTION__);

	ret = nashal_led_init(client);
	if(ret) {
		dprintk("%s is led init error\n", __FUNCTION__);
		goto __exit__;
	}

	ret = nashal_button_init(client);
  if(ret) {
  	dprintk("%s is button init error\n", __FUNCTION__);
  	goto __exit__;
  }

#ifdef CONFIG_LGNAS_HAS_NO_MCU
  nashal_emcu_init(client);
#else
  nashal_model_init(client);
#endif

__exit__:
	return ret;
}
/*
 * driver function ( probe, remove, init, exit )
 */
#ifdef CONFIG_LGNAS_HAS_NO_MCU
static int __init nashal_attach(void)
{

  struct i2c_board_info info;
  struct i2c_client *client;
	struct i2c_adapter *adap = i2c_get_adapter(0);

  dprintk("%s is called\n", __FUNCTION__);

	info.addr = 0x58;
	info.platform_data = "micom-emul";
	strlcpy(info.type,"io-micom", I2C_NAME_SIZE);
	client = i2c_new_device(adap, &info);
  if (client == NULL) {
    printk(KERN_ERR "micom-emul: failed to attach to i2c\n");
    return -1;
  }
  list_add_tail(&client->detected, &nashal_driver.clients);

  return 0;
}
#endif

static int nashal_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int i, err = 0;
	struct i2c_adapter *adapter = client->adapter;
	struct nashal_data *data;

  dprintk("%s is called\n", __FUNCTION__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;
//to check

	data = kzalloc(sizeof(struct nashal_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	ns2data = data;
#endif

	i2c_set_clientdata(client, data);
	
	nashal_data_init(client);

	/* Initialize the NASHAL */
	err = nashal_init_client(client);
	if(err){
		goto exit_free;
  }
	mutex_init(&data->update_lock);

	/* Register sysfs hooks */
	for (i = 0; i < ARRAY_SIZE(nashal_attr); i++) {
		if(nashal_attr[i].skip)
			continue;
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
	
	err = sysfs_create_bin_file(&client->dev.kobj, &nashal_binattr);

	dev_info(&client->dev, "NAS HAL Detected %s chip at 0x%02x\n",
		client->name, client->addr);

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
#ifdef CONFIG_LGNAS_HAS_NO_MCU
  nashal_emcu_exit(client);
#endif
	del_timer_sync(&data->timer_100ms);
	cancel_work_sync(&data->work.led);
	
	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		if(gpio_is_valid(data->leddata[i].led->gpio))
			gpio_free(data->leddata[i].led->gpio);
	}

	for (i = 0; i < ARRAY_SIZE(buttons); i++){
		free_irq(gpio_to_irq(data->bdata[i].button->gpio), &data->bdata[i]);
		if (data->bdata[i].button->debounce_interval)
			del_timer_sync(&data->bdata[i].timer);
		cancel_work_sync(&data->bdata[i].work);
		gpio_free(data->bdata[i].button->gpio);
	}

	if (data->nashal_dev)
		device_unregister(data->nashal_dev);

	for (i = 0; i < ARRAY_SIZE(nashal_attr); i++) {
		if(nashal_attr[i].skip)
			continue;
		device_remove_file(&client->dev, &nashal_attr[i].dev_attr);
	}
	
	sysfs_remove_bin_file(&client->dev.kobj, &nashal_binattr);

	i2c_set_clientdata(client, NULL);
	kfree(data);
	return 0;
}

static int __init nashal_init(void)
{
  int ret;
  dprintk("%s is called\n", __FUNCTION__);
	nashal_class = class_create(THIS_MODULE, "nas");

	if (IS_ERR(nashal_class)) {
		dprintk(KERN_ERR "hal.c: couldn't create sysfs class\n");
		return PTR_ERR(nashal_class);
	}
  ret = i2c_add_driver(&nashal_driver);
	if (ret != 0)
	  return ret;
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	nashal_attach(); 
#endif
	return ret;
}

static void __exit nashal_exit(void)
{
  dprintk("%s is called\n", __FUNCTION__);

	i2c_del_driver(&nashal_driver);
	class_destroy(nashal_class);	
}

module_init(nashal_init);
module_exit(nashal_exit);

MODULE_AUTHOR("Wonbae, Joo <wonbae.joo@lge.com>");
MODULE_DESCRIPTION("LG Electronics LGNAS HAL driver");
MODULE_LICENSE("GPL");

