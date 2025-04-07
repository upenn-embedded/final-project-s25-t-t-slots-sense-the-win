/* 
 * File:   imu.h
 *  Supported IMU(s): LSM6DSO
 *  Note: only init(), getAll(), and checkNewData() have been implemented
 * 
 * Author: James Steeman
 *
 * Created on February 22, 2025, 2:20 AM
 */

#include <stdint.h>

#ifndef IMU_H
#define	IMU_H

void IMU_init(uint8_t addr);

void IMU_getAll();

void IMU_getXAcc();

void IMU_getYAcc();

void IMU_getZAcc();

void IMU_getXGyro();

void IMU_getYGyro();

void IMU_getZGyro();

void IMU_getTemp();

int IMU_checkNewData();

#endif	/* LSM6DS0_H */

