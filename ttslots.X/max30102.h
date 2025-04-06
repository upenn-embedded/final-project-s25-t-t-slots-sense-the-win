#ifndef MAX30102_H
#define MAX30102_H

#include <stdint.h>

// MAX30102 I2C Address
#define MAX30102_I2C_ADDR 0x57

// MAX30102 Register Addresses
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
#define MAX30102_LED1_PA         0x0C
#define MAX30102_LED2_PA         0x0D
#define MAX30102_PILOT_PA        0x10
#define MAX30102_MULTI_LED_CTRL1 0x11
#define MAX30102_MULTI_LED_CTRL2 0x12
#define MAX30102_TEMP_INT        0x1F
#define MAX30102_TEMP_FRAC       0x20
#define MAX30102_TEMP_CONFIG     0x21
#define MAX30102_PROX_INT_THRESH 0x30
#define MAX30102_REV_ID          0xFE
#define MAX30102_PART_ID         0xFF

// MAX30102 Mode Values
#define MAX30102_MODE_HRONLY     0x02 // Heart Rate only mode
#define MAX30102_MODE_SPO2_HR    0x03 // SPO2 and Heart Rate mode
#define MAX30102_MODE_MULTI_LED  0x07 // Multi-LED mode

// Interrupt Status Flags
#define MAX30102_INT_A_FULL      (1 << 7)
#define MAX30102_INT_DATA_RDY    (1 << 6)
#define MAX30102_INT_ALC_OVF     (1 << 5)
#define MAX30102_INT_PROX_INT    (1 << 4)
#define MAX30102_INT_TEMP_RDY    (1 << 1)
#define MAX30102_INT_PWR_RDY     (1 << 0)

// Function Prototypes
void MAX30102_init(void);
void MAX30102_readFIFO(uint32_t *pun_red_led, uint32_t *pun_ir_led);
uint8_t MAX30102_available(void);
void MAX30102_setup(uint8_t powerLevel, uint8_t sampleAverage, uint8_t mode);
uint16_t MAX30102_getHeartRate(void);
void MAX30102_shutdown(void);
void MAX30102_wakeup(void);
void MAX30102_softReset(void);
uint8_t MAX30102_getRevisionID(void);
uint8_t MAX30102_readPartID(void);
void MAX30102_enableAFULL(void);
void MAX30102_disableAFULL(void);
void MAX30102_enableDATARDY(void);
void MAX30102_disableDATARDY(void);
void MAX30102_setFIFOAverage(uint8_t samples);
void MAX30102_setFIFOAlmostFull(uint8_t samples);

#endif // MAX30102_H