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

// Define I2C frequency
#define I2C_FREQUENCY      400000UL  // 400kHz for MAX30102 communication

// Define the number of samples to read in each iteration
#define SAMPLE_COUNT       10

// Sample buffer
max30102_fifo_sample_t samples[SAMPLE_COUNT];

#define BUZZER_PIN PD5    // Buzzer connected to PD5/OC0B

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
uint8_t animationFrame = 0;
max30102_result_t result;
volatile uint32_t heartRate;
volatile bool heartRateReady = false;
uint8_t sample_count;
uint8_t write_ptr, read_ptr, overflow;
uint8_t int_status_1, int_status_2;

// Function prototypes
void initialize(void);
void displayWelcomeScreen(void);
void displayPressButtonPrompt(void);
void displayMeasuringPrompt(void);
void displaySpinningPrompt(void);
void displayResultScreen(uint8_t win);
void setupButtonInterrupt(void);
uint8_t determineWinOdds(uint32_t heartRate);
uint16_t custom_rand(void);
uint16_t custom_rand_range(uint16_t max);

// Buzzer control functions
void init_buzzer() {
    // Set buzzer pin as output
    DDRD |= (1 << BUZZER_PIN);
}

// Play specific frequencies with fixed delays
void play_500hz(uint16_t duration_ms) {
    uint16_t cycles = duration_ms;  // Each cycle is about 1ms for 500Hz
    for (uint16_t i = 0; i < cycles; i++) {
        PORTD |= (1 << BUZZER_PIN);
        _delay_us(1000);  // Half period for 500Hz = 1000us
        PORTD &= ~(1 << BUZZER_PIN);
        _delay_us(1000);
    }
}

void play_1000hz(uint16_t duration_ms) {
    uint16_t cycles = duration_ms * 2;  // Each cycle is about 0.5ms for 1000Hz
    for (uint16_t i = 0; i < cycles; i++) {
        PORTD |= (1 << BUZZER_PIN);
        _delay_us(500);  // Half period for 1000Hz = 500us
        PORTD &= ~(1 << BUZZER_PIN);
        _delay_us(500);
    }
}

void play_1500hz(uint16_t duration_ms) {
    uint16_t cycles = duration_ms * 3;  // Each cycle is about 0.33ms for 1500Hz
    for (uint16_t i = 0; i < cycles; i++) {
        PORTD |= (1 << BUZZER_PIN);
        _delay_us(333);  // Half period for 1500Hz = 333us
        PORTD &= ~(1 << BUZZER_PIN);
        _delay_us(333);
    }
}

void play_2000hz(uint16_t duration_ms) {
    uint16_t cycles = duration_ms * 4;  // Each cycle is about 0.25ms for 2000Hz
    for (uint16_t i = 0; i < cycles; i++) {
        PORTD |= (1 << BUZZER_PIN);
        _delay_us(250);  // Half period for 2000Hz = 250us
        PORTD &= ~(1 << BUZZER_PIN);
        _delay_us(250);
    }
}

bool init_peripherals(void) {
    // Initialize I2C at 400kHz
    i2c_init(I2C_FREQUENCY);
    printf("I2C initialized\r\n");
    
    // Initialize MAX30102
    if (!max30102_init()) {
        printf("Failed to initialize MAX30102 sensor\r\n");
        return false;
    }
    printf("MAX30102 sensor initialized\r\n");
    
    // Configure MAX30102 with default settings
    max30102_led_amplitude_t led_amplitude = {
        .red = 0x1F,  // ~6.4mA
        .ir = 0x1F    // ~6.4mA
    };
    
    if (!max30102_configure(MAX30102_SAMPLE_RATE_100_HZ, 
                           MAX30102_PULSE_WIDTH_411_US,
                           MAX30102_ADC_RANGE_16384_NA,
                           led_amplitude)) {
        printf("Failed to configure MAX30102 sensor\r\n");
        return false;
    }
    
    // Set up external interrupt (INT1 on PD3)
    // Configure PD3 as input with pull-up
    DDRD &= ~(1 << PD3);      // Set PD3 as input
    PORTD |= (1 << PD3);      // Enable pull-up
    
    // Set up INT1 for falling edge detection
    EICRA &= ~(1 << ISC10);   // Clear ISC10
    EICRA |= (1 << ISC11);    // Set ISC11 for falling edge
    EIMSK |= (1 << INT1);     // Enable INT1
    
    // Clear any pending interrupts
    uint8_t int_status_1, int_status_2;
    max30102_read_interrupt_status(&int_status_1, &int_status_2);
    
    return true;
}

// Initialize hardware
void initialize(void) {
    // Initialize LCD
    lcd_init();
    
    init_buzzer();
    
    // Setup button input with internal pull-up
    // Using PD2 (INT0) as the button input pin
    DDRD &= ~(1 << DDD2);  // Set PD2 as input
    PORTD |= (1 << PORTD2); // Enable pull-up resistor
    
    // Initialize UART for debugging
    uart_init();
    if (!init_peripherals()) {
        while (1) {
            // Halt on error
            _delay_ms(1000);
        }
    }
    
    // Setup button interrupt
    setupButtonInterrupt();

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
    static uint8_t promptDisplayed = 0;
    
    if (!promptDisplayed) {
        LCD_setScreen(BLACK);
        
        // Draw header
        LCD_drawString(15, 15, "READY TO PLAY?", GREEN, BLACK);
        
        // Draw button prompt
        LCD_drawString(5, 40, "Press the Button", WHITE, BLACK);
        LCD_drawString(20, 55, "to try your luck!", WHITE, BLACK);
        
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
//    printf("measuring1\n");
    
    // First, clear screen and setup initial display
    if (animationFrame == 0) {
        LCD_setScreen(BLACK);
        LCD_drawString(5, 15, "CHARGING UP YOUR WIN...", CYAN, BLACK);
        LCD_drawString(10, 70, "Keep holding button", WHITE, BLACK);
    }
    
//    printf("measuring3\n");
    
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
    
    // Update animation frame
    animationFrame++;
    
//    printf("measuring4\n");
    
    // Timing for animation
    _delay_ms(250);
}

// Display spinning wheels prompt
void displaySpinningPrompt(void) {
    LCD_setScreen(BLACK);
    
    // Draw header
    LCD_drawString(30, 15, "SPINNING!", MAGENTA, BLACK);
    
    // Display heart rate info
//    char hrBuffer[20];
//    sprintf(hrBuffer, "Your HR: %3d BPM", heartRate);
//    LCD_drawString(20, 30, hrBuffer, WHITE, BLACK);
    
    // Draw spinning wheels
    char symbols[4] = {'7', '$', '#', '@'};
    
    for (uint8_t wheel = 0; wheel < 3; wheel++) {
        // Calculate position for each wheel
        uint8_t x = 40 + wheel * 40;
        
        // Draw wheel border
        LCD_drawBlock(x - 10, 50, x + 10, 90, BLUE);
        
        // Draw current symbol
        char symbol = symbols[custom_rand_range(4)];
        LCD_drawChar(x - 3, 65, symbol, WHITE, BLUE);
    }
    
    // Update animation frame
    animationFrame++;
    rand_seed ^= (uint16_t)TCNT0;
    
    // Slow down animation
    _delay_ms(200);
}

// Display result screen
void displayResultScreen(uint8_t win) {
    LCD_setScreen(BLACK);
    char symbols[4] = {'7', '$', '#', '@'};
    
    if (win) {
        play_2000hz(50);
        play_1500hz(50);
        play_2000hz(50);
        // Win screen
        LCD_drawString(30, 10, "YOU WIN!", YELLOW, BLACK);
        LCD_drawString(20, 30, "JACKPOT!!!", GREEN, BLACK);
        
        for (uint8_t wheel = 0; wheel < 3; wheel++) {
            // Calculate position for each wheel
            uint8_t x = 40 + wheel * 40;

            // Draw wheel border
            LCD_drawBlock(x - 10, 65, x + 10, 90, BLUE);

            // Draw current symbol
            char symbol = symbols[0];
            LCD_drawChar(x - 3, 80, symbol, WHITE, BLUE);
        }
    } else {
        play_500hz(50);
        play_1000hz(50);
        play_500hz(50);
        // Lose screen
        LCD_drawString(30, 10, "TRY AGAIN", RED, BLACK);
        LCD_drawString(15, 30, "Better luck", WHITE, BLACK);
        LCD_drawString(20, 45, "next time!", WHITE, BLACK);
        
        
    
        for (uint8_t wheel = 0; wheel < 3; wheel++) {
            // Calculate position for each wheel
            uint8_t x = 40 + wheel * 40;

            // Draw wheel border
            LCD_drawBlock(x - 10, 65, x + 10, 90, BLUE);

            // Draw current symbol
            char symbol = symbols[custom_rand_range(4)];
            LCD_drawChar(x - 3, 80, symbol, WHITE, BLUE);
        }
    }
    
    // Wait for a moment
    _delay_ms(3000);
    
    // Return to welcome screen
    currentState = STATE_WELCOME;
}


// Determine win odds based on heart rate
uint8_t determineWinOdds(uint32_t heartRate) {
    // Win logic according to SRS-03:
    // If HR is low, increase odds of winning
    // If HR is high, decrease odds to elongate play
    
    // Define heart rate thresholds
    #define LOW_HR_THRESHOLD 80
    #define HIGH_HR_THRESHOLD 100
    
    uint8_t winPercentage;
    
    if (heartRate < LOW_HR_THRESHOLD) {
        // Low heart rate - higher odds (up to 40%)
        // winPercentage = 40 - ((heartRate * 30) / LOW_HR_THRESHOLD);
        winPercentage = 100;
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

// Button interrupt handler
ISR(INT0_vect) {
    // Debounce
    _delay_ms(10);
    
//    // Check if button is still pressed
//    if (!(PIND & (1 << PIND2))) {
//        // Button was pressed
//        buttonPressed = 1;
//        printf("pressing\n");
//        // If we're in PRESS_BUTTON state, move to MEASURING state
//        if (currentState == STATE_PRESS_BUTTON) {
//            currentState = STATE_MEASURING;
//            animationFrame = 0;
//        }
//    }
    
    // Button was pressed
    printf("button pressed\n");
    if (currentState == STATE_PRESS_BUTTON) {
        animationFrame = 0;
        currentState = STATE_MEASURING;      
    }
}



// Heart rate sensor interrupt handler (INT1 - PD3)
ISR(INT1_vect) {
    if (max30102_read_interrupt_status(&int_status_1, &int_status_2)) {
                if (int_status_1 & MAX30102_INT_A_FULL) {
                    // printf("INT detected via polling: 0x%02X\r\n", int_status_1);
                    
                    // Read FIFO pointers to determine how many samples to read
                    if (max30102_read_fifo_ptrs(&write_ptr, &read_ptr, &overflow)) {
                        // Calculate number of samples to read
                        if (write_ptr >= read_ptr) {
                            sample_count = write_ptr - read_ptr;
                        } else {
                            sample_count = 32 - read_ptr + write_ptr;  // FIFO is 32 samples deep
                        }
                        
                        // Cap sample count to buffer size
                        if (sample_count > SAMPLE_COUNT) {
                            sample_count = SAMPLE_COUNT;
                        }
                        
                        // Only process data if we have enough samples
                        if (sample_count >= 5) {
                            // Read samples from FIFO
                            sample_count = max30102_read_fifo_samples(samples, sample_count);
                            
                            // Process samples to calculate heart rate and SpO2
                            if (max30102_calculate_hr_spo2(samples, sample_count, &result)) {
                                // Print sample data and results
//                                for (uint8_t i = 0; i < sample_count && i < 1; i++) {
//                                    printf("%lu\t%lu\t", samples[i].red, samples[i].ir);
//                                }
                                
                                heartRateReady = result.hr_valid;
                                
                                // Print heart rate and validity
                                if (heartRateReady) {
                                    printf("%ld\tValid\t\t\n", result.heart_rate);
                                    heartRate = result.heart_rate;
                                } else {
                                    printf("--\tInvalid\t\t\n");
                                }
                                
                                // Print SpO2 and validity
//                                if (result.spo2_valid) {
//                                    printf("%ld%%\tValid\r\n", result.spo2);
//                                } else {
//                                    printf("--%%\tInvalid\r\n");
//                                }
                            }
                        }
                    }
                }
            }
    
    // Update RNG seed on each interrupt for more randomness
    
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
    uint32_t loop_count = 0;
    
    // Main loop
    while (1) {   
        rand_seed ^= (uint16_t)TCNT0;
        // State machine
        switch (currentState) {
            case STATE_WELCOME:
                play_1000hz(50);
                play_1500hz(50);
                play_1000hz(50);
                displayWelcomeScreen();
                break;
                
            case STATE_PRESS_BUTTON:
                
                displayPressButtonPrompt();
                break;
                
            case STATE_MEASURING:
                displayMeasuringPrompt();
                loop_count++;
       
                if (heartRateReady) {
                    printf("HR=%u BPM\r\n", heartRate);
                    
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
                    uint8_t winPercentage = determineWinOdds(heartRate);
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