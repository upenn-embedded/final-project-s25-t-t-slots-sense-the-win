#define F_CPU 16000000UL
#include <avr/io.h>
#include <math.h>
#include <util/delay.h>
#include "ST7735.h"
#include "LCD_GFX.h"

void initialize() {
    lcd_init();
}
void main(void) {
    initialize();
    LCD_setScreen(BLACK);
}
