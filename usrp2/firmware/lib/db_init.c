/* -*- c++ -*- */
/*
 * Copyright 2008 Free Software Foundation, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <memory_map.h>
#include <i2c.h>
#include <usrp2_i2c_addr.h>
#include <string.h>
#include <stdio.h>
#include <db.h>
#include <db_base.h>
#include <hal_io.h>
#include <nonstdio.h>


struct db_base *tx_dboard;	// the tx daughterboard that's installed
struct db_base *rx_dboard;	// the rx daughterboard that's installed

extern struct db_base db_basic_tx;
extern struct db_base db_basic_rx;
extern struct db_base db_lf_tx;
extern struct db_base db_lf_rx;
extern struct db_base db_rfx_400_tx;
extern struct db_base db_rfx_400_rx;
extern struct db_base db_tvrx1;
extern struct db_base db_tvrx2;
extern struct db_base db_tvrx3;

struct db_base *all_dboards[] = {
  &db_basic_tx,
  &db_basic_rx,
  &db_lf_tx,
  &db_lf_rx,
  &db_rfx_400_tx,
  &db_rfx_400_rx,
  &db_tvrx1,
  &db_tvrx2,
  &db_tvrx3,
  0
};


typedef enum { UDBE_OK, UDBE_NO_EEPROM, UDBE_INVALID_EEPROM } usrp_dbeeprom_status_t;

static usrp_dbeeprom_status_t
read_raw_dboard_eeprom (unsigned char *buf, int i2c_addr)
{
  if (!eeprom_read (i2c_addr, 0, buf, DB_EEPROM_CLEN))
    return UDBE_NO_EEPROM;

  if (buf[DB_EEPROM_MAGIC] != DB_EEPROM_MAGIC_VALUE)
    return UDBE_INVALID_EEPROM;

  int sum = 0;
  unsigned int i;
  for (i = 0; i < DB_EEPROM_CLEN; i++)
    sum += buf[i];

  if ((sum & 0xff) != 0)
    return UDBE_INVALID_EEPROM;

  return UDBE_OK;
}


/*
 * Return DBID, -1 <none> or -2 <invalid eeprom contents>
 */
static int
read_dboard_eeprom(int i2c_addr)
{
  unsigned char buf[DB_EEPROM_CLEN];

  usrp_dbeeprom_status_t s = read_raw_dboard_eeprom (buf, i2c_addr);

  //printf("\nread_raw_dboard_eeprom: %d\n", s);

  switch (s){
  case UDBE_OK:
    return (buf[DB_EEPROM_ID_MSB] << 8) | buf[DB_EEPROM_ID_LSB];

  case UDBE_NO_EEPROM:
  default:
    return -1;

  case UDBE_INVALID_EEPROM:
    return -2;
  }
}


static struct db_base *
lookup_dbid(int dbid)
{
  if (dbid < 0)
    return 0;

  int i;
  for (i = 0; all_dboards[i]; i++)
    if (all_dboards[i]->dbid == dbid)
      return all_dboards[i];

  return 0;
}

static struct db_base *
lookup_dboard(int i2c_addr, struct db_base *default_db, char *msg)
{
  struct db_base *db;
  int dbid = read_dboard_eeprom(i2c_addr);
  printf("%s dbid: 0x%x\n", msg, dbid);

  if (dbid < 0){	// there was some kind of problem.  Treat as Basic Tx
    return default_db;
  }
  else if ((db = lookup_dbid(dbid)) == 0){
    return default_db;
    printf("No daugherboard code for dbid = 0x%x\n", dbid);
  }
  return db;
}

static void
set_atr_regs(int bank, struct db_base *db)
{
  uint32_t	val[4];
  int		shift;
  int		mask;
  int		i;

  val[ATR_IDLE] = db->atr_rxval;
  val[ATR_RX]   = db->atr_rxval;
  val[ATR_TX]   = db->atr_txval;
  val[ATR_FULL] = db->atr_txval;

  if (bank == GPIO_TX_BANK){
    mask = 0xffff0000;
    shift = 16;
  }
  else {
    mask = 0x0000ffff;
    shift = 0;
  }

  for (i = 0; i < 4; i++){
    int t = (atr_regs->v[i] & ~mask) | ((val[i] << shift) & mask);
    //printf("atr_regs[%d] = 0x%x\n", i, t);
    atr_regs->v[i] = t;
  }
}

static void
set_gpio_mode(int bank, struct db_base *db)
{
  int	i;

  hal_gpio_set_ddr(bank, db->output_enables, 0xffff);
  set_atr_regs(bank, db);

  for (i = 0; i < 16; i++){
    if (db->used_pins & (1 << i)){
      // set to either GPIO_SEL_SW or GPIO_SEL_ATR
      hal_gpio_set_sel(bank, i, (db->atr_mask & (1 << i)) ? 'a' : 's');
    }
  }
}

void
db_init(void)
{

  tx_dboard = lookup_dboard(I2C_ADDR_TX_A, &db_basic_tx, "Tx");
  //printf("db_init: tx dbid = 0x%x\n", tx_dboard->dbid);
  set_gpio_mode(GPIO_TX_BANK, tx_dboard);
  tx_dboard->init(tx_dboard);

  rx_dboard = lookup_dboard(I2C_ADDR_RX_A, &db_basic_rx, "Rx");
  //printf("db_init: rx dbid = 0x%x\n", rx_dboard->dbid);
  set_gpio_mode(GPIO_RX_BANK, rx_dboard);
  rx_dboard->init(rx_dboard);
}

/*!
 *  Calculate the frequency to use for setting the digital down converter.
 *
 *  \param[in] target_freq   desired RF frequency (Hz)
 *  \param[in] baseband_freq the RF frequency that corresponds to DC in the IF.
 * 
 *  \param[out] dxc_freq is the value for the ddc
 *  \param[out] inverted is true if we're operating in an inverted Nyquist zone.
*/
void
calc_dxc_freq(u2_fxpt_freq_t target_freq, u2_fxpt_freq_t baseband_freq,
	      u2_fxpt_freq_t *dxc_freq, bool *inverted)
{
  u2_fxpt_freq_t fs = U2_DOUBLE_TO_FXPT_FREQ(100e6);	// converter sample rate
  u2_fxpt_freq_t delta = target_freq - baseband_freq;

#if 0
  printf("calc_dxc_freq\n");
  printf("  fs       = "); print_fxpt_freq(fs); newline();
  printf("  target   = "); print_fxpt_freq(target_freq); newline();
  printf("  baseband = "); print_fxpt_freq(baseband_freq); newline();
  printf("  delta    = "); print_fxpt_freq(delta); newline();
#endif  

  if (delta >= 0){
    while (delta > fs)
      delta -= fs;
    if (delta <= fs/2){		// non-inverted region
      *dxc_freq = -delta;
      *inverted = false;
    }
    else {			// inverted region
      *dxc_freq = delta - fs;
      *inverted = true;
    }
  }
  else {
    while (delta < -fs){
      delta += fs;
      if (delta >= -fs/2){	// non-inverted region
	*dxc_freq = -delta;
	*inverted = false;
      }
      else {			// inverted region
	*dxc_freq = delta + fs;
	*inverted = true;
      }
    }
  }
}


bool
db_tune(struct db_base *db, u2_fxpt_freq_t target_freq, struct tune_result *result)
{
  memset(result, 0, sizeof(*result));
  bool inverted = false;
  u2_fxpt_freq_t dxc_freq;
  u2_fxpt_freq_t actual_dxc_freq;

  // Ask the d'board to tune as closely as it can to target_freq
  bool ok = db->set_freq(db, target_freq, &result->baseband_freq);

  // Calculate the DDC setting that will downconvert the baseband from the
  // daughterboard to our target frequency.
  calc_dxc_freq(target_freq, result->baseband_freq, &dxc_freq, &inverted);

  // If the spectrum is inverted, and the daughterboard doesn't do
  // quadrature downconversion, we can fix the inversion by flipping the
  // sign of the dxc_freq...  (This only happens using the basic_rx board)
  
  if (db->spectrum_inverted)
    inverted = !inverted;

  if (inverted && !db->is_quadrature){
    dxc_freq = -dxc_freq;
    inverted = !inverted;
  }

  if (db->is_tx){
    dxc_freq = -dxc_freq;	// down conversion versus up conversion
    ok &= db_set_duc_freq(dxc_freq, &actual_dxc_freq);
  }
  else {
    ok &= db_set_ddc_freq(dxc_freq, &actual_dxc_freq);
  }

  result->dxc_freq = dxc_freq;
  result->residual_freq = dxc_freq - actual_dxc_freq;
  result->inverted = inverted;
  return ok;
}

static int32_t
compute_freq_control_word(u2_fxpt_freq_t target_freq, u2_fxpt_freq_t *actual_freq)
{
  // If we were using floating point, we'd calculate
  //   master = 100e6;
  //   v = (int) rint(target_freq / master_freq) * pow(2.0, 32.0);

  //printf("compute_freq_control_word\n");
  //printf("  target_freq = "); print_fxpt_freq(target_freq); newline();

  int32_t master_freq = 100000000;	// 100M

  int32_t v = ((target_freq << 12)) / master_freq;
  //printf("  fcw = %d\n", v);

  *actual_freq = (v * (int64_t) master_freq) >> 12;

  //printf("  actual = "); print_fxpt_freq(*actual_freq); newline();

  return v;
}


bool
db_set_ddc_freq(u2_fxpt_freq_t dxc_freq, u2_fxpt_freq_t *actual_dxc_freq)
{
  int32_t v = compute_freq_control_word(dxc_freq, actual_dxc_freq);
  dsp_rx_regs->freq = v;
  return true;
}

bool
db_set_duc_freq(u2_fxpt_freq_t dxc_freq, u2_fxpt_freq_t *actual_dxc_freq)
{
  int32_t v = compute_freq_control_word(dxc_freq, actual_dxc_freq);
  dsp_tx_regs->freq = v;
  return true;
}

bool
db_set_gain(struct db_base *db, u2_fxpt_gain_t gain)
{
  return db->set_gain(db, gain);
}