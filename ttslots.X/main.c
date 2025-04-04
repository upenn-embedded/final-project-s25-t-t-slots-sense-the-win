#define F_CPU 16000000UL
#include <avr/io.h>
#include <math.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "ST7735.h"
#include "LCD_GFX.h"
#include "uart.h"
#include "i2c.h"
#include "i2c_test.h"
#include "imu.h"


// Define states for the slot machine
typedef enum {
    STATE_WELCOME,
    STATE_PRESS_BUTTON,
    STATE_MEASURING,
    STATE_SPINNING,
    STATE_RESULT
} SlotMachineState;

// Global variables
SlotMachineState currentState = STATE_WELCOME;
uint8_t buttonPressed = 0;
uint16_t heartRate = 0;
uint8_t animationFrame = 0;

// Function prototypes
void initialize(void);
void displayWelcomeScreen(void);
void displayPressButtonPrompt(void);
void displayMeasuringPrompt(void);
void displaySpinningPrompt(void);
void displayResultScreen(uint8_t win);
void setupButtonInterrupt(void);

// Initialize hardware
void initialize(void) {
    // Initialize LCD
    lcd_init();
    
    // Setup button input with internal pull-up
    // Using PD2 (INT0) as the button input pin
    DDRD &= ~(1 << DDD2);  // Set PD2 as input
    PORTD |= (1 << PORTD2); // Enable pull-up resistor
    
    // Initialize UART for debugging
    uart_init();
    
    // Enable interrupts
    sei();
    
    // Setup button interrupt
    setupButtonInterrupt();
    
    // Initialize the screen
    LCD_setScreen(BLACK);
}

// Setup button interrupt on INT0 (PD2)
void setupButtonInterrupt(void) {
    // Configure INT0 to trigger on falling edge
    EICRA |= (1 << ISC01);
    EICRA &= ~(1 << ISC00);
    
    // Enable INT0 interrupt
    EIMSK |= (1 << INT0);
}

// Display welcome screen
void displayWelcomeScreen(void) {
    LCD_setScreen(BLACK);
    
    // Display title
    LCD_drawString(20, 20, "T&T SLOTS", YELLOW, BLACK);
    LCD_drawString(15, 40, "Sense the Win", RED, BLACK);
    
    // Draw a decorative border
    LCD_drawBlock(10, 10, 150, 12, RED);    // Top border
    LCD_drawBlock(10, 110, 150, 112, RED);  // Bottom border
    LCD_drawBlock(10, 10, 12, 112, RED);    // Left border
    LCD_drawBlock(148, 10, 150, 112, RED);  // Right border
    
    // Display instructions
    LCD_drawString(15, 70, "Press Button", WHITE, BLACK);
    LCD_drawString(25, 85, "to Start", WHITE, BLACK);
    
    // Wait for a moment before proceeding
    _delay_ms(2000);
    
    // Transition to next state
    currentState = STATE_PRESS_BUTTON;
}

// Display prompt to press the button
void displayPressButtonPrompt(void) {
    LCD_setScreen(BLACK);
    
    // Draw header
    LCD_drawString(15, 15, "READY TO PLAY?", GREEN, BLACK);
    
    // Draw button prompt
    LCD_drawString(5, 40, "Press & Hold Button", WHITE, BLACK);
    LCD_drawString(20, 55, "to Measure HR", WHITE, BLACK);
    
    // Draw a simple button graphic
    LCD_drawBlock(60, 70, 100, 90, BLUE);
    LCD_drawString(67, 78, "PLAY", WHITE, BLUE);
    
    // Draw a heart symbol
    LCD_drawDisk(80, 105, 10, RED);
    LCD_drawDisk(90, 105, 10, RED);
    LCD_drawBlock(80, 105, 90, 115, RED);
    LCD_drawBlock(75, 100, 95, 105, BLACK);
    LCD_drawBlock(85, 115, 86, 116, RED);
}

// Display measuring heart rate prompt
void displayMeasuringPrompt(void) {
    LCD_setScreen(BLACK);
    
    // Draw header
    LCD_drawString(5, 15, "MEASURING PULSE...", CYAN, BLACK);
    
    // Draw progress animation based on animationFrame
    switch(animationFrame % 4) {
        case 0:
            LCD_drawString(70, 50, "|", WHITE, BLACK);
            break;
        case 1:
            LCD_drawString(70, 50, "/", WHITE, BLACK);
            break;
        case 2:
            LCD_drawString(70, 50, "-", WHITE, BLACK);
            break;
        case 3:
            LCD_drawString(70, 50, "\\", WHITE, BLACK);
            break;
    }
    
    // Draw instruction
    LCD_drawString(10, 70, "Keep holding button", WHITE, BLACK);
    
    // Draw a heart that "beats"
    if (animationFrame % 2) {
        // Larger heart for "beat" effect
        LCD_drawDisk(80, 100, 12, RED);
        LCD_drawDisk(90, 100, 12, RED);
        LCD_drawBlock(80, 100, 90, 112, RED);
        LCD_drawBlock(75, 95, 95, 100, BLACK);
        LCD_drawBlock(85, 112, 86, 114, RED);
    } else {
        // Normal heart
        LCD_drawDisk(80, 100, 10, RED);
        LCD_drawDisk(90, 100, 10, RED);
        LCD_drawBlock(80, 100, 90, 110, RED);
        LCD_drawBlock(75, 95, 95, 100, BLACK);
        LCD_drawBlock(85, 110, 86, 112, RED);
    }
    
    // Update animation frame
    animationFrame++;
    
    // Slow down animation
    _delay_ms(250);
}

// Display spinning wheels prompt
void displaySpinningPrompt(void) {
    LCD_setScreen(BLACK);
    
    // Draw header
    LCD_drawString(30, 15, "SPINNING!", MAGENTA, BLACK);
    
    // Draw spinning wheels
    char symbols[4] = {'7', '$', '#', '@'};
    
    for (uint8_t wheel = 0; wheel < 3; wheel++) {
        // Calculate position for each wheel
        uint8_t x = 40 + wheel * 40;
        
        // Draw wheel border
        LCD_drawBlock(x - 10, 40, x + 10, 80, BLUE);
        
        // Draw current symbol
        char symbol = symbols[(animationFrame + wheel) % 4];
        LCD_drawChar(x - 3, 55, symbol, WHITE, BLUE);
    }
    
    // Update animation frame
    animationFrame++;
    
    // Slow down animation
    _delay_ms(200);
}

// Display result screen
void displayResultScreen(uint8_t win) {
    LCD_setScreen(BLACK);
    
    if (win) {
        // Win screen
        LCD_drawString(30, 30, "YOU WIN!", YELLOW, BLACK);
        LCD_drawString(20, 50, "JACKPOT!!!", GREEN, BLACK);
        
        // Draw a smiley face
        LCD_drawCircle(80, 90, 20, YELLOW);
        LCD_drawCircle(70, 80, 3, BLACK);
        LCD_drawCircle(90, 80, 3, BLACK);
        LCD_drawBlock(70, 100, 90, 102, BLACK);
    } else {
        // Lose screen
        LCD_drawString(30, 40, "TRY AGAIN", RED, BLACK);
        LCD_drawString(15, 60, "Better luck", WHITE, BLACK);
        LCD_drawString(20, 75, "next time!", WHITE, BLACK);
        
        // Draw a sad face
        LCD_drawCircle(80, 90, 20, YELLOW);
        LCD_drawCircle(70, 80, 3, BLACK);
        LCD_drawCircle(90, 80, 3, BLACK);
        LCD_drawBlock(70, 102, 90, 104, BLACK);
    }
    
    // Wait for a moment
    _delay_ms(3000);
    
    // Return to welcome screen
    currentState = STATE_WELCOME;
}

// Button interrupt handler
ISR(INT0_vect) {
    // Button was pressed
    buttonPressed = 1;
    
    // If we're in PRESS_BUTTON state, move to MEASURING state
    if (currentState == STATE_PRESS_BUTTON) {
        currentState = STATE_MEASURING;
    }
}

int main(void) {
    // Initialize hardware
    initialize();
    
    // Print debug message over UART
    printf("T&T Slots - Sense the Win\r\n");
    printf("System initialized\r\n");
    
    // Start with welcome screen
    currentState = STATE_WELCOME;
    
    // Main loop
    while (1) {
        // State machine
        switch (currentState) {
            case STATE_WELCOME:
                displayWelcomeScreen();
                break;
                
            case STATE_PRESS_BUTTON:
                displayPressButtonPrompt();
                _delay_ms(500);  // Update animation every 500ms
                break;
                
            case STATE_MEASURING:
                displayMeasuringPrompt();
                
                // In Sprint 1, we're just displaying prompts, not actually measuring HR
                // This would be implemented in Sprint 2
                // Simulate measuring for 5 seconds then go to spinning
                // In the real implementation, this would be triggered by button release
                static uint8_t measureCount = 0;
                measureCount++;
                
                if (measureCount > 20) {  // ~5 seconds (20 * 250ms)
                    measureCount = 0;
                    currentState = STATE_SPINNING;
                }
                break;
                
            case STATE_SPINNING:
                displaySpinningPrompt();
                
                // Simulate spinning for 3 seconds, then show result
                static uint8_t spinCount = 0;
                spinCount++;
                
                if (spinCount > 15) {  // ~3 seconds (15 * 200ms)
                    spinCount = 0;
                    
                    // For Sprint 1, just randomly win or lose
                    uint8_t win = (1);  // 20% chance to win
                    currentState = STATE_RESULT;
                    displayResultScreen(win);
                }
                break;
                
            case STATE_RESULT:
                // This state is handled by displayResultScreen
                // which automatically transitions back to welcome
                break;
                
            default:
                // In case of unknown state, reset to welcome
                currentState = STATE_WELCOME;
                break;
        }
    }
    
    return 0;
}