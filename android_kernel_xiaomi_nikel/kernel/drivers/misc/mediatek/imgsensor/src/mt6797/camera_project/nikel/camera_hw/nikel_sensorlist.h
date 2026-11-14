/*
 * nikel: kernel sensor list, in the positions SHIFT's camera HAL expects
 * (its libcameracustom SensorList: 0 IMX258, 1 S5K3L8, 2 S5K3L8_ZK,
 * 3 S5K3P9SX). Included by kd_sensorlist.h when NIKEL_SENSOR_LIST is set.
 *
 *   0  empty            (HAL: IMX258)
 *   1  S5K3L8, main     (HAL: S5K3L8, same sensor, SHIFT's tuning)
 *   2  empty            (HAL: S5K3L8_ZK)
 *   3  S5K5E8YX, front  (HAL: S5K3P9SX; borrows SHIFT's front tuning)
 */
/* NIKEL_SLOT_SensorInit() is declared in kd_sensorlist.h (patch 0013) */
	{0xFFFF, "nikel_slot_0", NIKEL_SLOT_SensorInit},
	{S5K3L8_SENSOR_ID, SENSOR_DRVNAME_S5K3L8_MIPI_RAW, S5K3L8_MIPI_RAW_SensorInit},
	{0xFFFF, "nikel_slot_2", NIKEL_SLOT_SensorInit},
	{S5K5E8YX_SENSOR_ID, SENSOR_DRVNAME_S5K5E8YX_MIPI_RAW, S5K5E8YX_MIPI_RAW_SensorInit},
