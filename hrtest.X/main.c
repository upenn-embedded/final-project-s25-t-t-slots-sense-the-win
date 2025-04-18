/**
 * @file main.c
 * @brief Heart rate and SpO2 monitoring using MAXREFDES117# with ATMEGA328PB
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "uart.h"
#include "i2c.h"
#include "max30102.h"

// Define I2C frequency
#define I2C_FREQUENCY      400000UL  // 400kHz for MAX30102 communication

// Define the number of samples to read in each iteration
#define SAMPLE_COUNT       10

// Global variable for interrupt data
volatile bool new_data_ready = false;

// Sample buffer
max30102_fifo_sample_t samples[SAMPLE_COUNT];

/**
 * @brief Interrupt handler for MAX30102 INT pin (PD3/INT1)
 */
ISR(INT1_vect) {
    new_data_ready = true;
}

/**
 * @brief Initialize all peripherals
 * @return true if initialization successful, false otherwise
 */
bool init_peripherals(void) {
    // Initialize UART
    uart_init();
    printf("UART initialized\r\n");
    
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

/**
 * @brief Print sensor information
 */
void print_sensor_info(void) {
    uint8_t part_id, rev_id;
    float temperature;
    
    // Read and print part ID
    if (max30102_read_part_id(&part_id)) {
        printf("Sensor Part ID: 0x%02X\r\n", part_id);
    } else {
        printf("Could not read part ID\r\n");
    }
    
    // Read and print revision ID
    if (max30102_read_revision_id(&rev_id)) {
        printf("Sensor Revision ID: 0x%02X\r\n", rev_id);
    } else {
        printf("Could not read revision ID\r\n");
    }
    
    // Read and print temperature
    if (max30102_read_temperature(&temperature)) {
        printf("Sensor Temperature: %.2f C\r\n", temperature);
    } else {
        printf("Could not read temperature\r\n");
    }
}

/**
 * @brief Main program entry point
 */
int main(void) {
    // Variables for heart rate and SpO2 calculation
    max30102_result_t result;
    uint8_t sample_count;
    uint8_t write_ptr, read_ptr, overflow;
    uint8_t int_status_1, int_status_2;
    uint32_t loop_count = 0;
    
    // Initialize peripherals
    if (!init_peripherals()) {
        while (1) {
            // Halt on error
            _delay_ms(1000);
        }
    }
    
    // Print sensor information
    print_sensor_info();
    
    // Enable global interrupts
    sei();
    
    // Print header
    printf("\r\nHeart Rate and SpO2 Monitoring:\r\n");
    printf("--------------------------------\r\n");
    printf("Red\tIR\tHR\tHR Valid\tSpO2\tSpO2 Valid\r\n");
    
    // Main loop
    while (1) {
        loop_count++;
        
        // Check for interrupts via interrupt pin (should set new_data_ready)
        if (new_data_ready) {
            // printf("INT triggered!\r\n");
            new_data_ready = false;
        }

        // Check for interrupts via register polling every 20 iterations
        if (loop_count % 20 == 0) {
            // Read interrupt status
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
                        
                        // Print FIFO status
                        // printf("FIFO: W=%u R=%u OVF=%u Count=%u\r\n", write_ptr, read_ptr, overflow, sample_count);
                        
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
                                for (uint8_t i = 0; i < sample_count && i < 1; i++) {
                                    printf("%lu\t%lu\t", samples[i].red, samples[i].ir);
                                }
                                
                                // Print heart rate and validity
                                if (result.hr_valid) {
                                    printf("%ld\tValid\t\t", result.heart_rate);
                                } else {
                                    printf("--\tInvalid\t\t");
                                }
                                
                                // Print SpO2 and validity
                                if (result.spo2_valid) {
                                    printf("%ld%%\tValid\r\n", result.spo2);
                                } else {
                                    printf("--%%\tInvalid\r\n");
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Check interrupt pin state manually every 50 iterations
        if (loop_count % 50 == 0) {
            // Read PD3 pin state
            if (!(PIND & (1 << PD3))) {
                // printf("INT pin is LOW\r\n");
            }
        }
        
        // Every 100 loops, reconfigure interrupts to ensure they're enabled
        if (loop_count % 100 == 0) {
            // Re-enable FIFO almost full interrupt
            max30102_set_interrupt_enables(MAX30102_INT_A_FULL, 0x00);
            // printf("Reconfigured interrupts\r\n");
        }
        
        // Small delay to allow sensor to collect data
        _delay_ms(10);
    }
    
    return 0;  // Never reached
}