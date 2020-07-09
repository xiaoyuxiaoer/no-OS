/***************************************************************************//**
 *   @file   ad77681.c
 *   @brief  Implementation of AD7768-1 Driver.
 *   @author SPopa (stefan.popa@analog.com)
********************************************************************************
 * Copyright 2017(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/
#include "stdio.h"
#include "stdlib.h"
#include "stdbool.h"
#include <string.h>
#include "ad77681.h"
#include "error.h"

/******************************************************************************/
/************************** Functions Implementation **************************/
/******************************************************************************/
/**
 * Compute CRC8 checksum.
 * @param data - The data buffer.
 * @param data_size - The size of the data buffer.
 * @param init_val - CRC initial value.
 * @return CRC8 checksum.
 */
uint8_t ad77681_compute_crc8(uint8_t *data,
			     uint8_t data_size,
			     uint8_t init_val)
{
	uint8_t i;
	uint8_t crc = init_val;

	while (data_size) {
		for (i = 0x80; i != 0; i >>= 1) {
			if (((crc & 0x80) != 0) != ((*data & i) != 0)) {
				crc <<= 1;
				crc ^= AD77681_CRC8_POLY;
			} else
				crc <<= 1;
		}
		data++;
		data_size--;
	}
	return crc;
}

/**
 * Compute XOR checksum.
 * @param data - The data buffer.
 * @param data_size - The size of the data buffer.
 * @param init_val - CRC initial value.
 * @return XOR checksum.
 */
uint8_t ad77681_compute_xor(uint8_t *data,
			    uint8_t data_size,
			    uint8_t init_val)
{
	uint8_t crc = init_val;
	uint8_t buf[3];
	uint8_t i;

	for (i = 0; i < data_size; i++) {
		buf[i] = *data;
		crc ^= buf[i];
		data++;
	}
	return crc;
}

/**
 * Read from device.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param reg_data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_spi_reg_read(struct ad77681_dev *dev,
			     uint8_t reg_addr,
			     uint8_t *reg_data)
{
	int32_t ret;
	uint8_t crc;
	uint8_t buf[3], crc_buf[2];
	uint8_t buf_len = (dev->crc_sel == AD77681_NO_CRC) ? 2 : 3;

	buf[0] = AD77681_REG_READ(reg_addr);
	buf[1] = 0x00;

	ret = spi_write_and_read(dev->spi_desc, buf, buf_len);
	if (ret < 0)
		return ret;

	/* XOR or CRC checksum for read transactions */
	if (dev->crc_sel != AD77681_NO_CRC) {
		crc_buf[0] = AD77681_REG_READ(reg_addr);
		crc_buf[1] = buf[1];

		if (dev->crc_sel == AD77681_XOR)
			/* INITIAL_CRC is 0, when ADC is not in continuous-read mode */
			crc = ad77681_compute_xor(crc_buf, 2, INITIAL_CRC);
		else if(dev->crc_sel == AD77681_CRC)
			/* INITIAL_CRC is 0, when ADC is not in continuous-read mode */
			crc = ad77681_compute_crc8(crc_buf, 2, INITIAL_CRC);

		/* In buf[2] is CRC from the ADC */
		if (crc != buf[2])
			ret = FAILURE;
	}

	memcpy(reg_data, buf, ARRAY_SIZE(buf));

	return ret;
}

/**
 * Write to device.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param reg_data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_spi_reg_write(struct ad77681_dev *dev,
			      uint8_t reg_addr,
			      uint8_t reg_data)
{
	uint8_t buf[3];
	/* Buffer length in case of checksum usage */
	uint8_t buf_len = (dev->crc_sel == AD77681_NO_CRC) ? 2 : 3;

	buf[0] = AD77681_REG_WRITE(reg_addr);
	buf[1] = reg_data;

	/* CRC only for read transactions, CRC and XOR for write transactions*/
	if (dev->crc_sel != AD77681_NO_CRC)
		buf[2] = ad77681_compute_crc8(buf, 2, INITIAL_CRC);

	return spi_write_and_read(dev->spi_desc, buf, buf_len);
}

/**
 * SPI read from device using a mask.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param mask - The mask.
 * @param data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_spi_read_mask(struct ad77681_dev *dev,
			      uint8_t reg_addr,
			      uint8_t mask,
			      uint8_t *data)
{
	uint8_t reg_data[3];
	int32_t ret;

	ret = ad77681_spi_reg_read(dev, reg_addr, reg_data);
	*data = (reg_data[1] & mask);

	return ret;
}

/**
 * SPI write to device using a mask.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param mask - The mask.
 * @param data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_spi_write_mask(struct ad77681_dev *dev,
			       uint8_t reg_addr,
			       uint8_t mask,
			       uint8_t data)
{
	uint8_t reg_data[3];
	int32_t ret;

	ret = ad77681_spi_reg_read(dev, reg_addr, reg_data);
	reg_data[1] &= ~mask;
	reg_data[1] |= data;
	ret |= ad77681_spi_reg_write(dev, reg_addr, reg_data[1]);

	return ret;
}

/**
 * Helper function to get the number of rx bytes
 * @param dev - The device structure.
 * @return rx_buf_len - the number of rx bytes
 */
uint8_t ad77681_get_rx_buf_len(struct ad77681_dev *dev)
{
	uint8_t rx_buf_len = 0;
	uint8_t data_len = 0;
	uint8_t crc = 0;
	uint8_t status_bit = 0;

	data_len = 3;
	crc = (dev->crc_sel == AD77681_NO_CRC) ? 0 : 1; // 1 byte for crc
	status_bit = dev->status_bit; // one byte for status

	rx_buf_len = data_len + crc + status_bit;

	return rx_buf_len;
}

/**
 * Helper function to get the number of SPI 16bit frames for INTERRUPT ADC DATA READ
 * @param dev - The device structure.
 * @return frame_16bit - the number of 16 bit SPI frames
 */
uint8_t ad77681_get_frame_16bit(struct ad77681_dev *dev)
{
	/* number of 8bit frames */
	uint8_t frame_bytes, frame_16bit;
	if (dev->conv_len == AD77681_CONV_24BIT)
		frame_bytes = 3;
	else
		frame_bytes = 2;
	if (dev->crc_sel != AD77681_NO_CRC)
		frame_bytes++;
	if (dev->status_bit)
		frame_bytes++;

	/* Conversion from number of 8bit frames to number of 16bit frames */
	if (frame_bytes %2)
		frame_16bit = (frame_bytes / 2) + 1;
	else
		frame_16bit = frame_bytes / 2;

	dev->data_frame_16bit = frame_16bit;

	return frame_16bit;
}

/**
 * Read conversion result from device.
 * @param dev - The device structure.
 * @param adc_data - The conversion result data
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_spi_read_adc_data(struct ad77681_dev *dev,
				  uint8_t *adc_data)
{
	uint8_t crc_calc_buf[4], buf[6], crc, frames_8byte = 0;
	int32_t ret;

	if (dev->conv_len == AD77681_CONV_24BIT)
		frames_8byte += 3;
	else
		frames_8byte += 2;

	if (dev->crc_sel != AD77681_NO_CRC)
		frames_8byte++;

	if (dev->status_bit)
		frames_8byte++;

	buf[0] = AD77681_REG_READ(AD77681_REG_ADC_DATA);
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;	/* added 2 more array places for max data length read */
	buf[5] = 0x00;	/* register address + 3 bytes of data (24bit format) + CRC + Status bit */


	ret = spi_write_and_read(dev->spi_desc, buf, frames_8byte + 1);
	if (ret < 0)
		return ret;

	if (dev->crc_sel == AD77681_CRC) {
		memcpy(crc_calc_buf, buf, ARRAY_SIZE(buf));
		crc = ad77681_compute_crc8(crc_calc_buf, 4, INITIAL_CRC);
		if (crc != buf[3]) {
			printf("%s: CRC Error.\n", __func__);
			ret = FAILURE;
		}
	}

	/* Fill the adc_data buffer */
	memcpy(adc_data, buf, ARRAY_SIZE(buf));

	return ret;
}

/**
 * Set the power consumption mode of the ADC core.
 * @param dev - The device structure.
 * @param mode - The power mode.
 * 					Accepted values: AD77681_ECO
 *									 AD77681_MEDIAN
 *									 AD77681_FAST
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_set_power_mode(struct ad77681_dev *dev,
			       enum ad77681_power_mode mode)
{
	int32_t ret = 0;

	ret |= ad77681_spi_write_mask(dev,
				      AD77681_REG_POWER_CLOCK,
				      AD77681_POWER_CLK_PWRMODE_MSK,
				      AD77681_POWER_CLK_PWRMODE(mode));

	return ret;
}

/**
 * Set the MCLK divider.
 * @param dev - The device structure.
 * @param clk_div - The MCLK divider.
 * 					Accepted values: AD77681_MCLK_DIV_16
 *									 AD77681_MCLK_DIV_8
 *									 AD77681_MCLK_DIV_4
 *									 AD77681_MCLK_DIV_2
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_set_mclk_div(struct ad77681_dev *dev,
			     enum ad77681_mclk_div clk_div)
{
	ad77681_spi_write_mask(dev,
			       AD77681_REG_POWER_CLOCK,
			       AD77681_POWER_CLK_MCLK_DIV_MSK,
			       AD77681_POWER_CLK_MCLK_DIV(clk_div));

	dev->mclk_div = clk_div;

	return 0;
}

/**
 * Conversion mode and source select
 * @param dev - The device structure.
 * @param conv_mode - Sets the conversion mode of the ADC
 * 					  Accepted values: AD77681_CONV_CONTINUOUS
 *									   AD77681_CONV_ONE_SHOT
 *									   AD77681_CONV_SINGLE
 *									   AD77681_CONV_PERIODIC
 * @param diag_mux_sel - Selects which signal to route through diagnostic mux
 * 					  Accepted values: AD77681_TEMP_SENSOR
 *									   AD77681_AIN_SHORT
 *									   AD77681_POSITIVE_FS
 *									   AD77681_NEGATIVE_FS
 * @param conv_diag_sel - Select the input for conversion as AIN or diagnostic mux
 * 					  Accepted values: true
 *									   false
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_set_conv_mode(struct ad77681_dev *dev,
			      enum ad77681_conv_mode conv_mode,
			      enum ad77681_conv_diag_mux diag_mux_sel,
			      bool conv_diag_sel)
{
	ad77681_spi_write_mask(dev,
			       AD77681_REG_CONVERSION,
			       AD77681_CONVERSION_MODE_MSK,
			       AD77681_CONVERSION_MODE(conv_mode));

	ad77681_spi_write_mask(dev,
			       AD77681_REG_CONVERSION,
			       AD77681_CONVERSION_DIAG_MUX_MSK,
			       AD77681_CONVERSION_DIAG_MUX_SEL(diag_mux_sel));

	ad77681_spi_write_mask(dev,
			       AD77681_REG_CONVERSION,
			       AD77681_CONVERSION_DIAG_SEL_MSK,
			       AD77681_CONVERSION_DIAG_SEL(conv_diag_sel));

	dev->conv_mode = conv_mode;
	dev->diag_mux_sel = diag_mux_sel;
	dev->conv_diag_sel = conv_diag_sel;

	return 0;
}

/**
 * Set the Conversion Result Output Length.
 * @param dev - The device structure.
 * @param conv_len - The MCLK divider.
 * 					Accepted values: AD77681_CONV_24BIT
 *									 AD77681_CONV_16BIT
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_set_convlen(struct ad77681_dev *dev,
			    enum ad77681_conv_len conv_len)
{
	int32_t ret;

	ret = ad77681_spi_write_mask(dev,
				     AD77681_REG_INTERFACE_FORMAT,
				     AD77681_INTERFACE_CONVLEN_MSK,
				     AD77681_INTERFACE_CONVLEN(conv_len));

	if (ret == SUCCESS) {
		dev->conv_len = conv_len;
		ad77681_get_frame_16bit(dev);
	}

	return ret;
}

/**
 * Activates CRC on all SPI transactions and
 * Selects CRC method as XOR or 8-bit polynomial
 * @param dev - The device structure.
 * @param crc_sel - The CRC type.
 * 					Accepted values: AD77681_CRC
 *									 AD77681_XOR
 *									 AD77681_NO_CRC
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_set_crc_sel(struct ad77681_dev *dev,
			    enum ad77681_crc_sel crc_sel)
{
	int32_t ret;

	if (crc_sel == AD77681_NO_CRC) {
		ret = ad77681_spi_write_mask(dev,
					     AD77681_REG_INTERFACE_FORMAT,
					     AD77681_INTERFACE_CRC_EN_MSK,
					     AD77681_INTERFACE_CRC_EN(0));
	} else {
		ret = ad77681_spi_write_mask(dev,
					     AD77681_REG_INTERFACE_FORMAT,
					     AD77681_INTERFACE_CRC_EN_MSK,
					     AD77681_INTERFACE_CRC_EN(1));

		ret |= ad77681_spi_write_mask(dev,
					      AD77681_REG_INTERFACE_FORMAT,
					      AD77681_INTERFACE_CRC_TYPE_MSK,
					      AD77681_INTERFACE_CRC_TYPE(crc_sel));
	}

	if (ret == SUCCESS) {
		dev->crc_sel = crc_sel;
		ad77681_get_frame_16bit(dev);
	}

	return ret;
}

/**
 * Enables Status bits output
 * @param dev - The device structure.
 * @param status_bit - enable or disable status bit
 * 					Accepted values: true
 *									 false
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_set_status_bit(struct ad77681_dev *dev,
			       bool status_bit)
{
	int32_t ret;

	// Set status bit
	ret = 	ad77681_spi_write_mask(dev,
				       AD77681_REG_INTERFACE_FORMAT,
				       AD77681_INTERFACE_STATUS_EN_MSK,
				       AD77681_INTERFACE_STATUS_EN(status_bit));

	if (ret == SUCCESS) {
		dev->status_bit = status_bit;
		ad77681_get_frame_16bit(dev);
	}

	return ret;
}

/**
 * Device reset over SPI.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_soft_reset(struct ad77681_dev *dev)
{
	int32_t ret = 0;

	// Two writes are required to initialize the reset
	ret |= 	ad77681_spi_write_mask(dev,
				       AD77681_REG_SYNC_RESET,
				       AD77681_SYNC_RST_SPI_RESET_MSK,
				       AD77681_SYNC_RST_SPI_RESET(0x3));

	ret |= 	ad77681_spi_write_mask(dev,
				       AD77681_REG_SYNC_RESET,
				       AD77681_SYNC_RST_SPI_RESET_MSK,
				       AD77681_SYNC_RST_SPI_RESET(0x2));

	return ret;
}

/**
 * Initialize the device.
 * @param device - The device structure.
 * @param init_param - The structure that contains the device initial
 * 					   parameters.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad77681_setup(struct ad77681_dev **device,
		      struct ad77681_init_param init_param)
{
	struct ad77681_dev *dev;
	int32_t ret = 0;

	dev = (struct ad77681_dev *)malloc(sizeof(*dev));
	if (!dev) {
		return -1;
	}

	dev->power_mode = init_param.power_mode;
	dev->mclk_div = init_param.mclk_div;
	dev->conv_diag_sel = init_param.conv_diag_sel;
	dev->conv_mode = init_param.conv_mode;
	dev->diag_mux_sel = init_param.diag_mux_sel;
	dev->conv_len = init_param.conv_len;
	dev->crc_sel = init_param.crc_sel;
	dev->status_bit = init_param.status_bit;

	ret = spi_init(&dev->spi_desc, &init_param.spi_eng_dev_init);
	if (ret < 0)
		return ret;

	ret |= ad77681_soft_reset(dev);
	ret |= ad77681_set_power_mode(dev, dev->power_mode);
	ret |= ad77681_set_mclk_div(dev, dev->mclk_div);
	ret |= ad77681_set_conv_mode(dev,
				     dev->conv_mode,
				     dev->diag_mux_sel,
				     dev->conv_diag_sel);
	ret |= ad77681_set_convlen(dev, dev->conv_len);
	ret |= ad77681_set_status_bit(dev, dev->status_bit);
	ret |= ad77681_set_crc_sel(dev, dev->crc_sel);

	*device = dev;

	if (!ret)
		printf("ad77681 successfully initialized\n");

	return ret;
}