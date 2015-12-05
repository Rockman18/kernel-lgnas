#include <linux/input.h>
#include "model_info.h"
#include "hal.h"
#include "opcode.h"

#define	MODES_NUM	43

struct i2c_data {
	unsigned char id;
	unsigned char mode;
	unsigned char cur_blink;
	unsigned char data[4];
	unsigned char cur_pos;
};

struct mode_data {
	unsigned char cur_mode;
	unsigned char key_mode;
	unsigned char exec_mode;
	unsigned char next_mode[4];
	int priority;
};

struct addr_data {
	char ipaddr[4];
	char netmask[4];
	char gateway[4];
};

struct fsm_data {
	struct addr_data address;
	struct i2c_data msg_bit8;
	struct mode_data mode_info;
};

static struct fsm_data data;

static struct mode_data modes[MODES_NUM] = {
	{MAIN_WAIT_MODE, 0, 0, {ODD_BACKUP, INFORM_MODE, INFO_IPONLY}, PRIO_DEFAULT},
	{INFO_IPONLY, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}, PRIO_DEFAULT},
	{INFO_IP, 0, 1, {INFO_RAID, INFO_FWVER, INFORM_MODE}, PRIO_DEFAULT},
	{INFO_FWVER, 0, 1, {INFO_IP, INFO_MICOM, INFORM_MODE}, PRIO_DEFAULT},
	{INFO_MICOM, 0, 1, {INFO_FWVER, INFO_USAGE, INFORM_MODE}, PRIO_DEFAULT},
	{INFO_USAGE, 0, 1, {INFO_MICOM, INFO_TIME, INFORM_MODE}, PRIO_DEFAULT},
	{INFO_TIME, 0, 1, {INFO_USAGE, INFO_FAN, INFORM_MODE}, PRIO_DEFAULT},
	{INFO_FAN, 0, 1, {INFO_TIME, INFO_RAID, INFORM_MODE}, PRIO_DEFAULT},
	{INFO_RAID, 0, 1, {INFO_FAN, INFO_IP, INFORM_MODE}, PRIO_DEFAULT},
	{IP_MODE, 0, 0, {INFORM_MODE, USB_BACKUP, IP_STATIC_MODE}, PRIO_DEFAULT},	
	{IP_STATIC_MODE, 0, 0, {CANCEL_IP, IP_DHCP_MODE, IP_STATIC}, PRIO_DEFAULT},
	{IP_DHCP_MODE, 0, 0, {IP_STATIC_MODE, CANCEL_IP, IP_DHCP}, PRIO_DEFAULT},
	{CANCEL_IP, 0, 0, {IP_DHCP_MODE, IP_STATIC_MODE, MAIN_WAIT_MODE}, PRIO_DEFAULT},
	{IP_DHCP, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}, PRIO_DEFAULT},
	{IP_STATIC, 0, 1, {IP_MODE, IP_IPKEY, IP_IPKEY}, PRIO_DEFAULT},
	{IP_IPKEY, 1, 0, {MAIN_WAIT_MODE, IP_MASKSET, 0}, PRIO_DEFAULT},
	{IP_MASKSET, 0, 0, {IP_STATIC, IP_MASKKEY, IP_MASKKEY}, PRIO_DEFAULT},		
	{IP_MASKKEY, 1, 0, {IP_MASKSET, IP_GWSET, 0}, PRIO_DEFAULT},
	{IP_GWSET, 0, 1, {IP_MASKSET, IP_GWKEY, IP_GWKEY}, PRIO_DEFAULT},
	{IP_GWKEY, 1, 0, {IP_GWSET, IP_DONE, 0}, PRIO_DEFAULT},
	{IP_DONE, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}, PRIO_DEFAULT},
	{USB_BACKUP, 0, 0, {IP_MODE, ODD_BACKUP, INC_DONE}, PRIO_DEFAULT},
	{INC_DONE, 0, 1, {USB_CANCEL, MAIN_WAIT_MODE, MAIN_WAIT_MODE}, PRIO_DEFAULT},
	{ODD_BACKUP, 0, 0, {USB_BACKUP, MAIN_WAIT_MODE, ODD_DATA}, PRIO_DEFAULT},		
	{ODD_DATA, 0, 0, {CANCEL_ODD, ODD_IMAGE, DATA_DONE}, PRIO_DEFAULT},
	{ODD_IMAGE, 0, 0, {ODD_DATA, CANCEL_ODD, IMAGE_DONE}, PRIO_DEFAULT},		
	{CANCEL_ODD, 0, 0, {ODD_IMAGE, ODD_DATA, MAIN_WAIT_MODE}, PRIO_DEFAULT},		
	{DATA_DONE, 0, 1, {ODD_CANCEL, MAIN_WAIT_MODE, MAIN_WAIT_MODE}, PRIO_DEFAULT},		
	{IMAGE_DONE, 0, 1, {ODD_CANCEL, MAIN_WAIT_MODE, MAIN_WAIT_MODE}, PRIO_DEFAULT},
	{USB_ONETOUCH, 0, 0, {MAIN_WAIT_MODE, USB_ONETOUCH, ONETOUCH_DONE}, PRIO_DEFAULT},	
	{ONETOUCH_DONE, 0, 1, {USB_CANCEL, MAIN_WAIT_MODE, MAIN_WAIT_MODE}, PRIO_DEFAULT},
	{USB_CANCEL, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}, PRIO_USBCANCEL},
	{ODD_CANCEL, 0, 1, {MAIN_WAIT_MODE, MAIN_WAIT_MODE, MAIN_WAIT_MODE}, PRIO_ODDCANCEL},
	{HIB_MODE, 0, 0, {ODD_BACKUP, MAIN_WAIT_MODE, HIB_DONE}, PRIO_DEFAULT},
	{HIB_DONE, 0, 1, {0, 0, HIB_EXIT}, PRIO_DEFAULT},
	{MESSAGE_MODE, 0, 0, {MAIN_WAIT_MODE, IP_MODE, INFO_FAN}, PRIO_DEFAULT},
	{INFORM_MODE, 0, 0, {MAIN_WAIT_MODE, IP_MODE, INFO_IP}, PRIO_DEFAULT},
	{HIB_EXIT, 0, 1, {0, 0, 0}, PRIO_HIBEXIT},
	{USB_CANCEL_MODE, 0, 0, {USB_NOCANCEL, USB_CANCEL_MODE, USB_CANCEL}, PRIO_DEFAULT},
	{USB_NOCANCEL, 0, 1, {USB_CANCEL_MODE, 0, 0}, PRIO_DEFAULT},
	{ODD_CANCEL_MODE, 0, 0, {ODD_NOCANCEL, ODD_CANCEL_MODE, ODD_CANCEL}, PRIO_DEFAULT},
	{ODD_NOCANCEL, 0, 1, {ODD_CANCEL_MODE, 0, 0}, PRIO_DEFAULT},
	{POWER_OFF, 0, 1, {0, 0, 0}, PRIO_POWEROFF}
};

static int key_process(int button_id)
{
	struct i2c_data *msg = &data.msg_bit8;
	int pos, val;

	switch(button_id) {
		case KEY_LEFT:
			if(msg->cur_pos == 1)
				return 1;
			else if(msg->cur_pos % 4 == 1)
				msg->cur_pos -= 2;
			else				
				msg->cur_pos--;
			break;
		case KEY_RIGHT:
			if(msg->cur_pos == 15) { 
				msg->cur_pos = 1;
				if(msg->mode == IP_IPKEY) {
					memcpy(data.address.ipaddr, msg->data, 4);
				}
				if(msg->mode == IP_MASKKEY) {
					memcpy(data.address.netmask, msg->data, 4);
				}
				if(msg->mode == IP_GWKEY) {
					memcpy(data.address.gateway, msg->data, 4);
				}
				return 1;
			}
			else if(msg->cur_pos % 4 == 3)
				msg->cur_pos += 2;
			else
				msg->cur_pos++;
			break;	
		case KEY_SETUP:
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

void change_fsm(void)
{
	struct mode_data *fsm = &data.mode_info;
	int i;

	data.msg_bit8.cur_pos = 1;

	if(fsm->cur_mode == IP_IPKEY || fsm->cur_mode == INFO_IP)
		memcpy(data.msg_bit8.data, data.address.ipaddr, 4);
	if(fsm->cur_mode == IP_MASKKEY)
		memcpy(data.msg_bit8.data, data.address.netmask, 4);
	if(fsm->cur_mode == IP_GWKEY)
		memcpy(data.msg_bit8.data, data.address.gateway, 4);

	for(i = 0; i < MODES_NUM; i++) {
		if(fsm->cur_mode == modes[i].cur_mode) {
			fsm->key_mode = modes[i].key_mode;
			fsm->exec_mode = modes[i].exec_mode;
			fsm->next_mode[0] = modes[i].next_mode[0];
			fsm->next_mode[1] = modes[i].next_mode[1];
			fsm->next_mode[2] = modes[i].next_mode[2];
			fsm->next_mode[3] = modes[i].next_mode[3];
			fsm->priority = modes[i].priority;
			break;
		}
	}
}
static void process_mode(int button_id)
{
	struct mode_data *fsm = &data.mode_info;

	switch(button_id) {
		case KEY_POWER:
			fsm->cur_mode = POWER_OFF;
			change_fsm();
			break;
		case KEY_LEFT:
			if(fsm->next_mode[0] == 0)
				return;
			fsm->cur_mode = fsm->next_mode[0];
			change_fsm();
			break;
		case KEY_RIGHT:
			if(fsm->next_mode[1] == 0)
				return;
			fsm->cur_mode = fsm->next_mode[1];
			change_fsm();
			break;
		case KEY_SETUP:
			if(fsm->next_mode[2] == 0)
				return;
			fsm->cur_mode = fsm->next_mode[2];
			change_fsm();
			break;
	}
}

int fsm_comm_proc(struct i2c_client *client, int button_id, int priority)
{
	data.msg_bit8.id = MICOM_ID_SET1;

	if(data.mode_info.key_mode) {
		if(key_process(button_id) != 0)
			process_mode(button_id);
	}
	else {
		nashal_micom_read(client, (u8 *) &data.msg_bit8, sizeof(struct i2c_data));
		data.msg_bit8.id = MICOM_ID_SET1;
		data.mode_info.cur_mode = data.msg_bit8.mode;
		change_fsm();
		process_mode(button_id);
	}
	
	if(data.mode_info.priority < priority)
		return 0;

	data.msg_bit8.mode = data.mode_info.cur_mode;
	nashal_micom_write(client, (u8 *) &data.msg_bit8, sizeof(struct i2c_data));

	if((data.msg_bit8.mode == IP_GWKEY && data.mode_info.cur_mode == IP_DONE) ||
		data.mode_info.exec_mode)
		return 1;
	else
		return 0;
}

void fsm_init(void)
{
	data.msg_bit8.id = MICOM_ID_SET1;
	data.msg_bit8.cur_pos = 1;
	data.mode_info = modes[0];
	data.msg_bit8.mode = data.mode_info.cur_mode;
}

int fsm_set_mode(struct i2c_client *client, int mode)
{
	data.mode_info.cur_mode = mode;
	change_fsm();
	data.msg_bit8.id = MICOM_ID_SET1;
	data.msg_bit8.mode = data.mode_info.cur_mode;
	return nashal_micom_write(client,
					(u8 *) &data.msg_bit8, sizeof(struct i2c_data));	
}

int fsm_get_mode(void)
{
	return data.msg_bit8.mode;
}

static int set_address(char *dest, char *src)
{
	char *p;
	long val;
	int ret, idx = 0;

	while((p = strsep((char **) &src, ".")) != NULL) {
		ret = strict_strtol(p, 10, &val);
		if(!ret)
			dest[idx++] = val;
		else
			break;
	}
	return ret;
}

int fsm_store_address(const char *buf)
{
	char *p;
	int idx = 0;
	
	while((p = strsep((char **) &buf, " ")) != NULL) {
		if(set_address(data.address.ipaddr + idx, p))
			return 0;
		idx += 4;
		if(idx >= 12)
			break;
	}
	return 1;
}

int fsm_show_address(char *buf)
{
	struct addr_data addr = data.address;
	return sprintf(buf,
					"%d.%d.%d.%d\n%d.%d.%d.%d\n%d.%d.%d.%d\n",
					addr.ipaddr[0], addr.ipaddr[1], addr.ipaddr[2], addr.ipaddr[3],
					addr.netmask[0], addr.netmask[1], addr.netmask[2], addr.netmask[3],
					addr.gateway[0], addr.gateway[1], addr.gateway[2], addr.gateway[3]);
}
