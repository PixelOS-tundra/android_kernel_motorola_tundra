/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, 2020 The Linux Foundation. All rights reserved. */

#ifndef _DT_BINDINGS_CLK_QCOM_LAHAINA_RPMH_H
#define _DT_BINDINGS_CLK_QCOM_LAHAINA_RPMH_H

/*
 * RPMh clock indices of the lahaina vendor DT. These are the msm-5.4
 * qcom,rpmh.h values, which differ from the 6.1 ones from LN_BB_CLK1 on
 * (e.g. 22 is IPA here but RF_CLK5_A in 6.1). The lahaina provider is
 * indexed by these so the stock 5.4 DT and the in-tree DT agree.
 */
#define LAHAINA_RPMH_CXO_CLK			0
#define LAHAINA_RPMH_CXO_CLK_A			1
#define LAHAINA_RPMH_LN_BB_CLK1			2
#define LAHAINA_RPMH_LN_BB_CLK1_A		3
#define LAHAINA_RPMH_LN_BB_CLK2			4
#define LAHAINA_RPMH_LN_BB_CLK2_A		5
#define LAHAINA_RPMH_LN_BB_CLK3			6
#define LAHAINA_RPMH_LN_BB_CLK3_A		7
#define LAHAINA_RPMH_RF_CLK1			8
#define LAHAINA_RPMH_RF_CLK1_A			9
#define LAHAINA_RPMH_RF_CLK2			10
#define LAHAINA_RPMH_RF_CLK2_A			11
#define LAHAINA_RPMH_RF_CLK3			12
#define LAHAINA_RPMH_RF_CLK3_A			13
#define LAHAINA_RPMH_RF_CLK4			14
#define LAHAINA_RPMH_RF_CLK4_A			15
#define LAHAINA_RPMH_RF_CLK5			16
#define LAHAINA_RPMH_RF_CLK5_A			17
#define LAHAINA_RPMH_RF_CLKD3			18
#define LAHAINA_RPMH_RF_CLKD3_A			19
#define LAHAINA_RPMH_RF_CLKD4			20
#define LAHAINA_RPMH_RF_CLKD4_A			21
#define LAHAINA_RPMH_IPA_CLK			22
#define LAHAINA_RPMH_PKA_CLK			23
#define LAHAINA_RPMH_HWKM_CLK			24
#define LAHAINA_RPMH_QPIC_CLK			25
#define LAHAINA_RPMH_DIV_CLK1			26
#define LAHAINA_RPMH_DIV_CLK1_A			27

#endif
