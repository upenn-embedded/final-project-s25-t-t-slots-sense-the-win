/*
 * LCD_GFX.c
 *
 * Created: 9/20/2021 6:54:25 PM
 *  Author: You
 */ 

#include "LCD_GFX.h"
#include "ST7735.h"
#include <math.h>

/******************************************************************************
* Local Functions
******************************************************************************/



/******************************************************************************
* Global Functions
******************************************************************************/

/**************************************************************************//**
* @fn			uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
* @brief		Convert RGB888 value to RGB565 16-bit color data
* @note
*****************************************************************************/
uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
	return ((((31*(red+4))/255)<<11) | (((63*(green+2))/255)<<5) | ((31*(blue+4))/255));
}

/**************************************************************************//**
* @fn			void LCD_drawPixel(uint8_t x, uint8_t y, uint16_t color)
* @brief		Draw a single pixel of 16-bit rgb565 color to the x & y coordinate
* @note
*****************************************************************************/
void LCD_drawPixel(uint8_t x, uint8_t y, uint16_t color) {
	LCD_setAddr(x,y,x,y);
	SPI_ControllerTx_16bit(color);
}

/**************************************************************************//**
* @fn			void LCD_drawChar(uint8_t x, uint8_t y, uint16_t character, uint16_t fColor, uint16_t bColor)
* @brief		Draw a character starting at the point with foreground and background colors
* @note
*****************************************************************************/
void LCD_drawChar(uint8_t x, uint8_t y, uint16_t character, uint16_t fColor, uint16_t bColor){
	uint16_t row = character - 0x20;		//Determine row of ASCII table starting at space
	int i, j;
	if ((LCD_WIDTH-x>7)&&(LCD_HEIGHT-y>7)){
		for(i=0;i<5;i++){
			uint8_t pixels = ASCII[row][i]; //Go through the list of pixels
			for(j=0;j<8;j++){
				if ((pixels>>j)&1==1){
					LCD_drawPixel(x+i,y+j,fColor);
				}
				else {
					LCD_drawPixel(x+i,y+j,bColor);
				}
			}
		}
	}
}


/******************************************************************************
* LAB 4 TO DO. COMPLETE THE FUNCTIONS BELOW.
* You are free to create and add any additional files, libraries, and/or
*  helper function. All code must be authentically yours.
******************************************************************************/

/**************************************************************************//**
* @fn			void LCD_drawCircle(uint8_t x0, uint8_t y0, uint8_t radius,uint16_t color)
* @brief		Draw a colored circle of set radius at coordinates
* @note
*****************************************************************************/
void LCD_drawCircle(uint8_t x0, uint8_t y0, uint8_t radius,uint16_t color)
{
	int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        // Bresenham circle algorithm
        LCD_drawPixel(x0 + x, y0 + y, color);
        LCD_drawPixel(x0 + y, y0 + x, color);
        LCD_drawPixel(x0 - y, y0 + x, color);
        LCD_drawPixel(x0 - x, y0 + y, color);
        LCD_drawPixel(x0 - x, y0 - y, color);
        LCD_drawPixel(x0 - y, y0 - x, color);
        LCD_drawPixel(x0 + y, y0 - x, color);
        LCD_drawPixel(x0 + x, y0 - y, color);
        
        if (err <= 0)
        {
            y += 1;
            err += 2*y + 1;
        } else {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

void LCD_drawDisk(uint8_t x0, uint8_t y0, uint8_t radius, uint16_t color)
{
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        LCD_drawLine(x0 - x, y0 + y, x0 + x, y0 + y, color); // Lower half
        LCD_drawLine(x0 - x, y0 - y, x0 + x, y0 - y, color); // Upper half
        LCD_drawLine(x0 - y, y0 + x, x0 + y, y0 + x, color); // Lower half
        LCD_drawLine(x0 - y, y0 - x, x0 + y, y0 - x, color); // Upper half
        
        if (err <= 0)
        {
            y += 1;
            err += 2*y + 1;
        }
        else
        {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}



/**************************************************************************//**
* @fn			void LCD_drawLine(short x0,short y0,short x1,short y1,uint16_t c)
* @brief		Draw a line from and to a point with a color
* @note
*****************************************************************************/
void LCD_drawLine(short x0,short y0,short x1,short y1,uint16_t c)
{
	// Bresenham line algorithm
    
    // abs
    short dx = (x1 - x0 >= 0) ? x1 - x0 : x0 - x1;
    
    // -abs
    short dy = (y1 - y0 >= 0) ? y0 - y1 : y1 - y0;
    short sx = x0 < x1 ? 1 : -1;
    short sy = y0 < y1 ? 1 : -1;
    short err = dx + dy;
    short e2;

    while (1)
    {
        LCD_drawPixel(x0, y0, c);
        
        if (x0 == x1 && y0 == y1)
            break;
            
        e2 = 2 * err;
        
        if (e2 >= dy)
        {
            if (x0 == x1)
                break;
            err += dy;
            x0 += sx;
        }
        
        if (e2 <= dx)
        {
            if (y0 == y1)
                break;
            err += dx;
            y0 += sy;
        }
    }
}



/**************************************************************************//**
* @fn			void LCD_drawBlock(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,uint16_t color)
* @brief		Draw a colored block at coordinates
* @note
*****************************************************************************/
void LCD_drawBlock(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,uint16_t color)
{
	LCD_setAddr(x0,y0,x1,y1);
    int range = (x1 - x0) * (y1 - y0);
    for (int i = 1; i <= range; i++) {
        SPI_ControllerTx_16bit(color);
    }
    
}

/**************************************************************************//**
* @fn			void LCD_setScreen(uint16_t color)
* @brief		Draw the entire screen to a color
* @note
*****************************************************************************/
void LCD_setScreen(uint16_t color) 
{
    LCD_setAddr(0, 0, 159, 127);
    for (int i = 1; i <= 127 * 159; i++) {
        SPI_ControllerTx_16bit(color);
    }
	
}

/**************************************************************************//**
* @fn			void LCD_drawString(uint8_t x, uint8_t y, char* str, uint16_t fg, uint16_t bg)
* @brief		Draw a string starting at the point with foreground and background colors
* @note
*****************************************************************************/
void LCD_drawString(uint8_t x, uint8_t y, char* str, uint16_t fg, uint16_t bg)
{
	uint8_t i = 0;
    while (str[i] != '\0')
    {
        // Each character is 5 pixels wide + 1 pixel spacing
        LCD_drawChar(x + (i * 6), y, str[i], fg, bg);  
        i++;
    }
}