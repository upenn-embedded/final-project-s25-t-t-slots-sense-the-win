/**
 * @file max30102.c
 * @brief MAX30102 heart-rate and SpO2 sensor driver implementation for ATMEGA328PB
 * @details Driver for MAXREFDES117# Heart-Rate and Pulse-Oximetry Monitor
 */

#include "max30102.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Default configuration values
#define DEFAULT_SAMPLE_RATE MAX30102_SAMPLE_RATE_100_HZ
#define DEFAULT_PULSE_WIDTH MAX30102_PULSE_WIDTH_411_US
#define DEFAULT_ADC_RANGE MAX30102_ADC_RANGE_16384_NA
#define DEFAULT_LED_RED_AMPLITUDE 0x1F  // ~6.4mA
#define DEFAULT_LED_IR_AMPLITUDE 0x1F   // ~6.4mA
#define DEFAULT_SAMPLE_AVG 4            // 16 samples averaging
#define DEFAULT_FIFO_ROLLOVER true
#define DEFAULT_FIFO_ALMOST_FULL 15

// Number of samples to discard after configuration change
#define DISCARD_SAMPLES 5

// Private function prototypes
static bool max30102_write_register(uint8_t reg_addr, uint8_t data);
static bool max30102_read_register(uint8_t reg_addr, uint8_t *data);
static bool max30102_read_registers(uint8_t reg_addr, uint8_t *data, uint8_t len);
static bool max30102_clear_fifo(void);
static uint32_t max30102_extract_red_sample(uint8_t *buffer, uint8_t pulse_width);
static uint32_t max30102_extract_ir_sample(uint8_t *buffer, uint8_t pulse_width);

// Global variables
static max30102_pulse_width_t current_pulse_width = DEFAULT_PULSE_WIDTH;
static uint32_t ir_ac_prev = 0;
static uint32_t red_ac_prev = 0;
static float ir_dc_prev = 0;
static float red_dc_prev = 0;
static int32_t heart_rate_history[8] = {0};
static int32_t spo2_history[8] = {0};
static uint8_t history_index = 0;

/**
 * @brief Initialize the MAX30102 sensor
 * @return true if successful, false otherwise
 */
bool max30102_init(void) {
    uint8_t part_id;
    
    // Reset the sensor
    if (!max30102_reset()) {
        return false;
    }
    
    // Verify part ID
    if (!max30102_read_part_id(&part_id)) {
        return false;
    }
    
    if (part_id != 0x15) {  // MAX30102 part ID
        return false;
    }
    
    // Default configuration
    max30102_led_amplitude_t led_amplitude = {
        .red = DEFAULT_LED_RED_AMPLITUDE,
        .ir = DEFAULT_LED_IR_AMPLITUDE
    };
    
    if (!max30102_configure(DEFAULT_SAMPLE_RATE, DEFAULT_PULSE_WIDTH, 
                           DEFAULT_ADC_RANGE, led_amplitude)) {
        return false;
    }
    
    // Configure FIFO
    if (!max30102_configure_fifo(DEFAULT_SAMPLE_AVG, DEFAULT_FIFO_ROLLOVER, 
                                DEFAULT_FIFO_ALMOST_FULL)) {
        return false;
    }
    
    // Set SPO2+HR mode
    if (!max30102_set_mode(MAX30102_MODE_SPO2_HR)) {
        return false;
    }
    
    // Enable FIFO almost full interrupt
    if (!max30102_set_interrupt_enables(MAX30102_INT_A_FULL, 0x00)) {
        return false;
    }
    
    // Clear FIFO
    if (!max30102_clear_fifo()) {
        return false;
    }
    
    return true;
}

/**
 * @brief Reset the MAX30102 sensor
 * @return true if successful, false otherwise
 */
bool max30102_reset(void) {
    // Set reset bit in MODE_CONFIG register
    if (!max30102_write_register(MAX30102_MODE_CONFIG, MAX30102_MODE_RESET)) {
        return false;
    }
    
    // Wait for reset to complete (reset bit clears automatically)
    uint8_t mode_config;
    uint8_t retry = 10;
    
    do {
        if (!max30102_read_register(MAX30102_MODE_CONFIG, &mode_config)) {
            return false;
        }
        
        if ((mode_config & MAX30102_MODE_RESET) == 0) {
            break;
        }
        
        // Simple delay
        for (uint16_t i = 0; i < 10000; i++) {
            asm("nop");
        }
        
        retry--;
    } while (retry > 0);
    
    if (retry == 0) {
        return false;  // Reset timeout
    }
    
    return true;
}

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
                      max30102_led_amplitude_t led_amplitude) {
                      
    // Store current pulse width for sample extraction
    current_pulse_width = pulse_width;
    
    // Configure SPO2 settings (sample rate, pulse width, ADC range)
    uint8_t spo2_config = (sample_rate << 2) | (pulse_width << 0);
    spo2_config |= MAX30102_SPO2_HI_RES_EN; // Enable high resolution mode
    spo2_config |= (adc_range << 4);
    
    if (!max30102_write_register(MAX30102_SPO2_CONFIG, spo2_config)) {
        return false;
    }
    
    // Set LED pulse amplitudes
    if (!max30102_write_register(MAX30102_LED1_PA, led_amplitude.red)) {
        return false;
    }
    
    if (!max30102_write_register(MAX30102_LED2_PA, led_amplitude.ir)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Set the mode of operation
 * @param mode Mode setting (MAX30102_MODE_HR_ONLY or MAX30102_MODE_SPO2_HR)
 * @return true if successful, false otherwise
 */
bool max30102_set_mode(uint8_t mode) {
    // Read current mode configuration
    uint8_t mode_config;
    if (!max30102_read_register(MAX30102_MODE_CONFIG, &mode_config)) {
        return false;
    }
    
    // Clear mode bits (bits 2:0) and set new mode
    mode_config &= ~0x07;  // Clear bits 2:0
    mode_config |= mode;   // Set new mode
    
    // Write updated mode configuration
    if (!max30102_write_register(MAX30102_MODE_CONFIG, mode_config)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Set the interrupt enables
 * @param int_1 Interrupt enable 1 register value
 * @param int_2 Interrupt enable 2 register value
 * @return true if successful, false otherwise
 */
bool max30102_set_interrupt_enables(uint8_t int_1, uint8_t int_2) {
    if (!max30102_write_register(MAX30102_INT_ENABLE_1, int_1)) {
        return false;
    }
    
    if (!max30102_write_register(MAX30102_INT_ENABLE_2, int_2)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Read and clear interrupt status
 * @param int_status_1 Pointer to store interrupt status 1
 * @param int_status_2 Pointer to store interrupt status 2
 * @return true if successful, false otherwise
 */
bool max30102_read_interrupt_status(uint8_t *int_status_1, uint8_t *int_status_2) {
    if (!max30102_read_register(MAX30102_INT_STATUS_1, int_status_1)) {
        return false;
    }
    
    if (!max30102_read_register(MAX30102_INT_STATUS_2, int_status_2)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Configure the FIFO settings
 * @param sample_avg Sample averaging (0=1, 1=2, 2=4, 3=8, 4=16, 5=32)
 * @param fifo_rollover_en FIFO rollover enable
 * @param fifo_almost_full_samples FIFO almost full value (0-15)
 * @return true if successful, false otherwise
 */
bool max30102_configure_fifo(uint8_t sample_avg, bool fifo_rollover_en, uint8_t fifo_almost_full_samples) {
    uint8_t fifo_config = 0;
    
    // Set sample averaging bits (bits 7:5)
    fifo_config |= (sample_avg & 0x07) << 5;
    
    // Set FIFO rollover bit (bit 4)
    if (fifo_rollover_en) {
        fifo_config |= (1 << 4);
    }
    
    // Set FIFO almost full value (bits 3:0)
    fifo_config |= (fifo_almost_full_samples & 0x0F);
    
    // Write FIFO configuration
    if (!max30102_write_register(MAX30102_FIFO_CONFIG, fifo_config)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Read FIFO pointers
 * @param write_ptr Pointer to store write pointer value
 * @param read_ptr Pointer to store read pointer value
 * @param overflow_ctr Pointer to store overflow counter
 * @return true if successful, false otherwise
 */
bool max30102_read_fifo_ptrs(uint8_t *write_ptr, uint8_t *read_ptr, uint8_t *overflow_ctr) {
    if (!max30102_read_register(MAX30102_FIFO_WR_PTR, write_ptr)) {
        return false;
    }
    
    if (!max30102_read_register(MAX30102_FIFO_RD_PTR, read_ptr)) {
        return false;
    }
    
    if (!max30102_read_register(MAX30102_FIFO_OVF_CNT, overflow_ctr)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Read samples from FIFO
 * @param samples Pointer to array to store samples
 * @param count Number of samples to read
 * @return Number of samples read
 */
uint8_t max30102_read_fifo_samples(max30102_fifo_sample_t *samples, uint8_t count) {
    uint8_t bytes_per_sample;
    
    // Each sample is 3 bytes per LED
    bytes_per_sample = 6;  // 3 bytes for RED + 3 bytes for IR
    
    // Temporary buffer for raw data
    uint8_t buffer[bytes_per_sample * count];
    
    // Read data from FIFO
    if (!max30102_read_registers(MAX30102_FIFO_DATA, buffer, bytes_per_sample * count)) {
        return 0;
    }
    
    // Extract samples
    for (uint8_t i = 0; i < count; i++) {
        uint8_t *sample_ptr = buffer + (i * bytes_per_sample);
        
        // Extract RED sample (first 3 bytes)
        samples[i].red = max30102_extract_red_sample(sample_ptr, current_pulse_width);
        
        // Extract IR sample (next 3 bytes)
        samples[i].ir = max30102_extract_ir_sample(sample_ptr + 3, current_pulse_width);
    }
    
    return count;
}

/**
 * @brief Read die temperature
 * @param temperature_c Pointer to store temperature in degrees Celsius
 * @return true if successful, false otherwise
 */
bool max30102_read_temperature(float *temperature_c) {
    uint8_t temp_int, temp_frac;
    
    // Trigger temperature conversion
    if (!max30102_write_register(MAX30102_TEMP_CONFIG, 0x01)) {
        return false;
    }
    
    // Wait for temperature data to be ready
    uint8_t int_status_2;
    uint8_t retry = 10;
    
    do {
        if (!max30102_read_register(MAX30102_INT_STATUS_2, &int_status_2)) {
            return false;
        }
        
        if (int_status_2 & MAX30102_INT_DIE_TEMP_RDY) {
            break;
        }
        
        // Simple delay
        for (uint16_t i = 0; i < 1000; i++) {
            asm("nop");
        }
        
        retry--;
    } while (retry > 0);
    
    if (retry == 0) {
        return false;  // Timeout waiting for temperature
    }
    
    // Read temperature values
    if (!max30102_read_register(MAX30102_TEMP_INT, &temp_int)) {
        return false;
    }
    
    if (!max30102_read_register(MAX30102_TEMP_FRAC, &temp_frac)) {
        return false;
    }
    
    // Calculate temperature (integer part + fractional part)
    // Fractional part resolution is 0.0625°C
    *temperature_c = (float)temp_int + ((float)temp_frac * 0.0625);
    
    return true;
}

/**
 * @brief Read part ID
 * @param part_id Pointer to store part ID
 * @return true if successful, false otherwise
 */
bool max30102_read_part_id(uint8_t *part_id) {
    return max30102_read_register(MAX30102_PART_ID, part_id);
}

/**
 * @brief Read revision ID
 * @param rev_id Pointer to store revision ID
 * @return true if successful, false otherwise
 */
bool max30102_read_revision_id(uint8_t *rev_id) {
    return max30102_read_register(MAX30102_REV_ID, rev_id);
}

/**
 * @brief Process samples to calculate heart rate and SpO2
 * @param samples Array of samples
 * @param count Number of samples
 * @param result Pointer to store the results
 * @return true if calculation successful, false otherwise
 */
/**
 * @brief Process samples to calculate heart rate and SpO2
 * @param samples Array of samples
 * @param count Number of samples
 * @param result Pointer to store the results
 * @return true if calculation successful, false otherwise
 */
/**
 * @brief Process samples to calculate heart rate and SpO2
 * @param samples Array of samples
 * @param count Number of samples
 * @param result Pointer to store the results
 * @return true if calculation successful, false otherwise
 */
/**
 * @brief Process samples to calculate heart rate and SpO2
 * @param samples Array of samples
 * @param count Number of samples
 * @param result Pointer to store the results
 * @return true if calculation successful, false otherwise
 */
bool max30102_calculate_hr_spo2(max30102_fifo_sample_t *samples, uint8_t count, max30102_result_t *result) {
    if (count == 0 || samples == NULL || result == NULL) {
        return false;
    }
    
    // DC tracking and buffer for signal processing
    static int32_t dc_filtered_ir[128]; // Buffer for DC-removed signal
    static uint8_t buffer_pos = 0;      // Position in circular buffer
    static uint8_t peaks_detected = 0;  // Number of peaks detected
    static uint32_t last_peak_time = 0; // Time of last peak (in samples)
    static uint32_t peak_intervals[8];  // Store last 8 peak-to-peak intervals
    static uint8_t peak_interval_idx = 0;
    
    // Variables for signal analysis
    uint32_t ir_min = 0xFFFFFFFF;
    uint32_t ir_max = 0;
    uint32_t ir_avg = 0;
    
    // Variables for SpO2 calculation
    uint32_t red_min = 0xFFFFFFFF;
    uint32_t red_max = 0;
    uint32_t red_avg = 0;
    
    // First pass - find min, max and avg values
    for (uint8_t i = 0; i < count; i++) {
        ir_avg += samples[i].ir;
        red_avg += samples[i].red;
        
        if (samples[i].ir > ir_max) ir_max = samples[i].ir;
        if (samples[i].ir < ir_min) ir_min = samples[i].ir;
        if (samples[i].red > red_max) red_max = samples[i].red;
        if (samples[i].red < red_min) red_min = samples[i].red;
    }
    
    // Calculate averages
    ir_avg /= count;
    red_avg /= count;
    
    // Check if valid signal is present (finger on sensor)
    bool finger_present = false;
    
    // If IR average is significant and there's some variation, consider finger present
    if (ir_avg > 5000 && (ir_max - ir_min) > (ir_avg * 0.01)) {
        finger_present = true;
    }
    
    // If finger not present, invalid HR and maybe valid SpO2
    if (!finger_present) {
        result->hr_valid = false;
        
        // Calculate SpO2 even without valid HR if signal levels are reasonable
        if (red_avg > 1000 && ir_avg > 1000) {
            float red_ac = (float)(red_max - red_min);
            float ir_ac = (float)(ir_max - ir_min);
            float ratio = (red_ac * ir_avg) / (ir_ac * red_avg);
            
            // Empirical formula for SpO2 calculation
            float spo2 = 110.0f - 25.0f * ratio;
            
            // Clamp to physiological range
            if (spo2 > 100.0f) spo2 = 100.0f;
            if (spo2 < 0.0f) spo2 = 0.0f;
            
            result->spo2 = (int32_t)spo2;
            result->spo2_valid = (spo2 >= 70.0f && spo2 <= 100.0f);
        } else {
            result->spo2_valid = false;
        }
        
        return true;
    }
    
    // Second pass - process signal for heart rate detection
    // Apply DC removal and store filtered values
    for (uint8_t i = 0; i < count; i++) {
        // DC removal (high-pass filter)
        int32_t filtered = (int32_t)samples[i].ir - (int32_t)ir_avg;
        
        // Store in circular buffer
        dc_filtered_ir[buffer_pos] = filtered;
        buffer_pos = (buffer_pos + 1) % 128;
    }
    
    // Calculate heart rate from peak detection
    // Look for peaks in the most recent data
    int32_t peak_threshold = (ir_max - ir_min) / 10; // 10% of peak-to-peak as threshold
    if (peak_threshold < 10) peak_threshold = 10;    // Minimum threshold
    
    uint32_t current_sample = 0;
    
    // Process full buffer for peak detection
    for (uint8_t i = 0; i < 124; i++) {
        uint8_t idx = (buffer_pos + i) % 128;
        uint8_t prev_idx = (idx + 127) % 128;
        uint8_t next_idx = (idx + 1) % 128;
        
        // Check if this point is a peak (higher than neighbors and above threshold)
        if (dc_filtered_ir[idx] > peak_threshold && 
            dc_filtered_ir[idx] > dc_filtered_ir[prev_idx] && 
            dc_filtered_ir[idx] > dc_filtered_ir[next_idx]) {
            
            // Found a peak, calculate interval if not first peak
            if (last_peak_time > 0) {
                uint32_t interval = current_sample - last_peak_time;
                
                // Only consider reasonable intervals (corresponding to 40-220 BPM at 100Hz)
                // 100 Hz * 60 sec/min / 220 BPM = 27 samples minimum
                // 100 Hz * 60 sec/min / 40 BPM = 150 samples maximum
                if (interval >= 27 && interval <= 150) {
                    // Store interval
                    peak_intervals[peak_interval_idx] = interval;
                    peak_interval_idx = (peak_interval_idx + 1) % 8;
                    peaks_detected++;
                }
            }
            
            last_peak_time = current_sample;
        }
        
        current_sample++;
    }
    
    // Calculate heart rate if enough peaks detected
    if (peaks_detected >= 3) {
        // Average the intervals
        uint32_t sum = 0;
        uint8_t valid_intervals = 0;
        
        for (uint8_t i = 0; i < 8; i++) {
            if (peak_intervals[i] > 0) {
                sum += peak_intervals[i];
                valid_intervals++;
            }
        }
        
        if (valid_intervals > 0) {
            float avg_interval = (float)sum / valid_intervals;
            
            // Convert to BPM (assuming 100Hz sample rate)
            int32_t heart_rate = (int32_t)((60.0f * 100.0f) / avg_interval);
            
            // Validate heart rate is physiologically reasonable
            if (heart_rate >= 40 && heart_rate <= 220) {
                result->heart_rate = heart_rate / 2;
                result->hr_valid = true;
            } else {
                result->hr_valid = false;
            }
        } else {
            result->hr_valid = false;
        }
    } else {
        result->hr_valid = false;
    }
    
    // Calculate SpO2
    float red_ac = (float)(red_max - red_min);
    float ir_ac = (float)(ir_max - ir_min);
    
    if (red_ac > 0 && ir_ac > 0 && red_avg > 0 && ir_avg > 0) {
        float ratio = (red_ac * ir_avg) / (ir_ac * red_avg);
        
        // SpO2 empirical formula
        float spo2 = 110.0f - 25.0f * ratio;
        
        // Clamp to valid range
        if (spo2 > 100.0f) spo2 = 100.0f;
        if (spo2 < 0.0f) spo2 = 0.0f;
        
        result->spo2 = (int32_t)spo2;
        result->spo2_valid = (spo2 >= 70.0f && spo2 <= 100.0f);
    } else {
        result->spo2_valid = false;
    }
    
    return true;
}

/**
 * @brief Shutdown the sensor
 * @param shutdown true to enter shutdown mode, false to exit
 * @return true if successful, false otherwise
 */
bool max30102_shutdown(bool shutdown) {
    uint8_t mode_config;
    
    // Read current mode configuration
    if (!max30102_read_register(MAX30102_MODE_CONFIG, &mode_config)) {
        return false;
    }
    
    // Set or clear shutdown bit
    if (shutdown) {
        mode_config |= MAX30102_MODE_SHDN;
    } else {
        mode_config &= ~MAX30102_MODE_SHDN;
    }
    
    // Write updated mode configuration
    if (!max30102_write_register(MAX30102_MODE_CONFIG, mode_config)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Set up interrupt handling
 * @return true if successful, false otherwise
 */
bool max30102_setup_interrupt(void) {
    // This function should be customized based on hardware setup
    // It needs to set up the external interrupt pin (INT) connected to PD2
    
    // Example for ATMEGA328PB with INT connected to PD2 (INT0)
    // Enable external interrupt INT0 on falling edge
    EICRA = (1 << ISC01);  // Falling edge
    EIMSK |= (1 << INT0);  // Enable INT0
    
    return true;
}

/**
 * @brief Process new data when interrupt occurs
 * @param result Pointer to store the results
 * @return true if new data processed, false otherwise
 */
bool max30102_process_interrupt(max30102_result_t *result) {
    uint8_t int_status_1, int_status_2;
    
    // Read and clear interrupt status
    if (!max30102_read_interrupt_status(&int_status_1, &int_status_2)) {
        return false;
    }
    
    // Check if FIFO almost full interrupt occurred
    if (int_status_1 & MAX30102_INT_A_FULL) {
        uint8_t read_ptr, write_ptr, overflow;
        
        // Read FIFO pointers
        if (!max30102_read_fifo_ptrs(&write_ptr, &read_ptr, &overflow)) {
            return false;
        }
        
        // Calculate number of samples available
        uint8_t samples_available;
        if (write_ptr >= read_ptr) {
            samples_available = write_ptr - read_ptr;
        } else {
            samples_available = 32 - read_ptr + write_ptr;  // FIFO is 32 samples deep
        }
        
        // Cap at a reasonable number
        if (samples_available > 16) {
            samples_available = 16;
        }
        
        // Read samples from FIFO
        max30102_fifo_sample_t samples[16];
        uint8_t samples_read = max30102_read_fifo_samples(samples, samples_available);
        
        if (samples_read > 0) {
            // Process samples
            return max30102_calculate_hr_spo2(samples, samples_read, result);
        }
    }
    
    return false;
}

/**
 * @brief Write to a register on the MAX30102
 * @param reg_addr Register address
 * @param data Data to write
 * @return true if successful, false otherwise
 */
static bool max30102_write_register(uint8_t reg_addr, uint8_t data) {
    return i2c_write_register(MAX30102_I2C_ADDR, reg_addr, data);
}

/**
 * @brief Read from a register on the MAX30102
 * @param reg_addr Register address
 * @param data Pointer to store read data
 * @return true if successful, false otherwise
 */
static bool max30102_read_register(uint8_t reg_addr, uint8_t *data) {
    return i2c_read_register(MAX30102_I2C_ADDR, reg_addr, data);
}

/**
 * @brief Read multiple registers from the MAX30102
 * @param reg_addr Starting register address
 * @param data Pointer to buffer to store read data
 * @param len Number of bytes to read
 * @return true if successful, false otherwise
 */
static bool max30102_read_registers(uint8_t reg_addr, uint8_t *data, uint8_t len) {
    return i2c_read_registers(MAX30102_I2C_ADDR, reg_addr, data, len);
}

/**
 * @brief Clear the FIFO pointers
 * @return true if successful, false otherwise
 */
static bool max30102_clear_fifo(void) {
    // Reset FIFO pointers to 0
    if (!max30102_write_register(MAX30102_FIFO_WR_PTR, 0)) {
        return false;
    }
    
    if (!max30102_write_register(MAX30102_FIFO_RD_PTR, 0)) {
        return false;
    }
    
    if (!max30102_write_register(MAX30102_FIFO_OVF_CNT, 0)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Extract red LED sample from raw buffer
 * @param buffer Pointer to raw sample data
 * @param pulse_width Pulse width setting (determines sample resolution)
 * @return Extracted sample value
 */
static uint32_t max30102_extract_red_sample(uint8_t *buffer, uint8_t pulse_width) {
    uint32_t sample = ((uint32_t)buffer[0] << 16) | ((uint32_t)buffer[1] << 8) | buffer[2];
    
    // Mask unused bits based on pulse width
    switch (pulse_width) {
        case MAX30102_PULSE_WIDTH_69_US:
            return sample & 0x7FFF;  // 15-bit
        case MAX30102_PULSE_WIDTH_118_US:
            return sample & 0xFFFF;  // 16-bit
        case MAX30102_PULSE_WIDTH_215_US:
            return sample & 0x1FFFF; // 17-bit
        case MAX30102_PULSE_WIDTH_411_US:
            return sample & 0x3FFFF; // 18-bit
        default:
            return sample;
    }
}

/**
 * @brief Extract IR LED sample from raw buffer
 * @param buffer Pointer to raw sample data
 * @param pulse_width Pulse width setting (determines sample resolution)
 * @return Extracted sample value
 */
static uint32_t max30102_extract_ir_sample(uint8_t *buffer, uint8_t pulse_width) {
    // Same extraction logic as red sample
    return max30102_extract_red_sample(buffer, pulse_width);
}