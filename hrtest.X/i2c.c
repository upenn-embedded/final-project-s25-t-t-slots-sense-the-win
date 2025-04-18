/**
 * @file i2c.c
 * @brief I2C communication library implementation for ATMEGA328PB using XC8 compiler
 * @details Used for MAXREFDES117# sensor communication
 */
#define F_CPU 16000000UL
#include "i2c.h"
#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

// TWI status codes for XC8 compiler
#define TW_START        0x08
#define TW_REP_START    0x10
#define TW_MT_SLA_ACK   0x18
#define TW_MT_SLA_NACK  0x20
#define TW_MT_DATA_ACK  0x28
#define TW_MT_DATA_NACK 0x30
#define TW_MR_SLA_ACK   0x40
#define TW_MR_SLA_NACK  0x48
#define TW_MR_DATA_ACK  0x50
#define TW_MR_DATA_NACK 0x58

// XC8 bit definitions for TWI
#define TWINT  7
#define TWSTA  5
#define TWSTO  4
#define TWEA   6
#define TWEN   2
#define TWPS0  0
#define TWPS1  1

// Private function prototypes
static bool i2c_wait(uint16_t timeout_ms);
static void delay_us(uint16_t us);
static void delay_ms(uint16_t ms);

// Simple delay functions
static void delay_us(uint16_t us) {
    // Simple software delay - adjust based on your clock frequency
    for (uint16_t i = 0; i < us; i++) {
        for (uint8_t j = 0; j < 4; j++) {
            asm("nop");
        }
    }
}

static void delay_ms(uint16_t ms) {
    for (uint16_t i = 0; i < ms; i++) {
        delay_us(1000);
    }
}

/**
 * @brief Initialize I2C communication
 * @param frequency I2C bus frequency in Hz
 */
void i2c_init(uint32_t frequency) {
    // Calculate TWI bit rate register value for the desired frequency
    // SCL frequency = F_CPU / (16 + 2 * TWBR * 4^TWPS)
    // Assuming TWPS (prescaler) = 0, which means 4^TWPS = 1
    uint8_t twbr_value = ((F_CPU / frequency) - 16) / 2;
    
    // Set TWI bit rate register
    TWBR0 = twbr_value;
    
    // Set prescaler to 0 in TWSR register
    TWSR0 &= ~((1 << TWPS1) | (1 << TWPS0));
    
    // Enable TWI
    TWCR0 = (1 << TWEN);
}

/**
 * @brief Wait for I2C operations to complete with timeout
 * @param timeout_ms timeout in milliseconds
 * @return true if operation completed, false if timeout
 */
bool i2c_wait_for_complete(uint16_t timeout_ms) {
    uint16_t timer = 0;
    
    // Wait for TWINT flag to be set
    while (!(TWCR0 & (1 << TWINT))) {
        delay_us(100);
        timer += 1;
        if (timer >= timeout_ms * 10) {
            return false; // Timeout
        }
    }
    
    return true;
}

/**
 * @brief Private function to wait for TWI operation to complete
 * @param timeout_ms timeout in milliseconds
 * @return true if successful, false if timeout or error
 */
static bool i2c_wait(uint16_t timeout_ms) {
    if (!i2c_wait_for_complete(timeout_ms)) {
        return false;
    }
    
    // Check TWI status
    uint8_t status = TWSR0 & 0xF8;
    
    // These are general success codes, specific functions will check for specific success codes
    if (status == TW_START || status == TW_REP_START || 
        status == TW_MT_SLA_ACK || status == TW_MT_DATA_ACK || 
        status == TW_MR_SLA_ACK || status == TW_MR_DATA_ACK || 
        status == TW_MR_DATA_NACK) {
        return true;
    }
    
    return false;
}

/**
 * @brief Send start condition
 * @return true if successful, false otherwise
 */
bool i2c_start(void) {
    // Send START condition
    TWCR0 = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    
    // Wait for operation to complete
    if (!i2c_wait_for_complete(10)) {
        return false;
    }
    
    // Check if START condition was transmitted
    if ((TWSR0 & 0xF8) != TW_START) {
        return false;
    }
    
    return true;
}

/**
 * @brief Send repeated start condition
 * @return true if successful, false otherwise
 */
bool i2c_restart(void) {
    // Send repeated START condition
    TWCR0 = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    
    // Wait for operation to complete
    if (!i2c_wait_for_complete(10)) {
        return false;
    }
    
    // Check if repeated START condition was transmitted
    if ((TWSR0 & 0xF8) != TW_REP_START) {
        return false;
    }
    
    return true;
}

/**
 * @brief Send stop condition
 */
void i2c_stop(void) {
    // Send STOP condition
    TWCR0 = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    
    // Wait until STOP condition is executed
    uint16_t timeout = 1000; // 100ms timeout
    while (TWCR0 & (1 << TWSTO)) {
        delay_us(100);
        if (--timeout == 0) {
            break; // Timeout, exit anyway
        }
    }
}

/**
 * @brief Send address + R/W bit
 * @param address 7-bit device address
 * @param read true for read, false for write
 * @return true if ACK received, false if NACK
 */
bool i2c_address(uint8_t address, bool read) {
    // Prepare address with R/W bit
    uint8_t twdr_value = (address << 1) | (read ? 1 : 0);
    
    // Load address into TWDR
    TWDR0 = twdr_value;
    
    // Start transmission
    TWCR0 = (1 << TWINT) | (1 << TWEN);
    
    // Wait for operation to complete
    if (!i2c_wait_for_complete(10)) {
        return false;
    }
    
    // Check if SLA+R/W was transmitted and ACK received
    uint8_t status = TWSR0 & 0xF8;
    if (read) {
        return (status == TW_MR_SLA_ACK);
    } else {
        return (status == TW_MT_SLA_ACK);
    }
}

/**
 * @brief Write byte to I2C bus
 * @param data byte to send
 * @return true if ACK received, false if NACK
 */
bool i2c_write(uint8_t data) {
    // Load data into TWDR
    TWDR0 = data;
    
    // Start transmission
    TWCR0 = (1 << TWINT) | (1 << TWEN);
    
    // Wait for operation to complete
    if (!i2c_wait_for_complete(10)) {
        return false;
    }
    
    // Check if data was transmitted and ACK received
    return ((TWSR0 & 0xF8) == TW_MT_DATA_ACK);
}

/**
 * @brief Read byte from I2C bus
 * @param ack true to send ACK, false to send NACK
 * @return byte read from I2C bus
 */
uint8_t i2c_read(bool ack) {
    // Start reception with ACK/NACK
    if (ack) {
        TWCR0 = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    } else {
        TWCR0 = (1 << TWINT) | (1 << TWEN);
    }
    
    // Wait for operation to complete
    if (!i2c_wait_for_complete(10)) {
        return 0xFF; // Return 0xFF on error
    }
    
    // Check if data was received and ACK/NACK sent
    uint8_t status = TWSR0 & 0xF8;
    if (ack && status != TW_MR_DATA_ACK) {
        return 0xFF; // Return 0xFF on error
    }
    if (!ack && status != TW_MR_DATA_NACK) {
        return 0xFF; // Return 0xFF on error
    }
    
    // Return received data
    return TWDR0;
}

/**
 * @brief Write multiple bytes to device
 * @param address 7-bit device address
 * @param data pointer to data buffer
 * @param len number of bytes to write
 * @return true if successful, false otherwise
 */
bool i2c_write_buffer(uint8_t address, uint8_t *data, uint8_t len) {
    if (!i2c_start()) {
        return false;
    }
    
    if (!i2c_address(address, false)) {
        i2c_stop();
        return false;
    }
    
    for (uint8_t i = 0; i < len; i++) {
        if (!i2c_write(data[i])) {
            i2c_stop();
            return false;
        }
    }
    
    i2c_stop();
    return true;
}

/**
 * @brief Read multiple bytes from device
 * @param address 7-bit device address
 * @param data pointer to data buffer
 * @param len number of bytes to read
 * @return true if successful, false otherwise
 */
bool i2c_read_buffer(uint8_t address, uint8_t *data, uint8_t len) {
    if (!i2c_start()) {
        return false;
    }
    
    if (!i2c_address(address, true)) {
        i2c_stop();
        return false;
    }
    
    for (uint8_t i = 0; i < len; i++) {
        // Send ACK for all bytes except the last one
        data[i] = i2c_read(i < (len - 1));
    }
    
    i2c_stop();
    return true;
}

/**
 * @brief Write to device register
 * @param dev_addr 7-bit device address
 * @param reg_addr register address
 * @param data byte to write
 * @return true if successful, false otherwise
 */
bool i2c_write_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t data) {
    if (!i2c_start()) {
        return false;
    }
    
    if (!i2c_address(dev_addr, false)) {
        i2c_stop();
        return false;
    }
    
    if (!i2c_write(reg_addr)) {
        i2c_stop();
        return false;
    }
    
    if (!i2c_write(data)) {
        i2c_stop();
        return false;
    }
    
    i2c_stop();
    return true;
}

/**
 * @brief Read from device register
 * @param dev_addr 7-bit device address
 * @param reg_addr register address
 * @param data pointer to store read byte
 * @return true if successful, false otherwise
 */
bool i2c_read_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data) {
    if (!i2c_start()) {
        return false;
    }
    
    if (!i2c_address(dev_addr, false)) {
        i2c_stop();
        return false;
    }
    
    if (!i2c_write(reg_addr)) {
        i2c_stop();
        return false;
    }
    
    if (!i2c_restart()) {
        i2c_stop();
        return false;
    }
    
    if (!i2c_address(dev_addr, true)) {
        i2c_stop();
        return false;
    }
    
    *data = i2c_read(false); // Read with NACK
    
    i2c_stop();
    return true;
}

/**
 * @brief Read multiple bytes from device register
 * @param dev_addr 7-bit device address
 * @param reg_addr register address
 * @param data pointer to data buffer
 * @param len number of bytes to read
 * @return true if successful, false otherwise
 */
bool i2c_read_registers(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
    if (!i2c_start()) {
        return false;
    }
    
    if (!i2c_address(dev_addr, false)) {
        i2c_stop();
        return false;
    }
    
    if (!i2c_write(reg_addr)) {
        i2c_stop();
        return false;
    }
    
    if (!i2c_restart()) {
        i2c_stop();
        return false;
    }
    
    if (!i2c_address(dev_addr, true)) {
        i2c_stop();
        return false;
    }
    
    for (uint8_t i = 0; i < len; i++) {
        // Send ACK for all bytes except the last one
        data[i] = i2c_read(i < (len - 1));
    }
    
    i2c_stop();
    return true;
}

/**
 * @brief Check if device is present on I2C bus
 * @param address 7-bit device address
 * @return true if device responds, false otherwise
 */
bool i2c_is_device_ready(uint8_t address) {
    bool result = false;
    
    if (i2c_start()) {
        result = i2c_address(address, false);
    }
    
    i2c_stop();
    return result;
}