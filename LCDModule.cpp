//// This module use NewliquidCrystal library from https://bitbucket.org/fmalpartida/new-liquidcrystal/downloads
//// Later it'll be rewritten without using libraries
#include <compat/twi.h>
#include <util/delay.h>
#include <string.h>

#include "LCDModule.h"

unsigned char twi_start(unsigned char address)
{
    uint8_t tmp;
    TWCR = _BV(TWEN) | _BV(TWINT) | _BV(TWSTA);
    
    while(!(TWCR & _BV(TWINT)));
    
    tmp = TW_STATUS & 0xF8;
    if ( (tmp != TW_START) && (tmp != TW_REP_START)) return 1;

    // send address
    TWDR = address;
    TWCR = _BV(TWINT) | _BV(TWEN);

    while(!(TWCR & _BV(TWINT)));

    tmp = TW_STATUS & 0xF8;
    if ( (tmp != TW_MT_SLA_ACK) && (tmp != TW_MR_SLA_ACK) ) return 1;

    return 0;
}

void twi_stop(void)
{
    TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);  
    while(TWCR & _BV(TWSTO));
}

void twi_send_byte(unsigned char data)
{
    twi_start(LCDEX_ADDR);

    TWDR = data | _BV(LCDEX_LIGHT); //temporary
    TWCR = (1<<TWINT) | (1<<TWEN);

    while(!(TWCR & (1<<TWINT)));

    twi_stop();
}

void strobe_en(uint8_t data)
{
    data |= _BV(LCDEX_EN);
    twi_send_byte(data);
    lcd_e_delay();
    data &= ~_BV(LCDEX_EN);
    twi_send_byte(data);
}

#define TWI_PORT PORTC
#define TWI_DDR DDRC
#define TWI_SCL PC5
#define TWI_SDA PC4

void LCD_init()
{

    TWI_PORT |= _BV(TWI_SCL) | _BV(TWI_SDA);  // Set Pull-Up on SCL, SDA
    TWI_DDR &=~(_BV(TWI_SCL) | _BV(TWI_SDA)); // Set SCL,SDA as input

    // set bitrate
    TWSR = 0;                         /* no prescaler */
    TWBR = ((F_CPU/SCL_CLOCK)-16)/2;  /* must be > 10 for stable operation */    

    uint8_t lcd_data = 0b00000000;
    twi_send_byte(lcd_data);
    _delay_ms(40);

    // 8bit mode
    lcd_data = 0b00110000;
    twi_send_byte(lcd_data);

    strobe_en(lcd_data);  
    _delay_ms(5);              //delay > 4,1ms
    strobe_en(lcd_data);
    _delay_us(100);
    strobe_en(lcd_data);
    _delay_us(100);

    // set 4bit mode
    lcd_data = 0b00100000;
    twi_send_byte(lcd_data);
    strobe_en(lcd_data);
    twi_send_byte(lcd_data);
    _delay_us(100);
    
    // 4-bit-Mode already enabled
    twi_send_byte(LCD_FUNCTION_4BIT | LCD_FUNCTION_2LINE | LCD_FUNCTION_5X7);    //-  2-Lines, 5x7-Matrix
    twi_send_byte(LCD_DISPLAY_OFF);            // Display off
    twi_send_byte(LCD_CLEAR_DISPLAY);          //  Clear Screen
    twi_send_byte(LCD_DISPLAY_ON | LCD_BLINKING_OFF);    //-  Display on
}

void lcd_command(unsigned char cmd)
{  
    uint8_t lcd_data = (cmd & 0xF0);
    lcd_data &= ~_BV(LCDEX_RS);
    strobe_en(lcd_data);
    twi_send_byte(lcd_data);
    _delay_us(100);
 
    lcd_data = ((cmd & 0x0F)<<4);
    lcd_data &= ~_BV(LCDEX_RS);
    strobe_en(lcd_data);
    twi_send_byte(lcd_data);
 
    if(cmd & 0b11111100) _delay_us(100); else _delay_ms(2);
}

void lcd_putch(unsigned char chr)
{
    uint8_t lcd_data = (chr & 0xF0);
    lcd_data |= _BV(LCDEX_RS);
    strobe_en(lcd_data);
    _delay_us(100);
 
    lcd_data = ((chr & 0x0F)<<4);
    lcd_data |= _BV(LCDEX_RS);
    strobe_en(lcd_data);
    twi_send_byte(lcd_data);
    _delay_ms(2);
}


void LCD_clear()
{
    lcd_command( LCD_CLEAR_DISPLAY );
    _delay_ms( 4 );//LCD_CLEAR_DISPLAY_MS );
}

void LCD_setcursor(uint8_t x, uint8_t y)
{
   switch(y)
   {
        case 0: lcd_command(LCD_SET_DDADR + LCD_DDADR_LINE1 + x); break;
        case 1: lcd_command(LCD_SET_DDADR + LCD_DDADR_LINE2 + x); break;
        case 2: lcd_command(LCD_SET_DDADR + LCD_DDADR_LINE3 + x); break;
        case 3: lcd_command(LCD_SET_DDADR + LCD_DDADR_LINE4 + x); break;
   }
}

void LCD_print(const char* txt)
{
  for(uint8_t i = 0; i < strlen(txt); i++) lcd_putch(txt[i]);
}

// print digit
void LCD_print(uint8_t num)
{
    lcd_putch(num + 0x30);
}

void LCD_print(uint8_t x, uint8_t y, const char* txt)
{
  LCD_setcursor(x,y);
  for(uint8_t i = 0; i < strlen(txt); i++) lcd_putch(txt[i]);
}

void LCD_print(uint8_t x, uint8_t y, uint8_t num)
{
  LCD_setcursor(x,y);
  LCD_print(num);
}

