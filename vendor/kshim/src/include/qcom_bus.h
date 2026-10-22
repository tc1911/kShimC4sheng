/* SPDX-License-Identifier: MIT */
#pragma once
#include <Library/cr_geni.h>
#define kshim_bus_result CrIoResult
#define kshim_bus_io CrIo
#define kshim_qcom_protocol CrGeniProtocol
#define kshim_qcom_bus_config CrGeniConfig
#define kshim_qcom_bus CrGeni
#define kshim_i2c_msg CrI2cMessage
#define kshim_spi_segment CrSpiSegment
#define kshim_bus_default_io CrIoDefault
#define kshim_bus_io_valid CrIoValid
#define kshim_qcom_bus_init CrGeniInit
#define kshim_i2c_transfer CrGeniI2cTransfer
#define kshim_spi_transfer CrGeniSpiTransfer
#define kshim_qcom_bus_quiesce CrGeniQuiesce
#define KSHIM_BUS_AGAIN CR_BUS_AGAIN
#define KSHIM_BUS_BUSY CR_BUS_BUSY
#define KSHIM_BUS_INVALID CR_BUS_INVALID
#define KSHIM_BUS_IO CR_BUS_IO
#define KSHIM_BUS_NACK CR_BUS_NACK
#define KSHIM_BUS_OK CR_BUS_OK
#define KSHIM_BUS_TIMEOUT CR_BUS_TIMEOUT
#define KSHIM_BUS_UNSUPPORTED CR_BUS_UNSUPPORTED
#define KSHIM_GENI_I2C CR_GENI_I2C
#define KSHIM_GENI_SAVED_REGS CR_GENI_SAVED_REGS
#define KSHIM_GENI_SPI CR_GENI_SPI
