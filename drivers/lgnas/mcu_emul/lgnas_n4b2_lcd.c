#include "lgnas.h"
#include "lgnas_if.h"
#include "lgnas_sym.h"

/*****************************************************************************
 * N4B2 LCD Function
 ****************************************************************************/
static DEFINE_SPINLOCK(sio_lock); 
 /* initialize text LCD module */
void lcd_initialize(void)
{
  dprintk("%s is called\n", __FUNCTION__);

	lcd_backlight(NAS_INIT_LCD_BRIGHT_FULL);
	LCD_LOW_DA;
	LCD_LOW_RW_RS_EN;

	LCD_DELAY_100MS;
	LCD_W_U_NIBBLE(0x30);
	LCD_STROBE;
	//lcd_command(0x38);    // function set(8 bit, 2 line, 5x7 dot)
	lcd_command(0x28);      // function set(4 bit, 2 line, 5x7 dot)
	LCD_DELAY_50US;
	lcd_command(0x28);      // function set(4 bit, 2 line, 5x7 dot)
	LCD_DELAY_50US;
	lcd_command(0x0C);      // display control(display ON, cursor OFF)
	LCD_DELAY_50US;
	lcd_command(0x01);      // clear display
	LCD_DELAY_50US;
	lcd_command(0x06);      // entry mode set(increment, not shift)
	LCD_DELAY_50US;
	LCD_DELAY_200MS;
}

void lcd_init(void)
{
	//default message to lcd
  dprintk("%s is called\n", __FUNCTION__);
	lcd_initialize();
	lcd_string(0x80,LCD_DEF_STR1);
	lcd_string(0xc0,LCD_DEF_STR2);
}

void inline lcd_backlight_onoff(int on)
{
  dprintk("%s is called\n", __FUNCTION__);
  if(on)
 	  SIOClearBit(MASKS2_LCD_BL_EN,	SIO_GPIO_DATA_2);
  else
 	  SIOSetBit(	MASKS2_LCD_BL_EN,	SIO_GPIO_DATA_2);
}
void inline lcd_backlight(u32 pwm)
{
	SHWMOutB((u8)pwm, 0xc3);
}
void inline lcd_write(unsigned char data)
{
	LCD_DELAY_2MS;
  spin_lock(&sio_lock); 
	LCD_W_U_NIBBLE(data);
	LCD_LOW_RS;
	LCD_STROBE;
	LCD_W_L_NIBBLE(data);
	LCD_STROBE;
  spin_unlock(&sio_lock); 
}
/* write a command(instruction)to text LCD */
void inline lcd_command(unsigned char command)
{
	LCD_LOW_RW_RS_EN;
	lcd_write(command);
}
/* display a character on text LCD */
void inline lcd_data(unsigned char data)
{
	LCD_DELAY_2MS;
  
  spin_lock(&sio_lock); 
	LCD_W_U_NIBBLE(data);
	LCD_HIGH_RS;
	LCD_STROBE;
	LCD_W_L_NIBBLE(data);
	LCD_STROBE;
  spin_unlock(&sio_lock); 
}

/* display a string on LCD */
void inline lcd_string(unsigned char command, const char *string)
{
	int i=0;
	int j=0;
	// start position of string
  lcd_command(command);
  while((*string != '\0')&&(*string != '\n')){
    lcd_data(*string);
    string++;
    i++;
  }
	// clear remain line
	j = (i & 0xfff0) + 16;
	if((i%16) && (i < j)){
		while(i < j){
  	 	lcd_data(0x20);
			i++;
		}
	}
}

// Fill Blank LCD
void inline lcd_fill_blank(char pos, unsigned char size)
{
    lcd_command(0x80|pos);

    while(size--)
      lcd_data(' ');
}

// Cursor ON/OFF
void inline lcd_cursor_on(unsigned char on, unsigned char pos)
{
	lcd_command(0x80|pos);

  if(on)
  	lcd_command(0x0f);
  else
  	lcd_command(0x0c);
}

// write a string of chars to the LCD
void inline lcd_put_s(unsigned char pos, char *s, unsigned char num)
{
  //dprintk("%s is called string is %s\n", __FUNCTION__, s);
  lcd_command(0x80|pos);

  while(num--)
    lcd_data(*s++);
}

void inline lcd_disp(char pos, unsigned short value)
{
  char data[5];

  data[0] = (char)(value/1000) + '0';
  value = (value%1000);
  data[1] = (char)(value/100) + '0';
  value = (value%100);
  data[2] = (char)(value/10) + '0';
  value = (value%10);
  data[3] = (char)value + '0';
  data[4] = 0x00;

  lcd_put_s(pos, data, 4);
}

void inline lcd_disp_b0(char pos, unsigned char value)
{
  char data[2];

  value = (value%10);
  data[0] = value + '0';
  data[1] = 0x00;

  lcd_put_s(pos, data, 1);
}
void inline lcd_disp_b(char pos, unsigned char value)
{
  char data[3];

  data[0] = (value/10) + '0';
  value = (value%10);
  data[1] = value + '0';
  data[2] = 0x00;

  lcd_put_s(pos, data, 2);
}

void inline lcd_disp_b2(char pos, unsigned char value)
{
  char data[4];

  data[0] = (value/100) + '0';
  value = (value%100);
  data[1] = (value/10) + '0';
  value = (value%10);
  data[2] = value + '0';
  data[3] = 0x00;

  lcd_put_s(pos, data, 3);
}

void inline lcd_disp_h(char pos, unsigned char value)
{
  char data[3];
  unsigned char bTmp;

  bTmp = (value >> 4)&0x0f;
  if(bTmp < 10) data[0] = bTmp + '0';
  else data[0] = (bTmp - 10) + 'a';

  bTmp = (value)&0x0f;
  if(bTmp < 10) data[1] = bTmp + '0';
  else data[1] = (bTmp - 10) + 'a';

  data[2] = 0x00;

  lcd_put_s(pos, data, 2);
}
//
