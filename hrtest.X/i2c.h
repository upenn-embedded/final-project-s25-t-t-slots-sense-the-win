/**
 * @file i2c.h
 * @brief I2C communication library for ATMEGA328PB
 * @details Used for MAXREFDES117# sensor communication
 */

#ifndef I2C_H
#define I2C_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize I2C communication
 * @param frequency I2C bus frequency in Hz
 * @return none
 */
void i2c_init(uint32_t frequency);

/**
 * @brief Send start condition
 * @return true if successful, false otherwise
 */
bool i2c_start(void);

/**
 * @brief Send repeated start condition
 * @return true if successful, false otherwise
 */
bool i2c_restart(void);

/**
 * @brief Send stop condition
 * @return none
 */
void i2c_stop(void);

/**
 * @brief Send address + R/W bit
 * @param address 7-bit device address
 * @param read true for read, false for write
 * @return true if ACK received, false if NACK
 */
bool i2c_address(uint8_t address, bool read);

/**
 * @brief Write byte to I2C bus
 * @param data byte to send
 * @return true if ACK received, false if NACK
 */
bool i2c_write(uint8_t data);

/**
 * @brief Read byte from I2C bus
 * @param ack true to send ACK, false to send NACK
 * @return byte read from I2C bus
 */
uint8_t i2c_read(bool ack);

/**
 * @brief Write multiple bytes to device
 * @param address 7-bit device address
 * @param data pointer to data buffer
 * @param len number of bytes to write
 * @return true if successful, false otherwise
 */
bool i2c_write_buffer(uint8_t address, uint8_t *data, uint8_t len);

/**
 * @brief Read multiple bytes from device
 * @param address 7-bit device address
 * @param data pointer to data buffer
 * @param len number of bytes to read
 * @return true if successful, false otherwise
 */
bool i2c_read_buffer(uint8_t address, uint8_t *data, uint8_t len);

/**
 * @brief Write to device register
 * @param dev_addr 7-bit device address
 * @param reg_addr register address
 * @param data byte to write
 * @return true if successful, false otherwise
 */
bool i2c_write_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);

/**
 * @brief Read from device register
 * @param dev_addr 7-bit device address
 * @param reg_addr register address
 * @param data pointer to store read byte
 * @return true if successful, false otherwise
 */
bool i2c_read_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data);

/**
 * @brief Read multiple bytes from device register
 * @param dev_addr 7-bit device address
 * @param reg_addr register address
 * @param data pointer to data buffer
 * @param len number of bytes to read
 * @return true if successful, false otherwise
 */
bool i2c_read_registers(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

/**
 * @brief Check if device is present on I2C bus
 * @param address 7-bit device address
 * @return true if device responds, false otherwise
 */
bool i2c_is_device_ready(uint8_t address);

/**
 * @brief Wait for I2C operations to complete with timeout
 * @param timeout_ms timeout in milliseconds
 * @return true if operation completed, false if timeout
 */
bool i2c_wait_for_complete(uint16_t timeout_ms);

#endif /* I2C_H */