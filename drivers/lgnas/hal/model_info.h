/*
 *  model_info.h - The Model Config
 */
#ifndef _MODEL_INFO_H
#define _MODEL_INFO_H

#define true 1
#define false 0

#define dprintk(fmt, ...) \
	({ if(lgnas_debug) printk(pr_fmt(fmt), ##__VA_ARGS__); })

/* nas init data */
#define LCD_BRIGHT_FULL 0
#define LCD_BRIGHT_HALF 1

#define NAS_INIT_LCD_TO_DEFAULT_CNT              10  /* 10 sec */
#define NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_CANCEL  30  /* 30 sec */
#define NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT   60  /* 60 sec */
#define NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT2D 600 /* 600 sec */
#define NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT_DHCP   150  /* 150 sec */


#ifdef CONFIG_LGNAS_HAS_NO_MCU
#define MINFO_MAX_LCD_STR 				128
#define MINFO_MICOM				      	"emul"
#define MINFO_SUPPORT_BUTTON_BURN	true
#define NAS_INIT_FAN_PWM  				115 
#define NAS_INIT_GREET_MSG 				"* Enjoy LG-NAS with built-in \%ODD_TYPE\% drive"
#define NAS_INIT_LCD_BRIGHT_FULL 	128
#define NAS_INIT_LCD_BRIGHT_HALF  25
#else
#define MINFO_MAX_LCD_STR 				16
#define MINFO_MICOM				      	"micom"
#define MINFO_SUPPORT_BUTTON_BURN	false
#define NAS_INIT_GREET_MSG 				"LG Home NAS"
#define NAS_INIT_LCD_BRIGHT_FULL 	200
#define NAS_INIT_LCD_BRIGHT_HALF  20
#endif

#ifdef CONFIG_MACH_NS2 
#define MINFO_MAX_BTN 18
#define MINFO_MAX_BAY 4
#define MINFO_MAX_ESATA 1
#define MINFO_MAX_ODD 1
#define MINFO_MAX_LAN 1
#define MINFO_MAX_FAN 2
#define MINFO_MAX_LCD 1
#define MINFO_MAX_USB 3
#define MINFO_MAX_TEMP 1
#define MINFO_MAX_LED_HDD 4
#define MINFO_MAX_LED_ODD 1
#define MINFO_MAX_LED_USB 0 
#define MINFO_MAX_LED_POW 0
#define MINFO_MAX_LED_LAN 0
#define MINFO_MAX_LED_FAIL 4

#define MINFO_SUPPORT_WIFI 			false
#define MINFO_SUPPORT_ACPOW 		true
#define MINFO_SUPPORT_WOL 			true
#define MINFO_SUPPORT_SCHPOW 		true 
#define MINFO_SUPPORT_EXHDD 		false
#define MINFO_SUPPORT_EXODD 		false
#define MINFO_SUPPORT_BTNBACKUP true
#define MINFO_SUPPORT_BATTERY   true
#define MINFO_SUPPORT_LCDICON   false
#define MINFO_SUPPORT_MEMCARD   true
#define MINFO_SUPPORT_FAN2AUTO	true
#define MINFO_SUPPORT_FAN1RPM		true
#define MINFO_SUPPORT_FAN2RPM		true
#define MINFO_SUPPORT_FANRPM    { MINFO_SUPPORT_FAN1RPM, MINFO_SUPPORT_FAN2RPM }
#define MINFO_SUPPORT_STREAMING	false
#define MINFO_SUPPORT_FAMILYCAST false
#define MINFO_SUPPORT_TRANSCODE	false
#define MINFO_SUPPORT_DTCPIP		false
#define MINFO_SUPPORT_FANINFO		true

#define MINFO_PATH_BAY { "0:0:0:0", "1:0:0:0", "2:0:0:0", "3:0:0:0" }
#define MINFO_PATH_ESATA  { "5:0:0:0" } 
#define MINFO_PATH_ODD  { "4:0:0:0" } 
#if (MINFO_MAX_LCD > 0)
#  define MINFO_LCD1_LINE	2
#  define MINFO_LCD_LINE { MINFO_LCD1_LINE, }
#endif
#define MINFO_CHANNEL_TEMP { 2,0 }
#define MINFO_FS_DEFAULT				"ext4"
#define MINFO_FS_BLOCK_SIZE			4
#define	MINFO_SMB_SO_RCVBUF			0
#define	MINFO_SMB_SO_SNDBUF 		0
#define	MINFO_SMB_NR_REQUESTS		0
#define	MINFO_SMB_READ_AHEAD_KB	0
#define MINFO_BOOTLOADER				"grub"
#define MINFO_BUZZER				    "beep"
#define MINFO_SWAP_MODE         "hot"
#endif /* NS2 */

#if defined(CONFIG_MACH_NT1) || \
    defined(CONFIG_MACH_NT11) || \
    defined(CONFIG_MACH_NT3)
#define MINFO_MAX_BTN 4
#define MINFO_MAX_BAY 1
#define MINFO_MAX_ESATA 0
#ifdef CONFIG_LGNAS_HAS_CDROM
# define MINFO_MAX_ODD 1
#else
# define MINFO_MAX_ODD 0
#endif
#define MINFO_MAX_LAN 1
#ifdef CONFIG_MACH_NT3
#define MINFO_MAX_FAN 0
#else
#define MINFO_MAX_FAN 1
#endif
#define MINFO_MAX_LCD 0
#define MINFO_MAX_USB 1
#define MINFO_MAX_TEMP 0
#define MINFO_MAX_LED_HDD 1
#if (MINFO_MAX_ODD > 0)
# define MINFO_MAX_LED_ODD 1
#else
# define MINFO_MAX_LED_ODD 0
#endif
#define MINFO_MAX_LED_USB 1
#define MINFO_MAX_LED_POW 1
#define MINFO_MAX_LED_LAN 1
#define MINFO_MAX_LED_FAIL 0

#define MINFO_SUPPORT_WIFI 			false
#define MINFO_SUPPORT_ACPOW 		true
#ifdef CONFIG_MACH_NT3
#define MINFO_SUPPORT_WOL 			true
#else
#define MINFO_SUPPORT_WOL 			false
#endif
#define MINFO_SUPPORT_SCHPOW 		false
#define MINFO_SUPPORT_EXHDD 		true
#if (MINFO_MAX_ODD > 0)
# define MINFO_SUPPORT_EXODD		true
#else
# define MINFO_SUPPORT_EXODD		false
#endif
#define MINFO_SUPPORT_BTNBACKUP true
#define MINFO_SUPPORT_BATTERY   false
#define MINFO_SUPPORT_LCDICON   false
#define MINFO_SUPPORT_MEMCARD   false
#define MINFO_SUPPORT_FAN1RPM		false
#define MINFO_SUPPORT_FANRPM    { MINFO_SUPPORT_FAN1RPM }
#define MINFO_SUPPORT_STREAMING	false
#define MINFO_SUPPORT_FAMILYCAST true
#define MINFO_SUPPORT_TRANSCODE	false
#define MINFO_SUPPORT_DTCPIP		false
#if defined(CONFIG_MACH_NT11)
#define MINFO_SUPPORT_FANINFO		false
#else
#define MINFO_SUPPORT_FANINFO		true
#endif

#if defined(CONFIG_MACH_NT3)
#define MINFO_PATH_BAY { "1:0:0:0" }
#if (MINFO_MAX_ODD > 0) 
#  define MINFO_PATH_ODD { "0:0:0:0" }
#endif
#else
#define MINFO_PATH_BAY { "0:0:0:0" }
#if (MINFO_MAX_ODD > 0) 
#  define MINFO_PATH_ODD { "1:0:0:0" }
#endif
#endif
#define MINFO_FS_DEFAULT				"ext4"
#if defined(CONFIG_MACH_NT3)
#  define MINFO_FS_BLOCK_SIZE		64
#else
#  define MINFO_FS_BLOCK_SIZE		4
#endif
#if (MINFO_SUPPORT_EXHDD)
#if defined(CONFIG_MACH_NT3)
#define MINFO_USB_HOST_MOD_NAME		""
#define MINFO_USB_DEVICE_MOD_NAME	""
#else
#define MINFO_USB_HOST_MOD_NAME		"ehci_hcd"
#define MINFO_USB_DEVICE_MOD_NAME	"mv_udc"
#endif
#endif
#if defined(CONFIG_MACH_NT3)
#define	MINFO_SMB_SO_RCVBUF			65536
#define	MINFO_SMB_SO_SNDBUF 		65536
#define	MINFO_SMB_NR_REQUESTS		256
#define	MINFO_SMB_READ_AHEAD_KB	512
#else
#define	MINFO_SMB_SO_RCVBUF			0
#define	MINFO_SMB_SO_SNDBUF 		0
#define	MINFO_SMB_NR_REQUESTS		0
#define	MINFO_SMB_READ_AHEAD_KB	0
#endif
#define MINFO_BOOTLOADER				"uboot"
#define MINFO_MICOM				      "micom"
#define MINFO_BUZZER				    "micom"
#define MINFO_SWAP_MODE         "none"
#endif /* NT1 NT11 NT3 */

#if defined(CONFIG_MACH_NC2) || defined(CONFIG_MACH_NC21)
#define MINFO_MAX_BTN 3
#define MINFO_MAX_BAY 2
#define MINFO_MAX_ESATA 0
#ifdef CONFIG_LGNAS_HAS_CDROM
#  define MINFO_MAX_ODD 1
#else
#  define MINFO_MAX_ODD 0
#endif
#define MINFO_MAX_LAN 1
#define MINFO_MAX_FAN 1
#define MINFO_MAX_LCD 0
#define MINFO_MAX_USB 2
#define MINFO_MAX_TEMP 0
#define MINFO_MAX_LED_HDD 2
#define MINFO_MAX_LED_ODD 0 
#define MINFO_MAX_LED_USB 0
#define MINFO_MAX_LED_POW 1
#define MINFO_MAX_LED_LAN 1
#define MINFO_MAX_LED_FAIL 0

#define MINFO_SUPPORT_WIFI 			false
#define MINFO_SUPPORT_ACPOW 		true
#define MINFO_SUPPORT_WOL 			false
#define MINFO_SUPPORT_SCHPOW 		false
#define MINFO_SUPPORT_EXHDD 		false
#define MINFO_SUPPORT_EXODD 		false
#define MINFO_SUPPORT_BTNBACKUP true
#define MINFO_SUPPORT_BATTERY   false
#define MINFO_SUPPORT_LCDICON   false
#define MINFO_SUPPORT_MEMCARD   false
#define MINFO_SUPPORT_FAN1RPM		false
#define MINFO_SUPPORT_FANRPM    { MINFO_SUPPORT_FAN1RPM }
#define MINFO_SUPPORT_STREAMING	false
#define MINFO_SUPPORT_FAMILYCAST true
#define MINFO_SUPPORT_TRANSCODE	false
#define MINFO_SUPPORT_DTCPIP		false
#define MINFO_SUPPORT_FANINFO		true

#define MINFO_PATH_BAY { "0:0:0:0", "1:0:0:0" }
#if (MINFO_MAX_ODD > 0) 
#  define MINFO_PATH_ODD  { "2:0:0:0" } 
#endif
#define MINFO_FS_DEFAULT				"ext4"
#define MINFO_FS_BLOCK_SIZE			4
#define	MINFO_SMB_SO_RCVBUF			0
#define	MINFO_SMB_SO_SNDBUF 		0
#define	MINFO_SMB_NR_REQUESTS		0
#define	MINFO_SMB_READ_AHEAD_KB	0
#define MINFO_BOOTLOADER				"uboot"
#define MINFO_MICOM				      "micom"
#define MINFO_BUZZER				    "micom"
#define MINFO_SWAP_MODE         "none"
#endif /* NC2 NC21 */

#if defined(CONFIG_MACH_NC5) 
#define MINFO_MAX_BTN 4
#define MINFO_MAX_BAY 2
#define MINFO_MAX_ESATA 1
#ifdef CONFIG_LGNAS_HAS_CDROM
# define MINFO_MAX_ODD 1
#else
# define MINFO_MAX_ODD 0
#endif
#define MINFO_MAX_LAN 1
#define MINFO_MAX_FAN 1
#define MINFO_MAX_LCD 1
#define MINFO_MAX_USB 3
#define MINFO_MAX_TEMP 0
#define MINFO_MAX_LED_HDD 2
#define MINFO_MAX_LED_ODD 1
#define MINFO_MAX_LED_USB 0
#define MINFO_MAX_LED_POW 0
#define MINFO_MAX_LED_LAN 1
#define MINFO_MAX_LED_FAIL 2

#define MINFO_SUPPORT_WIFI 			false
#define MINFO_SUPPORT_ACPOW 		true
#define MINFO_SUPPORT_WOL 			true
#define MINFO_SUPPORT_SCHPOW 		false
#define MINFO_SUPPORT_EXHDD 		false
#define MINFO_SUPPORT_EXODD		  false
#define MINFO_SUPPORT_BTNBACKUP true
#define MINFO_SUPPORT_BATTERY   true
#define MINFO_SUPPORT_LCDICON   true
#define MINFO_SUPPORT_MEMCARD   true
#define MINFO_SUPPORT_FAN1RPM		true
#define MINFO_SUPPORT_FANRPM    { MINFO_SUPPORT_FAN1RPM }
#define MINFO_SUPPORT_STREAMING	false
#define MINFO_SUPPORT_FAMILYCAST true
#define MINFO_SUPPORT_TRANSCODE	false
#define MINFO_SUPPORT_DTCPIP		false
#define MINFO_SUPPORT_FANINFO		true

#define MINFO_PATH_BAY  { "0:0:0:0", "0:2:0:0" }
#define MINFO_PATH_ODD  { "0:1:0:0" } 
#define MINFO_PATH_ESATA  { "0:3:0:0" } 
#if (MINFO_MAX_LCD > 0)
#  define MINFO_LCD1_LINE	1
#  define MINFO_LCD_LINE  { MINFO_LCD1_LINE, }
#endif
#define MINFO_FS_DEFAULT				"ext4"
#define MINFO_FS_BLOCK_SIZE			64
#define	MINFO_SMB_SO_RCVBUF			65536
#define	MINFO_SMB_SO_SNDBUF 		65536
#define	MINFO_SMB_NR_REQUESTS		256
#define	MINFO_SMB_READ_AHEAD_KB	512
#define MINFO_BOOTLOADER				"uboot"
#define MINFO_MICOM				      "micom"
#define MINFO_BUZZER				    "micom"
#define MINFO_SWAP_MODE         "hot"
#endif /* NC5 */

#if defined(CONFIG_MACH_NC3) 
#define MINFO_MAX_BTN 2
#define MINFO_MAX_BAY 2
#define MINFO_MAX_ESATA 0
#ifdef CONFIG_LGNAS_HAS_CDROM
# define MINFO_MAX_ODD 1
#else
# define MINFO_MAX_ODD 0
#endif
#define MINFO_MAX_LAN 1
#define MINFO_MAX_FAN 1
#define MINFO_MAX_LCD 0
#define MINFO_MAX_USB 3
#define MINFO_MAX_TEMP 0
#define MINFO_MAX_LED_HDD 2
#define MINFO_MAX_LED_ODD 1
#define MINFO_MAX_LED_USB 0
#define MINFO_MAX_LED_POW 1
#define MINFO_MAX_LED_LAN 0
#define MINFO_MAX_LED_FAIL 0

#define MINFO_SUPPORT_WIFI 			false
#define MINFO_SUPPORT_ACPOW 		true
#define MINFO_SUPPORT_WOL 			true
#define MINFO_SUPPORT_SCHPOW 		false
#define MINFO_SUPPORT_EXHDD 		false
#define MINFO_SUPPORT_EXODD		  false
#define MINFO_SUPPORT_BTNBACKUP true
#define MINFO_SUPPORT_BATTERY   false
#define MINFO_SUPPORT_LCDICON   false
#define MINFO_SUPPORT_MEMCARD   false
#define MINFO_SUPPORT_FAN1RPM		false
#define MINFO_SUPPORT_FANRPM    { MINFO_SUPPORT_FAN1RPM }
#define MINFO_SUPPORT_STREAMING	false
#define MINFO_SUPPORT_FAMILYCAST true
#define MINFO_SUPPORT_TRANSCODE	false
#define MINFO_SUPPORT_DTCPIP		false
#define MINFO_SUPPORT_FANINFO		true

#define MINFO_PATH_BAY { "0:0:0:0", "1:0:0:0" }
#if (MINFO_MAX_ODD > 0) 
#  define MINFO_PATH_ODD  { "2:0:0:0" } 
#endif
#define MINFO_FS_DEFAULT				"ext4"
#define MINFO_FS_BLOCK_SIZE			64
#define	MINFO_SMB_SO_RCVBUF			65536
#define	MINFO_SMB_SO_SNDBUF 		65536
#define	MINFO_SMB_NR_REQUESTS		256
#define	MINFO_SMB_READ_AHEAD_KB	512
#define MINFO_BOOTLOADER				"uboot"
#define MINFO_MICOM				      "micom"
#define MINFO_BUZZER				    "micom"
#define MINFO_SWAP_MODE         "cold"
#endif /* NC3 */

#if defined(CONFIG_MACH_NC1)
#define MINFO_MAX_BTN 4
#define MINFO_MAX_BAY 2
#define MINFO_MAX_ESATA 1
#ifdef CONFIG_LGNAS_HAS_CDROM
#  define MINFO_MAX_ODD 1
#else
#  define MINFO_MAX_ODD 0
#endif
#define MINFO_MAX_LAN 1
#define MINFO_MAX_FAN 1
#define MINFO_MAX_LCD 1
#define MINFO_MAX_USB 3
#define MINFO_MAX_TEMP 0
#define MINFO_MAX_LED_HDD 0
#define MINFO_MAX_LED_ODD 0
#define MINFO_MAX_LED_USB 0
#define MINFO_MAX_LED_POW 0
#define MINFO_MAX_LED_LAN 0
#define MINFO_MAX_LED_FAIL 2

#define MINFO_SUPPORT_WIFI 			false
#define MINFO_SUPPORT_ACPOW 		false
#define MINFO_SUPPORT_WOL 			false
#define MINFO_SUPPORT_SCHPOW 		false
#define MINFO_SUPPORT_EXHDD 		false
#define MINFO_SUPPORT_EXODD 		false
#define MINFO_SUPPORT_BTNBACKUP true
#define MINFO_SUPPORT_BATTERY   true
#define MINFO_SUPPORT_LCDICON   true
#define MINFO_SUPPORT_MEMCARD   true
#define MINFO_SUPPORT_FAN1RPM		true
#define MINFO_SUPPORT_FANRPM    { MINFO_SUPPORT_FAN1RPM }
#define MINFO_SUPPORT_STREAMING	false
#define MINFO_SUPPORT_FAMILYCAST true
#define MINFO_SUPPORT_TRANSCODE	false
#define MINFO_SUPPORT_DTCPIP		false
#define MINFO_SUPPORT_FANINFO		true

#define MINFO_PATH_BAY    { "1:0:0:0", "1:1:0:0" }
#define MINFO_PATH_ESATA  { "0:0:0:0" } 
#if (MINFO_MAX_ODD > 0) 
#  define MINFO_PATH_ODD  { "1:2:0:0" } 
#endif
#if (MINFO_MAX_LCD > 0)
#  define MINFO_LCD1_LINE	1
#  define MINFO_LCD_LINE { MINFO_LCD1_LINE,}
#endif
#define MINFO_FS_DEFAULT				"ext4"
#define MINFO_FS_BLOCK_SIZE			4
#define	MINFO_SMB_SO_RCVBUF			0
#define	MINFO_SMB_SO_SNDBUF 		0
#define	MINFO_SMB_NR_REQUESTS		0
#define	MINFO_SMB_READ_AHEAD_KB	0
#define MINFO_BOOTLOADER				"uboot"
#define MINFO_MICOM				      "micom"
#define MINFO_BUZZER				    "micom"
#define MINFO_SWAP_MODE         "hot"
#endif /* NC1 */

#ifdef CONFIG_MACH_MM1
#define MINFO_MAX_BTN 1
#define MINFO_MAX_BAY 1
#define MINFO_MAX_ESATA 0
#define MINFO_MAX_ODD 1
#define MINFO_MAX_LAN 1
#define MINFO_MAX_FAN 2
#define MINFO_MAX_LCD 0
#define MINFO_MAX_USB 3
#define MINFO_MAX_TEMP 2
#define MINFO_MAX_LED_HDD 1
#define MINFO_MAX_LED_ODD 1
#define MINFO_MAX_LED_USB 0 
#define MINFO_MAX_LED_POW 3
#define MINFO_MAX_LED_LAN 0
#define MINFO_MAX_LED_FAIL 0

#define MINFO_SUPPORT_WIFI 			true
#define MINFO_SUPPORT_ACPOW 		false
#define MINFO_SUPPORT_WOL 			false
#define MINFO_SUPPORT_SCHPOW 		false
#define MINFO_SUPPORT_EXHDD 		false
#define MINFO_SUPPORT_EXODD 		false
#define MINFO_SUPPORT_BTNBACKUP false
#define MINFO_SUPPORT_BATTERY   true
#define MINFO_SUPPORT_LCDICON   false
#define MINFO_SUPPORT_MEMCARD   false
#define MINFO_SUPPORT_FAN1RPM		false
#define MINFO_SUPPORT_FAN2RPM		true
#define MINFO_SUPPORT_FANRPM    { MINFO_SUPPORT_FAN1RPM, MINFO_SUPPORT_FAN2RPM }
#define MINFO_SUPPORT_STREAMING	false
#define MINFO_SUPPORT_FAMILYCAST true
#define MINFO_SUPPORT_TRANSCODE	true
#define MINFO_SUPPORT_DTCPIP		false
#define MINFO_SUPPORT_FANINFO		true

#define MINFO_PATH_BAY { "0:0:0:0" }
#if (MINFO_MAX_ODD > 0) 
#  define MINFO_PATH_ODD { "2:0:0:0" }
#endif
#define MINFO_CHANNEL_TEMP { 2,0 }
#define MINFO_FS_DEFAULT				"ext4"
#define MINFO_FS_BLOCK_SIZE			4
#define	MINFO_SMB_SO_RCVBUF			0
#define	MINFO_SMB_SO_SNDBUF 		0
#define	MINFO_SMB_NR_REQUESTS		0
#define	MINFO_SMB_READ_AHEAD_KB	0
#define MINFO_BOOTLOADER				"uboot"
#define MINFO_MICOM				      "micom"
#define MINFO_BUZZER				    "micom"
#define MINFO_SWAP_MODE         "none"
#endif /* MM1 */

#define MINFO_MAX_LED_ALL MINFO_MAX_LED_HDD \
												+ MINFO_MAX_LED_ODD \
												+ MINFO_MAX_LED_USB	\
												+ MINFO_MAX_LED_POW \
												+ MINFO_MAX_LED_LAN \
												+ MINFO_MAX_LED_FAIL

#if defined(CONFIG_MACH_NT3)
#define LED_HDD1_INDEX	1
#define LED_ODD1_INDEX	0
#define LED_USB1_INDEX	2
#define LED_LAN1_INDEX	3
#define LED_POW1_INDEX	4
#elif defined(CONFIG_MACH_NC5)
#define LED_HDD1_INDEX	0
#define LED_HDD2_INDEX	2
#define LED_ODD1_INDEX	1
#define LED_LAN1_INDEX	3
#define LED_FAIL1_INDEX	4
#define LED_FAIL2_INDEX	5
#else
#define LED_HDD1_INDEX	0
#define LED_HDD2_INDEX	(LED_HDD1_INDEX + 1)
#define LED_HDD3_INDEX	(LED_HDD1_INDEX + 2)
#define LED_HDD4_INDEX	(LED_HDD1_INDEX + 3)
#define LED_ODD1_INDEX	MINFO_MAX_LED_HDD
#define LED_USB1_INDEX	(LED_ODD1_INDEX + MINFO_MAX_LED_ODD)
#define LED_LAN1_INDEX	(LED_USB1_INDEX + MINFO_MAX_LED_USB)
#define LED_POW1_INDEX	(LED_LAN1_INDEX + MINFO_MAX_LED_LAN)
#define LED_POW2_INDEX	(LED_POW1_INDEX + 1)
#define LED_POW3_INDEX	(LED_POW1_INDEX + 2)
#define LED_FAIL1_INDEX	(LED_POW1_INDEX + MINFO_MAX_LED_POW)
#define LED_FAIL2_INDEX	(LED_FAIL1_INDEX + 1)
#define LED_FAIL3_INDEX	(LED_FAIL1_INDEX + 2)
#define LED_FAIL4_INDEX	(LED_FAIL1_INDEX + 3)
#endif

#define MINFO_MAX_LCD_LINE	MINFO_LCD1_LINE

#if defined(CONFIG_MACH_NT3) || defined(CONFIG_MACH_NC5) || defined(CONFIG_MACH_NC3)
#define MINFO_MTU_MAX	9000
#elif defined(CONFIG_MACH_NS2) 
#define MINFO_MTU_MAX	7146 /* JUMBO MAX(7K) - ETH_HLEN(14) - ETH_FCS_LEN(4) - VLAN_HLEN(4) */
#else
#define MINFO_MTU_MAX	9022
#endif
#define MINFO_MTU_MIN 576

struct model_max {
  int bay;
  int odd;
  int esata;
  int lan;
  int fan;
  int lcd;
  int lcd_str;
  int usb;
  int temp;
  int led_hdd;
  int led_odd;
  int led_usb;
  int led_power;
  int led_lan;
  int led_fail;
};

struct model_support {
  bool wifi;
  bool powerloss;
  bool wol;
  bool scheduled_power;
  bool external_hdd;
  bool external_odd;
  bool button_backup;
  bool battery;
  bool lcd_icon;
  bool memcard;
#if (MINFO_MAX_FAN > 0) 
  bool fan_rpm[MINFO_MAX_FAN];
#endif	
	bool streaming;
	bool familycast;
	bool transcode;
	bool dtcpip;
	bool button_burn;
	bool faninfo;
};

struct model_fs {
	char *dflt;
	int block_size;
};

struct model_mtu {
	int max;
	int min;
};

struct model_usb {
	char *host_mod_name;
	char *device_mod_name;
};

struct model_smb {
	int so_rcvbuf;
	int so_sndbuf;
	int nr_requests;
	int read_ahead_kb;
};
struct model_misc {
  char *bootloader;
  char *micom;
  char *buzzer;
};
struct model_swap {
	char *mode;
};
struct model_info {
  char *name;
	struct model_max max; 
	struct model_support support; 
  char *bay_path[MINFO_MAX_BAY];
#if (MINFO_MAX_ODD > 0)
  char *odd_path[MINFO_MAX_ODD];
#endif
#if (MINFO_MAX_ESATA > 0)
  char *esata_path[MINFO_MAX_ESATA];
#endif
#if (MINFO_MAX_LCD > 0)
  int lcd_line[MINFO_MAX_LCD];
#endif
#if (MINFO_MAX_TEMP > 0)
  int temp_channel[2];
#endif
	struct model_fs fs;
	struct model_mtu mtu;
	struct model_usb usb;
	struct model_smb smb;
	struct model_misc misc;
	struct model_swap swap;
};

#endif /* _MODEL_INFO_H */
