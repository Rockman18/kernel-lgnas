/*
 *  hal.h - nas hal driver sysfs defines
 */
#ifndef _NASHAL_H
#define _NASHAL_H

#define MICOM_WAIT_TIME				300
#define MICOM_ID_GET					0x01
#define MICOM_ID_SET1					0x02
#define 	MICOM_PWM1							0x01 /* pwm(0-255) */
#define 	MICOM_PWM2							0x91 /* pwm(0-255) */
#define 	MICOM_VER								0x0f  
#define 	MICOM_EMRGNC_SHUTDOWN		0x92 /* enabale/disable(1/0) */
#define 	MICOM_POR								0x93 /* enabale/disable(1/0) */
#define 	MICOM_WTDG_VAL					0x94 /* value(0,1,...) */
#define 	MICOM_WTDG_TIMER				0x95 /* enabale/disable(1/0) */
#define		MICOM_FLASH_ATTACH			0x97 /* detach/attach(1/0) */
#define		MICOM_LCD_BRIGHTNESS		0x9c /* normal standby */
#define		MICOM_LED_BRIGHTNESS		0x81 /* led_id led_brightness */
#define		MICOM_LED_DELAY					0x82 /* led_id led_delay */
#define MICOM_ID_SET2					0x03
#define 	MICOM_ICON							0x30 /* position on/off(0xff/0) blink(1/0) */
#define 	MICOM_BUZZER						0x31 /* fre1 fre2 fre3 fre4 time_play time_wait */
#define MICOM_ID_SET3					0x04
#define MICOM_ID_SET4					0x05

#define NASHAL_BUFFER_SIZE 64

/*
 *  for lgnas hal sysfs
 */
struct nashal_device_attribute {
	struct device_attribute dev_attr;
	u8 index;
	u8 nr;
	u8 skip;
};
#define to_nashal_dev_attr(_dev_attr) \
	container_of(_dev_attr, struct nashal_device_attribute, dev_attr)

#define NASHAL_ATTR(_name, _mode, _show, _store, _nr, _index)	\
	{ .dev_attr = __ATTR(_name, _mode, _show, _store),	\
	  .index = _index,					\
	  .nr = _nr }

#define NASHAL_DEVICE_ATTR(_name,_mode,_show,_store,_nr,_index)	\
struct nashal_device_attribute nashal_dev_attr_##_name		\
	= NASHAL_ATTR(_name, _mode, _show, _store, _nr, _index)

/*
 *  NAS HAL data (each client gets its own)
 */
struct gpio_button {
	int code;
	int gpio;
	int active_low;
	char *desc;
	int type;
	int wakeup;
	int debounce_interval;
	bool can_disable;
	int trigger;
};

struct button_data {
	struct gpio_button *button;
	struct timer_list timer;
	struct work_struct work;
	int index;
	int priority;
	bool disabled;
};

struct fan_data {
	u16 pwm;
	u16 rpm;
	u16 pwm_max;
	u16 pwm_mid;
	u16 pwm_min;
	u16 temp_high;
	u16 temp_low;
};

#if (MINFO_MAX_LCD > 0)
struct lcd_data {
	u8 brightness[2];
#if (MINFO_MAX_LCD_LINE > 0)
	u8 timeout[MINFO_MAX_LCD_LINE];
	char greet_msg[MINFO_MAX_LCD_STR+1];
	char str[MINFO_MAX_LCD_LINE][MINFO_MAX_LCD_STR+1];
#endif
};
#endif

struct gpio_leds {
	u8 delay; /* 100ms timer delay */	
	unsigned gpio;
	unsigned state			: 1; /* 0:OFF 1:ON */
	unsigned active_low : 1;
	unsigned blink			: 1;
};

struct led_data {
	struct gpio_leds *led;
	u8 count;
};

struct timer_data {
	struct timer_list timer;
	u16 timer_delay;
};

struct emcu_data {
  void (* timer_func)(unsigned long);
  struct timer_list timer;
  u16 timer_delay;
};

struct sensor_data {
	u8 temp;	/* temperature */
};

struct gpio_data {
	unsigned gpio;
};

struct lock_data {
	int button;
	int buzzer;
	int led;
	int lcd;
	int activity;
	int micom;
};
struct schpower_data {
  u8 date;
  u8 hour;
  u8 minute;
  u8 second;
};
struct sysinfo_data {
	bool powerloss;
	bool wol;
	bool sch_power;
  struct schpower_data spdata;
};

struct priority_data {
	int button;
};

struct work_data {
	struct work_struct led;
};

#define MAX_FILENAME 2048
struct micom_data {
  int file_init;
	int file_num;
  int file_total;
	char filename[MAX_FILENAME];
	char hostname[16];
	char fwversion[16];
};

struct nashal_data {
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */
	struct device *nashal_dev;
	struct mutex update_lock;
	struct timer_list timer_100ms;
  struct micom_data mdata;
#if (MINFO_MAX_TEMP > 0)
	struct sensor_data sensor[MINFO_MAX_TEMP];
#endif
#if (MINFO_MAX_FAN > 0)
	struct fan_data fan[MINFO_MAX_FAN];
#endif
#if (MINFO_MAX_LCD > 0)
	struct lcd_data lcd[MINFO_MAX_LCD];
#endif
#if (MINFO_MAX_LED_ALL > 0)
	struct led_data leddata[MINFO_MAX_LED_ALL];
#endif
#if (MINFO_MAX_BTN > 0)
	struct button_data bdata[MINFO_MAX_BTN]; 
#endif
#ifdef CONFIG_LGNAS_HAS_NO_MCU
	struct emcu_data edata[2]; 
#endif
  struct input_event buffer[NASHAL_BUFFER_SIZE]; /* button event buffer */
	struct gpio_data gpio;
	struct lock_data lock;
	struct sysinfo_data sysinfo;
	struct priority_data priority;
	struct work_data work;
	struct i2c_client *client;
	int boot_complete;
	int head;
	int tail;
	spinlock_t buffer_lock; /* protects access to buffer, head and tail */
	wait_queue_head_t wait;
	u8 status;
	u8 buf[8];  						/* iomicom transfer buffer */
	u16 rsv[2];
};

/*
 * Functions declarations
 */
int nashal_micom_read(struct i2c_client *client, u8 *buf, u8 len );
int nashal_micom_write(struct i2c_client *client, u8 *buf, u8 len );
int nashal_micom_reg_read(struct i2c_client *client, u8 reg, u8 *buf, u8 len );

#endif /* _NASHAL_H */

