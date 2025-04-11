#define F_CPU 16000000UL
#include <avr/io.h>
#include <math.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "ST7735.h"
#include "LCD_GFX.h"
#include "uart.h"
#include "i2c.h"
#include "max30102.h"

// Custom RNG variables
static uint16_t rand_seed = 1;

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
volatile uint8_t buttonPressed = 0;
volatile uint8_t heartRateDataReady = 0;
uint16_t heartRate = 0;
uint8_t animationFrame = 0;
uint8_t measurementActive = 0;
uint32_t startTime = 0;
uint32_t elapsedTimeMs = 0;
uint16_t lastHeartRate = 75; // Default heart rate value
uint16_t maxHeartRate = 0;
uint16_t minHeartRate = 255;
uint8_t measurementSamples = 0;

// Function prototypes
void initialize(void);
void displayWelcomeScreen(void);
void displayPressButtonPrompt(void);
void displayMeasuringPrompt(void);
void displaySpinningPrompt(void);
void displayResultScreen(uint8_t win);
void setupButtonInterrupt(void);
void setupHeartRateSensorInterrupt(void);
void processHeartRateSample(void);
uint8_t determineWinOdds(uint16_t heartRate);
void updateTimer(void);
uint16_t custom_rand(void);
uint16_t custom_rand_range(uint16_t max);

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
    
    // Initialize I2C
    I2C_init();
    
    // Setup button interrupt
    setupButtonInterrupt();
    
    // Setup heart rate sensor interrupt
    setupHeartRateSensorInterrupt();
    
    // Initialize the heart rate sensor
    _delay_ms(500); // Give time for sensor to stabilize
    MAX30102_init();
    
    
    // Initialize the screen
    LCD_setScreen(BLACK);
    
    // Seed the random number generator with ADC noise
    // Enable ADC and set prescaler to 128
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
    // Select ADC channel 0 (can be any unused analog input)
    ADMUX = 0;
    
    // Do a few readings to seed our random number generator
    for (uint8_t i = 0; i < 16; i++) {
        // Start conversion
        ADCSRA |= (1 << ADSC);
        // Wait for conversion to complete
        while (ADCSRA & (1 << ADSC));
        // Use the least significant bits as they're noisier
        rand_seed = (rand_seed << 1) | (ADC & 0x01);
    }
    
    // Make sure seed is never 0
    if (rand_seed == 0) rand_seed = 0x1234;
    
    // Enable global interrupts
    sei();
    
    // Print debug info
    printf("T&T Slots - Sense the Win\r\n");
    printf("System Initialized\r\n");
    printf("MAX30102 Part ID: 0x%02X\r\n", MAX30102_readPartID());
    printf("MAX30102 Rev ID: 0x%02X\r\n", MAX30102_getRevisionID());
    
}

// Setup button interrupt on INT0 (PD2)
void setupButtonInterrupt(void) {
    // Configure INT0 to trigger on falling edge
    EICRA |= (1 << ISC01);
    EICRA &= ~(1 << ISC00);
    
    // Enable INT0 interrupt
    EIMSK |= (1 << INT0);
}

// Setup heart rate sensor interrupt on PD3 (INT1)
void setupHeartRateSensorInterrupt(void) {
    // Set PD3 as input
    DDRD &= ~(1 << DDD3);
    
    // Enable pull-up resistor on PD3
    PORTD |= (1 << PORTD3);
    
    // Configure INT1 to trigger on RISING edge since the pin is LOW when idle
    // ISC11 = 1, ISC10 = 1 for rising edge
    EICRA |= (1 << ISC11) | (1 << ISC10);
    
    // Enable INT1 interrupt
    EIMSK |= (1 << INT1);
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
    static uint8_t promptDisplayed = 0;
    
    if (!promptDisplayed) {
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
        
        promptDisplayed = 1;
    }
    
    // Simple animation for the heart
    if (animationFrame % 10 == 0) {
        // Make heart "beat" occasionally
        LCD_drawDisk(80, 105, 11, RED);
        LCD_drawDisk(90, 105, 11, RED);
        LCD_drawBlock(80, 105, 90, 116, RED);
        LCD_drawBlock(75, 100, 95, 105, BLACK);
        LCD_drawBlock(85, 116, 86, 117, RED);
    } else if (animationFrame % 10 == 1) {
        // Return to normal size
        LCD_drawDisk(80, 105, 10, RED);
        LCD_drawDisk(90, 105, 10, RED);
        LCD_drawBlock(80, 105, 90, 115, RED);
        LCD_drawBlock(75, 100, 95, 105, BLACK);
        LCD_drawBlock(85, 115, 86, 116, RED);
    }
    
    animationFrame++;
    _delay_ms(50);
    
    // Reset if we change states
    if (currentState != STATE_PRESS_BUTTON) {
        promptDisplayed = 0;
    }
}

// Display measuring heart rate prompt
void displayMeasuringPrompt(void) {
    static uint8_t lastHeartRateDisplayed = 0;
    printf("measuring1\n");
    
    // First, clear screen and setup initial display
    if (animationFrame == 0) {
        LCD_setScreen(BLACK);
        LCD_drawString(5, 15, "MEASURING PULSE...", CYAN, BLACK);
        LCD_drawString(10, 70, "Keep holding button", WHITE, BLACK);
        
        // Initialize measurement variables
        measurementActive = 1;
        startTime = 0;
        elapsedTimeMs = 0;
        measurementSamples = 0;
        maxHeartRate = 0;
        minHeartRate = 255;
        
        lastHeartRateDisplayed = 0;
    }
    
    printf("measuring2\n");
    
    // Process HR data if available
//    if (heartRateDataReady) {
//        processHeartRateSample();
//        heartRateDataReady = 0;
//    }
    
    processHeartRateSample();
    
    // Update timer (elapsed time)
    updateTimer();
    
    printf("measuring3\n");
    
    // Draw progress animation
    switch(animationFrame % 4) {
        case 0:
            LCD_drawString(70, 40, "|", WHITE, BLACK);
            break;
        case 1:
            LCD_drawString(70, 40, "/", WHITE, BLACK);
            break;
        case 2:
            LCD_drawString(70, 40, "-", WHITE, BLACK);
            break;
        case 3:
            LCD_drawString(70, 40, "\\", WHITE, BLACK);
            break;
    }
    
    // Display heart rate if it changed
    if (heartRate != lastHeartRateDisplayed) {
        char hrBuffer[20];
        sprintf(hrBuffer, "HR: %3d BPM", heartRate);
        
        // Clear previous value
        LCD_drawBlock(20, 85, 140, 95, BLACK);
        
        // Display new value
        uint16_t color = GREEN;
        if (heartRate > 100) color = YELLOW;
        if (heartRate > 120) color = RED;
        
        LCD_drawString(20, 85, hrBuffer, color, BLACK);
        
        lastHeartRateDisplayed = heartRate;
    }
    
    // Draw a heart that "beats"
    if (animationFrame % 2) {
        // Larger heart for "beat" effect
        LCD_drawDisk(80, 105, 12, RED);
        LCD_drawDisk(90, 105, 12, RED);
        LCD_drawBlock(80, 105, 90, 117, RED);
        LCD_drawBlock(75, 100, 95, 105, BLACK);
        LCD_drawBlock(85, 117, 86, 119, RED);
    } else {
        // Normal heart
        LCD_drawDisk(80, 105, 10, RED);
        LCD_drawDisk(90, 105, 10, RED);
        LCD_drawBlock(80, 105, 90, 115, RED);
        LCD_drawBlock(75, 100, 95, 105, BLACK);
        LCD_drawBlock(85, 115, 86, 117, RED);
    }
    
    // Update animation frame
    animationFrame++;
    
    printf("measuring4\n");
    
    // Timing for animation
    _delay_ms(250);
}

// Display spinning wheels prompt
void displaySpinningPrompt(void) {
    LCD_setScreen(BLACK);
    
    // Draw header
    LCD_drawString(30, 15, "SPINNING!", MAGENTA, BLACK);
    
    // Display heart rate info
    char hrBuffer[20];
    sprintf(hrBuffer, "Your HR: %3d BPM", lastHeartRate);
    LCD_drawString(20, 30, hrBuffer, WHITE, BLACK);
    
    // Draw spinning wheels
    char symbols[4] = {'7', '$', '#', '@'};
    
    for (uint8_t wheel = 0; wheel < 3; wheel++) {
        // Calculate position for each wheel
        uint8_t x = 40 + wheel * 40;
        
        // Draw wheel border
        LCD_drawBlock(x - 10, 50, x + 10, 90, BLUE);
        
        // Draw current symbol
        char symbol = symbols[(animationFrame + wheel) % 4];
        LCD_drawChar(x - 3, 65, symbol, WHITE, BLUE);
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

// Process heart rate sample
void processHeartRateSample(void) {
    // Get heart rate from the sensor
    uint16_t currentHR = MAX30102_getHeartRate();
    
    // Only update if we have a valid reading (> 0)
    if (currentHR >= 0) {
        // Track min and max heart rate
        if (currentHR > maxHeartRate) maxHeartRate = currentHR;
        if (currentHR < minHeartRate) minHeartRate = currentHR;
        
        // Update our heart rate value
        heartRate = currentHR;
        measurementSamples++;
        
        // Debug output
        printf("HR: %u BPM\r\n", heartRate);
    }
}

// Determine win odds based on heart rate
uint8_t determineWinOdds(uint16_t heartRate) {
    // Win logic according to SRS-03:
    // If HR is low, increase odds of winning
    // If HR is high, decrease odds to elongate play
    
    // Define heart rate thresholds
    #define LOW_HR_THRESHOLD 80
    #define HIGH_HR_THRESHOLD 100
    
    uint8_t winPercentage;
    
    if (heartRate < LOW_HR_THRESHOLD) {
        // Low heart rate - higher odds (up to 40%)
        winPercentage = 40 - ((heartRate * 30) / LOW_HR_THRESHOLD);
    } else if (heartRate > HIGH_HR_THRESHOLD) {
        // High heart rate - lower odds (down to 5%)
        winPercentage = 10 - ((heartRate - HIGH_HR_THRESHOLD) / 10);
        if (winPercentage < 5) winPercentage = 5;
    } else {
        // Medium heart rate - medium odds (10-20%)
        winPercentage = 20 - ((heartRate - LOW_HR_THRESHOLD) / 5);
    }
    
    printf("Win odds: %u%%\r\n", winPercentage);
    return winPercentage;
}

// Update timer for measuring pulse
void updateTimer(void) {
    if (measurementActive) {
        // Increment elapsed time (250ms per call since we delay 250ms in the measuring function)
        elapsedTimeMs += 250;
        
        // After 5 seconds of measurement, save the heart rate
        if (elapsedTimeMs >= 5000 && measurementSamples > 0) {
            // Use the current heart rate as our final value
            lastHeartRate = heartRate;
            
            printf("Measurement complete: HR=%u BPM (Min: %u, Max: %u)\r\n", 
                   lastHeartRate, minHeartRate, maxHeartRate);
                   
            // Reset measurement flag
            measurementActive = 0;
        }
    }
}

// Button interrupt handler
ISR(INT0_vect) {
    // Debounce
    _delay_ms(10);
    
    // Check if button is still pressed
    if (!(PIND & (1 << PIND2))) {
        // Button was pressed
        buttonPressed = 1;
        printf("pressing\n");
        // If we're in PRESS_BUTTON state, move to MEASURING state
        if (currentState == STATE_PRESS_BUTTON) {
            currentState = STATE_MEASURING;
            animationFrame = 0;
        }
    }
    
    // Button was pressed
    printf("button pressed, buttonPressed = %d\n", buttonPressed);
    currentState = STATE_MEASURING;
}

// Heart rate sensor interrupt handler (INT1 - PD3)
ISR(INT1_vect) {
    // Read the interrupt status register to see what triggered the interrupt
    uint8_t intStatus1, intStatus2;
    
    // Disable interrupts during I2C communication to prevent nested interrupts
    cli();
    
    // Read interrupt status registers
    I2C_readRegister(MAX30102_I2C_ADDR, &intStatus1, MAX30102_INT_STATUS_1);
    I2C_readRegister(MAX30102_I2C_ADDR, &intStatus2, MAX30102_INT_STATUS_2);
    
    // Re-enable interrupts
    sei();
    
    // For debugging
    printf("INT1 ISR triggered!\r\n");
    printf("Status1: 0x%02X, Status2: 0x%02X\r\n", intStatus1, intStatus2);
    
    // Set flag for main loop to process
    heartRateDataReady = 1;
    
    // Update RNG seed on each interrupt for more randomness
    rand_seed ^= (uint16_t)TCNT0;
}

// Simple Linear Congruential Generator (LCG) random number implementation
uint16_t custom_rand(void) {
    // LCG parameters: a good choice for 16-bit values
    rand_seed = (rand_seed * 31421 + 6927) & 0x7FFF;
    return rand_seed;
}

// Get a random number between 0 and max-1
uint16_t custom_rand_range(uint16_t max) {
    if (max == 0) return 0;
    return custom_rand() % max;
}

int main(void) {
    // Initialize hardware
    initialize();
    
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
                break;
                
            case STATE_MEASURING:
                displayMeasuringPrompt();
                
                printf("measuring\n");
                // Check if button was released
                if (PIND & (1 << PIND2)) {
                    printf("Button released, HR=%u BPM\r\n", lastHeartRate);
                    
                    // Button released, determine odds and start spinning
                    currentState = STATE_SPINNING;
                    animationFrame = 0;
                }
                break;
                
            case STATE_SPINNING:
                displaySpinningPrompt();
                
                // Simulate spinning for 3 seconds, then show result
                static uint8_t spinCount = 0;
                spinCount++;
                
                if (spinCount > 15) {  // ~3 seconds (15 * 200ms)
                    spinCount = 0;
                    
                    // Determine win based on heart rate
                    uint8_t winPercentage = determineWinOdds(lastHeartRate);
                    uint8_t randomValue = custom_rand_range(100);
                    uint8_t win = (randomValue < winPercentage);
                    
                    // Show result
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