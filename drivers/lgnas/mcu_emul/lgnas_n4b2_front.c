/*****************************************************************************
 * lgnas_n4b2_front.c
 ****************************************************************************/
#include <linux/types.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#include "lgnas.h"
#include "lgnas_if.h"
#include "lgnas_sym.h"
#include "lgnas_event.h"


int lgnas_front_started = 0;
u32 lgnas_btn_lock = 0;
u32 lgnas_key = 0;
u32 lgnas_backlight = 0;
u32 lgnas_sysfan = 0;

void gpio_s3_init(void)
{
  SHWMOutB(NAS_INIT_FAN_PWM, 0xb3);
	lcd_initialize();
}

int lgnas_sysfan_rpm(void)
{
    int rpm;
    rpm = SHWMInW(0xb0);
    rpm = rpm ? (1500000 / rpm) : 0;
    return (rpm < 500)? 0: rpm;
}
void lgnas_front_start(void)
{
	if(!lgnas_front_started){

  	FRONTDATA temp = {
  		.byte[0]=0x86, 
  		.byte[1]=0x0, 
  		.byte[2]=0x1,
  		.byte[7]=0x87,
  	};
    front_write((u8*)&temp,8);
    gpio_s3_init();
    lgnas_front_started = 1;
  } 
}
unsigned char lgnas_front_checksum(u8* data, u8 len)
{
	unsigned char bchecksum = 0;
	int i;

	for(i=0; i<len; i++){ 
		bchecksum ^= *(data+i);	
	}
	return bchecksum;
}
/*
	mode 
	d0: file name
	d1: short message
	d2: keep message
	d3: default message 
*/
void set_message1(struct nashal_data *data, u8 mode, const char *msg, int len)
{
	u8 temp[134];
	char *p;
	int i;

	if(len >128){
		printk("[LGNAS] message over length!\n");
		return;
	}
  lgnas_front_start();

	memset(temp, 0x00, 134);
	memset(data->mdata.filename, 0x00, sizeof(data->mdata.filename));

  sscanf(msg, "%x %x %s\n", &data->mdata.file_num, &data->mdata.file_total, data->mdata.filename);

  p = strstr(msg, data->mdata.filename);
  for(i=0; i<128; i++){
    if((*(p+i) == '\0')||(*(p+i) == '\n')) break;
    *(char*)(temp+4+i) = *(p+i);
    *(data->mdata.filename+i) = *(p+i);
  }

	temp[0] =  mode;
	temp[1] =  i + 2;
	temp[2] =  data->mdata.file_num & 0xff;
	temp[3] =  data->mdata.file_total & 0xff;
	temp[i + 4] = lgnas_front_checksum(temp, i + 4);
	temp[i + 5] = temp[i + 4];

	front_write(temp, i + 5);
}

int set_message(u8 mode, char *msg, int len)
{
	u8 temp[134];

  dprintk("%s is called\n", __FUNCTION__);
	memset(temp, 0x00, 134);

	if(len >128){
		printk("[LGNAS] message over length!\n");
		return -1;
	}
  lgnas_front_start();
	temp[0] =  mode;
	temp[1] =  len + 2;
	memcpy(temp+4, msg, len);
	temp[len + 4] = lgnas_front_checksum(temp, len + 4);
	temp[len + 5] = temp[len + 4];

	return front_write(temp, len + 5);
}
void set_hostname(struct nashal_data *data, const char *msg, int len)
{
	u8 temp[8];
	u8 blen;

	memset(temp, 0x00, 8);
  memset(data->mdata.hostname, 0x00,16);
  blen = (len >16)? 16 : len;
  memcpy(data->mdata.hostname, msg, blen);

	if(len <= 6){
		blen = len;
		temp[0] =  SET_SV_NAME_0;
		memcpy(temp + 1,msg,blen);
		temp[7] = lgnas_front_checksum(temp, blen + 1);
	}else{
		blen = 6;
		temp[0] =  SET_SV_NAME_0;
		memcpy(temp + 1,msg,blen);
		temp[7] = lgnas_front_checksum(temp, blen + 1);
		front_write(temp, 8);

		memset(temp, 0x00, 8);
		blen = (len > 12)? 6: len - 6;
		temp[0] =  SET_SV_NAME_1;
		memcpy(temp + 1,msg + 6,blen);
		temp[7] = lgnas_front_checksum(temp, blen + 1);
	}
	front_write(temp, 8);
}
void set_fwversion(struct nashal_data *data, const char *msg, int len)
{
	u8 temp[8];
	u8 blen;

	memset(temp, 0x00, 8);
  memset(data->mdata.fwversion, 0x00,16);
  blen = (len >16)? 16 : len;
  memcpy(data->mdata.fwversion, msg, blen);

	if(len <= 6){
		blen = len;
		temp[0] =  SET_MN_VER;
		memcpy(temp + 1,msg,blen);
		temp[7] = lgnas_front_checksum(temp, blen + 1);
	}else{
		blen = 6;
		temp[0] = SET_MN_VER;
		memcpy(temp + 1,msg,blen);
		temp[7] = lgnas_front_checksum(temp, blen + 1);
		front_write(temp, 8);

		memset(temp, 0x00, 8);
		blen = (len > 12)? 6: len - 6;
		temp[0] =  SET_MN_VER2;
		memcpy(temp + 1,msg + 6,blen);
		temp[7] = lgnas_front_checksum(temp, blen + 1);
	}
	front_write(temp, 8);
}


/*
 * lgnas_front interface
 */
int front_read(u8* pData, int iLen)
{
	u8	bTmp1;

	// read from front
	for(bTmp1=0; bTmp1 <= (DATA_FRM_NUM-1); bTmp1++) *(pxbuf+bTmp1) = *(pebuf+bTmp1);
	//for(bTmp1=0; bTmp1 <= 3; bTmp1++) *(pData+bTmp1) = *(pxbuf+bTmp1);
	for(bTmp1=0; bTmp1 <= (DATA_FRM_NUM-1); bTmp1++) *(pData+bTmp1) = *(pxbuf+bTmp1);

    if(*pxbuf == 0x00)
    {
	    ;	//key_state &= (~*(pxbuf+1));
        ;	//com_make_rd_data(0x00);
    }
    else if((*pxbuf == 0x01) && (*(pxbuf+1) == 0x01)
        && (*(pxbuf+2) == 0x00))
    {
        ;	//key_state &= (~*(pxbuf+3));
        ;	//com_make_rd_data(0x01);
    }
    else if(*pxbuf == 0x03)
    {
        *(pip + 0) = *(pip_s + 0);
    }

	dprintk("Front read data : %02x %02x %02x %02x %02x %02x %02x %02x (Len: %d)\n",
		pData[0], pData[1], pData[2], pData[3], pData[4], pData[5], pData[6], pData[7], iLen);

	return 0;
}

int front_write(u8* pData, int iLen)
{
	u8	bTmp1;
  
	dprintk("Front write data : %02x %02x %02x %02x %02x %02x %02x %02x (Len: %d)\n",
		pData[0], pData[1], pData[2], pData[3], pData[4], pData[5], pData[6], pData[7], iLen);

	if(!com_flag){
		long_data_f = 0;
    /* long message */
		if(((*pData)&0xf0) == 0xd0){
			long_data_f = 1;

      if((*pData == 0xd0) || (*pData == 0xd1) || (*pData == 0xd2) || (*pData == 0xd3))
	 	    for(bTmp1=0; bTmp1<MAX_MSG_SIZE; bTmp1++) 
          wc_msg[bTmp1] = 0x00;
		}

		if(long_data_f){
			for(bTmp1=0; bTmp1 <= 3; bTmp1++) 
        *(pdbuf+bTmp1) = *(pData+bTmp1);
			for(bTmp1=4; bTmp1 <= (*(pData+1)+2); bTmp1++)
        wc_msg[bTmp1 - 4] = *(pData+bTmp1);
			
      *(pdbuf+7) = *(pData+bTmp1);

      if((*pData == 0xd1) || (*pData == 0xd2) || (*pData == 0xd3))
        msg_check_flag_f = 1;

		}else
			for(bTmp1=0; bTmp1 <= (DATA_FRM_NUM-1); bTmp1++) 
        *(pdbuf+bTmp1) = *(pData+bTmp1);

		com_flag = 1;
		com_processing(ns2data);
	}

	return 0;
}

/*
 * Super IO Ctrl Function
 */
void inline SIOSetBit(u8 data, int x)
{
	u8 rdata;
	outb(x,SIO_BASE);
	rdata=inb(SIO_BASE+1) | data;
	outb(x,SIO_BASE);
	outb(rdata, SIO_BASE+1);
}
void inline SIOClearBit(u8 data, int x)
{
	u8 rdata;
	outb(x,SIO_BASE);
	rdata=inb(SIO_BASE+1) & ~data;
	outb(x,SIO_BASE);
	outb(rdata, SIO_BASE+1);
}
u8 inline SIOInB(int x)
{
	outb(x,SIO_BASE);
	return inb(SIO_BASE+1);
}
void inline SIOOutB(unsigned char data, int x)
{
	outb(x,SIO_BASE);
	outb(data, SIO_BASE+1);
}
u8 inline SHWMInB(int addr)
{
  outb(addr,SIO_PCI_BASE + ADDR_REG_OFFSET);
  return inb(SIO_PCI_BASE + DATA_REG_OFFSET);
}
void inline SHWMOutB( unsigned char data, unsigned char addr )
{
  outb(addr,SIO_PCI_BASE + ADDR_REG_OFFSET);
  outb(data,SIO_PCI_BASE + DATA_REG_OFFSET);
}
u16 inline SHWMInW(int addr)
{
  u16 val;
  outb(addr++, SIO_PCI_BASE + ADDR_REG_OFFSET);
  val = inb(SIO_PCI_BASE + DATA_REG_OFFSET) << 8;
  outb(addr, SIO_PCI_BASE + ADDR_REG_OFFSET);
  val |= inb(SIO_PCI_BASE + DATA_REG_OFFSET);
  return val;
}
/*
 * CMOS Register Control
 */
unsigned char inline cmos_read_byte(int x)
{
	outb(x,CMOS_ADD_BASE);
	return inb(CMOS_ADD_BASE+1);
}

static void inline cmos_write_byte(unsigned char data, int x)
{
	outb(x,CMOS_ADD_BASE);
	outb(data, CMOS_ADD_BASE+1);
}

static void inline cmos_postset_biso118(void)
{
  u8 power,sio,qlast,wol;

  power = cmos_read_byte(CMOS_ADD_POWER) & 0x03;
  wol   = cmos_read_byte(CMOS_ADD_WOL) & 0x01;
  qlast = cmos_read_byte(CMOS_ADD_QLAST);
  //1.SIO
  SIOSel(SIO_F71808E_LD_PME_ACPI);
  sio = SIOInB(SIO_KEEPLASTSTATE);
  sio &= ~0x7;
  sio |= 0x4;
	if (!power && !wol)
    sio |= 0x2;
  SIOOutB(sio, SIO_KEEPLASTSTATE);
  // Recover LDN for SIO GPIO
  SIOSel(SIO_F71808E_LD_GPIO);
  //2.Q-LAST
  qlast &= ~0x01;
	if (!power && wol)
    qlast |= 0x01;
  cmos_write_byte(qlast, CMOS_ADD_QLAST);
}

int cmos_control(u8 addr,u8 set,u8 onoff)
{
  u8 read=0,write=0;

  read = cmos_read_byte(addr);
  if(set){ 
    read &= ~0x01;
    if((addr == CMOS_ADD_POWER)||(addr == CMOS_ADD_WOL ))
      read &= ~0x02;
    
    write = read | onoff;
    cmos_write_byte(write, addr);
    read=cmos_read_byte(addr);
    cmos_postset_biso118();
/*
    // to application
    if(addr == CMOS_ADD_WOL){
      if(onoff)
        ret=system("ethtool -s eth0 wol g");
      else
        ret=system("ethtool -s eth0 wol d");
    }
*/
    if(read == write){
      return 0;
    }else{
      printk("CMOS Set Error [add 0x%02x : val 0x%02x]\n",addr, read);
			return -EFAULT;
    }
  }else{
    if(addr == CMOS_ADD_POWER){
      if((read & 0x03) == 2) return 1; /* resume */
      else                   return 0;
    }else if(addr == CMOS_ADD_WOL){
      if((read & 0x03) == 1) return 1; /* 0 or 3 is off */
      else                   return 0;
    }else
      return (read & 0x01);
  }
}
int cmos_control_rtc(
      unsigned char set
    , unsigned char date 
    , unsigned char hour
    , unsigned char minute
    , unsigned char second)
{
  unsigned char sec_read = 0;
  unsigned char min_read = 0;
  unsigned char hour_read = 0;
  unsigned char date_read = 0;
     
  if(set){ 
    if( (date > 31)    ||
        (hour >= 24)   ||
        (minute >= 60) ||
        (second >= 60)){
      printk("Invalid time value %d %d:%d:%d\n",date,hour,minute,second);
      return -EINVAL; 
    }
    cmos_write_byte(date,    CMOS_ADD_RTC_DATE);
    date_read=cmos_read_byte(CMOS_ADD_RTC_DATE);
    cmos_write_byte(hour,    CMOS_ADD_RTC_HOUR);
    hour_read=cmos_read_byte(CMOS_ADD_RTC_HOUR);
    cmos_write_byte(minute,  CMOS_ADD_RTC_MIN);
    min_read =cmos_read_byte(CMOS_ADD_RTC_MIN);
    cmos_write_byte(second,  CMOS_ADD_RTC_SEC);
    sec_read =cmos_read_byte(CMOS_ADD_RTC_SEC);
  
    if(  (sec_read  != second)
      || (min_read  != minute)
      || (hour_read != hour)
      || (date_read != date)){
      printk("CMOS Set Error [sec : %d min : %d hour : %d date :%d]\n",
         sec_read,min_read,hour_read,date_read);
		 return -EFAULT;
    }
    return 0;
  }else{
    /* get info */
    date_read=cmos_read_byte(CMOS_ADD_RTC_DATE);
    hour_read=cmos_read_byte(CMOS_ADD_RTC_HOUR);
    min_read=cmos_read_byte(CMOS_ADD_RTC_MIN);
    sec_read=cmos_read_byte(CMOS_ADD_RTC_SEC);
    return 0;
  }
}

