// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include "chan.h"
#include "debug.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8922a.h"
#include "rtw8922a_rfk.h"

static void rtw8922a_tssi_cont_en(struct rtw89_dev *rtwdev, bool en,
				  enum rtw89_rf_path path)
{
	static const u32 tssi_trk_man[2] = {R_TSSI_PWR_P0, R_TSSI_PWR_P1};

	if (en)
		rtw89_phy_write32_mask(rtwdev, tssi_trk_man[path], B_TSSI_CONT_EN, 0);
	else
		rtw89_phy_write32_mask(rtwdev, tssi_trk_man[path], B_TSSI_CONT_EN, 1);
}

void rtw8922a_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en, u8 phy_idx)
{
	if (rtwdev->mlo_dbcc_mode == MLO_1_PLUS_1_1RF) {
		if (phy_idx == RTW89_PHY_0)
			rtw8922a_tssi_cont_en(rtwdev, en, RF_PATH_A);
		else
			rtw8922a_tssi_cont_en(rtwdev, en, RF_PATH_B);
	} else {
		rtw8922a_tssi_cont_en(rtwdev, en, RF_PATH_A);
		rtw8922a_tssi_cont_en(rtwdev, en, RF_PATH_B);
	}
}

enum _rf_syn_pow {
	RF_SYN_ON_OFF,
	RF_SYN_OFF_ON,
	RF_SYN_ALLON,
	RF_SYN_ALLOFF,
};

static void rtw8922a_set_syn01_cav(struct rtw89_dev *rtwdev, enum _rf_syn_pow syn)
{
	if (syn == RF_SYN_ALLON) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x2);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x3);

		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN, 0x2);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN, 0x3);
	} else if (syn == RF_SYN_ON_OFF) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x2);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x3);

		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN, 0x0);
	} else if (syn == RF_SYN_OFF_ON) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x0);

		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN, 0x2);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN, 0x3);
	} else if (syn == RF_SYN_ALLOFF) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN, 0x0);
	}
}

static void rtw8922a_set_syn01_cbv(struct rtw89_dev *rtwdev, enum _rf_syn_pow syn)
{
	if (syn == RF_SYN_ALLON) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN_V1, 0xf);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN_V1, 0xf);
	} else if (syn == RF_SYN_ON_OFF) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN_V1, 0xf);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN_V1, 0x0);
	} else if (syn == RF_SYN_OFF_ON) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN_V1, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN_V1, 0xf);
	} else if (syn == RF_SYN_ALLOFF) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN_V1, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN_V1, 0x0);
	}
}

static void rtw8922a_set_syn01(struct rtw89_dev *rtwdev, enum _rf_syn_pow syn)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "SYN config=%d\n", syn);

	if (hal->cv == CHIP_CAV)
		rtw8922a_set_syn01_cav(rtwdev, syn);
	else
		rtw8922a_set_syn01_cbv(rtwdev, syn);
}

static void rtw8922a_chlk_ktbl_sel(struct rtw89_dev *rtwdev, u8 kpath, u8 idx)
{
	u32 tmp;

	if (idx > 2) {
		rtw89_warn(rtwdev, "[DBCC][ERROR]indx is out of limit!! index(%d)", idx);
		return;
	}

	if (kpath & RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL, B_COEF_SEL_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL, B_COEF_SEL_IQC_V1, idx);
		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL, B_COEF_SEL_MDPD_V1, idx);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MODOPT, RR_TXG_SEL, 0x4 | idx);

		tmp = rtw89_phy_read32_mask(rtwdev, R_COEF_SEL, BIT(0));
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_COEF_SEL, BIT(1));
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G5, tmp);
	}

	if (kpath & RF_B) {
		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL_C1, B_COEF_SEL_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL_C1, B_COEF_SEL_IQC_V1, idx);
		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL_C1, B_COEF_SEL_MDPD_V1, idx);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_MODOPT, RR_TXG_SEL, 0x4 | idx);

		tmp = rtw89_phy_read32_mask(rtwdev, R_COEF_SEL_C1, BIT(0));
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT_C1, B_CFIR_LUT_G3, tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_COEF_SEL_C1, BIT(1));
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT_C1, B_CFIR_LUT_G5, tmp);
	}
}

static void rtw8922a_chlk_reload(struct rtw89_dev *rtwdev)
{
	struct rtw89_rfk_mcc_info *rfk_mcc = &rtwdev->rfk_mcc;
	enum rtw89_sub_entity_idx sub_entity_idx;
	const struct rtw89_chan *chan;
	enum rtw89_entity_mode mode;
	u8 s0_tbl, s1_tbl;
	u8 tbl_sel;

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_MCC_PREPARE:
		sub_entity_idx = RTW89_SUB_ENTITY_1;
		tbl_sel = 1;
		break;
	default:
		sub_entity_idx = RTW89_SUB_ENTITY_0;
		tbl_sel = 0;
		break;
	}

	chan = rtw89_chan_get(rtwdev, sub_entity_idx);

	rfk_mcc->ch[tbl_sel] = chan->channel;
	rfk_mcc->band[tbl_sel] = chan->band_type;
	rfk_mcc->bw[tbl_sel] = chan->band_width;
	rfk_mcc->table_idx = tbl_sel;

	s0_tbl = tbl_sel;
	s1_tbl = tbl_sel;

	rtw8922a_chlk_ktbl_sel(rtwdev, RF_A, s0_tbl);
	rtw8922a_chlk_ktbl_sel(rtwdev, RF_B, s1_tbl);
}

static void rtw8922a_rfk_mlo_ctrl(struct rtw89_dev *rtwdev)
{
	enum _rf_syn_pow syn_pow;

	if (!rtwdev->dbcc_en)
		goto set_rfk_reload;

	switch (rtwdev->mlo_dbcc_mode) {
	case MLO_0_PLUS_2_1RF:
		syn_pow = RF_SYN_OFF_ON;
		break;
	case MLO_0_PLUS_2_2RF:
	case MLO_1_PLUS_1_2RF:
	case MLO_2_PLUS_0_1RF:
	case MLO_2_PLUS_0_2RF:
	case MLO_2_PLUS_2_2RF:
	case MLO_DBCC_NOT_SUPPORT:
	default:
		syn_pow = RF_SYN_ON_OFF;
		break;
	case MLO_1_PLUS_1_1RF:
	case DBCC_LEGACY:
		syn_pow = RF_SYN_ALLON;
		break;
	}

	rtw8922a_set_syn01(rtwdev, syn_pow);

set_rfk_reload:
	rtw8922a_chlk_reload(rtwdev);
}

static void rtw8922a_rfk_pll_init(struct rtw89_dev *rtwdev)
{
	int ret;
	u8 tmp;

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_PLL_1, &tmp);
	if (ret)
		return;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_PLL_1, tmp | 0xf8, 0xFF);
	if (ret)
		return;

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_APBT, &tmp);
	if (ret)
		return;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_APBT, tmp & ~0x60, 0xFF);
	if (ret)
		return;

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_XTAL_PLL, &tmp);
	if (ret)
		return;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_PLL, tmp | 0x38, 0xFF);
	if (ret)
		return;
}

void rtw8922a_rfk_hw_init(struct rtw89_dev *rtwdev)
{
	if (rtwdev->dbcc_en)
		rtw8922a_rfk_mlo_ctrl(rtwdev);

	rtw8922a_rfk_pll_init(rtwdev);
}
