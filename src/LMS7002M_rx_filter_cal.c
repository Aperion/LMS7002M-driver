///
/// \file LMS7002M_rx_filter_cal.c
///
/// Rx filter calibration functions for the LMS7002M C driver.
///
/// \copyright
/// Copyright (c) 2016-2016 Fairwaves, Inc.
/// Copyright (c) 2016-2016 Rice University
/// SPDX-License-Identifier: Apache-2.0
/// http://www.apache.org/licenses/LICENSE-2.0
///

#include "LMS7002M_impl.h"
#include "LMS7002M_filter_cal.h"
#include <LMS7002M/LMS7002M_logger.h>
#include <LMS7002M/LMS7002M_time.h>
#include <string.h> //memcpy

/***********************************************************************
 * Re-tune the RX LO based on the bandwidth
 **********************************************************************/
static int setup_rx_cal_tone(LMS7002M_t *self, const LMS7002M_chan_t channel, const double bw)
{
    int status = 0;
    LMS7002M_set_mac_ch(self, channel);
    const double sxr_freq = self->sxt_freq-bw;
    double sxr_freq_actual = 0;
    status = LMS7002M_set_lo_freq(self, LMS_RX, self->sxr_fref, sxr_freq, &sxr_freq_actual);
    LMS7002M_set_mac_ch(self, channel);
    if (status != 0)
    {
        LMS7_logf(LMS7_ERROR, "LMS7002M_set_lo_freq(LMS_RX, %f MHz)", sxr_freq/1e6);
        goto done;
    }
    double rxtsp_rate = self->cgen_freq/4;
    const double rx_nco_freq = (self->sxt_freq-sxr_freq_actual)-1e6;
    LMS7002M_rxtsp_set_freq(self, channel, rx_nco_freq/rxtsp_rate);

    done:
    return status;
}

/***********************************************************************
 * Prepare for RX filter self-calibration
 **********************************************************************/
static int rx_cal_init(LMS7002M_t *self, const LMS7002M_chan_t channel)
{
    int status = 0;
    LMS7002M_set_mac_ch(self, channel);
    const int g_tia_rfe_user = LMS7002M_regs(self)->reg_0x0113_g_tia_rfe;

    //--- rfe ---
    set_addrs_to_default(self, channel, 0x010C, 0x0114);
    LMS7002M_regs(self)->reg_0x010d_sel_path_rfe = 2;
    LMS7002M_regs(self)->reg_0x0113_g_rxloopb_rfe = 8;
    LMS7002M_regs(self)->reg_0x010c_pd_rloopb_2_rfe = 0;
    LMS7002M_regs(self)->reg_0x010d_en_inshsw_lb2_rfe = 0;
    LMS7002M_regs(self)->reg_0x010c_pd_mxlobuf_rfe = 0;
    LMS7002M_regs(self)->reg_0x010c_pd_qgen_rfe = 0;
    LMS7002M_regs(self)->reg_0x010f_ict_tiamain_rfe = 2;
    LMS7002M_regs(self)->reg_0x010f_ict_tiaout_rfe = 2;
    LMS7002M_regs(self)->reg_0x0114_rfb_tia_rfe = 16;
    LMS7002M_regs(self)->reg_0x0113_g_tia_rfe = g_tia_rfe_user;
    LMS7002M_regs_spi_write(self, 0x0113);
    LMS7002M_regs_spi_write(self, 0x0114);
    LMS7002M_regs_spi_write(self, 0x010c);
    LMS7002M_regs_spi_write(self, 0x010d);
    LMS7002M_regs_spi_write(self, 0x010f);

    //--- rbb ---
    set_addrs_to_default(self, channel, 0x0115, 0x011B);
    LMS7002M_regs(self)->reg_0x0119_ict_pga_out_rbb = 20;
    LMS7002M_regs(self)->reg_0x0119_ict_pga_in_rbb = 20;
    LMS7002M_regs(self)->reg_0x011a_c_ctl_pga_rbb = 3;
    LMS7002M_regs_spi_write(self, 0x0119);
    LMS7002M_regs_spi_write(self, 0x011a);

    //--- trf ---
    set_addrs_to_default(self, channel, 0x0100, 0x0104);
    LMS7002M_regs(self)->reg_0x0101_l_loopb_txpad_trf = 0;
    LMS7002M_regs(self)->reg_0x0101_en_loopb_txpad_trf = 1;
    LMS7002M_regs(self)->reg_0x0103_sel_band1_trf = 0;
    LMS7002M_regs(self)->reg_0x0103_sel_band2_trf = 1;
    LMS7002M_regs_spi_write(self, 0x0100);
    LMS7002M_regs_spi_write(self, 0x0101);
    LMS7002M_regs_spi_write(self, 0x0103);

    //--- tbb ---
    set_addrs_to_default(self, channel, 0x0105, 0x010B);
    LMS7002M_regs(self)->reg_0x0108_cg_iamp_tbb = 1;
    LMS7002M_regs(self)->reg_0x0108_ict_iamp_frp_tbb = 1;
    LMS7002M_regs(self)->reg_0x0108_ict_iamp_gg_frp_tbb = 6;
    LMS7002M_regs_spi_write(self, 0x0108);

    //--- rfe and trf nextrx -- must write to chA ---//
    LMS7002M_set_mac_ch(self, LMS_CHA);
    LMS7002M_regs(self)->reg_0x010d_en_nextrx_rfe = (channel == LMS_CHA)?0:1;
    LMS7002M_regs(self)->reg_0x0100_en_nexttx_trf = (channel == LMS_CHA)?0:1;
    LMS7002M_regs_spi_write(self, 0x010d);
    LMS7002M_regs_spi_write(self, 0x0100);
    LMS7002M_set_mac_ch(self, channel);

    //--- afe ---
    LMS7002M_afe_enable(self, LMS_RX, channel, true);
    LMS7002M_afe_enable(self, LMS_TX, channel, true);
    LMS7002M_set_mac_ch(self, channel);

    //--- sxr ---
    const double sxr_freq = 499.95e6;
    double sxr_freq_actual = 0.0;
    status = LMS7002M_set_lo_freq(self, LMS_RX, self->sxr_fref, sxr_freq, &sxr_freq_actual);
    LMS7002M_set_mac_ch(self, channel);
    if (status != 0)
    {
        LMS7_logf(LMS7_ERROR, "LMS7002M_set_lo_freq(LMS_RX, %f MHz)", sxr_freq/1e6);
        goto done;
    }

    //--- sxt ---
    const double sxt_freq = 500e6;
    double sxt_freq_actual = 0.0;
    status = LMS7002M_set_lo_freq(self, LMS_TX, self->sxt_fref, sxt_freq, &sxt_freq_actual);
    LMS7002M_set_mac_ch(self, channel);
    if (status != 0)
    {
        LMS7_logf(LMS7_ERROR, "LMS7002M_set_lo_freq(LMS_TX, %f MHz)", sxt_freq/1e6);
        goto done;
    }

    //--- TxTSP ---
    set_addrs_to_default(self, channel, 0x0200, 0x020c);
    LMS7002M_regs(self)->reg_0x0200_tsgmode = 1;
    LMS7002M_regs(self)->reg_0x0200_insel = 1;
    LMS7002M_regs(self)->reg_0x0208_cmix_byp = 1;
    LMS7002M_regs(self)->reg_0x0208_gfir3_byp = 1;
    LMS7002M_regs(self)->reg_0x0208_gfir2_byp = 1;
    LMS7002M_regs(self)->reg_0x0208_gfir1_byp = 1;
    LMS7002M_regs_spi_write(self, 0x0200);
    LMS7002M_regs_spi_write(self, 0x0208);
    LMS7002M_txtsp_tsg_const(self, channel, 0x7fff, 0x8000);
    LMS7002M_txtsp_set_freq(self, channel, 0.0);

    //--- RxTSP ---
    set_addrs_to_default(self, channel, 0x0400, 0x040f);
    LMS7002M_regs(self)->reg_0x040a_agc_mode = 1;
    LMS7002M_regs(self)->reg_0x040c_gfir3_byp = 1;
    LMS7002M_regs(self)->reg_0x040c_gfir2_byp = 1;
    LMS7002M_regs(self)->reg_0x040c_gfir1_byp = 1;
    LMS7002M_regs(self)->reg_0x040a_agc_avg = 7;
    LMS7002M_regs(self)->reg_0x040c_cmix_gain = 1;
    LMS7002M_regs_spi_write(self, 0x040a);
    LMS7002M_regs_spi_write(self, 0x040c);
    double rxtsp_rate = self->cgen_freq/4;
    double rx_nco_freq = (sxt_freq_actual-sxr_freq_actual)-1e6;
    LMS7002M_rxtsp_set_freq(self, channel, rx_nco_freq/rxtsp_rate);

    done:
    return status;
}

/***********************************************************************
 * Perform RFE TIA filter calibration
 **********************************************************************/
static int rx_cal_tia_rfe(LMS7002M_t *self, const LMS7002M_chan_t channel, const double bw)
{
    int status = 0;
    LMS7002M_set_mac_ch(self, channel);
    const int g_tia_rfe_user = LMS7002M_regs(self)->reg_0x0113_g_tia_rfe;

    if (bw < 0.5e6 || bw > 60e6)
    {
        LMS7_logf(LMS7_ERROR, "TIA bandwidth not in range[0.5 to 60 MHz]");
        status = -1;
        goto done;
    }

    //--- cfb_tia_rfe, ccomp_tia_rfe ---//
    int cfb_tia_rfe = 0;
    int ccomp_tia_rfe = 0;
    if (g_tia_rfe_user == 3 || g_tia_rfe_user == 2)
    {
        cfb_tia_rfe = (int)(1680e6/bw - 10);
        ccomp_tia_rfe = cfb_tia_rfe/100;
    }
    else if (g_tia_rfe_user == 1)
    {
        cfb_tia_rfe = (int)(5400e6/bw - 10);
        ccomp_tia_rfe = (int)(cfb_tia_rfe/100 + 1);
    }
    else
    {
        LMS7_logf(LMS7_ERROR, "g_tia_rfe must be [1, 2, or 3], got %d", g_tia_rfe_user);
        status = -1;
    }
    if (ccomp_tia_rfe > 15) ccomp_tia_rfe = 15;
    LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe = cfb_tia_rfe;
    LMS7002M_regs(self)->reg_0x0112_ccomp_tia_rfe = ccomp_tia_rfe;
    LMS7002M_regs_spi_write(self, 0x0112);

    //--- rcomp_tia_rfe ---//
    int rcomp_tia_rfe = (int)(15-2*cfb_tia_rfe/100);
    if (rcomp_tia_rfe < 0) rcomp_tia_rfe = 0;
    LMS7002M_regs(self)->reg_0x0114_rcomp_tia_rfe = rcomp_tia_rfe;
    LMS7002M_regs_spi_write(self, 0x0114);

    //--- rbb path ---//
    LMS7002M_regs(self)->reg_0x0118_input_ctl_pga_rbb = 2;
    LMS7002M_regs(self)->reg_0x0115_pd_lpfl_rbb = 0;
    LMS7002M_regs_spi_write(self, 0x0118);
    LMS7002M_regs_spi_write(self, 0x0115);

    //--- cgen already set prior ---//

    //--- gain selection ---//
    const int rssi_value_50k = cal_gain_selection(self, channel);

    //--- setup calibration tone ---
    status = setup_rx_cal_tone(self, channel, bw);
    if (status != 0) goto done;

    //--- cfb_tia_rf ---//
    uint16_t rssi_value = cal_read_rssi(self, channel);
    const int adjust = (rssi_value < rssi_value_50k*0.7071)?-1:+1;
    while (true)
    {
        LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe += adjust;
        LMS7002M_regs_spi_write(self, 0x0112);
        rssi_value = cal_read_rssi(self, channel);
        if (rssi_value > rssi_value_50k*0.7071) break;
        if (LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe == 0 ||
            LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe == 4095)
        {
            status = -1;
            LMS7_logf(LMS7_ERROR, "failed to cal c_ctl_lpfh_rbb -> %d", LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe);
            goto done;
        }
    }
    LMS7_logf(LMS7_DEBUG, "cfb_tia_rfe = %d", LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe);

    done:
    return status;
}

/***********************************************************************
 * Perform RBB LPFL filter calibration
 **********************************************************************/
static int rx_cal_rbb_lpfl(LMS7002M_t *self, const LMS7002M_chan_t channel, const double bw)
{
    int status = 0;
    LMS7002M_set_mac_ch(self, channel);

    if (bw < 0.5e6 || bw > 20e6)
    {
        LMS7_logf(LMS7_ERROR, "LPFL bandwidth not in range[0.5 to 20 MHz]");
        status = -1;
        goto done;
    }

    //--- c_ctl_lpfl_rbb, rcc_ctl_lpfl_rbb ---//
    LMS7002M_regs(self)->reg_0x0117_c_ctl_lpfl_rbb = (int)(2160e6/bw - 103);
    int rcc_ctl_lpfl_rbb = 0;
    if      (bw > 15e6)  rcc_ctl_lpfl_rbb = 5;
    else if (bw > 10e6)  rcc_ctl_lpfl_rbb = 4;
    else if (bw > 5e6)   rcc_ctl_lpfl_rbb = 3;
    else if (bw > 3e6)   rcc_ctl_lpfl_rbb = 2;
    else if (bw > 1.4e6) rcc_ctl_lpfl_rbb = 1;
    else                 rcc_ctl_lpfl_rbb = 0;
    LMS7002M_regs(self)->reg_0x0117_rcc_ctl_lpfl_rbb = rcc_ctl_lpfl_rbb;
    LMS7002M_regs_spi_write(self, 0x0117);

    //--- tia rfe registers ---//
    LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe = 15;
    LMS7002M_regs(self)->reg_0x0112_ccomp_tia_rfe = 1;
    LMS7002M_regs(self)->reg_0x0114_rcomp_tia_rfe = 15;
    LMS7002M_regs(self)->reg_0x0113_g_tia_rfe = 1;
    LMS7002M_regs_spi_write(self, 0x0112);
    LMS7002M_regs_spi_write(self, 0x0113);
    LMS7002M_regs_spi_write(self, 0x0114);

    //--- cgen already set prior ---//

    //--- gain selection ---//
    const int rssi_value_50k = cal_gain_selection(self, channel);

    //--- setup calibration tone ---
    status = setup_rx_cal_tone(self, channel, bw);
    if (status != 0) goto done;

    //--- c_ctl_lpfl_rbb ---//
    uint16_t rssi_value = cal_read_rssi(self, channel);
    const int adjust = (rssi_value < rssi_value_50k*0.7071)?-1:+1;
    while (true)
    {
        LMS7002M_regs(self)->reg_0x0117_c_ctl_lpfl_rbb += adjust;
        LMS7002M_regs_spi_write(self, 0x0117);
        rssi_value = cal_read_rssi(self, channel);
        if (rssi_value > rssi_value_50k*0.7071) break;
        if (LMS7002M_regs(self)->reg_0x0117_c_ctl_lpfl_rbb == 0 ||
            LMS7002M_regs(self)->reg_0x0117_c_ctl_lpfl_rbb == 2047)
        {
            status = -1;
            LMS7_logf(LMS7_ERROR, "failed to cal c_ctl_lpfl_rbb -> %d", LMS7002M_regs(self)->reg_0x0117_c_ctl_lpfl_rbb);
            goto done;
        }
    }
    LMS7_logf(LMS7_DEBUG, "c_ctl_lpfl_rbb = %d", LMS7002M_regs(self)->reg_0x0117_c_ctl_lpfl_rbb);

    done:
    return status;
}

/***********************************************************************
 * Perform RBB LPFH filter calibration
 **********************************************************************/
static int rx_cal_rbb_lpfh(LMS7002M_t *self, const LMS7002M_chan_t channel, const double bw)
{
    int status = 0;
    LMS7002M_set_mac_ch(self, channel);

    if (bw < 10e6 || bw > 60e6)
    {
        LMS7_logf(LMS7_ERROR, "LPFH bandwidth not in range[0.5 to 60 MHz]");
        status = -1;
        goto done;
    }

    //--- c_ctl_lpfl_rbb, rcc_ctl_lpfl_rbb ---//
    LMS7002M_regs(self)->reg_0x0116_c_ctl_lpfh_rbb = (int)(6000e6/bw - 50);
    int rcc_ctl_lpfh_rbb = (int)(bw/10e6 - 3);
    if (rcc_ctl_lpfh_rbb < 0) rcc_ctl_lpfh_rbb = 0;
    LMS7002M_regs(self)->reg_0x0116_rcc_ctl_lpfh_rbb = rcc_ctl_lpfh_rbb;
    LMS7002M_regs_spi_write(self, 0x0116);

    //--- tia rfe registers and rbb ---//
    LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe = 15;
    LMS7002M_regs(self)->reg_0x0112_ccomp_tia_rfe = 1;
    LMS7002M_regs(self)->reg_0x0114_rcomp_tia_rfe = 15;
    LMS7002M_regs(self)->reg_0x0113_g_tia_rfe = 1;
    LMS7002M_regs(self)->reg_0x0115_pd_lpfh_rbb = 0;
    LMS7002M_regs(self)->reg_0x0115_pd_lpfl_rbb = 1;
    LMS7002M_regs(self)->reg_0x0118_input_ctl_pga_rbb = 1;
    LMS7002M_regs_spi_write(self, 0x0112);
    LMS7002M_regs_spi_write(self, 0x0113);
    LMS7002M_regs_spi_write(self, 0x0114);
    LMS7002M_regs_spi_write(self, 0x0115);
    LMS7002M_regs_spi_write(self, 0x0118);

    //--- cgen already set prior ---//

    //--- gain selection ---//
    const int rssi_value_50k = cal_gain_selection(self, channel);

    //--- setup calibration tone ---
    status = setup_rx_cal_tone(self, channel, bw);
    if (status != 0) goto done;

    //--- c_ctl_lpfh_rbb ---//
    uint16_t rssi_value = cal_read_rssi(self, channel);
    const int adjust = (rssi_value < rssi_value_50k*0.7071)?-1:+1;
    while (true)
    {
        LMS7002M_regs(self)->reg_0x0116_c_ctl_lpfh_rbb += adjust;
        LMS7002M_regs_spi_write(self, 0x0116);
        rssi_value = cal_read_rssi(self, channel);
        if (rssi_value > rssi_value_50k*0.7071) break;
        if (LMS7002M_regs(self)->reg_0x0116_c_ctl_lpfh_rbb == 0 ||
            LMS7002M_regs(self)->reg_0x0116_c_ctl_lpfh_rbb == 255)
        {
            status = -1;
            LMS7_logf(LMS7_ERROR, "failed to cal c_ctl_lpfh_rbb -> %d", LMS7002M_regs(self)->reg_0x0116_c_ctl_lpfh_rbb);
            goto done;
        }
    }
    LMS7_logf(LMS7_DEBUG, "c_ctl_lpfh_rbb = %d", LMS7002M_regs(self)->reg_0x0116_c_ctl_lpfh_rbb);

    done:
    return status;
}

/***********************************************************************
 * Rx calibration dispatcher
 **********************************************************************/
int LMS7002M_rbb_set_filter_bw(LMS7002M_t *self, const LMS7002M_chan_t channel, const double bw, double *bwactual)
{
    LMS7002M_set_mac_ch(self, channel);
    int status = 0;
    const int path = (bw < 20e6)?LMS7002M_RBB_LBF:LMS7002M_RBB_HBF;

    //check for initialized reference frequencies
    if (self->cgen_fref == 0.0)
    {
        LMS7_logf(LMS7_ERROR, "cgen_fref not initialized");
        return -1;
    }
    if (self->sxr_fref == 0.0)
    {
        LMS7_logf(LMS7_ERROR, "sxr_fref not initialized");
        return -1;
    }
    if (self->sxt_fref == 0.0)
    {
        LMS7_logf(LMS7_ERROR, "sxt_fref not initialized");
        return -1;
    }

    ////////////////////////////////////////////////////////////////////
    // Save register map
    ////////////////////////////////////////////////////////////////////
    LMS7002M_regs_t saved_map[2];
    memcpy(saved_map, self->_regs, sizeof(saved_map));

    ////////////////////////////////////////////////////////////////////
    // Clocking configuration
    ////////////////////////////////////////////////////////////////////
    double cgen_freq = bw*20;
    if (cgen_freq < 60e6) cgen_freq = 60e6;
    if (cgen_freq > 640e6) cgen_freq = 640e6;
    while ((int)(cgen_freq/1e6) == (int)(bw/16e6)) cgen_freq -= 10e6;
    double cgen_freq_actual = 0.0;
    status = LMS7002M_set_data_clock(self, self->cgen_fref, cgen_freq, &cgen_freq_actual);
    if (status != 0)
    {
        LMS7_logf(LMS7_ERROR, "LMS7002M_set_data_clock(%f MHz)", cgen_freq/1e6);
        goto done;
    }

    ////////////////////////////////////////////////////////////////////
    // Load initial calibration state
    ////////////////////////////////////////////////////////////////////
    status = rx_cal_init(self, channel);
    if (status != 0)
    {
        LMS7_logf(LMS7_ERROR, "rx_cal_init() failed");
        goto done;
    }

    ////////////////////////////////////////////////////////////////////
    // RFE TIA calibration
    ////////////////////////////////////////////////////////////////////
    status = rx_cal_tia_rfe(self, channel, bw);
    if (status != 0)
    {
        LMS7_logf(LMS7_ERROR, "rx_cal_tia_rfe() failed");
        goto done;
    }

    ////////////////////////////////////////////////////////////////////
    // Initialize calibration again for LPF
    ////////////////////////////////////////////////////////////////////
    status = rx_cal_init(self, channel);
    if (status != 0)
    {
        LMS7_logf(LMS7_ERROR, "rx_cal_init() failed");
        goto done;
    }

    ////////////////////////////////////////////////////////////////////
    // RBB LPF calibration
    ////////////////////////////////////////////////////////////////////
    if (path == LMS7002M_RBB_LBF) status = rx_cal_rbb_lpfl(self, channel, bw);
    if (path == LMS7002M_RBB_HBF) status = rx_cal_rbb_lpfh(self, channel, bw);
    if (status != 0)
    {
        LMS7_logf(LMS7_ERROR, "rx_cal_rbb_lpf() failed");
        goto done;
    }

    done:

    ////////////////////////////////////////////////////////////////////
    // stash tia + rbb calibration results
    ////////////////////////////////////////////////////////////////////
    LMS7002M_set_mac_ch(self, channel);
    const int cfb_tia_rfe = LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe;
    const int ccomp_tia_rfe = LMS7002M_regs(self)->reg_0x0112_ccomp_tia_rfe;
    const int rcomp_tia_rfe = LMS7002M_regs(self)->reg_0x0114_rcomp_tia_rfe;
    const int rcc_ctl_lpfl_rbb = LMS7002M_regs(self)->reg_0x0117_rcc_ctl_lpfl_rbb;
    const int c_ctl_lpfl_rbb = LMS7002M_regs(self)->reg_0x0117_c_ctl_lpfl_rbb;
    const int rcc_ctl_lpfh_rbb = LMS7002M_regs(self)->reg_0x0116_rcc_ctl_lpfh_rbb;
    const int c_ctl_lpfh_rbb = LMS7002M_regs(self)->reg_0x0116_c_ctl_lpfh_rbb;

    ////////////////////////////////////////////////////////////////////
    // restore original register values
    ////////////////////////////////////////////////////////////////////
    memcpy(self->_regs, saved_map, sizeof(saved_map));
    LMS7002M_regs_to_rfic(self);
    LMS7002M_set_mac_ch(self, channel);

    ////////////////////////////////////////////////////////////////////
    // apply tia calibration results
    ////////////////////////////////////////////////////////////////////
    LMS7002M_set_mac_ch(self, channel);
    LMS7002M_regs(self)->reg_0x010f_ict_tiamain_rfe = 2;
    LMS7002M_regs(self)->reg_0x010f_ict_tiaout_rfe = 2;
    LMS7002M_regs(self)->reg_0x0114_rfb_tia_rfe = 16;
    LMS7002M_regs(self)->reg_0x0112_cfb_tia_rfe = cfb_tia_rfe;
    LMS7002M_regs(self)->reg_0x0112_ccomp_tia_rfe = ccomp_tia_rfe;
    LMS7002M_regs(self)->reg_0x0114_rcomp_tia_rfe = rcomp_tia_rfe;
    LMS7002M_regs_spi_write(self, 0x010f);
    LMS7002M_regs_spi_write(self, 0x0114);
    LMS7002M_regs_spi_write(self, 0x0112);

    ////////////////////////////////////////////////////////////////////
    // apply rbb calibration results
    ////////////////////////////////////////////////////////////////////
    LMS7002M_regs(self)->reg_0x0117_rcc_ctl_lpfl_rbb = rcc_ctl_lpfl_rbb;
    LMS7002M_regs(self)->reg_0x0117_c_ctl_lpfl_rbb = c_ctl_lpfl_rbb;
    LMS7002M_regs(self)->reg_0x0116_rcc_ctl_lpfh_rbb = rcc_ctl_lpfh_rbb;
    LMS7002M_regs(self)->reg_0x0116_c_ctl_lpfh_rbb = c_ctl_lpfh_rbb;
    LMS7002M_regs(self)->reg_0x0119_ict_pga_out_rbb = 20;
    LMS7002M_regs(self)->reg_0x0119_ict_pga_in_rbb = 20;
    LMS7002M_regs(self)->reg_0x0116_r_ctl_lpf_rbb = 16;
    LMS7002M_regs(self)->reg_0x011a_c_ctl_pga_rbb = 3;
    LMS7002M_regs_spi_write(self, 0x0117);
    LMS7002M_regs_spi_write(self, 0x0119);
    LMS7002M_regs_spi_write(self, 0x0116);
    LMS7002M_regs_spi_write(self, 0x011a);

    ////////////////////////////////////////////////////////////////////
    // set the filter selection
    ////////////////////////////////////////////////////////////////////
    LMS7002M_rbb_set_path(self, channel, path);

    if (bwactual != NULL) *bwactual = bw;
    return status;
}
