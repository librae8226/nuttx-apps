/****************************************************************************
 *
 *   Copyright (C) 2012 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/boardctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/analog/adc.h>

/*
 * ADC Hardware Registers
 */
#define STM32_ADC1_BASE            0x40012400     /* 0x40012400 - 0x400127ff: ADC1 */
#define STM32_ADC_SR_OFFSET        0x0000  /* ADC status register (32-bit) */
#define STM32_ADC_CR1_OFFSET       0x0004  /* ADC control register 1 (32-bit) */
#define STM32_ADC_CR2_OFFSET       0x0008  /* ADC control register 2 (32-bit) */
#define STM32_ADC_SMPR1_OFFSET     0x000c  /* ADC sample time register 1 (32-bit) */
#define STM32_ADC_SMPR2_OFFSET     0x0010  /* ADC sample time register 2 (32-bit) */
#define STM32_ADC_SMPR3_OFFSET     0x0014  /* ADC sample time register 3 (32-bit) */
#define STM32_ADC_JOFR1_OFFSET     0x0014  /* ADC injected channel data offset register 1 (32-bit) */
#define STM32_ADC_JOFR2_OFFSET     0x0018  /* ADC injected channel data offset register 2 (32-bit) */
#define STM32_ADC_JOFR3_OFFSET     0x001c  /* ADC injected channel data offset register 3 (32-bit) */
#define STM32_ADC_JOFR4_OFFSET     0x0020  /* ADC injected channel data offset register 4 (32-bit) */
#define STM32_ADC_HTR_OFFSET       0x0024  /* ADC watchdog high threshold register (32-bit) */
#define STM32_ADC_LTR_OFFSET       0x0028  /* ADC watchdog low threshold register (32-bit) */
#define STM32_ADC_SQR1_OFFSET      0x002c  /* ADC regular sequence register 1 (32-bit) */
#define STM32_ADC_SQR2_OFFSET      0x0030  /* ADC regular sequence register 2 (32-bit) */
#define STM32_ADC_SQR3_OFFSET      0x0034  /* ADC regular sequence register 3 (32-bit) */
#define STM32_ADC_JSQR_OFFSET      0x0038  /* ADC injected sequence register (32-bit) */
#define STM32_ADC_JDR1_OFFSET      0x003c  /* ADC injected data register 1 (32-bit) */
#define STM32_ADC_JDR2_OFFSET      0x0040  /* ADC injected data register 1 (32-bit) */
#define STM32_ADC_JDR3_OFFSET      0x0044  /* ADC injected data register 1 (32-bit) */
#define STM32_ADC_JDR4_OFFSET      0x0048  /* ADC injected data register 1 (32-bit) */
#define STM32_ADC_DR_OFFSET        0x004c  /* ADC regular data register (32-bit) */

#define ADC_SR_EOC                 (1 << 1)  /* Bit 1 : End of conversion */

#define ADC_CR2_ADON               (1 << 0)  /* Bit 0: A/D Converter ON / OFF */
#define ADC_CR2_CAL                (1 << 2)  /* Bit 2: A/D Calibration */
#define ADC_CR2_RSTCAL             (1 << 3)  /* Bit 3: Reset Calibration */
#define ADC_CR2_TSVREFE            (1 << 23) /* Bit 23: Temperature Sensor and VREFINT Enable */

/*
 * Macros for Registers
 */
#define REG(_reg)	(*(volatile uint32_t *)(STM32_ADC1_BASE + _reg))

#define rSR		REG(STM32_ADC_SR_OFFSET)
#define rCR1		REG(STM32_ADC_CR1_OFFSET)
#define rCR2		REG(STM32_ADC_CR2_OFFSET)
#define rSMPR1		REG(STM32_ADC_SMPR1_OFFSET)
#define rSMPR2		REG(STM32_ADC_SMPR2_OFFSET)
#define rJOFR1		REG(STM32_ADC_JOFR1_OFFSET)
#define rJOFR2		REG(STM32_ADC_JOFR2_OFFSET)
#define rJOFR3		REG(STM32_ADC_JOFR3_OFFSET)
#define rJOFR4		REG(STM32_ADC_JOFR4_OFFSET)
#define rHTR		REG(STM32_ADC_HTR_OFFSET)
#define rLTR		REG(STM32_ADC_LTR_OFFSET)
#define rSQR1		REG(STM32_ADC_SQR1_OFFSET)
#define rSQR2		REG(STM32_ADC_SQR2_OFFSET)
#define rSQR3		REG(STM32_ADC_SQR3_OFFSET)
#define rJSQR		REG(STM32_ADC_JSQR_OFFSET)
#define rJDR1		REG(STM32_ADC_JDR1_OFFSET)
#define rJDR2		REG(STM32_ADC_JDR2_OFFSET)
#define rJDR3		REG(STM32_ADC_JDR3_OFFSET)
#define rJDR4		REG(STM32_ADC_JDR4_OFFSET)
#define rDR		REG(STM32_ADC_DR_OFFSET)

/*
 * Functions
 */
int adc_init(void)
{
	/* put the ADC into power-down mode */
	rCR2 &= ~ADC_CR2_ADON;
	usleep(10);

	/* bring the ADC out of power-down mode */
	rCR2 |= ADC_CR2_ADON;
	usleep(10);

	/* do calibration if supported */
#ifdef ADC_CR2_CAL
	rCR2 |= ADC_CR2_RSTCAL;
	usleep(1);

	if (rCR2 & ADC_CR2_RSTCAL)
		return -1;

	rCR2 |= ADC_CR2_CAL;
	usleep(100);

	if (rCR2 & ADC_CR2_CAL)
		return -1;
#endif

	/*
	 * Configure sampling time.
	 *
	 * For electrical protection reasons, we want to be able to have
	 * 10K in series with ADC inputs that leave the board. At 12MHz this
	 * means we need 28.5 cycles of sampling time (per table 43 in the
	 * datasheet).
	 */
	rSMPR1 = 0b00000000011011011011011011011011;
	rSMPR2 = 0b00011011011011011011011011011011;

	rCR2 |=	ADC_CR2_TSVREFE;		/* enable the temperature sensor / Vrefint channel */

	/* configure for a single-channel sequence */
	rSQR1 = 0;
	rSQR2 = 0;
	rSQR3 = 0;	/* will be updated with the channel at conversion time */

	return 0;
}

uint16_t adc_measure(unsigned int channel)
{
	int cnt = 0;
	uint16_t  result = 0xffff;

	/* clear any previous EOC */
	rSR = 0;
	(void)rDR;

	/* run a single conversion right now - should take about 60 cycles (a few microseconds) max */
	rSQR3 = channel;
	rCR2 |= ADC_CR2_ADON;

	while (!(rSR & ADC_SR_EOC)) {
		usleep(100);
		cnt++;
		if (cnt > 1000) {
			printf("%s, adc ch%d timeout\n", __func__, channel);
			return 0xffff;
		}
	}

	/* read the result and clear EOC */
	result = rDR;
	rSR = 0;

	return result;
}
