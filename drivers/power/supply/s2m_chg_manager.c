/*
 * s2m_chg_manager.c - Example battery driver for s2m series
 *
 * Copyright (C) 2019 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/power/s2m_chg_manager.h>

static void get_charging_current(struct s2m_chg_manager_info *battery,
				 int *input_curr, int *charging_curr,
				 int *topoff_curr)
{
	int input_current = battery->pdata->charging_current[battery->cable_type].input_current_limit;
	int charging_current = battery->pdata->charging_current[battery->cable_type].fast_charging_current;
	int topoff_current = battery->pdata->charging_current[battery->cable_type].full_check_current;

	pr_info("%s: cable_type(%d), current(%d, %d, %d)\n", __func__,
		battery->cable_type, input_current, charging_current,
		topoff_current);

	mutex_lock(&battery->iolock);

#if defined(CONFIG_USE_CCIC)
	/*Limit input & charging current according to the max current*/
	if (battery->cable_type == POWER_SUPPLY_TYPE_PREPARE_TA ||
	    battery->cable_type == POWER_SUPPLY_TYPE_USB_PD) {
		pr_info("%s: input_curr(%dmA), PD_input_curr(%dmA)\n", __func__,
			input_current, battery->pd_input_current);
		input_current = battery->pd_input_current;

		if (input_current >= battery->default_limit_current)
			input_current = input_current - 50;
	} else {
		if (battery->rp_attach &&
		    !(battery->cable_type == POWER_SUPPLY_TYPE_BATTERY ||
		      battery->cable_type == POWER_SUPPLY_TYPE_UNKNOWN ||
		      battery->cable_type == POWER_SUPPLY_TYPE_OTG)) {
			input_current = battery->rp_input_current > input_current?
					battery->rp_input_current : input_current;
			charging_current = battery->rp_charging_current > charging_current?
					   battery->rp_charging_current : charging_current;
			pr_info("%s: Rp attached! input: %dmA, chg: %dmA\n",
				__func__, input_current, charging_current);
		}

		if (input_current > battery->max_input_current) {
			input_current = battery->max_input_current;
			pr_info("%s: limit input current. (%dmA)\n",
				__func__, input_current);
		}
		if (charging_current > battery->max_charging_current) {
			charging_current = battery->max_charging_current;
			pr_info("%s: limit charging current. (%dmA)\n",
				__func__, charging_current);
		}
	}
#endif
	/* return current value */
	*input_curr = input_current;
	*charging_curr = charging_current;
	*topoff_curr = topoff_current;

	mutex_unlock(&battery->iolock);

}

static int set_charging_current(struct s2m_chg_manager_info *battery, int coeff)
{
	union power_supply_propval value;
	struct power_supply *psy;
	int input_current = 0, charging_current = 0, topoff_current = 0, ret = 0;
#if defined(CONFIG_SMALL_CHARGER)
	int small_limit_curr = battery->small_limit_current;
#endif

	/* get input, charging, and topoff current */
	get_charging_current(battery, &input_current,
			     &charging_current, &topoff_current);

	mutex_lock(&battery->iolock);

	input_current = input_current * coeff / 10;
	charging_current = charging_current * coeff / 10;

#if defined(CONFIG_SMALL_CHARGER)
	if (input_current > small_limit_curr) {
		battery->small_input_flag = input_current - small_limit_curr;
		input_current = small_limit_curr;
	}
#endif

	/* old vs new current(input, charge, topoff) Log */
	pr_info("%s: input curr. (%d -> %d), charge curr. (%d -> %d), "
		"top-off curr. (%d -> %d)\n",	__func__,
		battery->input_current, input_current,
		battery->charging_current, charging_current,
		battery->topoff_current, topoff_current);

	/* set input current limit */
	if (battery->input_current != input_current) {
		value.intval = input_current;

		psy = power_supply_get_by_name(battery->pdata->charger_name);
		if (!psy) {
			ret = -EINVAL;
			goto out;
		}
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CURRENT_MAX, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);

		battery->input_current = input_current;
	}
	/* set fast charging current */
	if (battery->charging_current != charging_current) {
		value.intval = charging_current;

		psy = power_supply_get_by_name(battery->pdata->charger_name);
		if (!psy) {
			ret = -EINVAL;
			goto out;
		}
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);

		battery->charging_current = charging_current;
	}
	/* set topoff current */
	if (battery->topoff_current != topoff_current) {
		value.intval = topoff_current;

		psy = power_supply_get_by_name(battery->pdata->charger_name);
		if (!psy) {
			ret = -EINVAL;
			goto out;
		}
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CURRENT_FULL, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);

		battery->topoff_current = topoff_current;
	}
#if defined(CONFIG_SMALL_CHARGER)
	if (battery->small_input_flag == 0) {
		/* turn off small charger */
		value.intval = S2M_BAT_CHG_MODE_CHARGING_OFF;
		psy = power_supply_get_by_name(battery->pdata->smallcharger_name);
		if (!psy) {
			ret = -EINVAL;
			goto out;
		}
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CHARGING_ENABLED, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);
			ret = 0;
		goto out;
	}
	else {
		/* set input current limit for small charger */
		value.intval = battery->small_input_flag;
		psy = power_supply_get_by_name(battery->pdata->smallcharger_name);
		if (!psy) {
			ret = -EINVAL;
			goto out;
		}
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CURRENT_MAX, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);

		/* set fast charging current for small charger */
		value.intval = battery->pdata->small_charging_current;
		psy = power_supply_get_by_name(battery->pdata->smallcharger_name);
		if (!psy) {
			ret = -EINVAL;
			goto out;
		}
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);
	}
#endif
out:
	mutex_unlock(&battery->iolock);
	return ret;
}

/*
 * set_charger_mode(): charger_mode must have one of following values.
 * 1. S2M_BAT_CHG_MODE_CHARGING
 *	Charger on.
 *	Supply power to system & battery both.
 * 2. S2M_BAT_CHG_MODE_CHARGING_OFF
 *	Buck mode. Stop battery charging.
 *	But charger supplies system power.
 * 3. S2M_BAT_CHG_MODE_BUCK_OFF
 *	All off. Charger is completely off.
 *	Do not supply power to battery & system both.
 */

static int set_charger_mode(struct s2m_chg_manager_info *battery,
			    int charger_mode)
{
	union power_supply_propval val;
	struct power_supply *psy;
	int ret;

	if (charger_mode != S2M_BAT_CHG_MODE_CHARGING)
		battery->full_check_cnt = 0;

	val.intval = charger_mode;

	psy = power_supply_get_by_name(battery->pdata->charger_name);
	if (!psy)
		return -EINVAL;
	ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);

#if defined(CONFIG_SMALL_CHARGER) && defined(CONFIG_USE_CCIC)
	switch (charger_mode) {
	case S2M_BAT_CHG_MODE_CHARGING:
		if (battery->cable_type == POWER_SUPPLY_TYPE_PREPARE_TA ||
		    battery->cable_type == POWER_SUPPLY_TYPE_USB_PD) {
			if (battery->small_input_flag == 0)
				return 0;

			psy = power_supply_get_by_name(battery->pdata->smallcharger_name);
			if (!psy)
				return -EINVAL;
			ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
			if (ret < 0)
				pr_err("%s: Fail to execute property\n", __func__);
		}
		break;
	case S2M_BAT_CHG_MODE_CHARGING_OFF:
	case S2M_BAT_CHG_MODE_BUCK_OFF:
		battery->small_input_flag = 0;
		psy = power_supply_get_by_name(battery->pdata->smallcharger_name);
		if (!psy)
			return -EINVAL;
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);

		break;
	default:
		pr_err("%s: Fail to set charger_mode\n", __func__);
		break;
	}
#endif
	return 0;
}

static int set_battery_status(struct s2m_chg_manager_info *battery, int status)
{
	union power_supply_propval value;
	struct power_supply *psy;
	int ret;

	pr_info("%s: current status = %d, new status = %d\n",
		__func__, battery->status, status);

	switch (status) {
	case POWER_SUPPLY_STATUS_CHARGING:
		/* notify charger cable type */
		value.intval = battery->cable_type;

		psy = power_supply_get_by_name(battery->pdata->charger_name);
		if (!psy)
			return -EINVAL;
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_ONLINE, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);

#if defined(CONFIG_SMALL_CHARGER) && defined(CONFIG_USE_CCIC)
		if (battery->cable_type == POWER_SUPPLY_TYPE_PREPARE_TA ||
		    battery->cable_type == POWER_SUPPLY_TYPE_USB_PD) {
			psy = power_supply_get_by_name(battery->pdata->smallcharger_name);
			if (!psy)
				return -EINVAL;
			ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_ONLINE, &value);
			if (ret < 0)
				pr_err("%s: Fail to execute property\n", __func__);
		}
#endif
		/* charger on */
		set_charger_mode(battery, S2M_BAT_CHG_MODE_CHARGING);
		set_charging_current(battery, NORMAL_CURR);
		break;

	case POWER_SUPPLY_STATUS_DISCHARGING:
		set_charging_current(battery, NORMAL_CURR);

		/* notify charger cable type */
		value.intval = battery->cable_type;

#if defined(CONFIG_SMALL_CHARGER) && defined(CONFIG_USE_CCIC)
		if (battery->cable_type == POWER_SUPPLY_TYPE_PREPARE_TA ||
		    battery->cable_type == POWER_SUPPLY_TYPE_USB_PD) {
			psy = power_supply_get_by_name(battery->pdata->smallcharger_name);
			if (!psy)
				return -EINVAL;
			ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_ONLINE, &value);
			if (ret < 0)
				pr_err("%s: Fail to execute property\n", __func__);
		}
#endif

		psy = power_supply_get_by_name(battery->pdata->charger_name);
		if (!psy)
			return -EINVAL;
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_ONLINE, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);

		set_charger_mode(battery, S2M_BAT_CHG_MODE_CHARGING_OFF);
		break;

	case POWER_SUPPLY_STATUS_NOT_CHARGING:
		set_charger_mode(battery, S2M_BAT_CHG_MODE_BUCK_OFF);

		/* to recover charger configuration when heath is recovered */
		battery->input_current = 0;
		battery->charging_current = 0;
		battery->topoff_current = 0;
#if defined(CONFIG_SMALL_CHARGER)
		battery->small_input_flag = 0;
#endif
		break;

	case POWER_SUPPLY_STATUS_FULL:
		set_charger_mode(battery, S2M_BAT_CHG_MODE_CHARGING_OFF);
		break;
	}
	/* battery status update */
	battery->status = status;
	value.intval = battery->status;
	psy = power_supply_get_by_name(battery->pdata->charger_name);
	if (!psy)
		return -EINVAL;
	ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_STATUS, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	return 0;
}

static void set_bat_status_by_cable(struct s2m_chg_manager_info *battery)
{
	if (battery->cable_type == POWER_SUPPLY_TYPE_BATTERY ||
	    battery->cable_type == POWER_SUPPLY_TYPE_UNKNOWN ||
	    battery->cable_type == POWER_SUPPLY_TYPE_OTG) {
		battery->is_recharging = false;
#if defined(CONFIG_BAT_TEMP)
		battery->is_temp_control = false;
#endif
#if defined(CONFIG_USE_CCIC)
		battery->pdo_sel_num = 0;
		battery->pdo_sel_vol = 0;
		battery->pdo_sel_cur = 0;
#endif
		set_battery_status(battery, POWER_SUPPLY_STATUS_DISCHARGING);
		return;
	}
	if (battery->status != POWER_SUPPLY_STATUS_FULL) {
		set_battery_status(battery, POWER_SUPPLY_STATUS_CHARGING);
		return;
	}

	dev_info(battery->dev, "%s: abnormal cable_type or status", __func__);
}

static int s2m_chg_manager_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct s2m_chg_manager_info *battery = power_supply_get_drvdata(psy);
	int ret = 0;
	union power_supply_propval value;

	dev_dbg(battery->dev, "prop: %d\n", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = battery->status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = battery->health;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = battery->cable_type;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = battery->battery_valid;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!battery->battery_valid)
			val->intval = FAKE_BAT_LEVEL;
		else
			val->intval = battery->voltage_now * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		val->intval = battery->voltage_avg * 1000;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battery->temperature;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = battery->charging_mode;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (!battery->battery_valid)
			val->intval = FAKE_BAT_LEVEL;
		else {
			if (battery->status == POWER_SUPPLY_STATUS_FULL)
				val->intval = 100;
			else
				val->intval = battery->capacity;
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = battery->current_now;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = battery->current_avg;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		/*Get fuelgauge psy*/
		psy = power_supply_get_by_name(battery->pdata->fuelgauge_name);
		if (!psy)
			return -EINVAL;
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_COUNTER, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);

		val->intval = value.intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = 100;
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		val->intval = battery->vchg_voltage;
		break;
	case POWER_SUPPLY_PROP_SOH:
		val->intval = battery->soh;
		break;
	default:
		ret = -ENODATA;
	}
	return ret;
}

static int s2m_chg_manager_set_property(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct s2m_chg_manager_info *battery = power_supply_get_drvdata(psy);
	int ret = 0;

	dev_dbg(battery->dev, "prop: %d\n", psp);
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		set_battery_status(battery, val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		battery->health = val->intval;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		battery->cable_type = val->intval;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int s2m_usb_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct s2m_chg_manager_info *battery = power_supply_get_drvdata(psy);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	/* Set enable=1 only if the USB charger is connected */
	switch (battery->cable_type) {
	case POWER_SUPPLY_TYPE_USB:
	case POWER_SUPPLY_TYPE_USB_DCP:
	case POWER_SUPPLY_TYPE_USB_CDP:
	case POWER_SUPPLY_TYPE_USB_ACA:
		val->intval = 1;
		break;
	default:
		val->intval = 0;
		break;
	}

	return 0;
}

/*
 * AC charger operations
 */
static int s2m_ac_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct s2m_chg_manager_info *battery = power_supply_get_drvdata(psy);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	/* Set enable=1 only if the AC charger is connected */
	switch (battery->cable_type) {
	case POWER_SUPPLY_TYPE_MAINS:
	case POWER_SUPPLY_TYPE_UNKNOWN:
	case POWER_SUPPLY_TYPE_PREPARE_TA:
	case POWER_SUPPLY_TYPE_HV_MAINS:
	case POWER_SUPPLY_TYPE_USB_PD:
		val->intval = 1;
		break;
	default:
		val->intval = 0;
		break;
	}

	return 0;
}

#if defined(CONFIG_IFCONN_NOTIFIER)
static int s2m_bat_cable_check(struct s2m_chg_manager_info *battery,
				   muic_attached_dev_t attached_dev)
{
	int current_cable_type = -1;

	pr_info("[%s]ATTACHED(%d)\n", __func__, attached_dev);

	switch (attached_dev) {
	case ATTACHED_DEV_OTG_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_OTG;
		break;
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_USB_MUIC:
	case ATTACHED_DEV_TIMEOUT_OPEN_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_USB;
		break;
	case ATTACHED_DEV_TA_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_TA_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_TA_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_ANY_MUIC:
	case ATTACHED_DEV_UNSUPPORTED_ID_VB_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_MAINS;
		break;
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_CDP_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	case ATTACHED_DEV_UNDEFINED_RANGE_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_BATTERY;
		break;
#if defined(CONFIG_HV_MUIC_TURBO_CHARGER)
	case ATTACHED_DEV_TURBO_CHARGER:
		current_cable_type = POWER_SUPPLY_TYPE_HV_MAINS;
		pr_info("[%s]Turbo charger ATTACHED\n", __func__);
		break;
#endif
	/* TODO: add QC and pump express */
	case ATTACHED_DEV_AFC_CHARGER_9V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_9V_MUIC:
#if defined(CONFIG_HV_MUIC_PE)
	case ATTACHED_DEV_PE_CHARGER_9V_MUIC:
#endif
		current_cable_type = POWER_SUPPLY_TYPE_HV_MAINS;
		break;
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_QC_CHARGER_PREPARE_MUIC:
#if defined(CONFIG_HV_MUIC_PE)
	case ATTACHED_DEV_PE_CHARGER_PREPARE_MUIC:
#endif
		current_cable_type = POWER_SUPPLY_TYPE_PREPARE_TA;
		break;
	default:
		current_cable_type = POWER_SUPPLY_TYPE_BATTERY;
		pr_err("%s: invalid type for charger:%d\n",
			__func__, attached_dev);
	}

	return current_cable_type;
}
#endif

#if defined(CONFIG_IFCONN_NOTIFIER)
#if defined(CONFIG_USE_CCIC)
static void usbpd_select_pdo_work(struct work_struct *work)
{
	struct s2m_chg_manager_info *battery = container_of(work,
							    struct s2m_chg_manager_info,
							    select_pdo_work.work);

	int pdo_num = battery->pdo_sel_num, ret = -1;

	ret = ifconn_notifier_notify(IFCONN_NOTIFY_BATTERY,
				     IFCONN_NOTIFY_MANAGER,
				     IFCONN_NOTIFY_ID_SELECT_PDO,
				     pdo_num,
				     IFCONN_NOTIFY_PARAM_DATA,
				     NULL);
	if (ret < 0)
		pr_err("%s: Fail to send noti\n", __func__);

}

static int s2m_bat_set_pdo(struct s2m_chg_manager_info *battery,
			       ifconn_pd_sink_status_t *pdo_data)
{
	int ret = -1, pdo_num = battery->pdo_sel_num;

	if (pdo_num > pdo_data->available_pdo_num + 1 || pdo_num < 1) {
		dev_info(battery->dev,
			 "%s: wrong pdo number. Stop pdo select.\n", __func__);
		return ret;
	}

	ret = POWER_SUPPLY_TYPE_PREPARE_TA;

	schedule_delayed_work(&battery->select_pdo_work, msecs_to_jiffies(50));
	return ret;
}

static void s2m_bat_set_rp_current(struct s2m_chg_manager_info *battery,
				       struct ifconn_notifier_template *pd_info)
{
	ifconn_pd_sink_status_t *pd_data;
	pd_data = &((struct pdic_notifier_data *)pd_info->data)->sink_status;

	switch (pd_data->rp_currentlvl) {
	case RP_CURRENT_LEVEL3:
		battery->rp_input_current = RP_CURRENT3;
		battery->rp_charging_current = RP_CURRENT3;
		break;
	case RP_CURRENT_LEVEL2:
		battery->rp_input_current = RP_CURRENT2;
		battery->rp_charging_current = RP_CURRENT2;
		break;
	case RP_CURRENT_LEVEL_DEFAULT:
	default:
		battery->rp_input_current = RP_CURRENT1;
		battery->rp_charging_current = RP_CURRENT1;
		break;
	}

	dev_info(battery->dev, "%s: rp_currentlvl(%d), input: %d, chg: %d\n",
		 __func__, pd_data->rp_currentlvl,
		 battery->rp_input_current, battery->rp_charging_current);
}

static int s2m_bat_pdo_check(struct s2m_chg_manager_info *battery,
				 struct ifconn_notifier_template *pdo_info)
{
	int current_cable = -1;
	int i;
	int pd_input_current_limit = battery->pdata->charging_current[POWER_SUPPLY_TYPE_USB_PD].input_current_limit;
	ifconn_pd_sink_status_t *pdo_data =
		&((struct pdic_notifier_data *)pdo_info->data)->sink_status;

	dev_info(battery->dev, "%s: available_pdo_num:%d, selected_pdo_num:%d,"
		 "current_pdo_num:%d\n", __func__, pdo_data->available_pdo_num,
		 pdo_data->selected_pdo_num, pdo_data->current_pdo_num);

	dev_info(battery->dev, "%s: pdo_max_input_vol:%d, pdo_max_chg_power:%d,"
		 " pdo_sel_num:%d\n", __func__, battery->pdo_max_input_vol,
		 battery->pdo_max_chg_power, battery->pdo_sel_num);

	if (pdo_data->available_pdo_num < 0)
		return current_cable;

	if (battery->pdo_sel_num == pdo_data->selected_pdo_num) {
		dev_info(battery->dev, "%s: Already done. Finish pdo check.\n",
			 __func__);
		current_cable = POWER_SUPPLY_TYPE_USB_PD;
		goto end_pdo_check;
	}

	for (i = 1; i <= pdo_data->available_pdo_num; i++) {
		dev_info(battery->dev,
			 "%s: pdo_num:%d, max_voltage:%d, max_current:%d\n",
			 __func__, i, pdo_data->power_list[i].max_voltage,
			 pdo_data->power_list[i].max_current);

		if (pdo_data->power_list[i].max_voltage > battery->pdo_max_input_vol)
			continue;

		pd_input_current_limit = (pd_input_current_limit >
					  pdo_data->power_list[i].max_current)?
					  pdo_data->power_list[i].max_current :
					  pd_input_current_limit;

		if (((pdo_data->power_list[i].max_voltage/1000) *
		    pd_input_current_limit) <= battery->pdo_max_chg_power) {
			battery->pdo_sel_num = i;
			battery->pdo_sel_vol = pdo_data->power_list[i].max_voltage;
			battery->pdo_sel_cur = pdo_data->power_list[i].max_current;
			dev_info(battery->dev, "%s: new pdo_sel_num:%d\n",
				 __func__, battery->pdo_sel_num);
		}
	}

	battery->pd_input_current = pd_input_current_limit;

	if (battery->pdo_sel_num == 0) {
		dev_info(battery->dev,
			 "%s: There is no proper pdo. Do normal TA setting\n",
			 __func__);
		current_cable = POWER_SUPPLY_TYPE_MAINS;
	} else
		current_cable = s2m_bat_set_pdo(battery, pdo_data);

end_pdo_check:
	return current_cable;
}
#endif

#if defined(CONFIG_USE_CCIC)
static int s2m_ifconn_handle_cc_notification(struct notifier_block *nb,
						 unsigned long action, void *data)
{
	struct s2m_chg_manager_info *battery = container_of(nb,
							    struct s2m_chg_manager_info,
							    ifconn_cc_nb);
	struct ifconn_notifier_template *ifconn_info = (struct ifconn_notifier_template *)data;
	muic_attached_dev_t attached_dev = (muic_attached_dev_t)ifconn_info->event;
	int cable_type = 0;
	const char *cmd;

	dev_info(battery->dev,
		 "%s: action(%ld) dump(0x%01x, 0x%01x, 0x%02x"
		 ", 0x%04x, 0x%04x, 0x%04x, 0x%04x)\n",
		 __func__, action, ifconn_info->src, ifconn_info->dest,
		 ifconn_info->id, ifconn_info->attach, ifconn_info->rprd,
		 ifconn_info->cable_type, ifconn_info->event);

	ifconn_info->cable_type = (muic_attached_dev_t)ifconn_info->event;

	dev_info(battery->dev, "%s: pd_attach(%d) rp_attach(%d)\n",
		 __func__, battery->pd_attach, battery->rp_attach);

	action = ifconn_info->id;
	mutex_lock(&battery->ifconn_lock);

	switch (action) {
	case IFCONN_NOTIFY_ID_DETACH:
		battery->pd_attach = battery->rp_attach = false;
		battery->rp_input_current = battery->rp_charging_current = 0;

		cmd = "DETACH";
		cable_type = POWER_SUPPLY_TYPE_BATTERY;
		break;
	default:
		cmd = "ERROR";
		cable_type = -1;
		break;
	}

	pr_info("%s: CMD[%s] attached_dev(%d) current_cable(%d)"
		" former cable_type(%d) battery_valid(%d)\n",
		__func__, cmd,  attached_dev, cable_type,
		battery->cable_type, battery->battery_valid);

	if (cable_type < 0)
		battery->cable_type = POWER_SUPPLY_TYPE_UNKNOWN;
	else
		battery->cable_type = cable_type;

	set_bat_status_by_cable(battery);

	pr_info("%s: Status(%s), Health(%s), Cable(%d), Recharging(%d)\n",
		__func__, bat_status_str[battery->status],
		health_str[battery->health], battery->cable_type,
		battery->is_recharging);

	power_supply_changed(battery->psy_battery);
	alarm_cancel(&battery->monitor_alarm);
	wake_lock(&battery->monitor_wake_lock);
	queue_delayed_work(battery->monitor_wqueue, &battery->monitor_work, 0);
	mutex_unlock(&battery->ifconn_lock);
	return 0;
}
#endif

static int s2m_ifconn_handle_notification(struct notifier_block *nb,
					      unsigned long action, void *data)
{
	struct s2m_chg_manager_info *battery = container_of(nb,
							    struct s2m_chg_manager_info,
							    ifconn_nb);
	struct ifconn_notifier_template *ifconn_info = (struct ifconn_notifier_template *)data;
	muic_attached_dev_t attached_dev = (muic_attached_dev_t)ifconn_info->event;
	int cable_type = 0;
	const char *cmd;
#if defined(CONFIG_USE_CCIC)
	struct pdic_notifier_data *pdic_info = (struct pdic_notifier_data *)ifconn_info->data;
#endif
	dev_info(battery->dev,
		 "%s: action(%ld) dump(0x%01x, 0x%01x, 0x%02x"
		 ", 0x%04x, 0x%04x, 0x%04x, 0x%04x)\n",
		 __func__, action, ifconn_info->src, ifconn_info->dest,
		 ifconn_info->id, ifconn_info->attach, ifconn_info->rprd,
		 ifconn_info->cable_type, ifconn_info->event);

	ifconn_info->cable_type = (muic_attached_dev_t)ifconn_info->event;
#if defined(CONFIG_USE_CCIC)
	dev_info(battery->dev, "%s: pd_attach(%d) rp_attach(%d)\n",
		 __func__, battery->pd_attach, battery->rp_attach);
#endif
	action = ifconn_info->id;
	mutex_lock(&battery->ifconn_lock);

	switch (action) {
	case IFCONN_NOTIFY_ID_DETACH:
#if defined(CONFIG_USE_CCIC)
		if (battery->pd_attach) {
			pr_info("%s, [IFCONN_NOTIFY_ID_DETACH] "
				"PD TA is attached. Skip cable check\n",
				__func__);
			mutex_unlock(&battery->ifconn_lock);
			return 0;
		}

		battery->rp_attach = false;
		battery->rp_input_current = battery->rp_charging_current = 0;
#endif
		cmd = "DETACH";
		cable_type = POWER_SUPPLY_TYPE_BATTERY;
		break;
	case IFCONN_NOTIFY_ID_ATTACH:
#if defined(CONFIG_USE_CCIC)
		if (battery->pd_attach) {
			pr_info("%s, [IFCONN_NOTIFY_ID_ATTACH] "
				"PD TA is attached. Skip cable check\n",
				__func__);
			cable_type =  POWER_SUPPLY_TYPE_USB_PD;
			cmd = "PD ATTACH";
			break;
		}
#endif
		cmd = "ATTACH";
		cable_type = s2m_bat_cable_check(battery, attached_dev);
		break;
#if defined(CONFIG_USE_CCIC)
	case IFCONN_NOTIFY_ID_POWER_STATUS:
		if (pdic_info->event == IFCONN_NOTIFY_EVENT_RP_ATTACH) {
			if (battery->pd_attach) {
				pr_info("%s: [IFCONN_NOTIFY_ID_POWER_STATUS] "
					"PD TA is attached. "
					"Skip Rp current setting\n",
					__func__);
				mutex_unlock(&battery->ifconn_lock);
				return 0;
			}
			/* Do Rp current setting*/
			s2m_bat_set_rp_current(battery, ifconn_info);
			cmd = "Rp ATTACH";
			battery->rp_attach = true;
			cable_type = battery->cable_type;
			attached_dev = ATTACHED_DEV_TYPE3_CHARGER_MUIC;
		} else {
			cable_type = s2m_bat_pdo_check(battery, ifconn_info);
			battery->pd_attach = true;
			if (battery->rp_attach) {
				pr_info("%s: [IFCONN_NOTIFY_ID_POWER_STATUS] "
					"PD TA setting. Clear rp_attach flag "
					"Clear rp_attach flag\n", __func__);
				battery->rp_attach = false;
			}

			switch (cable_type) {
			case POWER_SUPPLY_TYPE_USB_PD:
				cmd = "PD ATTACH";
				attached_dev = ATTACHED_DEV_TYPE3_CHARGER_MUIC;
				break;
			case POWER_SUPPLY_TYPE_PREPARE_TA:
				cmd = "PD PREPARE";
				attached_dev = ATTACHED_DEV_TYPE3_CHARGER_MUIC;
				break;
			default:
				cmd = "PD FAIL";
				break;
			}
		}
		break;
#endif
	default:
		cmd = "ERROR";
		cable_type = -1;
		break;
	}

	pr_info("%s: CMD[%s] attached_dev(%d) current_cable(%d)"
		" former cable_type(%d) battery_valid(%d)\n",
		__func__, cmd,  attached_dev, cable_type,
		battery->cable_type, battery->battery_valid);

	if (cable_type < 0)
		battery->cable_type = POWER_SUPPLY_TYPE_UNKNOWN;
	else
		battery->cable_type = cable_type;

	set_bat_status_by_cable(battery);

	pr_info("%s: Status(%s), Health(%s), Cable(%d), Recharging(%d)\n",
		__func__, bat_status_str[battery->status],
		health_str[battery->health], battery->cable_type,
		battery->is_recharging);

	power_supply_changed(battery->psy_battery);
	alarm_cancel(&battery->monitor_alarm);
	wake_lock(&battery->monitor_wake_lock);
	queue_delayed_work(battery->monitor_wqueue, &battery->monitor_work, 0);
	mutex_unlock(&battery->ifconn_lock);
	return 0;
}
#endif

static void get_battery_capacity(struct s2m_chg_manager_info *battery)
{

	union power_supply_propval value;
	struct power_supply *psy;
	unsigned int raw_soc = 0;
	int new_capacity = 0, ret;

	psy = power_supply_get_by_name(battery->pdata->fuelgauge_name);
	if (!psy) {
		pr_err("%s: Fail to get power supply name\n", __func__);

		return;
	}

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &value);
	if (ret < 0) {
		pr_err("%s: Fail to execute property\n", __func__);

		return;
	}
	raw_soc = value.intval;

	if (battery->status == POWER_SUPPLY_STATUS_FULL) {
		battery->max_rawsoc = raw_soc - battery->max_rawsoc_offset;
		if (battery->max_rawsoc <= 0)
			battery->max_rawsoc = 10;
	}

	new_capacity = (raw_soc * 100) / battery->max_rawsoc;

	if ((new_capacity == 0) && (raw_soc != 0)) {
		dev_info(battery->dev, "%s: new_capacity is 0, "
			 "but raw_soc is not 0. Maintain SOC 1\n", __func__);
		new_capacity = 1;
	}

	if (new_capacity > 100)
		new_capacity = 100;

	if (new_capacity > battery->capacity)
		new_capacity = battery->capacity + 1;
	else if (new_capacity < battery->capacity)
		new_capacity = battery->capacity - 1;

	if (new_capacity > 100)
		new_capacity = 100;
	else if (new_capacity < 0)
		new_capacity = 0;

	battery->capacity = new_capacity;

	dev_info(battery->dev, "%s: SOC(%u), rawsoc(%d), max_rawsoc(%u).\n",
		__func__, battery->capacity, raw_soc, battery->max_rawsoc);
}

static int get_battery_info(struct s2m_chg_manager_info *battery)
{
	union power_supply_propval value;
	struct power_supply *psy;
	int ret;

	/*Get fuelgauge psy*/
	psy = power_supply_get_by_name(battery->pdata->fuelgauge_name);
	if (!psy)
		return -EINVAL;

	/* Get voltage and current value */
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->voltage_now = value.intval;

	value.intval = S2M_BATTERY_VOLTAGE_AVERAGE;
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_AVG, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->voltage_avg = value.intval;

	value.intval = S2M_BATTERY_CURRENT_MA;
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->current_now = value.intval;

	value.intval = S2M_BATTERY_CURRENT_MA;
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CURRENT_AVG, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->current_avg = value.intval;

	/* Get temperature info */
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TEMP, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->temperature = value.intval;
	get_battery_capacity(battery);

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_SOH, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->soh = value.intval;

	/*Get charger psy*/
	psy = power_supply_get_by_name(battery->pdata->charger_name);
	if (!psy)
		return -EINVAL;

	/* Get input current limit */
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CURRENT_MAX, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->current_max = value.intval;

	/* Get charge current limit */
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->current_chg = value.intval;

	/* Get charger status*/
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_STATUS, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);

	if (battery->status != value.intval)
		pr_err("%s: battery status = %d, charger status = %d\n",
				__func__, battery->status, value.intval);

	psy = power_supply_get_by_name("s2mu106_pmeter");
	if (!psy)
		return -EINVAL;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_VCHGIN, &value);

	/* Get input voltage & current from powermeter */
	battery->vchg_voltage = value.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ICHGIN, &value);
	battery->vchg_current = value.intval;

	dev_info(battery->dev,
		 "%s:Vnow(%dmV),Inow(%dmA),Imax(%dmA),Ichg(%dmA),SOC(%d%%)"
		 ",Tbat(%d),SOH(%d%%),Vbus(%dmV),Ibus(%dmA)\n", __func__,
		 battery->voltage_now, battery->current_now,
		 battery->current_max, battery->current_chg,
		 battery->capacity, battery->temperature,
		 battery->soh, battery->vchg_voltage, battery->vchg_current
			);
	dev_dbg(battery->dev,
		"%s,Vavg(%dmV),Vocv(%dmV),Iavg(%dmA)\n",
		battery->battery_valid ? "Connected" : "Disconnected",
		battery->voltage_avg, battery->voltage_ocv, battery->current_avg);

#if defined(CONFIG_SMALL_CHARGER)
	psy = power_supply_get_by_name(battery->pdata->smallcharger_name);
	if (!psy)
		return -EINVAL;

	/* Get input current limit */
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CURRENT_MAX, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->small_input = value.intval;

	/* Get charge current limit */
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);
	battery->small_chg = value.intval;

	dev_info(battery->dev,
		 "%s: small Imax(%dmA), Ichg(%dmA)\n", __func__,
		 battery->small_input, battery->small_chg);
#endif
	return 0;
}

static int get_battery_health(struct s2m_chg_manager_info *battery)
{
	int health = POWER_SUPPLY_HEALTH_UNKNOWN, ret;
	struct power_supply *psy;
	union power_supply_propval value;
	/* Get health status from charger */
	psy = power_supply_get_by_name(battery->pdata->charger_name);
	if (!psy)
		return -EINVAL;
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_HEALTH, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);

	health = value.intval;

	return health;
}

static int get_temperature_health(struct s2m_chg_manager_info *battery)
{
	return POWER_SUPPLY_HEALTH_GOOD;
#if 0
	int health = POWER_SUPPLY_HEALTH_UNKNOWN;

	switch (battery->health) {
	case POWER_SUPPLY_HEALTH_OVERHEAT_LIMIT:
		if (battery->temperature < battery->temp_high_limit_recovery)
			health = POWER_SUPPLY_HEALTH_OVERHEAT;
		else
			health = POWER_SUPPLY_HEALTH_OVERHEAT_LIMIT;
		break;
	case POWER_SUPPLY_HEALTH_OVERHEAT:
		if (battery->temperature < battery->temp_high_recovery)
			health = POWER_SUPPLY_HEALTH_GOOD;
		else if (battery->temperature > battery->temp_high_limit)
			health = POWER_SUPPLY_HEALTH_OVERHEAT_LIMIT;
		else
			health = POWER_SUPPLY_HEALTH_OVERHEAT;
		break;
	case POWER_SUPPLY_HEALTH_COLD_LIMIT:
		if (battery->temperature > battery->temp_low_limit_recovery)
			health = POWER_SUPPLY_HEALTH_COLD;
		else
			health = POWER_SUPPLY_HEALTH_COLD_LIMIT;
		break;
	case POWER_SUPPLY_HEALTH_COLD:
		if (battery->temperature > battery->temp_low_recovery)
			health = POWER_SUPPLY_HEALTH_GOOD;
		else if (battery->temperature < battery->temp_low_limit)
			health = POWER_SUPPLY_HEALTH_COLD_LIMIT;
		else
			health = POWER_SUPPLY_HEALTH_COLD;
		break;
	case POWER_SUPPLY_HEALTH_GOOD:
	default:
		if (battery->temperature > battery->temp_high)
			health = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (battery->temperature < battery->temp_low)
			health = POWER_SUPPLY_HEALTH_COLD;
		else
			health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	}

#if defined(CONFIG_BAT_TEMP)
	if (health != POWER_SUPPLY_HEALTH_GOOD && health != battery->health)
		battery->is_temp_control = false;
#endif
	return health;
#endif
}

#if defined(CONFIG_BAT_TEMP)
static void charge_control_by_temp(struct s2m_chg_manager_info *battery)
{
	int ret = 0;
	union power_supply_propval value;
	struct power_supply *psy;

	if (battery->cable_type == POWER_SUPPLY_TYPE_BATTERY ||
	    battery->cable_type == POWER_SUPPLY_TYPE_UNKNOWN ||
	    battery->cable_type == POWER_SUPPLY_TYPE_OTG) {
		pr_info("%s: Not charging. Skip.\n", __func__);
		battery->is_temp_control = false;
		return;
	}

	switch (battery->health) {
	case POWER_SUPPLY_HEALTH_OVERHEAT_LIMIT:
	case POWER_SUPPLY_HEALTH_COLD_LIMIT:
		if (battery->status != POWER_SUPPLY_STATUS_NOT_CHARGING) {
			battery->is_recharging = false;
			/* Take the wakelock during 10 seconds	*
			 * when not_charging status is detected */
			wake_lock_timeout(&battery->vbus_wake_lock, HZ * 10);
			set_battery_status(battery,
					   POWER_SUPPLY_STATUS_NOT_CHARGING);
			battery->is_temp_control = true;
		}
		break;
	case POWER_SUPPLY_HEALTH_OVERHEAT:
		if (battery->is_temp_control == false) {
			/* Reduce float voltage */
			psy = power_supply_get_by_name(battery->pdata->charger_name);
			value.intval = battery->pdata->temp_limit_float_voltage;
			ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_VOLTAGE_MAX,
							&value);
			if (ret < 0)
				pr_err("%s: Fail to execute property\n", __func__);

			/* charger on */
			set_charger_mode(battery, S2M_BAT_CHG_MODE_CHARGING);
			battery->status = POWER_SUPPLY_STATUS_CHARGING;

			/* Reduce charging current */
			set_charging_current(battery, 5);

			battery->is_temp_control = true;
		}
		break;
	case POWER_SUPPLY_HEALTH_COLD:
		if (battery->is_temp_control == false) {
			if (battery->voltage_now >
			    battery->pdata->temp_limit_float_voltage)
				set_charging_current(battery, 2);
			else
				set_charging_current(battery, 5);

		/* charger on */
		set_charger_mode(battery, S2M_BAT_CHG_MODE_CHARGING);
		battery->status = POWER_SUPPLY_STATUS_CHARGING;

		battery->is_temp_control = true;
		} else {
			if (battery->voltage_now >
			    battery->pdata->temp_limit_float_voltage)
				set_charging_current(battery, 2);
			else if (battery->voltage_now <
				(battery->pdata->temp_limit_float_voltage - 100))
				set_charging_current(battery, 5);
		}
		break;
	case POWER_SUPPLY_HEALTH_GOOD:
		if (battery->is_temp_control == true) {
			pr_info("%s: Recover normal charging\n", __func__);
			/* Recover float voltage */
			value.intval = battery->pdata->chg_float_voltage;
			psy = power_supply_get_by_name(battery->pdata->charger_name);
			ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_VOLTAGE_MAX,
							&value);
			if (ret < 0)
				pr_err("%s: Fail to execute property\n", __func__);

			set_bat_status_by_cable(battery);
			battery->is_temp_control = false;
		}
		break;
	default:
		break;
	}

	pr_info("%s: T = %d, health(%s), Charging(%s), is_temp_control(%d)\n",
		__func__, battery->temperature, health_str[battery->health],
		bat_status_str[battery->status], battery->is_temp_control);
}
#endif
static void check_health(struct s2m_chg_manager_info *battery)
{
	int battery_health = get_battery_health(battery);
	int temperature_health = get_temperature_health(battery);

	if (battery_health < 0) {
		pr_err("%s: fail to get battery_health\n", __func__);

		return;
	}

	pr_info("%s: T = %d, bat_health(%s), T_health(%s), Charging(%s)\n",
		__func__, battery->temperature, health_str[battery_health],
		health_str[temperature_health], bat_status_str[battery->status]);

	switch (battery_health) {
	case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
	case POWER_SUPPLY_HEALTH_UNDERVOLTAGE:
	case POWER_SUPPLY_HEALTH_UNKNOWN:
		battery->health = battery_health;

		/* If battery voltage is under/over voltage, turn off charger */
		if (battery->status != POWER_SUPPLY_STATUS_NOT_CHARGING) {
			battery->is_recharging = false;
#if defined(CONFIG_BAT_TEMP)
			battery->is_temp_control = false;
#endif
			/* Take the wakelock during 10 seconds	*
			 * when not_charging status is detected */
			wake_lock_timeout(&battery->vbus_wake_lock, HZ * 10);
			set_battery_status(battery, POWER_SUPPLY_STATUS_NOT_CHARGING);
		}
		pr_info("%s: Battery health is bad!! Skip temp. health check!\n",
			__func__);
		return;
	default:
		break;
	}

	/* If battery health is ok, check temperature health */
	battery->health = temperature_health;
#if defined(CONFIG_BAT_TEMP)
	charge_control_by_temp(battery);
#endif
	/* If battery & temperature both are normal,			 *
	 *	set battery->health GOOD and recover battery->status */
	if (battery_health == POWER_SUPPLY_HEALTH_GOOD &&
	    temperature_health == POWER_SUPPLY_HEALTH_GOOD) {
		battery->health = POWER_SUPPLY_HEALTH_GOOD;
		if (battery->status == POWER_SUPPLY_STATUS_NOT_CHARGING)
			set_bat_status_by_cable(battery);
	}
}

static void check_charging_full(struct s2m_chg_manager_info *battery)
{
	pr_info("%s Start\n", __func__);

	if ((battery->status == POWER_SUPPLY_STATUS_DISCHARGING) ||
	    (battery->status == POWER_SUPPLY_STATUS_NOT_CHARGING)) {
		dev_dbg(battery->dev,
			"%s: No Need to Check Full-Charged\n", __func__);
		return;
	}

	/* 1. Recharging check */
	if (battery->status == POWER_SUPPLY_STATUS_FULL &&
	    battery->voltage_now < battery->pdata->chg_recharge_vcell &&
	    !battery->is_recharging) {
		pr_info("%s: Recharging start\n", __func__);
		set_battery_status(battery, POWER_SUPPLY_STATUS_CHARGING);
		battery->is_recharging = true;
	}

	/* 2. Full charged check */
	if ((battery->current_now >= 0 &&
	     battery->current_now < battery->pdata->charging_current[
	     battery->cable_type].full_check_current) &&
	    (battery->voltage_avg > battery->pdata->chg_full_vcell)) {
		battery->full_check_cnt++;
		pr_info("%s: Full Check Cnt (%d)\n",
			__func__, battery->full_check_cnt);
	} else if (battery->full_check_cnt != 0) {
	/* Reset full check cnt when it is out of full condition */
		battery->full_check_cnt = 0;
		pr_info("%s: Reset Full Check Cnt\n", __func__);
	}

	/* 3. If full charged, turn off charging. */
	if (battery->full_check_cnt >= battery->pdata->full_check_count) {
		battery->full_check_cnt = 0;
		battery->is_recharging = false;
		set_battery_status(battery, POWER_SUPPLY_STATUS_FULL);
		pr_info("%s: Full charged, charger off\n", __func__);
	}
}
#ifdef CONFIG_FAKE_BATT
static void bat_monitor_work(struct work_struct *work)
{
	struct s2m_chg_manager_info *battery = container_of(work,
							    struct s2m_chg_manager_info,
							    monitor_work.work);

	if (battery->monitor_trigger == false) {
		pr_info("%s: monitor_trigger is false", __func__);

		goto fault_trigger;
	}

	pr_info("%s: start monitoring\n", __func__);

	battery->battery_valid = false;

	get_battery_info(battery);

	check_health(battery);

	check_charging_full(battery);

	power_supply_changed(battery->psy_battery);

	pr_err(	"%s: Status(%s), Health(%s), Cable(%d), Recharging(%d))"
		"\n", __func__,
		bat_status_str[battery->status],
		health_str[battery->health],
		battery->cable_type,
		battery->is_recharging);

fault_trigger:
	alarm_cancel(&battery->monitor_alarm);
	alarm_start_relative(&battery->monitor_alarm,
			     ktime_set(battery->monitor_alarm_interval, 0));
	wake_unlock(&battery->monitor_wake_lock);
}
#else
static void bat_monitor_work(struct work_struct *work)
{
	struct s2m_chg_manager_info *battery = container_of(work,
							    struct s2m_chg_manager_info,
							    monitor_work.work);
	union power_supply_propval value;
	struct power_supply *psy;
	int ret;

	if (battery->monitor_trigger == false) {
		pr_info("%s: monitor_trigger is false", __func__);

		goto fault_trigger;
	}

	pr_info("%s: start monitoring\n", __func__);

	psy = power_supply_get_by_name(battery->pdata->charger_name);
	if (!psy)
		return;
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &value);
	if (ret < 0)
		pr_err("%s: Fail to execute property\n", __func__);

	if (!value.intval) {
		battery->battery_valid = false;
		pr_info("%s: There is no battery, skip monitoring.\n", __func__);
		goto continue_monitor;
	} else
		battery->battery_valid = true;

	get_battery_info(battery);

	check_health(battery);

	check_charging_full(battery);

	power_supply_changed(battery->psy_battery);

continue_monitor:
	pr_err(	"%s: Status(%s), Health(%s), Cable(%d), Recharging(%d))"
		"\n", __func__,
		bat_status_str[battery->status],
		health_str[battery->health],
		battery->cable_type,
		battery->is_recharging);

fault_trigger:
	alarm_cancel(&battery->monitor_alarm);
	alarm_start_relative(&battery->monitor_alarm,
			     ktime_set(battery->monitor_alarm_interval, 0));
	wake_unlock(&battery->monitor_wake_lock);
}
#endif

#ifdef CONFIG_OF
static int s2m_chg_manager_parse_dt(struct device *dev,
				    struct s2m_chg_manager_info *battery)
{
	struct device_node *np = of_find_node_by_name(NULL, "battery");
	s2m_chg_manager_platform_data_t *pdata = battery->pdata;
	int ret = 0, len;
	unsigned int i;
	const u32 *p;
	u32 temp;
	u32 default_input_current, default_charging_current, default_full_check_current;

	if (!np) {
		pr_info("%s np NULL(battery)\n", __func__);
		return -1;
	}
	ret = of_property_read_string(np, "battery,vendor",
				      (char const **)&pdata->vendor);
	if (ret)
		pr_info("%s: Vendor is empty\n", __func__);

	ret = of_property_read_string(np, "battery,charger_name",
				      (char const **)&pdata->charger_name);
	if (ret)
		pr_info("%s: Charger name is empty\n", __func__);

#if defined(CONFIG_SMALL_CHARGER)
	ret = of_property_read_string(np, "battery,smallcharger_name",
				      (char const **)&pdata->smallcharger_name);
	if (ret)
		pr_info("%s: Small charger name is empty\n", __func__);
#endif

	ret = of_property_read_string(np, "battery,fuelgauge_name",
				      (char const **)&pdata->fuelgauge_name);
	if (ret)
		pr_info("%s: Fuelgauge name is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,technology",
				   &pdata->technology);
	if (ret)
		pr_info("%s : technology is empty\n", __func__);

	p = of_get_property(np, "battery,input_current_limit", &len);
	if (!p)
		return 1;

	len = len / sizeof(u32);

	if (len < POWER_SUPPLY_TYPE_END)
		len = POWER_SUPPLY_TYPE_END;

	pdata->charging_current = kzalloc(sizeof(s2m_charging_current_t) * len,
					  GFP_KERNEL);

	ret = of_property_read_u32(np, "battery,default_input_current",
				   &default_input_current);
	if (ret)
		pr_info("%s : default_input_current is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,default_charging_current",
				   &default_charging_current);
	if (ret)
		pr_info("%s : default_charging_current is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,default_full_check_current",
				   &default_full_check_current);
	if (ret)
		pr_info("%s : default_full_check_current is empty\n", __func__);

	for (i = 0; i < len; i++) {
		ret = of_property_read_u32_index(np,
						 "battery,input_current_limit", i,
						 &pdata->charging_current[i].input_current_limit);
		if (ret) {
			pr_info("%s : Input_current_limit is empty\n",
				__func__);
			pdata->charging_current[i].input_current_limit  = default_input_current;
		}

		ret = of_property_read_u32_index(np,
						 "battery,fast_charging_current", i,
						 &pdata->charging_current[i].fast_charging_current);
		if (ret) {
			pr_info("%s : Fast charging current is empty\n",
					__func__);
			pdata->charging_current[i].fast_charging_current = default_charging_current;
		}

		ret = of_property_read_u32_index(np,
						 "battery,full_check_current", i,
						 &pdata->charging_current[i].full_check_current);
		if (ret) {
			pr_info("%s : Full check current is empty\n", __func__);
			pdata->charging_current[i].full_check_current = default_full_check_current;
		}
	}

	ret = of_property_read_u32(np, "battery,max_input_current",
				   &pdata->max_input_current);
	if (ret)
		pr_info("%s : max_input_current is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,max_charging_current",
				   &pdata->max_charging_current);
	if (ret)
		pr_info("%s : max_charging_current is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,default_limit_current",
				   &pdata->default_limit_current);
	if (ret) {
		pr_info("%s : default_limit_current is empty\n", __func__);
		pdata->default_limit_current = 1500;
	}

#if defined(CONFIG_SMALL_CHARGER)
	ret = of_property_read_u32(np, "battery,small_limit_current",
				   &pdata->small_limit_current);
	if (ret) {
		pr_info("%s : small_limit_current is empty\n", __func__);
		pdata->small_limit_current = 2000;
	}

	ret = of_property_read_u32(np, "battery,small_input_current",
				   &pdata->small_input_current);
	if (ret) {
		pr_info("%s : small_input_current is empty\n", __func__);
		pdata->small_input_current = 500;
	}

	ret = of_property_read_u32(np, "battery,small_charging_current",
				   &pdata->small_charging_current);
	if (ret) {
		pr_info("%s : small_charging_current is empty\n", __func__);
		pdata->small_charging_current = 800;
	}
#endif

#if defined(CONFIG_USE_CCIC)
	ret = of_property_read_u32(np, "battery,pdo_max_chg_power",
				   &pdata->pdo_max_chg_power);
	if (ret)
		pr_info("%s : pdo_max_chg_power is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,pdo_max_input_vol",
				   &pdata->pdo_max_input_vol);
	if (ret)
		pr_info("%s : pdo_max_input_vol is empty\n", __func__);
#endif
	/* Parse the values for temperature control */
	ret = of_property_read_u32(np, "battery,temp_high_limit", &temp);
	if (ret) {
		pr_info("%s : temp_high_limit is empty\n", __func__);
		pdata->temp_high_limit = 600;
	} else
		pdata->temp_high_limit = (int)temp;

	ret = of_property_read_u32(np, "battery,temp_high_limit_recovery", &temp);
	if (ret) {
		pr_info("%s : temp_high_limit_recovery is empty\n", __func__);
		pdata->temp_high_limit_recovery = 550;
	} else
		pdata->temp_high_limit_recovery = (int)temp;

	ret = of_property_read_u32(np, "battery,temp_high", &temp);
	if (ret) {
		pr_info("%s : temp_high is empty\n", __func__);
		pdata->temp_high = 450;
	} else
		pdata->temp_high = (int)temp;

	ret = of_property_read_u32(np, "battery,temp_high_recovery", &temp);
	if (ret) {
		pr_info("%s : temp_high_recovery is empty\n", __func__);
		pdata->temp_high_recovery = 430;
	} else
		pdata->temp_high_recovery = (int)temp;

	ret = of_property_read_u32(np, "battery,temp_low_limit", &temp);
	if (ret) {
		pr_info("%s : temp_low_limit is empty\n", __func__);
		pdata->temp_low_limit = 0;
	} else
		pdata->temp_low_limit = (int)temp;

	ret = of_property_read_u32(np, "battery,temp_low_limit_recovery", &temp);
	if (ret) {
		pr_info("%s : temp_low_limit_recovery is empty\n", __func__);
		pdata->temp_low_limit_recovery = 20;
	} else
		pdata->temp_low_limit_recovery = (int)temp;

	ret = of_property_read_u32(np, "battery,temp_low", &temp);
	if (ret) {
		pr_info("%s : temp_low is empty\n", __func__);
		pdata->temp_low = 100;
	} else
		pdata->temp_low = (int)temp;

	ret = of_property_read_u32(np, "battery,temp_low_recovery", &temp);
	if (ret) {
		pr_info("%s : temp_low_recovery is empty\n", __func__);
		pdata->temp_low_recovery = 120;
	} else
		pdata->temp_low_recovery = (int)temp;

	pr_info("%s : temp_high(%d), temp_high_recovery(%d)"
		", temp_low(%d), temp_low_recovery(%d)\n",
		__func__,
		pdata->temp_high, pdata->temp_high_recovery,
		pdata->temp_low, pdata->temp_low_recovery);
	pr_info("%s : temp_high_limit(%d), temp_high_limit_recovery(%d),"
		" temp_low_limit(%d), temp_low_limit_recovery(%d)\n",
		__func__,
		pdata->temp_high_limit, pdata->temp_high_limit_recovery,
		pdata->temp_low_limit, pdata->temp_low_limit_recovery);

	ret = of_property_read_u32(np, "battery,temp_limit_float_voltage",
				   &pdata->temp_limit_float_voltage);
	if (ret) {
		pr_info("%s : temp_limit_float_voltage is empty\n", __func__);
		pdata->temp_limit_float_voltage = 4100;
	}
	ret = of_property_read_u32(np, "battery,chg_float_voltage",
				   &pdata->chg_float_voltage);
	if (ret) {
		pr_info("%s : chg_float_voltage is empty\n", __func__);
		pdata->chg_float_voltage = 4200;
	}

	ret = of_property_read_u32(np, "battery,full_check_count",
				   &pdata->full_check_count);
	if (ret)
		pr_info("%s : full_check_count is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,chg_full_vcell",
				   &pdata->chg_full_vcell);
	if (ret)
		pr_info("%s : chg_full_vcell is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,chg_recharge_vcell",
				   &pdata->chg_recharge_vcell);
	if (ret)
		pr_info("%s : chg_recharge_vcell is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,max_rawsoc",
				   &pdata->max_rawsoc);
	if (ret)
		pr_info("%s : max_rawsoc is empty\n", __func__);

	ret = of_property_read_u32(np, "battery,max_rawsoc_offset",
				   &pdata->max_rawsoc_offset);
	if (ret)
		pr_info("%s : max_rawsoc_offset is empty\n", __func__);

	pr_info("%s:DT parsing is done, vendor : %s, technology : %d\n",
		__func__, pdata->vendor, pdata->technology);
	return ret;
}
#else
static int s2m_chg_manager_parse_dt(struct device *dev,
		struct s2m_chg_manager_platform_data *pdata)
{
	return pdev->dev.platform_data;
}
#endif

static const struct of_device_id s2m_chg_manager_match_table[] = {
	{ .compatible = "samsung,s2m-battery",},
	{},
};

static enum alarmtimer_restart bat_monitor_alarm(
	struct alarm *alarm, ktime_t now)
{
	struct s2m_chg_manager_info *battery = container_of(alarm,
				struct s2m_chg_manager_info, monitor_alarm);

	wake_lock(&battery->monitor_wake_lock);
	queue_delayed_work(battery->monitor_wqueue, &battery->monitor_work, 0);

	return ALARMTIMER_NORESTART;
}

static int s2m_chg_manager_probe(struct platform_device *pdev)
{
	struct s2m_chg_manager_info *battery;
	struct power_supply_config psy_cfg = {};
	union power_supply_propval value;
	int ret = 0, temp = 0;
	struct power_supply *psy;
#ifndef CONFIG_OF
	int i;
#endif
	pr_info("%s: S2M battery driver loading\n", __func__);

	/* Allocate necessary device data structures */
	battery = kzalloc(sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;

	pr_info("%s: battery is allocated\n", __func__);

	battery->pdata = devm_kzalloc(&pdev->dev, sizeof(*(battery->pdata)),
			GFP_KERNEL);
	if (!battery->pdata) {
		ret = -ENOMEM;
		goto err_bat_free;
	}

	pr_info("%s: pdata is allocated\n", __func__);

	/* Get device/board dependent configuration data from DT */
	temp = s2m_chg_manager_parse_dt(&pdev->dev, battery);
	if (temp) {
		pr_info("%s: s2m_chg_manager_parse_dt(&pdev->dev, battery) == %d\n", __func__, temp);
		dev_err(&pdev->dev, "%s: Failed to get battery dt\n", __func__);
		ret = -EINVAL;
		goto err_parse_dt_nomem;
	}

	pr_info("%s: DT parsing is done\n", __func__);

	/* Set driver data */
	platform_set_drvdata(pdev, battery);
	battery->dev = &pdev->dev;

	mutex_init(&battery->iolock);
	mutex_init(&battery->ifconn_lock);

	wake_lock_init(&battery->monitor_wake_lock, WAKE_LOCK_SUSPEND,
			"sec-battery-monitor");
	wake_lock_init(&battery->vbus_wake_lock, WAKE_LOCK_SUSPEND,
			"sec-battery-vbus");

	pr_info("%s %d\n", __func__, __LINE__);
	/* Inintialization of battery information */
	battery->status = POWER_SUPPLY_STATUS_DISCHARGING;
	battery->health = POWER_SUPPLY_HEALTH_GOOD;

	battery->input_current = 0;
	battery->charging_current = 0;
	battery->topoff_current = 0;
#if defined(CONFIG_SMALL_CHARGER)
	battery->small_input_flag = 0;
	battery->small_limit_current = battery->pdata->small_limit_current;
#endif
	battery->default_limit_current = battery->pdata->default_limit_current;
	battery->max_input_current = battery->pdata->max_input_current;
	battery->max_charging_current = battery->pdata->max_charging_current;
#if defined(CONFIG_USE_CCIC)
	battery->pdo_max_input_vol = battery->pdata->pdo_max_input_vol;
	battery->pdo_max_chg_power = battery->pdata->pdo_max_chg_power;
	battery->pd_input_current = 2000;
	battery->pd_attach = false;
	battery->rp_attach = false;
#endif
	/* temperature */
	battery->temp_high_limit = battery->pdata->temp_high_limit;
	battery->temp_high_limit_recovery = battery->pdata->temp_high_limit_recovery;
	battery->temp_high = battery->pdata->temp_high;
	battery->temp_high_recovery = battery->pdata->temp_high_recovery;
	battery->temp_low_limit = battery->pdata->temp_low_limit;
	battery->temp_low_limit_recovery = battery->pdata->temp_low_limit_recovery;
	battery->temp_low = battery->pdata->temp_low;
	battery->temp_low_recovery = battery->pdata->temp_low_recovery;

	battery->max_rawsoc = battery->pdata->max_rawsoc;
	battery->max_rawsoc_offset = battery->pdata->max_rawsoc_offset;

	battery->is_recharging = false;
	battery->cable_type = POWER_SUPPLY_TYPE_BATTERY;

	pr_info("%s %d\n", __func__, __LINE__);
	psy = power_supply_get_by_name(battery->pdata->charger_name);
	if (!psy)
		pr_info("%s: there's no charger driver\n", __func__);
	else {
#ifdef CONFIG_FAKE_BATT
		battery->battery_valid = false;
#else
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);

		if (!value.intval)
			battery->battery_valid = false;
		else
			battery->battery_valid = true;
#endif
	}
	/* Register battery as "POWER_SUPPLY_TYPE_BATTERY" */
	battery->psy_battery_desc.name = "battery";
	battery->psy_battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	battery->psy_battery_desc.get_property =  s2m_chg_manager_get_property;
	battery->psy_battery_desc.set_property =  s2m_chg_manager_set_property;
	battery->psy_battery_desc.properties = s2m_chg_manager_props;
	battery->psy_battery_desc.num_properties =  ARRAY_SIZE(s2m_chg_manager_props);

	battery->psy_usb_desc.name = "usb";
	battery->psy_usb_desc.type = POWER_SUPPLY_TYPE_USB;
	battery->psy_usb_desc.get_property = s2m_usb_get_property;
	battery->psy_usb_desc.properties = s2m_power_props;
	battery->psy_usb_desc.num_properties = ARRAY_SIZE(s2m_power_props);

	battery->psy_ac_desc.name = "ac";
	battery->psy_ac_desc.type = POWER_SUPPLY_TYPE_MAINS;
	battery->psy_ac_desc.properties = s2m_power_props;
	battery->psy_ac_desc.num_properties = ARRAY_SIZE(s2m_power_props);
	battery->psy_ac_desc.get_property = s2m_ac_get_property;

	/* Initialize work queue for periodic polling thread */
	battery->monitor_wqueue =
		create_singlethread_workqueue(dev_name(&pdev->dev));
	if (!battery->monitor_wqueue) {
		dev_err(battery->dev,
				"%s: Fail to Create Workqueue\n", __func__);
		goto err_irr;
	}

	/* Init work & alarm for monitoring */
	INIT_DELAYED_WORK(&battery->monitor_work, bat_monitor_work);
	alarm_init(&battery->monitor_alarm, ALARM_BOOTTIME, bat_monitor_alarm);
	battery->monitor_alarm_interval = DEFAULT_ALARM_INTERVAL;
	battery->monitor_trigger = true;

#if defined(CONFIG_USE_CCIC)
	INIT_DELAYED_WORK(&battery->select_pdo_work, usbpd_select_pdo_work);
#endif
	/* Register power supply to framework */
	psy_cfg.drv_data = battery;
	psy_cfg.supplied_to = s2m_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(s2m_supplied_to);

	battery->psy_battery = power_supply_register(&pdev->dev, &battery->psy_battery_desc, &psy_cfg);
	if (IS_ERR(battery->psy_battery)) {
		pr_err("%s: Failed to Register psy_battery\n", __func__);
		ret = PTR_ERR(battery->psy_battery);
		goto err_workqueue;
	}
	pr_info("%s: Registered battery as power supply\n", __func__);

	battery->psy_usb = power_supply_register(&pdev->dev, &battery->psy_usb_desc, &psy_cfg);
	if (IS_ERR(battery->psy_usb)) {
		pr_err("%s: Failed to Register psy_usb\n", __func__);
		ret = PTR_ERR(battery->psy_usb);
		goto err_unreg_battery;
	}
	pr_info("%s: Registered USB as power supply\n", __func__);

	battery->psy_ac = power_supply_register(&pdev->dev, &battery->psy_ac_desc, &psy_cfg);
	if (IS_ERR(battery->psy_ac)) {
		pr_err("%s: Failed to Register psy_ac\n", __func__);
		ret = PTR_ERR(battery->psy_ac);
		goto err_unreg_usb;
	}
	pr_info("%s: Registered AC as power supply\n", __func__);

	/* Initialize battery level*/
	value.intval = 0;

	psy = power_supply_get_by_name(battery->pdata->fuelgauge_name);
	if (!psy)
		pr_info("%s: there's no fuelgauge driver\n", __func__);
	else {
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);
		battery->capacity = value.intval / 10;
	}

	/* Set float voltage for charger */
	psy = power_supply_get_by_name(battery->pdata->charger_name);
	if (!psy)
		pr_info("%s: there's no charger driver\n", __func__);
	else {
		value.intval = battery->pdata->chg_float_voltage;
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_VOLTAGE_MAX, &value);
		if (ret < 0)
			pr_err("%s: Fail to execute property\n", __func__);
	}
#if defined(CONFIG_BAT_TEMP)
	battery->is_temp_control = false;
#endif
#if defined(CONFIG_IFCONN_NOTIFIER)
	ifconn_notifier_register(&battery->ifconn_nb,
				 s2m_ifconn_handle_notification,
				 IFCONN_NOTIFY_BATTERY,
				 IFCONN_NOTIFY_MANAGER);
#if defined(CONFIG_USE_CCIC)
	ifconn_notifier_register(&battery->ifconn_cc_nb,
				 s2m_ifconn_handle_cc_notification,
				 IFCONN_NOTIFY_BATTERY,
				 IFCONN_NOTIFY_CCIC);
#endif
#endif

	/* Kick off monitoring thread */
	pr_info("%s: start battery monitoring work\n", __func__);
	queue_delayed_work(battery->monitor_wqueue, &battery->monitor_work, 5*HZ);

	dev_info(battery->dev, "%s: Battery driver is loaded\n", __func__);
	return 0;

err_unreg_usb:
	power_supply_unregister(battery->psy_usb);
err_unreg_battery:
	power_supply_unregister(battery->psy_battery);
err_workqueue:
	destroy_workqueue(battery->monitor_wqueue);
err_irr:
	wake_lock_destroy(&battery->monitor_wake_lock);
	wake_lock_destroy(&battery->vbus_wake_lock);
	mutex_destroy(&battery->iolock);
	mutex_destroy(&battery->ifconn_lock);
err_parse_dt_nomem:
	kfree(battery->pdata);
err_bat_free:
	kfree(battery);

	return ret;
}

static int s2m_chg_manager_remove(struct platform_device *pdev)
{
	return 0;
}

#if defined CONFIG_PM
static int s2m_chg_manager_prepare(struct device *dev)
{
	struct s2m_chg_manager_info *battery = dev_get_drvdata(dev);

	battery->monitor_trigger = false;

	alarm_cancel(&battery->monitor_alarm);
	cancel_delayed_work_sync(&battery->monitor_work);
	wake_unlock(&battery->monitor_wake_lock);
	/* If charger is connected, monitoring is required*/
	if (battery->cable_type != POWER_SUPPLY_TYPE_BATTERY) {
		battery->monitor_alarm_interval = SLEEP_ALARM_INTERVAL;
		pr_info("%s: Increase battery monitoring interval -> %d\n",
				__func__, battery->monitor_alarm_interval);
		alarm_start_relative(&battery->monitor_alarm,
				ktime_set(battery->monitor_alarm_interval, 0));
	}

	return 0;
}

static int s2m_chg_manager_suspend(struct device *dev)
{
	return 0;
}

static int s2m_chg_manager_resume(struct device *dev)
{
	return 0;
}

static void s2m_chg_manager_complete(struct device *dev)
{
	struct s2m_chg_manager_info *battery = dev_get_drvdata(dev);

	if (battery->monitor_alarm_interval != DEFAULT_ALARM_INTERVAL) {
		battery->monitor_alarm_interval = DEFAULT_ALARM_INTERVAL;
		pr_info("%s: Recover battery monitoring interval -> %d\n",
			__func__, battery->monitor_alarm_interval);
	}
	alarm_cancel(&battery->monitor_alarm);
	wake_lock(&battery->monitor_wake_lock);
	queue_delayed_work(battery->monitor_wqueue, &battery->monitor_work, 0);

	battery->monitor_trigger = true;
}

#else
#define s2m_chg_manager_prepare NULL
#define s2m_chg_manager_suspend NULL
#define s2m_chg_manager_resume NULL
#define s2m_chg_manager_complete NULL
#endif

static const struct dev_pm_ops s2m_chg_manager_pm_ops = {
	.prepare = s2m_chg_manager_prepare,
	.suspend = s2m_chg_manager_suspend,
	.resume = s2m_chg_manager_resume,
	.complete = s2m_chg_manager_complete,
};

static struct platform_driver s2m_chg_manager_driver = {
	.driver         = {
		.name   = "s2m-battery",
		.owner  = THIS_MODULE,
		.pm     = &s2m_chg_manager_pm_ops,
		.of_match_table = s2m_chg_manager_match_table,
	},
	.probe		= s2m_chg_manager_probe,
	.remove		= s2m_chg_manager_remove,
};

static int __init s2m_chg_manager_init(void)
{
	int ret = 0;

	pr_info("%s %d\n", __func__, __LINE__);
	ret = platform_driver_register(&s2m_chg_manager_driver);
	return ret;
}
late_initcall(s2m_chg_manager_init);

static void __exit s2m_chg_manager_exit(void)
{
	platform_driver_unregister(&s2m_chg_manager_driver);
}
module_exit(s2m_chg_manager_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Samsung Electronics");
MODULE_DESCRIPTION("Battery manager driver for S2M");
