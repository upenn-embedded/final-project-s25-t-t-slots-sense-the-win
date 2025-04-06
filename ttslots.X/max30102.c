#include "max30102.h"
#include "i2c.h"
#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>

// Heartbeat detection variables
#define AVERAGE_WINDOW_SIZE 4  // Number of samples to average
static uint32_t ir_moving_avg = 0;
static uint32_t prev_ir_values[AVERAGE_WINDOW_SIZE] = {0};
static uint8_t avg_index = 0;
static uint8_t beat_detected = 0;
static uint32_t last_beat_time = 0;
static uint32_t current_time = 0;
static uint16_t beatInterval = 0;
static uint16_t heartRate = 0;

// Initialize the MAX30102 sensor
void MAX30102_init(void) {
    // Reset the sensor
    MAX30102_softReset();
    _delay_ms(100);  // Wait for reset to complete
    
    // Setup FIFO configuration
    uint8_t fifoConfig = 0x0F;  // Sample averaging of 4, FIFO rollover enabled, FIFO almost full at 17 samples
    I2C_writeRegister(MAX30102_I2C_ADDR, fifoConfig, MAX30102_FIFO_CONFIG);
    
    // Configure sensor for heart rate mode
    uint8_t modeConfig = MAX30102_MODE_HRONLY;  // Heart rate only mode
    I2C_writeRegister(MAX30102_I2C_ADDR, modeConfig, MAX30102_MODE_CONFIG);
    
    // Configure SPO2 settings
    uint8_t spo2Config = 0x27;  // SPO2 ADC range = 4096nA, SPO2 sample rate = 100Hz, LED pulse width = 411us
    I2C_writeRegister(MAX30102_I2C_ADDR, spo2Config, MAX30102_SPO2_CONFIG);
    
    // Set LED pulse amplitude
    I2C_writeRegister(MAX30102_I2C_ADDR, 0x24, MAX30102_LED1_PA);  // Red LED pulse amplitude
    I2C_writeRegister(MAX30102_I2C_ADDR, 0x24, MAX30102_LED2_PA);  // IR LED pulse amplitude
    
    // Enable FIFO almost full interrupt and new data ready interrupt
    I2C_writeRegister(MAX30102_I2C_ADDR, MAX30102_INT_A_FULL | MAX30102_INT_DATA_RDY, MAX30102_INT_ENABLE_1);
    I2C_writeRegister(MAX30102_I2C_ADDR, 0x00, MAX30102_INT_ENABLE_2);
    
    // Read and clear interrupt flags
    uint8_t intStatus;
    I2C_readRegister(MAX30102_I2C_ADDR, &intStatus, MAX30102_INT_STATUS_1);
    I2C_readRegister(MAX30102_I2C_ADDR, &intStatus, MAX30102_INT_STATUS_2);
    
    printf("MAX30102 Initialized\r\n");
}

// Read data from FIFO
void MAX30102_readFIFO(uint32_t *pun_red_led, uint32_t *pun_ir_led) {
    uint8_t data[6];
    
    // Read 6 bytes from the FIFO data register
    I2C_readCompleteStream(data, MAX30102_I2C_ADDR, MAX30102_FIFO_DATA, 6);
    
    // Construct 18-bit values (data is stored with the MSB first)
    *pun_red_led = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    *pun_ir_led = ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 8) | data[5];
    
    // Mask with 18-bit mask (0x3FFFF) to ensure only 18 bits are used
    *pun_red_led &= 0x3FFFF;
    *pun_ir_led &= 0x3FFFF;
}

// Check if data is available
uint8_t MAX30102_available(void) {
    uint8_t writePtr, readPtr, ovfCounter;
    
    // Read FIFO write pointer
    I2C_readRegister(MAX30102_I2C_ADDR, &writePtr, MAX30102_FIFO_WR_PTR);
    
    // Read FIFO read pointer
    I2C_readRegister(MAX30102_I2C_ADDR, &readPtr, MAX30102_FIFO_RD_PTR);
    
    // Read overflow counter
    I2C_readRegister(MAX30102_I2C_ADDR, &ovfCounter, MAX30102_FIFO_OVF_CNT);
    
    // Calculate number of available samples
    uint8_t num_samples = 0;
    
    if (ovfCounter > 0) {
        // Overflow occurred, reset pointers
        MAX30102_softReset();
        return 0;
    }
    
    if (writePtr >= readPtr) {
        num_samples = writePtr - readPtr;
    } else {
        num_samples = 32 - readPtr + writePtr;  // 32 is the size of the FIFO
    }
    
    return num_samples;
}

// Configure the sensor
void MAX30102_setup(uint8_t powerLevel, uint8_t sampleAverage, uint8_t mode) {
    // Set mode configuration
    I2C_writeRegister(MAX30102_I2C_ADDR, mode, MAX30102_MODE_CONFIG);
    
    // Set FIFO configuration (sample averaging)
    uint8_t fifoConfig = 0;
    switch (sampleAverage) {
        case 1: fifoConfig = 0x00 << 5; break;
        case 2: fifoConfig = 0x01 << 5; break;
        case 4: fifoConfig = 0x02 << 5; break;
        case 8: fifoConfig = 0x03 << 5; break;
        case 16: fifoConfig = 0x04 << 5; break;
        case 32: fifoConfig = 0x05 << 5; break;
        default: fifoConfig = 0x02 << 5; break;  // Default to 4 samples
    }
    fifoConfig |= 0x10;  // Enable FIFO rollover
    fifoConfig |= 0x0F;  // Set FIFO almost full at 15 samples
    I2C_writeRegister(MAX30102_I2C_ADDR, fifoConfig, MAX30102_FIFO_CONFIG);
    
    // Set LED pulse amplitude
    I2C_writeRegister(MAX30102_I2C_ADDR, powerLevel, MAX30102_LED1_PA);  // Red LED
    I2C_writeRegister(MAX30102_I2C_ADDR, powerLevel, MAX30102_LED2_PA);  // IR LED
}

// Calculate heart rate from IR LED data
uint16_t MAX30102_getHeartRate(void) {
    uint32_t red_led, ir_led;
    
    // Read data from FIFO
    MAX30102_readFIFO(&red_led, &ir_led);
    
    // Add to moving average
    ir_moving_avg -= prev_ir_values[avg_index];
    prev_ir_values[avg_index] = ir_led;
    ir_moving_avg += ir_led;
    avg_index = (avg_index + 1) % AVERAGE_WINDOW_SIZE;
    
    // Simple beat detection algorithm
    static uint32_t threshold = 0;
    static uint8_t state = 0;  // 0: below threshold, 1: above threshold
    
    // Adaptive threshold
    threshold = ir_moving_avg / AVERAGE_WINDOW_SIZE;
    
    // Increment time counter for heart rate calculation
    current_time++;
    
    // Detect rising edge (beat)
    if (state == 0 && ir_led > threshold * 1.2) {  // 20% above average
        state = 1;  // Now above threshold
        
        // Detect beat only if enough time has passed since last beat
        if (current_time - last_beat_time > 60) {  // Minimum time between beats (prevents double-counting)
            beat_detected = 1;
            
            // Calculate time interval between beats
            beatInterval = current_time - last_beat_time;
            
            // Calculate heart rate (beats per minute)
            // Assuming our time units are based on the sampling rate (100Hz)
            if (beatInterval > 0) {
                heartRate = (uint16_t)(60.0 * 100 / beatInterval);  // 100Hz to beats per minute
                
                // Sanity check for heart rate range (40-220 BPM)
                if (heartRate < 40 || heartRate > 220) {
                    heartRate = 0;  // Invalid reading
                }
            }
            
            last_beat_time = current_time;
        }
    } 
    else if (state == 1 && ir_led < threshold * 0.8) {  // 20% below average
        state = 0;  // Now below threshold
    }
    
    return heartRate;
}

// Shutdown the sensor
void MAX30102_shutdown(void) {
    uint8_t modeConfig;
    I2C_readRegister(MAX30102_I2C_ADDR, &modeConfig, MAX30102_MODE_CONFIG);
    
    // Set shutdown bit
    modeConfig |= 0x80;
    I2C_writeRegister(MAX30102_I2C_ADDR, modeConfig, MAX30102_MODE_CONFIG);
}

// Wake up the sensor
void MAX30102_wakeup(void) {
    uint8_t modeConfig;
    I2C_readRegister(MAX30102_I2C_ADDR, &modeConfig, MAX30102_MODE_CONFIG);
    
    // Clear shutdown bit
    modeConfig &= ~0x80;
    I2C_writeRegister(MAX30102_I2C_ADDR, modeConfig, MAX30102_MODE_CONFIG);
}

// Perform a soft reset
void MAX30102_softReset(void) {
    I2C_writeRegister(MAX30102_I2C_ADDR, 0x40, MAX30102_MODE_CONFIG);  // Set reset bit
    _delay_ms(50);  // Wait for reset to complete
}

// Get the revision ID
uint8_t MAX30102_getRevisionID(void) {
    uint8_t revId;
    I2C_readRegister(MAX30102_I2C_ADDR, &revId, MAX30102_REV_ID);
    return revId;
}

// Read the part ID
uint8_t MAX30102_readPartID(void) {
    uint8_t partId;
    I2C_readRegister(MAX30102_I2C_ADDR, &partId, MAX30102_PART_ID);
    return partId;
}

// Enable A_FULL interrupt
void MAX30102_enableAFULL(void) {
    uint8_t intEnable;
    I2C_readRegister(MAX30102_I2C_ADDR, &intEnable, MAX30102_INT_ENABLE_1);
    intEnable |= MAX30102_INT_A_FULL;
    I2C_writeRegister(MAX30102_I2C_ADDR, intEnable, MAX30102_INT_ENABLE_1);
}

// Disable A_FULL interrupt
void MAX30102_disableAFULL(void) {
    uint8_t intEnable;
    I2C_readRegister(MAX30102_I2C_ADDR, &intEnable, MAX30102_INT_ENABLE_1);
    intEnable &= ~MAX30102_INT_A_FULL;
    I2C_writeRegister(MAX30102_I2C_ADDR, intEnable, MAX30102_INT_ENABLE_1);
}

// Enable DATA_RDY interrupt
void MAX30102_enableDATARDY(void) {
    uint8_t intEnable;
    I2C_readRegister(MAX30102_I2C_ADDR, &intEnable, MAX30102_INT_ENABLE_1);
    intEnable |= MAX30102_INT_DATA_RDY;
    I2C_writeRegister(MAX30102_I2C_ADDR, intEnable, MAX30102_INT_ENABLE_1);
}

// Disable DATA_RDY interrupt
void MAX30102_disableDATARDY(void) {
    uint8_t intEnable;
    I2C_readRegister(MAX30102_I2C_ADDR, &intEnable, MAX30102_INT_ENABLE_1);
    intEnable &= ~MAX30102_INT_DATA_RDY;
    I2C_writeRegister(MAX30102_I2C_ADDR, intEnable, MAX30102_INT_ENABLE_1);
}

// Set FIFO average
void MAX30102_setFIFOAverage(uint8_t samples) {
    uint8_t fifoConfig;
    I2C_readRegister(MAX30102_I2C_ADDR, &fifoConfig, MAX30102_FIFO_CONFIG);
    
    // Clear sample average bits (bits 5:7)
    fifoConfig &= 0x1F;
    
    // Set new sample average value
    switch (samples) {
        case 1: fifoConfig |= 0x00 << 5; break;
        case 2: fifoConfig |= 0x01 << 5; break;
        case 4: fifoConfig |= 0x02 << 5; break;
        case 8: fifoConfig |= 0x03 << 5; break;
        case 16: fifoConfig |= 0x04 << 5; break;
        case 32: fifoConfig |= 0x05 << 5; break;
        default: fifoConfig |= 0x02 << 5; break;  // Default to 4 samples
    }
    
    I2C_writeRegister(MAX30102_I2C_ADDR, fifoConfig, MAX30102_FIFO_CONFIG);
}

// Set FIFO almost full value
void MAX30102_setFIFOAlmostFull(uint8_t samples) {
    uint8_t fifoConfig;
    I2C_readRegister(MAX30102_I2C_ADDR, &fifoConfig, MAX30102_FIFO_CONFIG);
    
    // Clear FIFO almost full bits (bits 0:3)
    fifoConfig &= 0xF0;
    
    // Set new FIFO almost full value (limit to 15 samples max)
    if (samples > 15) samples = 15;
    fifoConfig |= samples;
    
    I2C_writeRegister(MAX30102_I2C_ADDR, fifoConfig, MAX30102_FIFO_CONFIG);
}