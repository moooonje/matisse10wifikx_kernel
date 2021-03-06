/*
 * drivers/video/decon_3475/panels/ea8061s_lcd_ctrl.c
 *
 * Copyright (c) 2015 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <video/mipi_display.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/lcd.h>
#include <linux/backlight.h>

#include "../dsim.h"
#include "../decon.h"
#include "dsim_panel.h"

#ifdef CONFIG_PANEL_AID_DIMMING
#include "aid_dimming.h"
#include "dimming_core.h"
#include "ea8061s_aid_dimming.h"
#endif

#include "ea8061s_param.h"
#define LEVEL_IS_ACL_OFF(brightness)	(brightness == UI_MAX_BRIGHTNESS)

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
#include "mdnie.h"
#include "mdnie_lite_table_j7xe.h"
#endif

struct lcd_info {
	struct lcd_device *ld;
	struct backlight_device *bd;
	unsigned char id[3];
	unsigned char code[5];
	unsigned char tset[8];
	unsigned char elvss_def[8];
	int	temperature;
	unsigned int coordinate[2];
	unsigned char date[7];
	unsigned int state;
	unsigned int auto_brightness;
	unsigned int br_index;
	unsigned int acl_enable;
	unsigned int current_acl;
	unsigned int current_hbm;
	unsigned int current_vint;
	unsigned int siop_enable;
	unsigned char dump_info[3];
	unsigned int weakness_hbm_comp;

	void *dim_data;
	void *dim_info;
	unsigned int *br_tbl;
	unsigned char **hbm_tbl;
	unsigned char **acl_cutoff_tbl;
	unsigned char **acl_opr_tbl;
	struct mutex lock;
};

#ifdef CONFIG_PANEL_AID_DIMMING
static unsigned int get_actual_br_value(struct dsim_device *dsim, int index)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct SmtDimInfo *dimming_info = (struct SmtDimInfo *)lcd->dim_info;

	if (dimming_info == NULL) {
		dsim_err("%s : dimming info is NULL\n", __func__);
		goto get_br_err;
	}

	if (index > MAX_BR_INFO)
		index = MAX_BR_INFO;

	return dimming_info[index].br;

get_br_err:
	return 0;
}
static unsigned char *get_gamma_from_index(struct dsim_device *dsim, int index)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct SmtDimInfo *dimming_info = (struct SmtDimInfo *)lcd->dim_info;

	if (dimming_info == NULL) {
		dsim_err("%s : dimming info is NULL\n", __func__);
		goto get_gamma_err;
	}

	if (index > MAX_BR_INFO)
		index = MAX_BR_INFO;

	return (unsigned char *)dimming_info[index].gamma;

get_gamma_err:
	return NULL;
}

static unsigned char *get_aid_from_index(struct dsim_device *dsim, int index)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct SmtDimInfo *dimming_info = (struct SmtDimInfo *)lcd->dim_info;

	if (dimming_info == NULL) {
		dsim_err("%s : dimming info is NULL\n", __func__);
		goto get_aid_err;
	}

	if (index > MAX_BR_INFO)
		index = MAX_BR_INFO;

	return (u8 *)dimming_info[index].aid;

get_aid_err:
	return NULL;
}

static unsigned char *get_elvss_from_index(struct dsim_device *dsim, int index, int acl)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct SmtDimInfo *dimming_info = (struct SmtDimInfo *)lcd->dim_info;

	if (dimming_info == NULL) {
		dsim_err("%s : dimming info is NULL\n", __func__);
		goto get_elvess_err;
	}

	if (acl)
		return (unsigned char *)dimming_info[index].elvAcl;
	else
		return (unsigned char *)dimming_info[index].elv;

get_elvess_err:
	return NULL;
}

/*====== for ea8061s =====*/
static void dsim_panel_gamma_ctrl(struct dsim_device *dsim)
{
	u8 *gamma = NULL;
	struct lcd_info *lcd = dsim->priv.par;

	gamma = get_gamma_from_index(dsim, lcd->br_index);
	if (gamma == NULL) {
		dsim_err("%s :faied to get gamma\n", __func__);
		return;
	}
	if (dsim_write_hl_data(dsim, gamma, GAMMA_CMD_CNT) < 0)
		dsim_err("%s : failed to write gamma\n", __func__);
}

static void dsim_panel_aid_ctrl(struct dsim_device *dsim)
{
	u8 *aid = NULL;
	struct lcd_info *lcd = dsim->priv.par;

	aid = get_aid_from_index(dsim, lcd->br_index);
	if (aid == NULL) {
		dsim_err("%s : faield to get aid value\n", __func__);
		return;
	}
	if (dsim_write_hl_data(dsim, aid, AID_CMD_CNT) < 0)
		dsim_err("%s : failed to write gamma\n", __func__);
}

static void dsim_panel_set_elvss(struct dsim_device *dsim)
{
	u8 *elvss = NULL;
	u8 elvss_val[ELVSS_READ_SIZE + 1] = { 0xB6, };
	int real_br = 0;
	struct lcd_info *lcd = dsim->priv.par;

	real_br = get_actual_br_value(dsim, lcd->br_index);
	elvss = get_elvss_from_index(dsim, lcd->br_index, lcd->acl_enable);

	if (elvss == NULL) {
		dsim_err("%s : failed to get elvss value\n", __func__);
		return;
	}

	memcpy(elvss_val + 1, lcd->elvss_def, ELVSS_READ_SIZE);

	elvss_val[0] = elvss[0];
	elvss_val[1] = elvss[1];
	elvss_val[2] = elvss[2];

	if (lcd->temperature > 0) {
		elvss_val[8] = elvss[3];
	} else if (lcd->temperature <= 0 && lcd->temperature > -15) {
		elvss_val[8] = elvss[4];
	} else {
		elvss_val[8] = elvss[5];
	}

	if (elvss_val[8] == 0x00) {
		elvss_val[8] = lcd->elvss_def[7];
	}

	if (dsim_write_hl_data(dsim, elvss_val, ARRAY_SIZE(elvss_val)) < 0)
		dsim_err("%s : failed to write elvss\n", __func__);

}

static int dsim_panel_set_acl(struct dsim_device *dsim, int force)
{
	int ret = 0, level = ACL_STATUS_15P;
	struct panel_private *panel = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;
	int brightness = 0;

	if (panel == NULL) {
		dsim_err("%s : panel is NULL\n", __func__);
		goto exit;
	}

	brightness = lcd->bd->props.brightness;

	if (lcd->siop_enable || LEVEL_IS_HBM(lcd->auto_brightness))  /* auto acl or hbm is acl on */
		goto acl_update;

	if (!lcd->acl_enable && LEVEL_IS_ACL_OFF(brightness) && lcd->weakness_hbm_comp == 2)
		level = ACL_STATUS_0P;

acl_update:
	if (force || lcd->current_acl != lcd->acl_cutoff_tbl[level][1]) {
		if (dsim_write_hl_data(dsim, lcd->acl_cutoff_tbl[level], 2) < 0) {
			dsim_err("fail to write acl command.\n");
			ret = -EPERM;
			goto exit;
		}
		lcd->current_acl = lcd->acl_cutoff_tbl[level][1];
		dsim_info("acl: %d, auto_brightness: %d\n", lcd->current_acl, lcd->auto_brightness);
	}
exit:
	if (!ret)
		ret = -EPERM;
	return ret;
}

static int dsim_panel_set_tset(struct dsim_device *dsim, int force)
{
	int ret = 0;
	int tset = 0;
	unsigned char TSET[TSET_LEN] = { TSET_REG, TSET_DEF };
	struct lcd_info *lcd = dsim->priv.par;

	tset = ((lcd->temperature >= 0) ? 0 : BIT(7)) | abs(lcd->temperature);

	if (force || lcd->tset[0] != tset) {
		lcd->tset[0] = TSET[1] = tset;

		if (dsim_write_hl_data(dsim, TSET, TSET_LEN) < 0) {
			dsim_err("fail to write tset command.\n");
			ret = -EPERM;
		}

		dsim_info("temperature: %d, tset: %d\n", lcd->temperature, TSET[1]);
	}
	return ret;
}

static int dsim_panel_set_hbm(struct dsim_device *dsim, int force)
{
	int ret = 0, level = 0;
	struct panel_private *panel = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;

	level = LEVEL_IS_HBM(lcd->auto_brightness);
	if (panel == NULL) {
		dsim_err("%s : panel is NULL\n", __func__);
		goto exit;
	}

	if (force || lcd->current_hbm != lcd->hbm_tbl[level][1]) {
		lcd->current_hbm = lcd->hbm_tbl[level][1];
		if (dsim_write_hl_data(dsim, lcd->hbm_tbl[level], ARRAY_SIZE(SEQ_HBM_OFF)) < 0) {
			dsim_err("fail to write hbm command.\n");
			ret = -EPERM;
		}

		if (dsim_write_hl_data(dsim, SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE)) < 0) {
			dsim_err("%s : fail to write SEQ_GAMMA_UPDATE on command.\n", __func__);
			ret = -EPERM;
		}

		dsim_info("hbm: %d, auto_brightness: %d\n", lcd->current_hbm, lcd->auto_brightness);
	}
exit:
	return ret;
}

static int low_level_set_brightness(struct dsim_device *dsim, int force)
{
	if (dsim_write_hl_data(dsim, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0)) < 0)
		dsim_err("%s : fail to write F0 on command.\n", __func__);

	dsim_panel_gamma_ctrl(dsim);

	dsim_panel_aid_ctrl(dsim);

	dsim_panel_set_elvss(dsim);

	dsim_panel_set_acl(dsim, force);

	if (dsim_write_hl_data(dsim, SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE)) < 0)
			dsim_err("%s : fail to write SEQ_GAMMA_UPDATE on command.\n", __func__);

	dsim_panel_set_tset(dsim, force);

	dsim_panel_set_hbm(dsim, force);

	if (dsim_write_hl_data(dsim, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0)) < 0)
		dsim_err("%s : fail to write F0 on command\n", __func__);

	return 0;
}

static int get_acutal_br_index(struct dsim_device *dsim, int br)
{
	int i;
	int min;
	int gap;
	int index = 0;
	struct lcd_info *lcd = dsim->priv.par;
	struct SmtDimInfo *dimming_info = lcd->dim_info;

	if (dimming_info == NULL) {
		dsim_err("%s : dimming_info is NULL\n", __func__);
		return 0;
	}

	min = MAX_BRIGHTNESS;

	for (i = 0; i < MAX_BR_INFO; i++) {
		if (br > dimming_info[i].br)
			gap = br - dimming_info[i].br;
		else
			gap = dimming_info[i].br - br;

		if (gap == 0) {
			index = i;
			break;
		}

		if (gap < min) {
			min = gap;
			index = i;
		}
	}
	return index;
}
#endif

static int dsim_panel_set_brightness(struct dsim_device *dsim, int force)
{
	struct lcd_info *lcd = dsim->priv.par;
	int ret = 0;

	struct dim_data *dimming;
	int p_br = lcd->bd->props.brightness;
	int acutal_br = 0;
	int real_br = 0;
	int prev_index = lcd->br_index;

	dimming = (struct dim_data *)lcd->dim_data;
	if ((dimming == NULL) || (lcd->br_tbl == NULL)) {
		dsim_info("%s : this panel does not support dimming\n", __func__);
		return ret;
	}

	acutal_br = lcd->br_tbl[p_br];
	lcd->br_index = get_acutal_br_index(dsim, acutal_br);
	real_br = get_actual_br_value(dsim, lcd->br_index);
	lcd->acl_enable = ACL_IS_ON(real_br);

	if (LEVEL_IS_HBM(lcd->auto_brightness) && (p_br == lcd->bd->props.max_brightness)) {
		lcd->br_index = HBM_INDEX;
		lcd->acl_enable = 1;				/* hbm is acl on */
	}
	if (lcd->siop_enable)					/* check auto acl */
		lcd->acl_enable = 1;

	if (lcd->state != PANEL_STATE_RESUMED) {
		dsim_info("%s : panel is not active state..\n", __func__);
		goto set_br_exit;
	}

	dsim_info("%s : platform : %d, : mapping : %d, real : %d, index : %d\n",
		__func__, p_br, acutal_br, real_br, lcd->br_index);

	if (!force && lcd->br_index == prev_index)
		goto set_br_exit;

	if ((acutal_br == 0) || (real_br == 0))
		goto set_br_exit;

	mutex_lock(&lcd->lock);

	ret = low_level_set_brightness(dsim, force);
	if (ret)
		dsim_err("%s failed to set brightness : %d\n", __func__, acutal_br);

	mutex_unlock(&lcd->lock);

set_br_exit:
	return ret;
}

static int panel_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int panel_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	int brightness = bd->props.brightness;
	struct panel_private *priv = bl_get_data(bd);
	struct dsim_device *dsim;

	dsim = container_of(priv, struct dsim_device, priv);

	if (brightness < UI_MIN_BRIGHTNESS || brightness > UI_MAX_BRIGHTNESS) {
		pr_alert("Brightness should be in the range of 0 ~ 255\n");
		ret = -EINVAL;
		goto exit_set;
	}

	ret = dsim_panel_set_brightness(dsim, 0);
	if (ret) {
		dsim_err("%s : fail to set brightness\n", __func__);
		goto exit_set;
	}
exit_set:
	return ret;
}

static const struct backlight_ops panel_backlight_ops = {
	.get_brightness = panel_get_brightness,
	.update_status = panel_set_brightness,
};

static int dsim_backlight_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct lcd_info *lcd = dsim->priv.par;

	lcd->bd = backlight_device_register("panel", dsim->dev, &dsim->priv,
					&panel_backlight_ops, NULL);
	if (IS_ERR(lcd->bd)) {
		dsim_err("%s:failed register backlight\n", __func__);
		ret = PTR_ERR(lcd->bd);
	}

	lcd->bd->props.max_brightness = UI_MAX_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;
	lcd->bd->props.state = 0;

	return ret;
}


static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;

	sprintf(buf, "SDC_%02X%02X%02X\n", lcd->id[0], lcd->id[1], lcd->id[2]);

	return strlen(buf);
}

static ssize_t window_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;

	sprintf(buf, "%x %x %x\n", lcd->id[0], lcd->id[1], lcd->id[2]);

	return strlen(buf);
}

static ssize_t brightness_table_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *panel = dev_get_drvdata(dev);
	struct lcd_info *lcd = panel->par;

	char *pos = buf;
	int nit, i;

	if (IS_ERR_OR_NULL(lcd->br_tbl))
		return strlen(buf);

	for (i = 0; i <= UI_MAX_BRIGHTNESS; i++) {
		nit = lcd->br_tbl[i];
		pos += sprintf(pos, "%3d %3d\n", i, nit);
	}
	return pos - buf;
}

static ssize_t auto_brightness_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;

	sprintf(buf, "%u\n", lcd->auto_brightness);

	return strlen(buf);
}

static ssize_t auto_brightness_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct dsim_device *dsim;
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;
	int value;
	int rc;

	dsim = container_of(priv, struct dsim_device, priv);

	rc = kstrtoul(buf, (unsigned int)0, (unsigned long *)&value);
	if (rc < 0)
		return rc;
	else {
		if (lcd->auto_brightness != value) {
			dev_info(dev, "%s: %d, %d\n", __func__, lcd->auto_brightness, value);
			mutex_lock(&lcd->lock);
			lcd->auto_brightness = value;
			mutex_unlock(&lcd->lock);
			dsim_panel_set_brightness(dsim, 0);
		}
	}
	return size;
}

static ssize_t siop_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;

	sprintf(buf, "%u\n", lcd->siop_enable);

	return strlen(buf);
}

static ssize_t siop_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct dsim_device *dsim;
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;
	int value;
	int rc;

	dsim = container_of(priv, struct dsim_device, priv);

	rc = kstrtoul(buf, (unsigned int)0, (unsigned long *)&value);
	if (rc < 0)
		return rc;
	else {
		if (lcd->siop_enable != value) {
			dev_info(dev, "%s: %d, %d\n", __func__, lcd->siop_enable, value);
			mutex_lock(&lcd->lock);
			lcd->siop_enable = value;
			mutex_unlock(&lcd->lock);

			dsim_panel_set_brightness(dsim, 1);
		}
	}
	return size;
}

static ssize_t power_reduce_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;

	sprintf(buf, "%u\n", lcd->acl_enable);

	return strlen(buf);
}

static ssize_t power_reduce_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct dsim_device *dsim;
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;
	int value;
	int rc;

	dsim = container_of(priv, struct dsim_device, priv);
	rc = kstrtoul(buf, (unsigned int)0, (unsigned long *)&value);

	if (rc < 0)
		return rc;
	else {
		if (lcd->acl_enable != value) {
			dev_info(dev, "%s: %d, %d\n", __func__, lcd->acl_enable, value);
			mutex_lock(&lcd->lock);
			lcd->acl_enable = value;
			mutex_unlock(&lcd->lock);
			dsim_panel_set_brightness(dsim, 1);
		}
	}
	return size;
}

static ssize_t temperature_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char temp[] = "-20, -19, 0, 1\n";

	strcat(buf, temp);
	return strlen(buf);
}

static ssize_t temperature_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct dsim_device *dsim;
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;
	int value;
	int rc;

	dsim = container_of(priv, struct dsim_device, priv);

	rc = kstrtoint(buf, 10, &value);

	if (rc < 0)
		return rc;
	else {
		mutex_lock(&lcd->lock);
		lcd->temperature = value;
		mutex_unlock(&lcd->lock);

		dsim_panel_set_brightness(dsim, 1);

		dev_info(dev, "%s: %d, %d\n", __func__, value, lcd->temperature);
	}

	return size;
}

static ssize_t color_coordinate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;

	sprintf(buf, "%u, %u\n", lcd->coordinate[0], lcd->coordinate[1]);
	return strlen(buf);
}

static ssize_t manufacture_date_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;
	u16 year;
	u8 month, day, hour, min, sec;
	u32 msec;

	year = (lcd->date[0] >> 4) + 2011;
	month = lcd->date[0] & 0x0f;
	day = lcd->date[1];
	hour = lcd->date[2];
	min = lcd->date[3];
	sec = lcd->date[4];
	msec = (lcd->date[5] << 8) | lcd->date[6];


	sprintf(buf, "%d, %d, %d, %d:%d:%d.%d\n", year, month, day, hour, min, sec, msec);
	return strlen(buf);
}

#ifdef CONFIG_PANEL_AID_DIMMING
static void show_aid_log(struct dsim_device *dsim)
{
	int i, j, k;
	struct dim_data *dim_data = NULL;
	struct SmtDimInfo *dimming_info = NULL;
	struct panel_private *panel = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;
	u8 temp[256];


	if (panel == NULL) {
		dsim_err("%s:panel is NULL\n", __func__);
		return;
	}

	dim_data = (struct dim_data *)(lcd->dim_data);
	if (dim_data == NULL) {
		dsim_info("%s:dimming is NULL\n", __func__);
		return;
	}

	dimming_info = (struct SmtDimInfo *)(lcd->dim_info);
	if (dimming_info == NULL) {
		dsim_info("%s:dimming is NULL\n", __func__);
		return;
	}

	dsim_info("MTP VT : %d %d %d\n",
			dim_data->vt_mtp[CI_RED], dim_data->vt_mtp[CI_GREEN], dim_data->vt_mtp[CI_BLUE]);

	for (i = 0; i < VMAX; i++) {
		dsim_info("MTP V%d : %4d %4d %4d\n",
			vref_index[i], dim_data->mtp[i][CI_RED], dim_data->mtp[i][CI_GREEN], dim_data->mtp[i][CI_BLUE]);
	}

	for (i = 0; i < MAX_BR_INFO; i++) {
		memset(temp, 0, sizeof(temp));
		for (j = 1; j < OLED_CMD_GAMMA_CNT; j++) {
			if (j == 1 || j == 3 || j == 5)
				k = dimming_info[i].gamma[j++] * 256;
			else
				k = 0;
			snprintf(temp + strnlen(temp, 256), 256, " %d", dimming_info[i].gamma[j] + k);
		}
		dsim_info("nit :%3d %s\n", dimming_info[i].br, temp);
	}
}


static ssize_t aid_log_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct dsim_device *dsim;
	struct panel_private *priv = dev_get_drvdata(dev);

	dsim = container_of(priv, struct dsim_device, priv);

	show_aid_log(dsim);
	return strlen(buf);
}
#endif

static ssize_t manufacture_code_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;

	sprintf(buf, "%02X%02X%02X%02X%02X\n",
		lcd->code[0], lcd->code[1], lcd->code[2], lcd->code[3], lcd->code[4]);

	return strlen(buf);
}

static ssize_t dump_register_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct dsim_device *dsim;
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;
	char *pos = buf;
	u8 reg, len, offset;
	int ret, i;
	u8 *dump = NULL;

	dsim = container_of(priv, struct dsim_device, priv);

	reg = lcd->dump_info[0];
	len = lcd->dump_info[1];
	offset = lcd->dump_info[2];

	if (!reg || !len || reg > 0xff || len > 255 || offset > 255)
		goto exit;

	dump = kzalloc(len * sizeof(u8), GFP_KERNEL);

	if (lcd->state == PANEL_STATE_RESUMED) {
		if (dsim_write_hl_data(dsim, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0)) < 0)
			dsim_err("fail to write test on f0 command.\n");
		if (dsim_write_hl_data(dsim, SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC)) < 0)
			dsim_err("fail to write test on fc command.\n");

		ret = dsim_read_hl_data(dsim, reg, len, dump);

		if (dsim_write_hl_data(dsim, SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC)) < 0)
			dsim_err("fail to write test off fc command.\n");
		if (dsim_write_hl_data(dsim, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0)) < 0)
			dsim_err("fail to write test off f0 command.\n");
	}

	pos += sprintf(pos, "+ [%02X]\n", reg);
	for (i = 0; i < len; i++)
		pos += sprintf(pos, "%2d: %02x\n", i + offset + 1, dump[i]);
	pos += sprintf(pos, "- [%02X]\n", reg);

	dev_info(&lcd->ld->dev, "+ [%02X]\n", reg);
	for (i = 0; i < len; i++)
		dev_info(&lcd->ld->dev, "%2d: %02x\n", i + offset + 1, dump[i]);
	dev_info(&lcd->ld->dev, "- [%02X]\n", reg);

	kfree(dump);
exit:
	return pos - buf;
}

static ssize_t dump_register_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	unsigned int reg, len, offset;
	int ret;
	struct lcd_info *lcd = priv->par;

	ret = sscanf(buf, "%x %d %d", &reg, &len, &offset);

	if (ret == 2)
		offset = 0;

	dev_info(dev, "%s: %x %d %d\n", __func__, reg, len, offset);

	if (ret < 0)
		return ret;
	else {
		if (!reg || !len || reg > 0xff || len > 255 || offset > 255)
			return -EINVAL;

		lcd->dump_info[0] = reg;
		lcd->dump_info[1] = len;
		lcd->dump_info[2] = offset;
	}

	return size;
}

static ssize_t weakness_hbm_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;

	sprintf(buf, "%d\n", lcd->weakness_hbm_comp);

	return strlen(buf);
}

static ssize_t weakness_hbm_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int rc, value;
	struct dsim_device *dsim;
	struct panel_private *priv = dev_get_drvdata(dev);
	struct lcd_info *lcd = priv->par;

	dsim = container_of(priv, struct dsim_device, priv);

	rc = kstrtouint(buf, (unsigned int)0, &value);

	if (rc < 0)
		return rc;
	else {
		if (lcd->weakness_hbm_comp != value) {
			if ((lcd->weakness_hbm_comp == 1) && (value == 2)) {
				pr_info("%s don't support color blind -> gallery\n", __func__);
				return size;
			}
			if ((lcd->weakness_hbm_comp == 2) || (value == 2)) {
				lcd->weakness_hbm_comp = value;
				dsim_panel_set_brightness(dsim, 1);
				dev_info(dev, "%s: %d, %d\n", __func__, lcd->weakness_hbm_comp, value);
			}
		}
	}
	return size;
}

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);
static DEVICE_ATTR(manufacture_code, 0444, manufacture_code_show, NULL);
static DEVICE_ATTR(brightness_table, 0444, brightness_table_show, NULL);
static DEVICE_ATTR(auto_brightness, 0644, auto_brightness_show, auto_brightness_store);
static DEVICE_ATTR(siop_enable, 0664, siop_enable_show, siop_enable_store);
static DEVICE_ATTR(power_reduce, 0664, power_reduce_show, power_reduce_store);
static DEVICE_ATTR(temperature, 0664, temperature_show, temperature_store);
static DEVICE_ATTR(color_coordinate, 0444, color_coordinate_show, NULL);
static DEVICE_ATTR(manufacture_date, 0444, manufacture_date_show, NULL);
#ifdef CONFIG_PANEL_AID_DIMMING
static DEVICE_ATTR(aid_log, 0444, aid_log_show, NULL);
#endif
static DEVICE_ATTR(dump_register, 0664, dump_register_show, dump_register_store);
static DEVICE_ATTR(weakness_hbm_comp, 0664, weakness_hbm_show, weakness_hbm_store);

static void lcd_init_sysfs(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	int ret = -1;

	ret = device_create_file(&lcd->ld->dev, &dev_attr_lcd_type);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_window_type);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_manufacture_code);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_brightness_table);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->bd->dev, &dev_attr_auto_brightness);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_siop_enable);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_power_reduce);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_temperature);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_color_coordinate);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_manufacture_date);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

#ifdef CONFIG_PANEL_AID_DIMMING
	ret = device_create_file(&lcd->ld->dev, &dev_attr_aid_log);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);
#endif
	ret = device_create_file(&lcd->ld->dev, &dev_attr_dump_register);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd->bd->dev, &dev_attr_weakness_hbm_comp);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n", __LINE__);
}


#ifdef CONFIG_PANEL_AID_DIMMING
static const unsigned char *HBM_TABLE[HBM_STATUS_MAX] = {SEQ_HBM_OFF, SEQ_HBM_ON};
static const unsigned char *ACL_CUTOFF_TABLE[ACL_STATUS_MAX] = {SEQ_ACL_OFF, SEQ_ACL_15};

static const unsigned int br_tbl[256] = {
	2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22,
	24, 25, 27, 29, 30, 32, 34, 37, 39, 41, 41, 44, 44, 47, 47, 50, 50, 53, 53, 56,
	56, 56, 60, 60, 60, 64, 64, 64, 68, 68, 68, 72, 72, 72, 72, 77, 77, 77, 82,
	82, 82, 82, 87, 87, 87, 87, 93, 93, 93, 93, 98, 98, 98, 98, 98, 105, 105,
	105, 105, 111, 111, 111, 111, 111, 111, 119, 119, 119, 119, 119, 126, 126, 126,
	126, 126, 126, 134, 134, 134, 134, 134, 134, 134, 143, 143, 143, 143, 143, 143,
	152, 152, 152, 152, 152, 152, 152, 152, 162, 162, 162, 162, 162, 162, 162, 172,
	172, 172, 172, 172, 172, 172, 172, 183, 183, 183, 183, 183, 183, 183, 183, 183,
	195, 195, 195, 195, 195, 195, 195, 195, 207, 207, 207, 207, 207, 207, 207, 207,
	207, 207, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 234, 234, 234, 234,
	234, 234, 234, 234, 234, 234, 234, 249, 249, 249, 249, 249, 249, 249, 249, 249,
	249, 249, 249, 265, 265, 265, 265, 265, 265, 265, 265, 265, 265, 265, 265, 282,
	282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 300, 300, 300, 300,
	300, 300, 300, 300, 300, 300, 300, 300, 316, 316, 316, 316, 316, 316, 316, 316,
	316, 316, 316, 316, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 360,
};

static const short center_gamma[NUM_VREF][CI_MAX] = {
	{0x000, 0x000, 0x000},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x100, 0x100, 0x100},
};

static struct SmtDimInfo daisy_dimming_info[MAX_BR_INFO + 1] = {				/* add hbm array */
	{.br = 5,	.cTbl = ctbl5nit_ea, .aid = aid5_ea, 	.elvAcl = elv_5nit_ea, .elv = elv_5nit_ea, .m_gray = m_gray_5},
	{.br = 6,	.cTbl = ctbl6nit_ea, .aid = aid6_ea, 	.elvAcl = elv_6nit_ea, .elv = elv_6nit_ea, .m_gray = m_gray_6},
	{.br = 7,	.cTbl = ctbl7nit_ea, .aid = aid7_ea, 	.elvAcl = elv_7nit_ea, .elv = elv_7nit_ea, .m_gray = m_gray_7},
	{.br = 8,	.cTbl = ctbl8nit_ea, .aid = aid8_ea, 	.elvAcl = elv_8nit_ea, .elv = elv_8nit_ea, .m_gray = m_gray_8},
	{.br = 9,	.cTbl = ctbl9nit_ea, .aid = aid9_ea, 	.elvAcl = elv_9nit_ea, .elv = elv_9nit_ea, .m_gray = m_gray_9},
	{.br = 10,	.cTbl = ctbl10nit_ea, .aid = aid10_ea, 	.elvAcl = elv_10nit_ea, .elv = elv_10nit_ea, .m_gray = m_gray_10},
	{.br = 11,	.cTbl = ctbl11nit_ea, .aid = aid11_ea, 	.elvAcl = elv_11nit_ea, .elv = elv_11nit_ea, .m_gray = m_gray_11},
	{.br = 12,	.cTbl = ctbl12nit_ea, .aid = aid12_ea, 	.elvAcl = elv_12nit_ea, .elv = elv_12nit_ea, .m_gray = m_gray_12},
	{.br = 13,	.cTbl = ctbl13nit_ea, .aid = aid13_ea, 	.elvAcl = elv_13nit_ea, .elv = elv_13nit_ea, .m_gray = m_gray_13},
	{.br = 14,	.cTbl = ctbl14nit_ea, .aid = aid14_ea, 	.elvAcl = elv_14nit_ea, .elv = elv_14nit_ea, .m_gray = m_gray_14},
	{.br = 15,	.cTbl = ctbl15nit_ea, .aid = aid15_ea, 	.elvAcl = elv_15nit_ea, .elv = elv_15nit_ea, .m_gray = m_gray_15},
	{.br = 16,	.cTbl = ctbl16nit_ea, .aid = aid16_ea, 	.elvAcl = elv_16nit_ea, .elv = elv_16nit_ea, .m_gray = m_gray_16},
	{.br = 17,	.cTbl = ctbl17nit_ea, .aid = aid17_ea, 	.elvAcl = elv_17nit_ea, .elv = elv_17nit_ea, .m_gray = m_gray_17},
	{.br = 19,	.cTbl = ctbl19nit_ea, .aid = aid19_ea, 	.elvAcl = elv_19nit_ea, .elv = elv_19nit_ea, .m_gray = m_gray_19},
	{.br = 20,	.cTbl = ctbl20nit_ea, .aid = aid20_ea, 	.elvAcl = elv_20nit_ea, .elv = elv_20nit_ea, .m_gray = m_gray_20},
	{.br = 21,	.cTbl = ctbl21nit_ea, .aid = aid21_ea, 	.elvAcl = elv_21nit_ea, .elv = elv_21nit_ea, .m_gray = m_gray_21},
	{.br = 22,	.cTbl = ctbl22nit_ea, .aid = aid22_ea, 	.elvAcl = elv_22nit_ea, .elv = elv_22nit_ea, .m_gray = m_gray_22},
	{.br = 24,	.cTbl = ctbl24nit_ea, .aid = aid24_ea, 	.elvAcl = elv_24nit_ea, .elv = elv_24nit_ea, .m_gray = m_gray_24},
	{.br = 25,	.cTbl = ctbl25nit_ea, .aid = aid25_ea, 	.elvAcl = elv_25nit_ea, .elv = elv_25nit_ea, .m_gray = m_gray_25},
	{.br = 27,	.cTbl = ctbl27nit_ea, .aid = aid27_ea, 	.elvAcl = elv_27nit_ea, .elv = elv_27nit_ea, .m_gray = m_gray_27},
	{.br = 29,	.cTbl = ctbl29nit_ea, .aid = aid29_ea, 	.elvAcl = elv_29nit_ea, .elv = elv_29nit_ea, .m_gray = m_gray_29},
	{.br = 30,	.cTbl = ctbl30nit_ea, .aid = aid30_ea, 	.elvAcl = elv_30nit_ea, .elv = elv_30nit_ea, .m_gray = m_gray_30},
	{.br = 32,	.cTbl = ctbl32nit_ea, .aid = aid32_ea, 	.elvAcl = elv_32nit_ea, .elv = elv_32nit_ea, .m_gray = m_gray_32},
	{.br = 34,	.cTbl = ctbl34nit_ea, .aid = aid34_ea, 	.elvAcl = elv_34nit_ea, .elv = elv_34nit_ea, .m_gray = m_gray_34},
	{.br = 37,	.cTbl = ctbl37nit_ea, .aid = aid37_ea, 	.elvAcl = elv_37nit_ea, .elv = elv_37nit_ea, .m_gray = m_gray_37},
	{.br = 39,	.cTbl = ctbl39nit_ea, .aid = aid39_ea, 	.elvAcl = elv_39nit_ea, .elv = elv_39nit_ea, .m_gray = m_gray_39},
	{.br = 41,	.cTbl = ctbl41nit_ea, .aid = aid41_ea, 	.elvAcl = elv_41nit_ea, .elv = elv_41nit_ea, .m_gray = m_gray_41},
	{.br = 44,	.cTbl = ctbl44nit_ea, .aid = aid44_ea, 	.elvAcl = elv_44nit_ea, .elv = elv_44nit_ea, .m_gray = m_gray_44},
	{.br = 47,	.cTbl = ctbl47nit_ea, .aid = aid47_ea, 	.elvAcl = elv_47nit_ea, .elv = elv_47nit_ea, .m_gray = m_gray_47},
	{.br = 50,	.cTbl = ctbl50nit_ea, .aid = aid50_ea, 	.elvAcl = elv_50nit_ea, .elv = elv_50nit_ea, .m_gray = m_gray_50},
	{.br = 53,	.cTbl = ctbl53nit_ea, .aid = aid53_ea, 	.elvAcl = elv_53nit_ea, .elv = elv_53nit_ea, .m_gray = m_gray_53},
	{.br = 56,	.cTbl = ctbl56nit_ea, .aid = aid56_ea, 	.elvAcl = elv_56nit_ea, .elv = elv_56nit_ea, .m_gray = m_gray_56},
	{.br = 60,	.cTbl = ctbl60nit_ea, .aid = aid60_ea, 	.elvAcl = elv_60nit_ea, .elv = elv_60nit_ea, .m_gray = m_gray_60},
	{.br = 64,	.cTbl = ctbl64nit_ea, .aid = aid64_ea, 	.elvAcl = elv_64nit_ea, .elv = elv_64nit_ea, .m_gray = m_gray_64},
	{.br = 68,	.cTbl = ctbl68nit_ea, .aid = aid68_ea, 	.elvAcl = elv_68nit_ea, .elv = elv_68nit_ea, .m_gray = m_gray_68},
	{.br = 72,	.cTbl = ctbl72nit_ea, .aid = aid72_ea, 	.elvAcl = elv_72nit_ea, .elv = elv_72nit_ea, .m_gray = m_gray_72},
	{.br = 77,	.cTbl = ctbl77nit_ea, .aid = aid72_ea, 	.elvAcl = elv_77nit_ea, .elv = elv_77nit_ea, .m_gray = m_gray_77},
	{.br = 82,	.cTbl = ctbl82nit_ea, .aid = aid72_ea, 	.elvAcl = elv_82nit_ea, .elv = elv_82nit_ea, .m_gray = m_gray_82},
	{.br = 87,	.cTbl = ctbl87nit_ea, .aid = aid72_ea, 	.elvAcl = elv_87nit_ea, .elv = elv_87nit_ea, .m_gray = m_gray_87},
	{.br = 93,	.cTbl = ctbl93nit_ea, .aid = aid72_ea, 	.elvAcl = elv_93nit_ea, .elv = elv_93nit_ea, .m_gray = m_gray_93},
	{.br = 98,	.cTbl = ctbl98nit_ea, .aid = aid72_ea, 	.elvAcl = elv_98nit_ea, .elv = elv_98nit_ea, .m_gray = m_gray_98},
	{.br = 105,	.cTbl = ctbl105nit_ea, .aid = aid72_ea, 	.elvAcl = elv_105nit_ea, .elv = elv_105nit_ea, .m_gray = m_gray_105},
	{.br = 111,	.cTbl = ctbl111nit_ea, .aid = aid72_ea, 	.elvAcl = elv_111nit_ea, .elv = elv_111nit_ea, .m_gray = m_gray_111},
	{.br = 119,	.cTbl = ctbl119nit_ea, .aid = aid119_ea, 	.elvAcl = elv_119nit_ea, .elv = elv_119nit_ea, .m_gray = m_gray_119},
	{.br = 126,	.cTbl = ctbl126nit_ea, .aid = aid126_ea, 	.elvAcl = elv_126nit_ea, .elv = elv_126nit_ea, .m_gray = m_gray_126},
	{.br = 134,	.cTbl = ctbl134nit_ea, .aid = aid134_ea, 	.elvAcl = elv_134nit_ea, .elv = elv_134nit_ea, .m_gray = m_gray_134},
	{.br = 143,	.cTbl = ctbl143nit_ea, .aid = aid143_ea, 	.elvAcl = elv_143nit_ea, .elv = elv_143nit_ea, .m_gray = m_gray_143},
	{.br = 152,	.cTbl = ctbl152nit_ea, .aid = aid152_ea, 	.elvAcl = elv_152nit_ea, .elv = elv_152nit_ea, .m_gray = m_gray_152},
	{.br = 162,	.cTbl = ctbl162nit_ea, .aid = aid162_ea, 	.elvAcl = elv_162nit_ea, .elv = elv_162nit_ea, .m_gray = m_gray_162},
	{.br = 172,	.cTbl = ctbl172nit_ea, .aid = aid172_ea, 	.elvAcl = elv_172nit_ea, .elv = elv_172nit_ea, .m_gray = m_gray_172},
	{.br = 183,	.cTbl = ctbl183nit_ea, .aid = aid183_ea, 	.elvAcl = elv_183nit_ea, .elv = elv_183nit_ea, .m_gray = m_gray_183},
	{.br = 195,	.cTbl = ctbl195nit_ea, .aid = aid183_ea, 	.elvAcl = elv_195nit_ea, .elv = elv_195nit_ea, .m_gray = m_gray_195},
	{.br = 207,	.cTbl = ctbl207nit_ea, .aid = aid183_ea, 	.elvAcl = elv_207nit_ea, .elv = elv_207nit_ea, .m_gray = m_gray_207},
	{.br = 220,	.cTbl = ctbl220nit_ea, .aid = aid183_ea, 	.elvAcl = elv_220nit_ea, .elv = elv_220nit_ea, .m_gray = m_gray_220},
	{.br = 234,	.cTbl = ctbl234nit_ea, .aid = aid183_ea, 	.elvAcl = elv_234nit_ea, .elv = elv_234nit_ea, .m_gray = m_gray_234},
	{.br = 249,	.cTbl = ctbl249nit_ea, .aid = aid183_ea, 	.elvAcl = elv_249nit_ea, .elv = elv_249nit_ea, .m_gray = m_gray_249},
	{.br = 265,	.cTbl = ctbl265nit_ea, .aid = aid183_ea, 	.elvAcl = elv_265nit_ea, .elv = elv_265nit_ea, .m_gray = m_gray_265},
	{.br = 282,	.cTbl = ctbl282nit_ea, .aid = aid183_ea, 	.elvAcl = elv_282nit_ea, .elv = elv_282nit_ea, .m_gray = m_gray_282},
	{.br = 300,	.cTbl = ctbl300nit_ea, .aid = aid183_ea, 	.elvAcl = elv_300nit_ea, .elv = elv_300nit_ea, .m_gray = m_gray_300},
	{.br = 316,	.cTbl = ctbl316nit_ea, .aid = aid183_ea, 	.elvAcl = elv_316nit_ea, .elv = elv_316nit_ea, .m_gray = m_gray_316},
	{.br = 333,	.cTbl = ctbl333nit_ea, .aid = aid183_ea, 	.elvAcl = elv_333nit_ea, .elv = elv_333nit_ea, .m_gray = m_gray_333},
	{.br = 360,	.cTbl = ctbl360nit_ea, .aid = aid183_ea, 	.elvAcl = elv_360nit_ea, .elv = elv_360nit_ea, .m_gray = m_gray_360},
	{.br = 360,	.cTbl = ctbl360nit_ea, .aid = aid183_ea, 	.elvAcl = elv_360nit_ea, .elv = elv_360nit_ea, .m_gray = m_gray_360},
};

static int init_dimming(struct dsim_device *dsim, u8 *mtp)
{
	int i, j;
	int pos = 0;
	int ret = 0;
	short temp;
	struct dim_data *dimming;
	struct lcd_info *lcd = dsim->priv.par;
	struct SmtDimInfo *diminfo = NULL;

	dimming = kzalloc(sizeof(struct dim_data), GFP_KERNEL);
	if (!dimming) {
		dsim_err("failed to allocate memory for dim data\n");
		ret = -ENOMEM;
		goto error;
	}

	diminfo = daisy_dimming_info;
	dsim_info("%s : ea8061s\n", __func__);

	lcd->dim_data = (void *)dimming;
	lcd->dim_info = (void *)diminfo; /* dimming info */

	lcd->br_tbl = (unsigned int *)br_tbl; /* backlight table */
	lcd->hbm_tbl = (unsigned char **)HBM_TABLE; /* command hbm on and off */
	lcd->acl_cutoff_tbl = (unsigned char **)ACL_CUTOFF_TABLE; /* ACL on and off command */

	/* CENTER GAMMA V255 */
	for (j = 0; j < CI_MAX; j++) {
		temp = ((mtp[pos] & 0x01) ? -1 : 1) * mtp[pos+1];
		dimming->t_gamma[V255][j] = (int)center_gamma[V255][j] + temp;
		dimming->mtp[V255][j] = temp;
		pos += 2;
	}

	/* CENTER GAMME FOR V3 ~ V203 */
	for (i = V203; i > V0; i--) {
		for (j = 0; j < CI_MAX; j++) {
			temp = ((mtp[pos] & 0x80) ? -1 : 1) * (mtp[pos] & 0x7f);
			dimming->t_gamma[i][j] = (int)center_gamma[i][j] + temp;
			dimming->mtp[i][j] = temp;
			pos++;
		}
	}

	/* CENTER GAMMA FOR V0 */
	for (j = 0; j < CI_MAX; j++) {
		dimming->t_gamma[V0][j] = (int)center_gamma[V0][j] + temp;
		dimming->mtp[V0][j] = 0;
	}

	for (j = 0; j < CI_MAX; j++) {
		dimming->vt_mtp[j] = mtp[pos];
		pos++;
	}

	/* Center gamma */
	dimm_info("Center Gamma Info :\n");
	for (i = 0; i < VMAX; i++) {
		dsim_info("Gamma : %3d %3d %3d : %3x %3x %3x\n",
			dimming->t_gamma[i][CI_RED], dimming->t_gamma[i][CI_GREEN], dimming->t_gamma[i][CI_BLUE],
			dimming->t_gamma[i][CI_RED], dimming->t_gamma[i][CI_GREEN], dimming->t_gamma[i][CI_BLUE]);
	}


	dimm_info("VT MTP :\n");
	dimm_info("Gamma : %3d %3d %3d : %3x %3x %3x\n",
			dimming->vt_mtp[CI_RED], dimming->vt_mtp[CI_GREEN], dimming->vt_mtp[CI_BLUE],
			dimming->vt_mtp[CI_RED], dimming->vt_mtp[CI_GREEN], dimming->vt_mtp[CI_BLUE]);

	/* MTP value get from ddi */
	dimm_info("MTP Info from ddi:\n");
	for (i = 0; i < VMAX; i++) {
		dimm_info("Gamma : %3d %3d %3d : %3x %3x %3x\n",
			dimming->mtp[i][CI_RED], dimming->mtp[i][CI_GREEN], dimming->mtp[i][CI_BLUE],
			dimming->mtp[i][CI_RED], dimming->mtp[i][CI_GREEN], dimming->mtp[i][CI_BLUE]);
	}

	ret = generate_volt_table(dimming);
	if (ret) {
		dimm_err("[ERR:%s] failed to generate volt table\n", __func__);
		goto error;
	}

	for (i = 0; i < MAX_BR_INFO - 1; i++) {
		ret = cal_gamma_from_index(dimming, &diminfo[i]);
		if (ret) {
			dsim_err("failed to calculate gamma : index : %d\n", i);
			goto error;
		}
	}
	memcpy(diminfo[i].gamma, SEQ_GAMMA_CONDITION_SET, ARRAY_SIZE(SEQ_GAMMA_CONDITION_SET));
error:
	return ret;
}
#endif

static int ea8061s_read_init_info(struct dsim_device *dsim, unsigned char *mtp)
{
	int i = 0;
	int ret;
	struct panel_private *panel = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;
	unsigned char bufForDate[DATE_LEN] = {0,};
	unsigned char buf[MTP_SIZE] = {0, };
	unsigned char buf_elvss[ELVSS_READ_SIZE] = {0, };

	dsim_info("MDD : %s was called\n", __func__);

	ret = dsim_write_hl_data(dsim, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	if (ret < 0)
		dsim_err("%s : fail to write CMD : SEQ_TEST_KEY_ON_F0\n", __func__);

	/* id */
	ret = dsim_read_hl_data(dsim, ID_REG, ID_LEN, lcd->id);
	if (ret != ID_LEN) {
		dsim_err("%s : can't find connected panel. check panel connection\n", __func__);
		panel->lcdConnected = PANEL_DISCONNEDTED;
		goto read_exit;
	}

	dsim_info("READ ID : ");
	for (i = 0; i < ID_LEN; i++)
		dsim_info("%02x, ", lcd->id[i]);
	dsim_info("\n");

	/* mtp */
	ret = dsim_read_hl_data(dsim, MTP_ADDR, MTP_SIZE, buf);
	if (ret != MTP_SIZE) {
		dsim_err("failed to read mtp, check panel connection\n");
		goto read_fail;
	}
	memcpy(mtp, buf, MTP_SIZE);
	dsim_info("READ MTP SIZE : %d\n", MTP_SIZE);
	dsim_info("=========== MTP INFO ===========\n");
	for (i = 0; i < MTP_SIZE; i++)
		dsim_info("MTP[%2d] : %2d : %2x\n", i, mtp[i], mtp[i]);

	/* date & coordinate */
	ret = dsim_read_hl_data(dsim, DATE_REG, DATE_LEN, bufForDate);
	if (ret != DATE_LEN) {
		dsim_err("fail to read date on command.\n");
		goto read_fail;
	}
	lcd->date[0] = bufForDate[4];		/* y,m */
	lcd->date[1] = bufForDate[5] & 0x1f;	/* d */
	lcd->date[2] = bufForDate[6] & 0x1f;	/* h */
	lcd->date[3] = bufForDate[7] & 0x2f;	/* m */
	lcd->date[4] = bufForDate[8] & 0x2f;	/* s */
	lcd->date[5] = bufForDate[9] & 0x3f;	/* ms */
	lcd->date[6] = bufForDate[10];		/* ms */

	for(i = 4; i < 11; i++) {
		dsim_info("date code %d : 0x%02x\n", i, bufForDate[i]);
	}

	lcd->coordinate[0] = bufForDate[0] << 8 | bufForDate[1];	/* X */
	lcd->coordinate[1] = bufForDate[2] << 8 | bufForDate[3];	/* Y */

	/* Read elvss from B6h */
	ret = dsim_read_hl_data(dsim, ELVSS_READ_ADDR, ELVSS_READ_SIZE, buf_elvss);
	if (ret != ELVSS_READ_SIZE) {
		dsim_err("fail to read HBM from B6h.\n");
		goto read_fail;
	}
	memcpy(lcd->elvss_def, buf_elvss, ELVSS_READ_SIZE);

	ret = dsim_write_hl_data(dsim, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_TEST_KEY_OFF_F0\n", __func__);
		goto read_fail;
	}
read_exit:
	return 0;

read_fail:
	return -ENODEV;
}

static int ea8061s_displayon(struct dsim_device *dsim)
{
	int ret = 0;

	dsim_info("MDD : %s was called\n", __func__);

	ret = dsim_write_hl_data(dsim, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : DISPLAY_ON\n", __func__);
		goto displayon_err;
	}

	usleep_range(12000, 13000);

displayon_err:
	return ret;
}

static int ea8061s_exit(struct dsim_device *dsim)
{
	int ret = 0;

	dsim_info("MDD : %s was called\n", __func__);
	ret = dsim_write_hl_data(dsim, SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : DISPLAY_OFF\n", __func__);
		goto exit_err;
	}

	msleep(20);

	ret = dsim_write_hl_data(dsim, SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SLEEP_IN\n", __func__);
		goto exit_err;
	}

	msleep(120);

exit_err:
	return ret;
}

static int ea8061s_init(struct dsim_device *dsim)
{
	int ret = 0;

	dsim_info("MDD : %s was called : ea8061s\n", __func__);
	usleep_range(5000, 6000);

	ret = dsim_write_hl_data(dsim, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_TEST_KEY_ON_F0\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_TEST_KEY_ON_FC\n", __func__);
		goto init_exit;
	}

	/* common setting */
	ret = dsim_write_hl_data(dsim, SEQ_HSYNC_GEN_ON, ARRAY_SIZE(SEQ_HSYNC_GEN_ON));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_HSYNC_GEN_ON\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_SOURCE_SLEW, ARRAY_SIZE(SEQ_SOURCE_SLEW));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_SOURCE_SLEW\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_AID_SET, ARRAY_SIZE(SEQ_AID_SET));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_AID_SET\n", __func__);
		goto init_exit;
	}


	ret = dsim_write_hl_data(dsim, SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_GAMMA_UPDATE\n", __func__);
		goto init_exit;
	}


	ret = dsim_write_hl_data(dsim, SEQ_S_WIRE, ARRAY_SIZE(SEQ_S_WIRE));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_S_WIRE\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_SLEEP_OUT\n", __func__);
		goto init_exit;
	}

	msleep(20);

	/* 1. Brightness setting */

	ret = dsim_write_hl_data(dsim, SEQ_GAMMA_CONDITION_SET, ARRAY_SIZE(SEQ_GAMMA_CONDITION_SET));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_GAMMA_CONDITION_SET\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_AID_SET, ARRAY_SIZE(SEQ_AID_SET));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_AID_SET\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_ELVSS_SET, ARRAY_SIZE(SEQ_ELVSS_SET));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_ELVSS_SET\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_ACL_OFF, ARRAY_SIZE(SEQ_ACL_OFF));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_ACL_OFF\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_GAMMA_UPDATE\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_TSET, ARRAY_SIZE(SEQ_TSET));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_TSET\n", __func__);
		goto init_exit;
	}

	msleep(120);

	ret = dsim_write_hl_data(dsim, SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));
	if (ret < 0) {
		dsim_err(":%s fail to write CMD : SEQ_TEST_KEY_OFF_FC\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(dsim, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	if (ret < 0) {
		dsim_err("%s : fail to write CMD : SEQ_TEST_KEY_OFF_F0\n", __func__);
		goto init_exit;
	}

init_exit:
	return ret;
}

static int ea8061s_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *panel = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;
	unsigned char mtp[MTP_SIZE] = {0, };

	dsim_info("MDD : %s was called\n", __func__);

	lcd->dim_data = (void *)NULL;
	panel->lcdConnected = PANEL_CONNECTED;

	if (panel->lcdConnected == PANEL_DISCONNEDTED) {
		dsim_err("dsim : %s lcd was not connected\n", __func__);
		goto probe_exit;
	}

	ea8061s_read_init_info(dsim, mtp);

	if (panel->lcdConnected == PANEL_DISCONNEDTED) {
		dsim_err("dsim : %s lcd was not connected\n", __func__);
		goto probe_exit;
	}

#ifdef CONFIG_PANEL_AID_DIMMING
	ret = init_dimming(dsim, mtp);
	if (ret)
		dsim_err("%s : failed to generate gamma tablen\n", __func__);
#endif

	dsim_panel_set_brightness(dsim, 1);

probe_exit:
	return ret;
}

struct dsim_panel_ops ea8061s_panel_ops = {
	.early_probe = NULL,
	.probe		= ea8061s_probe,
	.displayon	= ea8061s_displayon,
	.exit		= ea8061s_exit,
	.init		= ea8061s_init,
};

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
static int mdnie_lite_write_set(struct dsim_device *dsim, struct lcd_seq_info *seq, u32 num)
{
	int ret = 0, i;

	for (i = 0; i < num; i++) {
		if (seq[i].cmd) {
			ret = dsim_write_hl_data(dsim, seq[i].cmd, seq[i].len);
			if (ret != 0) {
				dsim_err("%s failed.\n", __func__);
				return ret;
			}
		}
		if (seq[i].sleep)
			usleep_range(seq[i].sleep * 1000 , seq[i].sleep * 1000);
	}
	return ret;
}

static int mdnie_lite_send_seq(struct dsim_device *dsim, struct lcd_seq_info *seq, u32 num)
{
	int ret = 0;
	struct lcd_info *lcd = dsim->priv.par;

	if (lcd->state != PANEL_STATE_RESUMED) {
		dsim_info("%s : panel is not active\n", __func__);
		return -EIO;
	}

	mutex_lock(&lcd->lock);
	ret = mdnie_lite_write_set(dsim, seq, num);

	mutex_unlock(&lcd->lock);

	return ret;
}

static int mdnie_lite_read(struct dsim_device *dsim, u8 addr, u8 *buf, u32 size)
{
	int ret = 0;
	struct lcd_info *lcd = dsim->priv.par;

	if (lcd->state != PANEL_STATE_RESUMED) {
		dsim_info("%s : panel is not active\n", __func__);
		return -EIO;
	}
	mutex_lock(&lcd->lock);
	ret = dsim_read_hl_data(dsim, addr, size, buf);

	mutex_unlock(&lcd->lock);

	return ret;
}
#endif

static int dsim_panel_early_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *panel = &dsim->priv;

	panel->ops = dsim_panel_get_priv_ops(dsim);

	if (panel->ops->early_probe)
		ret = panel->ops->early_probe(dsim);

	return ret;
}

static int dsim_panel_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *panel = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;
#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
	u16 coordinate[2] = {0, };
#endif
	panel->par = lcd = kzalloc(sizeof(struct lcd_info), GFP_KERNEL);
	if (!lcd) {
		pr_err("failed to allocate for lcd\n");
		ret = -ENOMEM;
		goto probe_err;
	}

	dsim->lcd = lcd->ld = lcd_device_register("panel", dsim->dev, &dsim->priv, NULL);
	if (IS_ERR(dsim->lcd)) {
		dsim_err("%s : faield to register lcd device\n", __func__);
		ret = PTR_ERR(dsim->lcd);
		goto probe_err;
	}
	ret = dsim_backlight_probe(dsim);
	if (ret) {
		dsim_err("%s : failed to prbe backlight driver\n", __func__);
		goto probe_err;
	}

	panel->lcdConnected = PANEL_CONNECTED;
	lcd->state = PANEL_STATE_RESUMED;
	lcd->temperature = NORMAL_TEMPERATURE;
	lcd->acl_enable = 0;
	lcd->current_acl = 0;
	lcd->auto_brightness = 0;
	lcd->siop_enable = 0;
	lcd->current_hbm = 0;
	lcd->current_vint = 0;
	lcd->weakness_hbm_comp = 0;
	mutex_init(&lcd->lock);

	if (panel->ops->probe) {
		ret = panel->ops->probe(dsim);
		if (ret) {
			dsim_err("%s : failed to probe panel\n", __func__);
			goto probe_err;
		}
	}

#if defined(CONFIG_EXYNOS_DECON_LCD_SYSFS)
	lcd_init_sysfs(dsim);
#endif
#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
	coordinate[0] = (u16)lcd->coordinate[0];
	coordinate[1] = (u16)lcd->coordinate[1];
	mdnie_register(&lcd->ld->dev, dsim, (mdnie_w)mdnie_lite_send_seq, (mdnie_r)mdnie_lite_read, coordinate, &tune_info);
#endif
	dsim_info("%s: %s: done\n", kbasename(__FILE__), __func__);
probe_err:
	return ret;
}

static int dsim_panel_displayon(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *panel = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;

	if (lcd->state == PANEL_STATE_SUSPENED) {
		if (panel->ops->init) {
			ret = panel->ops->init(dsim);
			if (ret) {
				dsim_err("%s : failed to panel init\n", __func__);
				lcd->state = PANEL_STATE_SUSPENED;
				goto displayon_err;
			}
		}
	}

	if (panel->ops->displayon) {
		ret = panel->ops->displayon(dsim);
		if (ret) {
			dsim_err("%s : failed to panel display on\n", __func__);
			goto displayon_err;
		}
	}
	lcd->state = PANEL_STATE_RESUMED;
	dsim_panel_set_brightness(dsim, 1);

displayon_err:
	return ret;
}

static int dsim_panel_suspend(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *panel = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;

	mutex_lock(&lcd->lock);

	if (lcd->state == PANEL_STATE_SUSPENED)
		goto suspend_err;

	lcd->state = PANEL_STATE_SUSPENDING;

	if (panel->ops->exit) {
		ret = panel->ops->exit(dsim);
		if (ret) {
			dsim_err("%s : failed to panel exit\n", __func__);
			goto suspend_err;
		}
	}
	lcd->state = PANEL_STATE_SUSPENED;

suspend_err:
	mutex_unlock(&lcd->lock);

	return ret;
}

static int dsim_panel_resume(struct dsim_device *dsim)
{
	int ret = 0;
	return ret;
}

struct mipi_dsim_lcd_driver ea8061s_mipi_lcd_driver = {
	.early_probe = dsim_panel_early_probe,
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
	.resume		= dsim_panel_resume,
};

