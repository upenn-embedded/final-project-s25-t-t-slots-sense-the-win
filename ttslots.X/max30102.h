/**
 * @file max30102.h
 * @brief MAX30102 heart-rate and SpO2 sensor driver for ATMEGA328PB
 * @details Driver for MAXREFDES117# Heart-Rate and Pulse-Oximetry Monitor
 */

#ifndef MAX30102_H
#define MAX30102_H

#include <stdint.h>
#include <stdbool.h>
#include "i2c.h"

// MAX30102 I2C Address
#define MAX30102_I2C_ADDR 0x57

// Register addresses
#define MAX30102_INT_STATUS_1    0x00
#define MAX30102_INT_STATUS_2    0x01
#define MAX30102_INT_ENABLE_1    0x02
#define MAX30102_INT_ENABLE_2    0x03
#define MAX30102_FIFO_WR_PTR     0x04
#define MAX30102_FIFO_OVF_CNT    0x05
#define MAX30102_FIFO_RD_PTR     0x06
#define MAX30102_FIFO_DATA       0x07
#define MAX30102_FIFO_CONFIG     0x08
#define MAX30102_MODE_CONFIG     0x09
#define MAX30102_SPO2_CONFIG     0x0A
#define MAX30102_LED1_PA         0x0C // Red LED
#define MAX30102_LED2_PA         0x0D // IR LED
#define MAX30102_PILOT_PA        0x10
#define MAX30102_MULTI_LED_CONFIG1 0x11
#define MAX30102_MULTI_LED_CONFIG2 0x12
#define MAX30102_TEMP_INT        0x1F
#define MAX30102_TEMP_FRAC       0x20
#define MAX30102_TEMP_CONFIG     0x21
#define MAX30102_REV_ID          0xFE
#define MAX30102_PART_ID         0xFF

// Interrupt status bits
#define MAX30102_INT_A_FULL      (1<<7)
#define MAX30102_INT_PPG_RDY     (1<<6)
#define MAX30102_INT_ALC_OVF     (1<<5)
#define MAX30102_INT_DIE_TEMP_RDY (1<<1)

// Mode configuration bits
#define MAX30102_MODE_SHDN       (1<<7)
#define MAX30102_MODE_RESET      (1<<6)
#define MAX30102_MODE_HR_ONLY    0x02
#define MAX30102_MODE_SPO2_HR    0x03

// SpO2 configuration bits
#define MAX30102_SPO2_HI_RES_EN  (1<<6)

// Sample rates
typedef enum {
    MAX30102_SAMPLE_RATE_50_HZ = 0x00,
    MAX30102_SAMPLE_RATE_100_HZ = 0x01,
    MAX30102_SAMPLE_RATE_200_HZ = 0x02,
    MAX30102_SAMPLE_RATE_400_HZ = 0x03,
    MAX30102_SAMPLE_RATE_800_HZ = 0x04,
    MAX30102_SAMPLE_RATE_1000_HZ = 0x05,
    MAX30102_SAMPLE_RATE_1600_HZ = 0x06,
    MAX30102_SAMPLE_RATE_3200_HZ = 0x07
} max30102_sample_rate_t;

// Pulse width options
typedef enum {
    MAX30102_PULSE_WIDTH_69_US = 0x00, // 15-bit resolution
    MAX30102_PULSE_WIDTH_118_US = 0x01, // 16-bit resolution
    MAX30102_PULSE_WIDTH_215_US = 0x02, // 17-bit resolution
    MAX30102_PULSE_WIDTH_411_US = 0x03  // 18-bit resolution
} max30102_pulse_width_t;

// ADC range options
typedef enum {
    MAX30102_ADC_RANGE_2048_NA = 0x00,
    MAX30102_ADC_RANGE_4096_NA = 0x01,
    MAX30102_ADC_RANGE_8192_NA = 0x02,
    MAX30102_ADC_RANGE_16384_NA = 0x03
} max30102_adc_range_t;

// LED pulse amplitude values (0-255)
typedef struct {
    uint8_t red;    // LED1 pulse amplitude (Red)
    uint8_t ir;     // LED2 pulse amplitude (IR)
} max30102_led_amplitude_t;

// FIFO sample structure
typedef struct {
    uint32_t red;   // Red LED data
    uint32_t ir;    // IR LED data
} max30102_fifo_sample_t;

// Heart-rate and SpO2 results
typedef struct {
    int32_t heart_rate;    // Heart rate in BPM
    bool hr_valid;         // Heart rate validity flag
    int32_t spo2;          // SpO2 value in percentage (0-100)
    bool spo2_valid;       // SpO2 validity flag
} max30102_result_t;

/**
 * @brief Initialize the MAX30102 sensor
 * @return true if successful, false otherwise
 */
bool max30102_init(void);

/**
 * @brief Reset the MAX30102 sensor
 * @return true if successful, false otherwise
 */
bool max30102_reset(void);

/**
 * @brief Configure the MAX30102 sensor
 * @param sample_rate Sample rate setting
 * @param pulse_width Pulse width setting
 * @param adc_range ADC range setting
 * @param led_amplitude LED current amplitudes
 * @return true if successful, false otherwise
 */
bool max30102_configure(max30102_sample_rate_t sample_rate, 
                        max30102_pulse_width_t pulse_width,
                        max30102_adc_range_t adc_range,
                        max30102_led_amplitude_t led_amplitude);

/**
 * @brief Set the mode of operation
 * @param mode Mode setting (MAX30102_MODE_HR_ONLY or MAX30102_MODE_SPO2_HR)
 * @return true if successful, false otherwise
 */
bool max30102_set_mode(uint8_t mode);

/**
 * @brief Set the interrupt enables
 * @param int_1 Interrupt enable 1 register value
 * @param int_2 Interrupt enable 2 register value
 * @return true if successful, false otherwise
 */
bool max30102_set_interrupt_enables(uint8_t int_1, uint8_t int_2);

/**
 * @brief Read and clear interrupt status
 * @param int_status_1 Pointer to store interrupt status 1
 * @param int_status_2 Pointer to store interrupt status 2
 * @return true if successful, false otherwise
 */
bool max30102_read_interrupt_status(uint8_t *int_status_1, uint8_t *int_status_2);

/**
 * @brief Configure the FIFO settings
 * @param sample_avg Sample averaging (0=1, 1=2, 2=4, 3=8, 4=16, 5=32)
 * @param fifo_rollover_en FIFO rollover enable
 * @param fifo_almost_full_samples FIFO almost full value (0-15)
 * @return true if successful, false otherwise
 */
bool max30102_configure_fifo(uint8_t sample_avg, bool fifo_rollover_en, uint8_t fifo_almost_full_samples);

/**
 * @brief Read FIFO pointers
 * @param write_ptr Pointer to store write pointer value
 * @param read_ptr Pointer to store read pointer value
 * @param overflow_ctr Pointer to store overflow counter
 * @return true if successful, false otherwise
 */
bool max30102_read_fifo_ptrs(uint8_t *write_ptr, uint8_t *read_ptr, uint8_t *overflow_ctr);

/**
 * @brief Read samples from FIFO
 * @param samples Pointer to array to store samples
 * @param count Number of samples to read
 * @return Number of samples read
 */
uint8_t max30102_read_fifo_samples(max30102_fifo_sample_t *samples, uint8_t count);

/**
 * @brief Read die temperature
 * @param temperature_c Pointer to store temperature in degrees Celsius
 * @return true if successful, false otherwise
 */
bool max30102_read_temperature(float *temperature_c);

/**
 * @brief Read part ID
 * @param part_id Pointer to store part ID
 * @return true if successful, false otherwise
 */
bool max30102_read_part_id(uint8_t *part_id);

/**
 * @brief Read revision ID
 * @param rev_id Pointer to store revision ID
 * @return true if successful, false otherwise
 */
bool max30102_read_revision_id(uint8_t *rev_id);

/**
 * @brief Process samples to calculate heart rate and SpO2
 * @param samples Array of samples
 * @param count Number of samples
 * @param result Pointer to store the results
 * @return true if calculation successful, false otherwise
 */
bool max30102_calculate_hr_spo2(max30102_fifo_sample_t *samples, uint8_t count, max30102_result_t *result);

/**
 * @brief Shutdown the sensor
 * @param shutdown true to enter shutdown mode, false to exit
 * @return true if successful, false otherwise
 */
bool max30102_shutdown(bool shutdown);

/**
 * @brief Set up interrupt handling
 * @return true if successful, false otherwise
 */
bool max30102_setup_interrupt(void);

/**
 * @brief Process new data when interrupt occurs
 * @param result Pointer to store the results
 * @return true if new data processed, false otherwise
 */
bool max30102_process_interrupt(max30102_result_t *result);

#endif /* MAX30102_H */