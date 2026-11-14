/*
 * nikel: empty sensor slot.
 *
 * The MediaTek camera HAL selects kernel sensor drivers by their position in
 * kdSensorList, using the order of its own sensor list (SHIFT's
 * libcameracustom: IMX258, S5K3L8, S5K3L8_ZK, S5K3P9SX). Positions nikel
 * has no sensor for are filled with this driver, which never finds a
 * sensor, so the HAL's search moves on. See nikel_sensorlist.h.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"

static UINT32 slot_open(void)
{
	return ERROR_SENSOR_CONNECT_FAIL;
}

static UINT32 slot_get_info(MSDK_SCENARIO_ID_ENUM scenario,
			    MSDK_SENSOR_INFO_STRUCT *info,
			    MSDK_SENSOR_CONFIG_STRUCT *config)
{
	return ERROR_SENSOR_CONNECT_FAIL;
}

static UINT32 slot_get_resolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT *res)
{
	return ERROR_SENSOR_CONNECT_FAIL;
}

static UINT32 slot_feature_control(MSDK_SENSOR_FEATURE_ENUM id, UINT8 *para,
				   UINT32 *len)
{
	if (id == SENSOR_FEATURE_CHECK_SENSOR_ID && para)
		*(UINT32 *)para = 0xFFFFFFFF;
	return ERROR_SENSOR_CONNECT_FAIL;
}

static UINT32 slot_control(MSDK_SCENARIO_ID_ENUM scenario,
			   MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *window,
			   MSDK_SENSOR_CONFIG_STRUCT *config)
{
	return ERROR_SENSOR_CONNECT_FAIL;
}

static UINT32 slot_close(void)
{
	return ERROR_NONE;
}

static SENSOR_FUNCTION_STRUCT slot_func = {
	slot_open,
	slot_get_info,
	slot_get_resolution,
	slot_feature_control,
	slot_control,
	slot_close
};

UINT32 NIKEL_SLOT_SensorInit(PSENSOR_FUNCTION_STRUCT *pfFunc)
{
	if (pfFunc)
		*pfFunc = &slot_func;
	return ERROR_NONE;
}
