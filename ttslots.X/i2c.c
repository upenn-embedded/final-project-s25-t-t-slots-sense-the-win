/*
 * File:   i2c.c
 * I2C/TWI implementation for ESE 3500
 * Adapted for ATmega328PB with XC8 compiler
 * 
 * Author: Based on header by James Steeman
 * Implementation for T&T Slots project
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include "i2c.h"

// Status code constants
#define I2C_START_STATUS      0x08  // Start condition transmitted
#define I2C_REP_START_STATUS  0x10  // Repeated start condition transmitted
#define I2C_MT_SLA_ACK        0x18  // SLA+W transmitted, ACK received
#define I2C_MT_SLA_NACK       0x20  // SLA+W transmitted, NACK received
#define I2C_MT_DATA_ACK       0x28  // Data transmitted, ACK received
#define I2C_MT_DATA_NACK      0x30  // Data transmitted, NACK received
#define I2C_MR_SLA_ACK        0x40  // SLA+R transmitted, ACK received
#define I2C_MR_SLA_NACK       0x48  // SLA+R transmitted, NACK received
#define I2C_MR_DATA_ACK       0x50  // Data received, ACK returned
#define I2C_MR_DATA_NACK      0x58  // Data received, NACK returned

// TWI bit rate for 100kHz standard mode on 16MHz
#define TWI_FREQ 100000L
#define TWBR_VAL ((F_CPU / TWI_FREQ) - 16) / 2

/**
 * @brief Error handling function
 */
void ERROR() {
    // In a real application, this would do proper error handling
    // For now, just a placeholder for error detection
}

/**
 * @brief Initialize the TWI module
 */
void I2C_init() {
    // Set bit rate register for standard 100kHz
    TWBR0 = (uint8_t)TWBR_VAL;
    
    // Enable TWI module
    TWCR0 = (1 << TWEN);
}

/**
 * @brief Wait for TWI operation to complete
 * @return TWI status register value
 */
static uint8_t I2C_wait() {
    // Wait for TWINT flag to be set, indicating completion of TWI operation
    while (!(TWCR0 & (1 << TWINT)));
    
    // Return TWI status register value (masked with status bits)
    return (TWSR0 & 0xF8);
}

/**
 * @brief Send a start condition
 */
void I2C_start() {
    // Send START condition
    TWCR0 = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    
    // Wait for START to be transmitted
    if (I2C_wait() != I2C_START_STATUS) {
        ERROR();
    }
}

/**
 * @brief Send a repeated start condition
 */
void I2C_repStart() {
    // Send repeated START condition
    TWCR0 = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    
    // Wait for repeated START to be transmitted
    if (I2C_wait() != I2C_REP_START_STATUS) {
        ERROR();
    }
}

/**
 * @brief Send a stop condition
 */
void I2C_stop() {
    // Send STOP condition
    TWCR0 = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
    
    // Small delay to ensure the stop bit is fully processed
    _delay_us(10);
}

/**
 * @brief Begin a write operation to the specified address
 * @param addr 7-bit device address
 */
void I2C_writeBegin(uint8_t addr) {
    // Send device address with write bit (0)
    TWDR0 = (addr << 1) & 0xFE;
    
    // Clear TWINT flag to start transmission
    TWCR0 = (1 << TWINT) | (1 << TWEN);
    
    // Wait for address to be transmitted and ACK/NACK to be received
    uint8_t status = I2C_wait();
    if (status != I2C_MT_SLA_ACK) {
        ERROR();
    }
}

/**
 * @brief Begin a read operation from the specified address
 * @param addr 7-bit device address
 */
void I2C_readBegin(uint8_t addr) {
    // Send device address with read bit (1)
    TWDR0 = (addr << 1) | 0x01;
    
    // Clear TWINT flag to start transmission
    TWCR0 = (1 << TWINT) | (1 << TWEN);
    
    // Wait for address to be transmitted and ACK/NACK to be received
    uint8_t status = I2C_wait();
    if (status != I2C_MR_SLA_ACK) {
        ERROR();
    }
}

/**
 * @brief Write a byte to the bus and check for ACK
 * @param data Byte to write
 * @return 1 if ACK received, 0 if NACK received
 */
static uint8_t I2C_write(uint8_t data) {
    // Load data into TWDR register
    TWDR0 = data;
    
    // Clear TWINT flag to start transmission
    TWCR0 = (1 << TWINT) | (1 << TWEN);
    
    // Wait for data to be transmitted and ACK/NACK to be received
    uint8_t status = I2C_wait();
    
    // Return 1 if ACK received, 0 if NACK received
    return (status == I2C_MT_DATA_ACK);
}

/**
 * @brief Read a byte from the bus and send ACK/NACK
 * @param ack 1 to send ACK, 0 to send NACK
 * @return Byte read from the bus
 */
static uint8_t I2C_read(uint8_t ack) {
    // Start read operation with or without ACK
    if (ack) {
        // Read with ACK (expecting more data)
        TWCR0 = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    } else {
        // Read with NACK (last byte to read)
        TWCR0 = (1 << TWINT) | (1 << TWEN);
    }
    
    // Wait for data to be received
    I2C_wait();
    
    // Return received data
    return TWDR0;
}

/**
 * @brief Write data to a specific register on a device
 * @param addr 7-bit device address
 * @param data Data to write
 * @param reg Register address
 */
void I2C_writeRegister(uint8_t addr, uint8_t data, uint8_t reg) {
    // Send start condition
    I2C_start();
    
    // Send device address with write bit
    I2C_writeBegin(addr);
    
    // Send register address
    if (!I2C_write(reg)) {
        ERROR();
    }
    
    // Send data
    if (!I2C_write(data)) {
        ERROR();
    }
    
    // Send stop condition
    I2C_stop();
}

/**
 * @brief Read data from a specific register on a device
 * @param addr 7-bit device address
 * @param data_addr Pointer where read data will be stored
 * @param reg Register address
 */
void I2C_readRegister(uint8_t addr, uint8_t* data_addr, uint8_t reg) {
    // Send start condition
    I2C_start();
    
    // Send device address with write bit
    I2C_writeBegin(addr);
    
    // Send register address
    if (!I2C_write(reg)) {
        ERROR();
    }
    
    // Send repeated start
    I2C_repStart();
    
    // Send device address with read bit
    I2C_readBegin(addr);
    
    // Read data with NACK (only one byte)
    *data_addr = I2C_read(0);
    
    // Send stop condition
    I2C_stop();
}

/**
 * @brief Write a stream of data bytes
 * @param data Pointer to data array
 * @param len Number of bytes to write
 */
void I2C_writeStream(uint8_t* data, int len) {
    // Write each byte
    for (int i = 0; i < len; i++) {
        if (!I2C_write(data[i])) {
            ERROR();
            return;
        }
    }
}

/**
 * @brief Read a stream of data bytes
 * @param data_addr Pointer where read data will be stored
 * @param len Number of bytes to read
 */
void I2C_readStream(uint8_t* data_addr, int len) {
    // Read each byte
    for (int i = 0; i < len; i++) {
        // Send ACK for all bytes except the last one
        data_addr[i] = I2C_read(i < len - 1);
    }
}

/**
 * @brief Write data to multiple consecutive registers
 * @param dataArrPtr Pointer to data array
 * @param addrArrPtr Pointer to register address array
 * @param len Number of registers to write
 * @param addr 7-bit device address
 */
void I2C_writeCompleteStream(uint8_t *dataArrPtr, uint8_t *addrArrPtr, int len, uint8_t addr) {
    // Send start condition
    I2C_start();
    
    // Send device address with write bit
    I2C_writeBegin(addr);
    
    // Write to each register
    for (int i = 0; i < len; i++) {
        // Send register address
        if (!I2C_write(addrArrPtr[i])) {
            ERROR();
            break;
        }
        
        // Send data
        if (!I2C_write(dataArrPtr[i])) {
            ERROR();
            break;
        }
    }
    
    // Send stop condition
    I2C_stop();
}

/**
 * @brief Read data from multiple consecutive registers
 * @param dataArrPtr Pointer where read data will be stored
 * @param addr 7-bit device address
 * @param reg Starting register address
 * @param len Number of registers to read
 */
void I2C_readCompleteStream(uint8_t* dataArrPtr, uint8_t addr, uint8_t reg, int len) {
    // Send start condition
    I2C_start();
    
    // Send device address with write bit
    I2C_writeBegin(addr);
    
    // Send register address
    if (!I2C_write(reg)) {
        ERROR();
        return;
    }
    
    // Send repeated start
    I2C_repStart();
    
    // Send device address with read bit
    I2C_readBegin(addr);
    
    // Read data
    I2C_readStream(dataArrPtr, len);
    
    // Send stop condition
    I2C_stop();
}