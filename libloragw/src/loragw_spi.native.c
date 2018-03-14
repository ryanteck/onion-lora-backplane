/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
  (C)2013 Semtech-Cycleo

Description:
	Host specific functions to address the LoRa concentrator registers through
	a SPI interface.
	Single-byte read/write and burst read/write.
	Does not handle pagination.
	Could be used with multiple SPI ports in parallel (explicit file descriptor)

License: Revised BSD License, see LICENSE.TXT file include in the project
Maintainer: Sylvain Miermont
*/


/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */

#include <stdint.h>		/* C99 types */
#include <stdio.h>		/* printf fprintf */
#include <stdlib.h>		/* malloc free */
#include <unistd.h>		/* lseek, close */
#include <fcntl.h>		/* open */
#include <string.h>		/* memset */

#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include "loragw_spi.h"
#include "loragw_hal.h"


/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#if DEBUG_SPI == 1
	#define DEBUG_MSG(str)				fprintf(stderr, str)
	#define DEBUG_PRINTF(fmt, args...)	fprintf(stderr,"%s:%d: "fmt, __FUNCTION__, __LINE__, args)
	#define CHECK_NULL(a)				if(a==NULL){fprintf(stderr,"%s:%d: ERROR: NULL POINTER AS ARGUMENT\n", __FUNCTION__, __LINE__);return LGW_SPI_ERROR;}
#else
	#define DEBUG_MSG(str)
	#define DEBUG_PRINTF(fmt, args...)
	#define CHECK_NULL(a)				if(a==NULL){return LGW_SPI_ERROR;}
#endif

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS ---------------------------------------------------- */

#define READ_ACCESS		0x00
#define WRITE_ACCESS	0x80

/* -------------------------------------------------------------------------- */
/* --- PUBLIC FUNCTIONS DEFINITION ------------------------------------------ */

/* SPI initialization and configuration */
int lgw_spi_open(void **spi_target_ptr) {
	int *spi_device = NULL;
	int dev;
	int a=0, b=0;
	int i;

	/* check input variables */
	CHECK_NULL(spi_target_ptr); /* cannot be null, must point on a void pointer (*spi_target_ptr can be null) */

	/* allocate memory for the device descriptor */
	spi_device = malloc(sizeof(int));
	if (spi_device == NULL) {
		DEBUG_MSG("ERROR: MALLOC FAIL\n");
		return LGW_SPI_ERROR;
	}

	/* open SPI device */
	dev = open(SPI_DEV_PATH, O_RDWR);
	if (dev < 0) {
		DEBUG_PRINTF("ERROR: failed to open SPI device %s\n", SPI_DEV_PATH);
		return LGW_SPI_ERROR;
	}

	/* setting SPI mode to 'mode 0' */
	i = SPI_MODE_0;
	a = ioctl(dev, SPI_IOC_WR_MODE, &i);
	b = ioctl(dev, SPI_IOC_RD_MODE, &i);
	if ((a < 0) || (b < 0)) {
		DEBUG_MSG("ERROR: SPI PORT FAIL TO SET IN MODE 0\n");
		close(dev);
		free(spi_device);
		return LGW_SPI_ERROR;
	}

	/* setting SPI max clk (in Hz) */
	i = SPI_SPEED;
	a = ioctl(dev, SPI_IOC_WR_MAX_SPEED_HZ, &i);
	b = ioctl(dev, SPI_IOC_RD_MAX_SPEED_HZ, &i);
	if ((a < 0) || (b < 0)) {
		DEBUG_MSG("ERROR: SPI PORT FAIL TO SET MAX SPEED\n");
		close(dev);
		free(spi_device);
		return LGW_SPI_ERROR;
	}

	/* setting SPI to MSB first */
	i = 0;
	a = ioctl(dev, SPI_IOC_WR_LSB_FIRST, &i);
	b = ioctl(dev, SPI_IOC_RD_LSB_FIRST, &i);
	if ((a < 0) || (b < 0)) {
		DEBUG_MSG("ERROR: SPI PORT FAIL TO SET MSB FIRST\n");
		close(dev);
		free(spi_device);
		return LGW_SPI_ERROR;
	}

	/* setting SPI to 8 bits per word */
	i = 0;
	a = ioctl(dev, SPI_IOC_WR_BITS_PER_WORD, &i);
	b = ioctl(dev, SPI_IOC_RD_BITS_PER_WORD, &i);
	if ((a < 0) || (b < 0)) {
		DEBUG_MSG("ERROR: SPI PORT FAIL TO SET 8 BITS-PER-WORD\n");
		close(dev);
		return LGW_SPI_ERROR;
	}

	*spi_device = dev;
	*spi_target_ptr = (void *)spi_device;
	DEBUG_MSG("Note: SPI port opened and configured ok\n");
	return LGW_SPI_SUCCESS;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* SPI release */
int lgw_spi_close(void *spi_target) {
	int spi_device;
	int a;

	/* check input variables */
	CHECK_NULL(spi_target);

	/* close file & deallocate file descriptor */
	spi_device = *(int *)spi_target; /* must check that spi_target is not null beforehand */
	a = close(spi_device);
	free(spi_target);

	/* determine return code */
	if (a < 0) {
		DEBUG_MSG("ERROR: SPI PORT FAILED TO CLOSE\n");
		return LGW_SPI_ERROR;
	} else {
		DEBUG_MSG("Note: SPI port closed\n");
		return LGW_SPI_SUCCESS;
	}
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Simple write */
int lgw_spi_w(void *spi_target, uint8_t address, uint8_t data) {
	int spi_device;
	uint8_t out_buf[2];
	struct spi_ioc_transfer k;
	int a;

	/* check input variables */
	CHECK_NULL(spi_target);
	if ((address & 0x80) != 0) {
		DEBUG_MSG("WARNING: SPI address > 127\n");
	}

	spi_device = *(int *)spi_target; /* must check that spi_target is not null beforehand */

	/* prepare frame to be sent */
	out_buf[0] = WRITE_ACCESS | (address & 0x7F);
	out_buf[1] = data;

	/* I/O transaction */
	memset(&k, 0, sizeof(k)); /* clear k */
	k.tx_buf = (unsigned long) out_buf;
	k.len = ARRAY_SIZE(out_buf);
	k.speed_hz = SPI_SPEED;
	k.cs_change = SPI_CS_CHANGE;
	k.bits_per_word = 8;
	a = ioctl(spi_device, SPI_IOC_MESSAGE(1), &k);

	/* determine return code */
	if (a != 2) {
		DEBUG_MSG("ERROR: SPI WRITE FAILURE\n");
		return LGW_SPI_ERROR;
	} else {
		DEBUG_MSG("Note: SPI write success\n");
		return LGW_SPI_SUCCESS;
	}
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Simple read */
int lgw_spi_r(void *spi_target, uint8_t address, uint8_t *data) {
	int spi_device;
	uint8_t out_buf[2];
	uint8_t in_buf[ARRAY_SIZE(out_buf)];
#if CFG_SPI_HALF_DUPLEX == 1
	struct spi_ioc_transfer k[2];
#else
	struct spi_ioc_transfer k;
#endif
	int a;

	/* check input variables */
	CHECK_NULL(spi_target);
	if ((address & 0x80) != 0) {
		DEBUG_MSG("WARNING: SPI address > 127\n");
	}
	CHECK_NULL(data);

	spi_device = *(int *)spi_target; /* must check that spi_target is not null beforehand */

	/* prepare frame to be sent */
	out_buf[0] = READ_ACCESS | (address & 0x7F);
	out_buf[1] = 0x00;

	/* I/O transaction */
	memset(&k, 0, sizeof(k)); /* clear k */
#if CFG_SPI_HALF_DUPLEX == 1
	k[0].tx_buf = (unsigned long) out_buf;
	k[1].rx_buf = (unsigned long) in_buf;
	k[0].len = ARRAY_SIZE(out_buf) / 2;
	k[1].len = ARRAY_SIZE(out_buf) / 2;
	k[0].cs_change = 0;
	k[1].cs_change = SPI_CS_CHANGE;
	a = ioctl(spi_device, SPI_IOC_MESSAGE(2), &k);
#else
	k.tx_buf = (unsigned long) out_buf;
	k.rx_buf = (unsigned long) in_buf;
	k.len = ARRAY_SIZE(out_buf);
	k.cs_change = SPI_CS_CHANGE;
	a = ioctl(spi_device, SPI_IOC_MESSAGE(1), &k);
#endif
	/* determine return code */
	if (a != 2) {
		DEBUG_MSG("ERROR: SPI READ FAILURE\n");
		return LGW_SPI_ERROR;
	} else {
		DEBUG_MSG("Note: SPI read success\n");
#if CFG_SPI_HALF_DUPLEX == 1
		*data = in_buf[0];
#else
		*data = in_buf[1];
#endif
		return LGW_SPI_SUCCESS;
	}
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Burst (multiple-byte) write */
int lgw_spi_wb(void *spi_target, uint8_t address, uint8_t *data, uint16_t size) {
	int spi_device;
	uint8_t command;
	struct spi_ioc_transfer k[2];
	int size_to_do, chunk_size, offset;
	int byte_transfered = 0;
	int i;

	/* check input parameters */
	CHECK_NULL(spi_target);
	if ((address & 0x80) != 0) {
		DEBUG_MSG("WARNING: SPI address > 127\n");
	}
	CHECK_NULL(data);
	if (size == 0) {
		DEBUG_MSG("ERROR: BURST OF NULL LENGTH\n");
		return LGW_SPI_ERROR;
	}

	spi_device = *(int *)spi_target; /* must check that spi_target is not null beforehand */

	/* prepare command byte */
	command = WRITE_ACCESS | (address & 0x7F);
	size_to_do = size;

	/* I/O transaction */
	memset(&k, 0, sizeof(k)); /* clear k */
	k[0].tx_buf = (unsigned long) &command;
	k[0].len = 1;
	k[0].cs_change = 0;
	k[1].cs_change = SPI_CS_CHANGE;
	for (i=0; size_to_do > 0; ++i) {
		chunk_size = (size_to_do < LGW_BURST_CHUNK) ? size_to_do : LGW_BURST_CHUNK;
		offset = i * LGW_BURST_CHUNK;
		k[1].tx_buf = (unsigned long)(data + offset);
		k[1].len = chunk_size;
		byte_transfered += (ioctl(spi_device, SPI_IOC_MESSAGE(2), &k) - 1 );
		DEBUG_PRINTF("BURST WRITE: to trans %d # chunk %d # transferred %d \n", size_to_do, chunk_size, byte_transfered);
		size_to_do -= chunk_size; /* subtract the quantity of data already transferred */
	}

	/* determine return code */
	if (byte_transfered != size) {
		DEBUG_MSG("ERROR: SPI BURST WRITE FAILURE\n");
		return LGW_SPI_ERROR;
	} else {
		DEBUG_MSG("Note: SPI burst write success\n");
		return LGW_SPI_SUCCESS;
	}
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Burst (multiple-byte) read */
int lgw_spi_rb(void *spi_target, uint8_t address, uint8_t *data, uint16_t size) {
	int spi_device;
	uint8_t command;
	struct spi_ioc_transfer k[2];
	int size_to_do, chunk_size, offset;
	int byte_transfered = 0;
	int i;

	/* check input parameters */
	CHECK_NULL(spi_target);
	if ((address & 0x80) != 0) {
		DEBUG_MSG("WARNING: SPI address > 127\n");
	}
	CHECK_NULL(data);
	if (size == 0) {
		DEBUG_MSG("ERROR: BURST OF NULL LENGTH\n");
		return LGW_SPI_ERROR;
	}

	spi_device = *(int *)spi_target; /* must check that spi_target is not null beforehand */

	/* prepare command byte */
	command = READ_ACCESS | (address & 0x7F);
	size_to_do = size;

	/* I/O transaction */
	memset(&k, 0, sizeof(k)); /* clear k */
	k[0].tx_buf = (unsigned long) &command;
	k[0].len = 1;
	k[0].cs_change = 0;
	k[1].cs_change = SPI_CS_CHANGE;
	for (i=0; size_to_do > 0; ++i) {
		chunk_size = (size_to_do < LGW_BURST_CHUNK) ? size_to_do : LGW_BURST_CHUNK;
		offset = i * LGW_BURST_CHUNK;
		k[1].rx_buf = (unsigned long)(data + offset);
		k[1].len = chunk_size;
		byte_transfered += (ioctl(spi_device, SPI_IOC_MESSAGE(2), &k) - 1 );
		DEBUG_PRINTF("BURST READ: to trans %d # chunk %d # transferred %d \n", size_to_do, chunk_size, byte_transfered);
		size_to_do -= chunk_size;  /* subtract the quantity of data already transferred */
	}

	/* determine return code */
	if (byte_transfered != size) {
		DEBUG_MSG("ERROR: SPI BURST READ FAILURE\n");
		return LGW_SPI_ERROR;
	} else {
		DEBUG_MSG("Note: SPI burst read success\n");
		return LGW_SPI_SUCCESS;
	}
}

/* --- EOF ------------------------------------------------------------------ */
