#ifndef _LGNAS_EVENT_H_
#define _LGNAS_EVENT_H_

#define FRONT_DEVICE_NAME 	"lgnas_front"
#define FRONT_MAJOR 		241
#define FRONT_BUFSIZE		8
#define EVENT_DEVICE_NAME 	"lgnas_btns"
#define EVENT_MAJOR 		240

struct front_buf {
	char in[FRONT_BUFSIZE];
	char out[FRONT_BUFSIZE];
};

typedef union _FRONTDATA
{
	u32 dword[2];
	u8	byte[8];
}FRONTDATA;

typedef struct _USER_DATA
{
	char ipaddr[4];
	char netmask[4];
	char gateway[4];
}USER_DATA;

#define GETMESSAGE  _IOWR('c', 150, FRONTDATA)
#define INITIPSET   _IOWR('c', 151, USER_DATA)
#define GETIPSET    _IOWR('c', 152, USER_DATA)
#define SETLOCK     _IOWR('c', 153, int)

#define GET_KEY_STATE	0x00
#define GET_INT_RSN		0x01
#define GET_MC_VER		0x02
#define GET_IP_ADDR 	0x03
#define GET_NET_MASK 	0x04
#define GET_GATE_WAY 	0x05
#define GET_FILE_NAME 	0x06
#define GET_SER_NUM0 	0x07
#define GET_SER_NUM1 	0x08
#define GET_BOOT_TYPE 	0x09
#define GET_AGING_TYPE 	0x0a
#define GET_FAN_RPM 	0x0b
#define SET_PWD_INIT 	0x0d

#define VKEY_POWER		0x20
#define VKEY_DISPLAY	0x21
#define VKEY_BACKUP		0x22
#define VKEY_EJECT		0x23
#define VKEY_BURN		0x24
#define VKEY_USB		0x25
#define VKEY_CANCEL		0x26
#define VKEY_DHCP 		0x30

#define SET_RD_DATA 	0x80
#define SET_SV_NAME_0 	0x81
#define SET_SV_NAME_1 	0x82
#define SET_IP_ADDR 	0x83
#define SET_TIME 		0x84
#define SET_ST_CAPA 	0x85
#define SET_SYS_STATE 	0x86
#define SET_SVC_CODE 	0x87
#define SET_RAID_SYNC 	0x88
#define SET_USB_INFO 	0x89
#define SET_UPS_INFO 	0x8A
#define SET_HDD_FAIL 	0x8B
#define SET_MN_VER2 	0x8C
#define SET_MN_VER 		0x8D
#define SET_LED_STATE 	0x8E
#define SET_FAN_RPM 	0x8F
#define SET_VOL_NAME_0 	0x90
#define SET_VOL_NAME_1 	0x91
#define SET_NET_MASK 	0x92
#define SET_GATE_WAY 	0x93
#define SET_BUZ 		0xA0
#define SET_FILE_NAME 	0xD0
#define SET_ERR_MSG 	0xD1
#define SET_MSG_KEEP 	0xD2
#define SET_MC_UPDATE 	0xF0

#endif
