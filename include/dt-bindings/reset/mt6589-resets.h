/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 Matthias Brugger
 * Author: Matthias Brugger <matthias.bgg@kernel.org>
 */

#ifndef _DT_BINDINGS_RESET_CONTROLLER_MT6589
#define _DT_BINDINGS_RESET_CONTROLLER_MT6589

/* INFRACFG resets */
#define MT6589_INFRA_EMI_REG_RST        0
#define MT6589_INFRA_DRAMC0_A0_RST      1
#define MT6589_INFRA_CCIF0_RST          2
#define MT6589_INFRA_APCIRQ_EINT_RST    3
#define MT6589_INFRA_APXGPT_RST         4
#define MT6589_INFRA_SCPSYS_RST         5
#define MT6589_INFRA_CCIF1_RST          6
#define MT6589_INFRA_PMIC_WRAP_RST      7
#define MT6589_INFRA_KP_RST             8
#define MT6589_INFRA_EMI_RST            32
#define MT6589_INFRA_DRAMC0_RST         34
#define MT6589_INFRA_SMI_RST            35
#define MT6589_INFRA_M4U_RST            36

/*  PERICFG resets */
#define MT6589_PERI_UART0_SW_RST        0
#define MT6589_PERI_UART1_SW_RST        1
#define MT6589_PERI_UART2_SW_RST        2
#define MT6589_PERI_UART3_SW_RST        3
#define MT6589_PERI_IRDA_SW_RST         4
#define MT6589_PERI_PTP_SW_RST          5
#define MT6589_PERI_AP_HIF_SW_RST       6
#define MT6589_PERI_MD_HIF_SW_RST       8
#define MT6589_PERI_NLI_SW_RST          9
#define MT6589_PERI_AUXADC_SW_RST       10
#define MT6589_PERI_DMA_SW_RST          11
#define MT6589_PERI_NFI_SW_RST          14
#define MT6589_PERI_PWM_SW_RST          15
#define MT6589_PERI_THERM_SW_RST        16
#define MT6589_PERI_MSDC0_SW_RST        17
#define MT6589_PERI_MSDC1_SW_RST        18
#define MT6589_PERI_MSDC2_SW_RST        19
#define MT6589_PERI_MSDC3_SW_RST        20
#define MT6589_PERI_I2C0_SW_RST         22
#define MT6589_PERI_I2C1_SW_RST         23
#define MT6589_PERI_I2C2_SW_RST         24
#define MT6589_PERI_I2C3_SW_RST         25
#define MT6589_PERI_I2C4_SW_RST         26
#define MT6589_PERI_I2C5_SW_RST         27
#define MT6589_PERI_I2C6_SW_RST         28
#define MT6589_PERI_USB_SW_RST          29
#define MT6589_PERI_SPI1_SW_RST         33

#endif  /* _DT_BINDINGS_RESET_CONTROLLER_MT6589 */
