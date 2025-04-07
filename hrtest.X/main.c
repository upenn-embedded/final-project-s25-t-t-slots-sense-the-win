#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "ST7735.h"
#include "LCD_GFX.h"
#include "uart.h"
#include "i2c.h"
#include "max30102.h"

// Global variables
volatile uint8_t heartRateDataReady = 0;
volatile uint32_t interruptCount = 0;
uint8_t debugMode = 0; // 0=basic, 1=FIFO debug, 2=register dump, 3=INT pin polling

// Function prototypes
void initialize(void);
void setupHeartRateSensorInterrupt(void);
void updateDisplay(void);
void i2cTest(void);
void dumpRegisters(void);
void testFifoOperation(void);
void reinitializeSensor(void);
void testInterruptPin(void);
void toggleDebugMode(void);

// Setup heart rate sensor interrupt on PD3 (INT1)
void setupHeartRateSensorInterrupt(void) {
    // Set PD3 as input
    DDRD &= ~(1 << DDD3);
    
    // Enable pull-up resistor on PD3
    PORTD |= (1 << PORTD3);
    
    // Configure INT1 to trigger on falling edge
    // ISC11 = 1, ISC10 = 0 for falling edge
    EICRA |= (1 << ISC11);
    EICRA &= ~(1 << ISC10);
    
    // Enable INT1 interrupt
    EIMSK |= (1 << INT1);
    
    printf("INT1 (PD3) configured for falling edge\r\n");
}

// Initialize hardware
void initialize(void) {
    // Initialize LCD
    lcd_init();
    LCD_setScreen(BLACK);
    
    // Initialize UART for debugging
    uart_init();
    
    // Initialize I2C
    I2C_init();
    
    // Print banner
    printf("\r\n--------------------------\r\n");
    printf("MAX30102 Heart Rate Sensor DEBUGGING\r\n");
    printf("--------------------------\r\n");
    
    // Display initial screen
    LCD_drawString(10, 10, "HR Sensor Debug", RED, BLACK);
    
    // Setup button for mode switching (using PD2/INT0)
    DDRD &= ~(1 << DDD2);  // Set PD2 as input
    PORTD |= (1 << PORTD2); // Enable pull-up
    
    // Configure INT0 to trigger on falling edge
    EICRA |= (1 << ISC01);
    EICRA &= ~(1 << ISC00);
    EIMSK |= (1 << INT0);    // Enable INT0
    
    // Test I2C communication
    i2cTest();
    
    // Setup heart rate sensor interrupt
    setupHeartRateSensorInterrupt();
    
    // Initialize the heart rate sensor
    _delay_ms(500); // Give time for sensor to stabilize
    reinitializeSensor();
    
    // Enable global interrupts
    sei();
}

// Perform quick I2C communication test
void i2cTest(void) {
    printf("Testing I2C communication...\r\n");
    
    // Try to read part ID
    uint8_t partID = MAX30102_readPartID();
    uint8_t revID = MAX30102_getRevisionID();
    
    printf("MAX30102 Part ID: 0x%02X (expected 0x15)\r\n", partID);
    printf("MAX30102 Rev ID: 0x%02X\r\n", revID);
    
    if (partID == 0x15) {
        LCD_drawString(10, 30, "I2C OK: ID=0x15", GREEN, BLACK);
        printf("I2C communication SUCCESSFUL\r\n");
    } else {
        LCD_drawString(10, 30, "I2C FAIL", RED, BLACK);
        char buffer[30];
        sprintf(buffer, "Bad ID: 0x%02X", partID);
        LCD_drawString(10, 45, buffer, RED, BLACK);
        printf("I2C communication FAILED!\r\n");
        
        // Try direct I2C operations using public functions
        printf("Attempting direct register access...\r\n");
        
        uint8_t directReadID;
        I2C_readRegister(MAX30102_I2C_ADDR, &directReadID, MAX30102_PART_ID);
        printf("Direct I2C read of PART_ID: 0x%02X\r\n", directReadID);
        
        // Test write operation with a harmless register (LED pulse amplitude)
        printf("Testing write operation...\r\n");
        uint8_t originalValue, testValue;
        
        // Read current value
        I2C_readRegister(MAX30102_I2C_ADDR, &originalValue, MAX30102_LED1_PA);
        printf("Current LED1_PA: 0x%02X\r\n", originalValue);
        
        // Write test value
        testValue = 0x1F; // Half brightness
        I2C_writeRegister(MAX30102_I2C_ADDR, testValue, MAX30102_LED1_PA);
        
        // Read back to verify
        uint8_t readbackValue;
        I2C_readRegister(MAX30102_I2C_ADDR, &readbackValue, MAX30102_LED1_PA);
        printf("LED1_PA after write: 0x%02X (expected 0x1F)\r\n", readbackValue);
        
        // Restore original value
        I2C_writeRegister(MAX30102_I2C_ADDR, originalValue, MAX30102_LED1_PA);
    }
}

// Reinitialize the sensor with different settings for testing
void reinitializeSensor(void) {
    printf("Reinitializing MAX30102 sensor...\r\n");
    
    // Reset the sensor
    MAX30102_softReset();
    _delay_ms(100);  // Wait for reset to complete
    
    // Setup FIFO configuration - changed for debugging
    uint8_t fifoConfig = 0x4F;  // Sample averaging of 16, FIFO rollover enabled, FIFO almost full at 15 samples
    I2C_writeRegister(MAX30102_I2C_ADDR, fifoConfig, MAX30102_FIFO_CONFIG);
    printf("FIFO Config set to 0x%02X\r\n", fifoConfig);
    
    // Configure sensor for heart rate mode
    uint8_t modeConfig = MAX30102_MODE_HRONLY;  // Heart rate only mode
    I2C_writeRegister(MAX30102_I2C_ADDR, modeConfig, MAX30102_MODE_CONFIG);
    printf("Mode Config set to 0x%02X\r\n", modeConfig);
    
    // Configure SPO2 settings
    uint8_t spo2Config = 0x27;  // SPO2 ADC range = 4096nA, SPO2 sample rate = 100Hz, LED pulse width = 411us
    I2C_writeRegister(MAX30102_I2C_ADDR, spo2Config, MAX30102_SPO2_CONFIG);
    printf("SPO2 Config set to 0x%02X\r\n", spo2Config);
    
    // Set LED pulse amplitude - increase for better signal
    I2C_writeRegister(MAX30102_I2C_ADDR, 0x3F, MAX30102_LED1_PA);  // Red LED pulse amplitude - maximum
    I2C_writeRegister(MAX30102_I2C_ADDR, 0x3F, MAX30102_LED2_PA);  // IR LED pulse amplitude - maximum
    printf("LED pulse amplitude set to maximum (0x3F)\r\n");
    
    // Enable FIFO almost full interrupt and new data ready interrupt
    I2C_writeRegister(MAX30102_I2C_ADDR, MAX30102_INT_A_FULL | MAX30102_INT_DATA_RDY, MAX30102_INT_ENABLE_1);
    I2C_writeRegister(MAX30102_I2C_ADDR, 0x00, MAX30102_INT_ENABLE_2);
    printf("Interrupts enabled: A_FULL and DATA_RDY\r\n");
    
    // Read and clear interrupt flags
    uint8_t intStatus1, intStatus2;
    I2C_readRegister(MAX30102_I2C_ADDR, &intStatus1, MAX30102_INT_STATUS_1);
    I2C_readRegister(MAX30102_I2C_ADDR, &intStatus2, MAX30102_INT_STATUS_2);
    printf("Interrupt status cleared. INT_STATUS_1=0x%02X, INT_STATUS_2=0x%02X\r\n", intStatus1, intStatus2);
    
    // Readback configuration registers to verify
    uint8_t readback;
    I2C_readRegister(MAX30102_I2C_ADDR, &readback, MAX30102_FIFO_CONFIG);
    printf("FIFO Config readback: 0x%02X\r\n", readback);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &readback, MAX30102_INT_ENABLE_1);
    printf("INT_ENABLE_1 readback: 0x%02X\r\n", readback);
    
    // Reset FIFO pointers
    uint8_t readPtr, writePtr;
    I2C_readRegister(MAX30102_I2C_ADDR, &readPtr, MAX30102_FIFO_RD_PTR);
    I2C_readRegister(MAX30102_I2C_ADDR, &writePtr, MAX30102_FIFO_WR_PTR);
    printf("Initial FIFO pointers - Read: 0x%02X, Write: 0x%02X\r\n", readPtr, writePtr);
    
    // Match the pointers to clear the FIFO
    I2C_writeRegister(MAX30102_I2C_ADDR, writePtr, MAX30102_FIFO_RD_PTR);
    printf("FIFO pointers synchronized\r\n");
    
    printf("MAX30102 Initialization complete\r\n");
}

// Dump all important registers for debugging
void dumpRegisters(void) {
    uint8_t regValue;
    
    printf("\r\n--- MAX30102 Register Dump ---\r\n");
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_INT_STATUS_1);
    printf("INT_STATUS_1: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_INT_STATUS_2);
    printf("INT_STATUS_2: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_INT_ENABLE_1);
    printf("INT_ENABLE_1: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_INT_ENABLE_2);
    printf("INT_ENABLE_2: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_FIFO_WR_PTR);
    printf("FIFO_WR_PTR: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_FIFO_RD_PTR);
    printf("FIFO_RD_PTR: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_FIFO_OVF_CNT);
    printf("FIFO_OVF_CNT: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_FIFO_CONFIG);
    printf("FIFO_CONFIG: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_MODE_CONFIG);
    printf("MODE_CONFIG: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_SPO2_CONFIG);
    printf("SPO2_CONFIG: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_LED1_PA);
    printf("LED1_PA: 0x%02X\r\n", regValue);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_LED2_PA);
    printf("LED2_PA: 0x%02X\r\n", regValue);
    
    printf("--- End Register Dump ---\r\n\r\n");
    
    // Update the display with the key status
    LCD_drawBlock(0, 70, 160, 127, BLACK);
    
    char buffer[30];
    sprintf(buffer, "INT_STAT_1: 0x%02X", regValue);
    LCD_drawString(10, 70, buffer, WHITE, BLACK);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_FIFO_WR_PTR);
    sprintf(buffer, "WR_PTR: 0x%02X", regValue);
    LCD_drawString(10, 85, buffer, WHITE, BLACK);
    
    I2C_readRegister(MAX30102_I2C_ADDR, &regValue, MAX30102_FIFO_RD_PTR);
    sprintf(buffer, "RD_PTR: 0x%02X", regValue);
    LCD_drawString(10, 100, buffer, WHITE, BLACK);
    
    sprintf(buffer, "INT Count: %lu", interruptCount);
    LCD_drawString(10, 115, buffer, WHITE, BLACK);
}

// Test FIFO operation directly
void testFifoOperation(void) {
    printf("\r\n--- Testing FIFO operation ---\r\n");
    
    // Read pointers
    uint8_t writePtr, readPtr, ovfCounter;
    I2C_readRegister(MAX30102_I2C_ADDR, &writePtr, MAX30102_FIFO_WR_PTR);
    I2C_readRegister(MAX30102_I2C_ADDR, &readPtr, MAX30102_FIFO_RD_PTR);
    I2C_readRegister(MAX30102_I2C_ADDR, &ovfCounter, MAX30102_FIFO_OVF_CNT);
    
    printf("FIFO Pointers - Write: 0x%02X, Read: 0x%02X, Overflow: 0x%02X\r\n", 
           writePtr, readPtr, ovfCounter);
    
    // Calculate available samples
    uint8_t available = 0;
    if (writePtr > readPtr) {
        available = writePtr - readPtr;
    } else if (writePtr < readPtr) {
        available = 32 - readPtr + writePtr;
    }
    printf("Calculated Available Samples: %d\r\n", available);
    
    // Try reading a sample directly from FIFO
    if (available > 0) {
        printf("Reading sample directly from FIFO...\r\n");
        
        // Read 6 bytes from the FIFO data register (one sample)
        uint8_t data[6];
        I2C_readCompleteStream(data, MAX30102_I2C_ADDR, MAX30102_FIFO_DATA, 6);
        
        // Construct the values
        uint32_t red_led = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
        uint32_t ir_led = ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 8) | data[5];
        
        // Mask with 18-bit mask (0x3FFFF)
        red_led &= 0x3FFFF;
        ir_led &= 0x3FFFF;
        
        printf("Sample - RED: %lu, IR: %lu\r\n", red_led, ir_led);
        
        // Update read pointer
        readPtr = (readPtr + 1) & 0x1F; // Increment and wrap at 32
        I2C_writeRegister(MAX30102_I2C_ADDR, readPtr, MAX30102_FIFO_RD_PTR);
        printf("Read pointer updated to: 0x%02X\r\n", readPtr);
    } else {
        printf("No samples available in FIFO\r\n");
    }
    
    // Update display with FIFO status
    LCD_drawBlock(0, 70, 160, 127, BLACK);
    
    char buffer[30];
    sprintf(buffer, "FIFO WR: 0x%02X RD: 0x%02X", writePtr, readPtr);
    LCD_drawString(10, 70, buffer, YELLOW, BLACK);
    
    sprintf(buffer, "Available: %d  OVF: %d", available, ovfCounter);
    LCD_drawString(10, 85, buffer, YELLOW, BLACK);
    
    sprintf(buffer, "INT Count: %lu", interruptCount);
    LCD_drawString(10, 115, buffer, WHITE, BLACK);
}

// Test interrupt pin directly by polling
void testInterruptPin(void) {
    printf("\r\n--- Testing INT pin directly ---\r\n");
    
    // Read current pin state
    uint8_t pinState = (PIND & (1 << PIND3)) ? 1 : 0;
    printf("Current INT pin state (PD3): %d\r\n", pinState);
    
    // Read interrupt status registers
    uint8_t intStatus1, intStatus2;
    I2C_readRegister(MAX30102_I2C_ADDR, &intStatus1, MAX30102_INT_STATUS_1);
    I2C_readRegister(MAX30102_I2C_ADDR, &intStatus2, MAX30102_INT_STATUS_2);
    
    printf("INT_STATUS_1: 0x%02X, INT_STATUS_2: 0x%02X\r\n", intStatus1, intStatus2);
    
    // Check if specific interrupts are active
    printf("A_FULL interrupt: %s\r\n", (intStatus1 & MAX30102_INT_A_FULL) ? "ACTIVE" : "inactive");
    printf("DATA_RDY interrupt: %s\r\n", (intStatus1 & MAX30102_INT_DATA_RDY) ? "ACTIVE" : "inactive");
    printf("ALC_OVF interrupt: %s\r\n", (intStatus1 & MAX30102_INT_ALC_OVF) ? "ACTIVE" : "inactive");
    printf("PROX_INT interrupt: %s\r\n", (intStatus1 & MAX30102_INT_PROX_INT) ? "ACTIVE" : "inactive");
    printf("TEMP_RDY interrupt: %s\r\n", (intStatus2 & MAX30102_INT_TEMP_RDY) ? "ACTIVE" : "inactive");
    
    // Update display with INT pin status
    LCD_drawBlock(0, 70, 160, 127, BLACK);
    
    char buffer[30];
    sprintf(buffer, "INT Pin: %s", pinState ? "HIGH" : "LOW");
    LCD_drawString(10, 70, buffer, pinState ? GREEN : RED, BLACK);
    
    sprintf(buffer, "INT_STAT_1: 0x%02X", intStatus1);
    LCD_drawString(10, 85, buffer, WHITE, BLACK);
    
    // Highlight active interrupts
    if (intStatus1 & MAX30102_INT_A_FULL) {
        LCD_drawString(10, 100, "A_FULL Active", RED, BLACK);
    } else if (intStatus1 & MAX30102_INT_DATA_RDY) {
        LCD_drawString(10, 100, "DATA_RDY Active", RED, BLACK);
    } else {
        LCD_drawString(10, 100, "No active interrupts", YELLOW, BLACK);
    }
    
    sprintf(buffer, "INT Count: %lu", interruptCount);
    LCD_drawString(10, 115, buffer, WHITE, BLACK);
}

// Heart rate sensor interrupt handler (INT1 - PD3)
ISR(INT1_vect) {
    // INT1 interrupt is configured for falling edge
    interruptCount++;
    heartRateDataReady = 1;
    
    // Debug output (be careful with printf in ISR)
    // printf("INT triggered! Count: %lu\r\n", interruptCount);
}

// Button interrupt handler (INT0 - PD2)
ISR(INT0_vect) {
    // Debounce
    _delay_ms(50);
    
    // Check if button is still pressed
    if (!(PIND & (1 << PIND2))) {
        toggleDebugMode();
    }
}

// Toggle between debug modes
void toggleDebugMode(void) {
    debugMode = (debugMode + 1) % 4;
    
    LCD_drawBlock(0, 45, 160, 65, BLACK);
    
    switch(debugMode) {
        case 0:
            LCD_drawString(10, 45, "Mode: Basic Test", WHITE, BLACK);
            printf("\r\nSwitched to Basic Test mode\r\n");
            break;
        case 1:
            LCD_drawString(10, 45, "Mode: FIFO Debug", CYAN, BLACK);
            printf("\r\nSwitched to FIFO Debug mode\r\n");
            break;
        case 2:
            LCD_drawString(10, 45, "Mode: Register Dump", MAGENTA, BLACK);
            printf("\r\nSwitched to Register Dump mode\r\n");
            break;
        case 3:
            LCD_drawString(10, 45, "Mode: INT Pin Test", YELLOW, BLACK);
            printf("\r\nSwitched to INT Pin Test mode\r\n");
            break;
    }
}

// Update display based on current debug mode
void updateDisplay(void) {
    switch(debugMode) {
        case 0: // Basic test
            if (heartRateDataReady) {
                heartRateDataReady = 0;
                LCD_drawBlock(0, 70, 160, 127, BLACK);
                
                char buffer[30];
                sprintf(buffer, "ISR Triggered!");
                LCD_drawString(10, 70, buffer, GREEN, BLACK);
                
                sprintf(buffer, "INT Count: %lu", interruptCount);
                LCD_drawString(10, 85, buffer, WHITE, BLACK);
                
                // Read interrupt status
                uint8_t intStatus1;
                I2C_readRegister(MAX30102_I2C_ADDR, &intStatus1, MAX30102_INT_STATUS_1);
                sprintf(buffer, "INT_STAT_1: 0x%02X", intStatus1);
                LCD_drawString(10, 100, buffer, WHITE, BLACK);
                
                printf("ISR Triggered! Count: %lu, INT_STATUS_1: 0x%02X\r\n", 
                       interruptCount, intStatus1);
            }
            break;
            
        case 1: // FIFO debug
            testFifoOperation();
            break;
            
        case 2: // Register dump
            dumpRegisters();
            break;
            
        case 3: // INT pin test
            testInterruptPin();
            break;
    }
}

int main(void) {
    // Initialize hardware
    initialize();
    
    // Start with basic debug mode
    debugMode = 0;
    toggleDebugMode();
    
    // Main loop
    while (1) {
        // Run appropriate debug mode
        updateDisplay();
        
        // Small delay to prevent overwhelming the processor and UART
        _delay_ms(1000);
    }
    
    return 0;
}