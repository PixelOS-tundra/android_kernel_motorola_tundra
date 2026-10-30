// SPDX-License-Identifier: GPL-2.0
/*
 *  of-thermal.c - Generic Thermal Management device tree support.
 *
 *  Copyright (C) 2013 Texas Instruments
 *  Copyright (C) 2013 Eduardo Valentin <eduardo.valentin@ti.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/export.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/string.h>

#include "thermal_core.h"

/**
 * of_thermal_get_ntrips - function to export number of available trip
 *			   points.
 * @tz: pointer to a thermal zone
 *
 * This function is a globally visible wrapper to get number of trip points
 * stored in the local struct __thermal_zone
 *
 * Return: number of available trip points, -ENODEV when data not available
 */
int of_thermal_get_ntrips(struct thermal_zone_device *tz)
{
	return tz->num_trips;
}
EXPORT_SYMBOL_GPL(of_thermal_get_ntrips);

/**
 * of_thermal_is_trip_valid - function to check if trip point is valid
 *
 * @tz:	pointer to a thermal zone
 * @trip:	trip point to evaluate
 *
 * This function is responsible for checking if passed trip point is valid
 *
 * Return: true if trip point is valid, false otherwise
 */
bool of_thermal_is_trip_valid(struct thermal_zone_device *tz, int trip)
{
	if (trip >= tz->num_trips || trip < 0)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(of_thermal_is_trip_valid);

/**
 * of_thermal_get_trip_points - function to get access to a globally exported
 *				trip points
 *
 * @tz:	pointer to a thermal zone
 *
 * This function provides a pointer to trip points table
 *
 * Return: pointer to trip points table, NULL otherwise
 */
const struct thermal_trip *
of_thermal_get_trip_points(struct thermal_zone_device *tz)
{
	return tz->trips;
}
EXPORT_SYMBOL_GPL(of_thermal_get_trip_points);

static int of_thermal_get_trip_type(struct thermal_zone_device *tz, int trip,
				    enum thermal_trip_type *type)
{
	if (trip >= tz->num_trips || trip < 0)
		return -EDOM;

	*type = tz->trips[trip].type;

	return 0;
}

static int of_thermal_get_trip_temp(struct thermal_zone_device *tz, int trip,
				    int *temp)
{
	if (trip >= tz->num_trips || trip < 0)
		return -EDOM;

	*temp = tz->trips[trip].temperature;

	return 0;
}

static int of_thermal_get_trip_hyst(struct thermal_zone_device *tz, int trip,
				    int *hyst)
{
	if (trip >= tz->num_trips || trip < 0)
		return -EDOM;

	*hyst = tz->trips[trip].hysteresis;

	return 0;
}

static int of_thermal_set_trip_hyst(struct thermal_zone_device *tz, int trip,
				    int hyst)
{
	if (trip >= tz->num_trips || trip < 0)
		return -EDOM;

	/* thermal framework should take care of data->mask & (1 << trip) */
	tz->trips[trip].hysteresis = hyst;

	return 0;
}

static int of_thermal_get_crit_temp(struct thermal_zone_device *tz,
				    int *temp)
{
	int i;

	for (i = 0; i < tz->num_trips; i++)
		if (tz->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = tz->trips[i].temperature;
			return 0;
		}

	return -EINVAL;
}

/***   functions parsing device tree nodes   ***/

static int of_find_trip_id(struct device_node *np, struct device_node *trip)
{
	struct device_node *trips;
	struct device_node *t;
	int i = 0;

	trips = of_get_child_by_name(np, "trips");
	if (!trips) {
		pr_err("Failed to find 'trips' node\n");
		return -EINVAL;
	}

	/*
	 * Find the trip id point associated with the cooling device map
	 */
	for_each_child_of_node(trips, t) {

		if (t == trip) {
			of_node_put(t);
			goto out;
		}
		i++;
	}

	i = -ENXIO;
out:
	of_node_put(trips);

	return i;
}

/*
 * It maps 'enum thermal_trip_type' found in include/linux/thermal.h
 * into the device tree binding of 'trip', property type.
 */
static const char * const trip_types[] = {
	[THERMAL_TRIP_ACTIVE]	= "active",
	[THERMAL_TRIP_PASSIVE]	= "passive",
	[THERMAL_TRIP_HOT]	= "hot",
	[THERMAL_TRIP_CRITICAL]	= "critical",
};

/**
 * thermal_of_get_trip_type - Get phy mode for given device_node
 * @np:	Pointer to the given device_node
 * @type: Pointer to resulting trip type
 *
 * The function gets trip type string from property 'type',
 * and store its index in trip_types table in @type,
 *
 * Return: 0 on success, or errno in error case.
 */
static int thermal_of_get_trip_type(struct device_node *np,
				    enum thermal_trip_type *type)
{
	const char *t;
	int err, i;

	err = of_property_read_string(np, "type", &t);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(trip_types); i++)
		if (!strcasecmp(t, trip_types[i])) {
			*type = i;
			return 0;
		}

	return -ENODEV;
}

static int thermal_of_populate_trip(struct device_node *np,
				    struct thermal_trip *trip)
{
	int prop;
	int ret;

	ret = of_property_read_u32(np, "temperature", &prop);
	if (ret < 0) {
		pr_err("missing temperature property\n");
		return ret;
	}
	trip->temperature = prop;

	ret = of_property_read_u32(np, "hysteresis", &prop);
	if (ret < 0) {
		pr_err("missing hysteresis property\n");
		return ret;
	}
	trip->hysteresis = prop;

	ret = thermal_of_get_trip_type(np, &trip->type);
	if (ret < 0) {
		pr_err("wrong trip type property\n");
		return ret;
	}

	return 0;
}

static struct thermal_trip *thermal_of_trips_init(struct device_node *np, int *ntrips)
{
	struct thermal_trip *tt;
	struct device_node *trips;
	int ret, count;

	*ntrips = 0;
	
	trips = of_get_child_by_name(np, "trips");
	if (!trips)
		return NULL;

	count = of_get_child_count(trips);
	if (!count)
		return NULL;

	tt = kzalloc(sizeof(*tt) * count, GFP_KERNEL);
	if (!tt) {
		ret = -ENOMEM;
		goto out_of_node_put;
	}

	*ntrips = count;

	count = 0;
	for_each_child_of_node_scoped(trips, trip) {
		ret = thermal_of_populate_trip(trip, &tt[count++]);
		if (ret)
			goto out_kfree;
	}

	of_node_put(trips);

	return tt;

out_kfree:
	kfree(tt);
out_of_node_put:
	of_node_put(trips);

	return ERR_PTR(ret);
}

/*
 * Find the next thermal zone after @from (NULL: the first one) that uses the
 * couple sensor/id. QTI device trees point several zones at one sensor, e.g.
 * a "-usr" zone for userspace and a "-step" zone for in-kernel mitigation.
 */
static struct device_node *of_thermal_zone_find_from(struct device_node *sensor,
						     int id,
						     struct device_node *from)
{
	struct device_node *np, *tz;
	struct of_phandle_args sensor_specs;
	bool skip = from;

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np) {
		pr_debug("No thermal zones description\n");
		return ERR_PTR(-ENODEV);
	}

	/*
	 * Search for each thermal zone, a defined sensor
	 * corresponding to the one passed as parameter
	 */
	for_each_available_child_of_node_scoped(np, child) {

		int count, i;

		if (skip) {
			if (child == from)
				skip = false;
			continue;
		}

		count = of_count_phandle_with_args(child, "thermal-sensors",
						   "#thermal-sensor-cells");
		if (count <= 0) {
			pr_err("%pOFn: missing thermal sensor\n", child);
			tz = ERR_PTR(-EINVAL);
			goto out;
		}

		for (i = 0; i < count; i++) {

			int ret;

			ret = of_parse_phandle_with_args(child, "thermal-sensors",
							 "#thermal-sensor-cells",
							 i, &sensor_specs);
			if (ret < 0) {
				pr_err("%pOFn: Failed to read thermal-sensors cells: %d\n", child, ret);
				tz = ERR_PTR(ret);
				goto out;
			}

			of_node_put(sensor_specs.np);
			if ((sensor == sensor_specs.np) && id == (sensor_specs.args_count ?
								  sensor_specs.args[0] : 0)) {
				pr_debug("sensor %pOFn id=%d belongs to %pOFn\n", sensor, id, child);
				tz = no_free_ptr(child);
				goto out;
			}
		}
	}
	tz = ERR_PTR(-ENODEV);
out:
	of_node_put(np);
	return tz;
}

static struct device_node *of_thermal_zone_find(struct device_node *sensor, int id)
{
	return of_thermal_zone_find_from(sensor, id, NULL);
}

static int thermal_of_monitor_init(struct device_node *np, int *delay, int *pdelay)
{
	int ret;

	ret = of_property_read_u32(np, "polling-delay-passive", pdelay);
	if (ret == -EINVAL) {
		*pdelay = 0;
	} else if (ret < 0) {
		pr_err("%pOFn: Couldn't get polling-delay-passive: %d\n", np, ret);
		return ret;
	}

	ret = of_property_read_u32(np, "polling-delay", delay);
	if (ret == -EINVAL) {
		*delay = 0;
	} else if (ret < 0) {
		pr_err("%pOFn: Couldn't get polling-delay: %d\n", np, ret);
		return ret;
	}

	return 0;
}

static struct thermal_zone_params *thermal_of_parameters_init(struct device_node *np)
{
	struct thermal_zone_params *tzp;
	int coef[2];
	int ncoef = ARRAY_SIZE(coef);
	const char *governor;
	int prop, ret;

	tzp = kzalloc(sizeof(*tzp), GFP_KERNEL);
	if (!tzp)
		return ERR_PTR(-ENOMEM);

	tzp->no_hwmon = true;

	if (!of_property_read_u32(np, "sustainable-power", &prop))
		tzp->sustainable_power = prop;

	/*
	 * For now, the thermal framework supports only one sensor per
	 * thermal zone. Thus, we are considering only the first two
	 * values as slope and offset.
	 */
	ret = of_property_read_u32_array(np, "coefficients", coef, ncoef);
	if (ret) {
		coef[0] = 1;
		coef[1] = 0;
	}

	tzp->slope = coef[0];
	tzp->offset = coef[1];

	if (!of_property_read_string(np, "thermal-governor", &governor))
		strscpy(tzp->governor_name, governor, THERMAL_NAME_LENGTH);

	return tzp;
}

static struct device_node *thermal_of_zone_get_by_name(struct thermal_zone_device *tz)
{
	struct device_node *np, *tz_np;

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np)
		return ERR_PTR(-ENODEV);

	tz_np = of_get_child_by_name(np, tz->type);

	of_node_put(np);

	if (!tz_np)
		return ERR_PTR(-ENODEV);

	return tz_np;
}

static int __thermal_of_unbind(struct device_node *map_np, int index, int trip_id,
			       struct thermal_zone_device *tz, struct thermal_cooling_device *cdev)
{
	struct of_phandle_args cooling_spec;
	int ret;

	ret = of_parse_phandle_with_args(map_np, "cooling-device", "#cooling-cells",
					 index, &cooling_spec);

	if (ret < 0) {
		pr_err("Invalid cooling-device entry\n");
		return ret;
	}

	of_node_put(cooling_spec.np);

	if (cooling_spec.args_count < 2) {
		pr_err("wrong reference to cooling device, missing limits\n");
		return -EINVAL;
	}

	if (cooling_spec.np != cdev->np)
		return 0;

	ret = thermal_zone_unbind_cooling_device(tz, trip_id, cdev);
	if (ret)
		pr_err("Failed to unbind '%s' with '%s': %d\n", tz->type, cdev->type, ret);

	return ret;
}

static int __thermal_of_bind(struct device_node *map_np, int index, int trip_id,
			     struct thermal_zone_device *tz, struct thermal_cooling_device *cdev)
{
	struct of_phandle_args cooling_spec;
	int ret, weight = THERMAL_WEIGHT_DEFAULT;

	of_property_read_u32(map_np, "contribution", &weight);

	ret = of_parse_phandle_with_args(map_np, "cooling-device", "#cooling-cells",
					 index, &cooling_spec);

	if (ret < 0) {
		pr_err("Invalid cooling-device entry\n");
		return ret;
	}

	of_node_put(cooling_spec.np);

	if (cooling_spec.args_count < 2) {
		pr_err("wrong reference to cooling device, missing limits\n");
		return -EINVAL;
	}

	if (cooling_spec.np != cdev->np)
		return 0;

	ret = thermal_zone_bind_cooling_device(tz, trip_id, cdev, cooling_spec.args[1],
					       cooling_spec.args[0],
					       weight);
	if (ret)
		pr_err("Failed to bind '%s' with '%s': %d\n", tz->type, cdev->type, ret);

	return ret;
}

static int thermal_of_for_each_cooling_device(struct device_node *tz_np, struct device_node *map_np,
					      struct thermal_zone_device *tz, struct thermal_cooling_device *cdev,
					      int (*action)(struct device_node *, int, int,
							    struct thermal_zone_device *, struct thermal_cooling_device *))
{
	struct device_node *tr_np;
	int count, i, trip_id;

	tr_np = of_parse_phandle(map_np, "trip", 0);
	if (!tr_np)
		return -ENODEV;

	trip_id = of_find_trip_id(tz_np, tr_np);
	if (trip_id < 0)
		return trip_id;

	count = of_count_phandle_with_args(map_np, "cooling-device", "#cooling-cells");
	if (count <= 0) {
		pr_err("Add a cooling_device property with at least one device\n");
		return -ENOENT;
	}

	/*
	 * At this point, we don't want to bail out when there is an
	 * error, we will try to bind/unbind as many as possible
	 * cooling devices
	 */
	for (i = 0; i < count; i++)
		action(map_np, i, trip_id, tz, cdev);

	return 0;
}

static int thermal_of_for_each_cooling_maps(struct thermal_zone_device *tz,
					    struct thermal_cooling_device *cdev,
					    int (*action)(struct device_node *, int, int,
							  struct thermal_zone_device *, struct thermal_cooling_device *))
{
	struct device_node *tz_np, *cm_np, *child;
	int ret = 0;

	tz_np = thermal_of_zone_get_by_name(tz);
	if (IS_ERR(tz_np)) {
		pr_err("Failed to get node tz by name\n");
		return PTR_ERR(tz_np);
	}

	cm_np = of_get_child_by_name(tz_np, "cooling-maps");
	if (!cm_np)
		goto out;

	for_each_child_of_node(cm_np, child) {
		ret = thermal_of_for_each_cooling_device(tz_np, child, tz, cdev, action);
		if (ret) {
			of_node_put(child);
			break;
		}
	}

	of_node_put(cm_np);
out:
	of_node_put(tz_np);

	return ret;
}

static int thermal_of_bind(struct thermal_zone_device *tz,
			   struct thermal_cooling_device *cdev)
{
	return thermal_of_for_each_cooling_maps(tz, cdev, __thermal_of_bind);
}

static int thermal_of_unbind(struct thermal_zone_device *tz,
			     struct thermal_cooling_device *cdev)
{
	return thermal_of_for_each_cooling_maps(tz, cdev, __thermal_of_unbind);
}

/*
 * Zones sharing one sensor: the sensor has a single threshold window, so it
 * is programmed with the intersection of the windows of all enabled zones,
 * and a sensor event updates every zone of the group.
 */
struct thermal_of_group {
	struct mutex lock;
	int num_tz;
	struct thermal_zone_device **tz;
	int *low;
	int *high;
};

static struct thermal_of_group *thermal_of_group_alloc(int count)
{
	struct thermal_of_group *group;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return NULL;

	group->tz = kcalloc(count, sizeof(*group->tz), GFP_KERNEL);
	group->low = kcalloc(count, sizeof(*group->low), GFP_KERNEL);
	group->high = kcalloc(count, sizeof(*group->high), GFP_KERNEL);
	if (!group->tz || !group->low || !group->high) {
		kfree(group->tz);
		kfree(group->low);
		kfree(group->high);
		kfree(group);
		return NULL;
	}
	mutex_init(&group->lock);

	return group;
}

static void thermal_of_group_free(struct thermal_of_group *group)
{
	if (!group)
		return;

	kfree(group->tz);
	kfree(group->low);
	kfree(group->high);
	kfree(group);
}

struct thermal_of_ops {
	struct thermal_zone_device_ops ops;	/* must be first, kfree()d */
	struct thermal_of_group *group;
	int (*set_trips)(struct thermal_zone_device *tz, int low, int high);
};

static int thermal_of_group_set_trips(struct thermal_zone_device *tz,
				      int low, int high)
{
	struct thermal_of_ops *of_ops = container_of(tz->ops,
						     struct thermal_of_ops, ops);
	struct thermal_of_group *group = of_ops->group;
	int agg_low = low, agg_high = high;
	int i, ret;

	if (!group)
		return of_ops->set_trips(tz, low, high);

	mutex_lock(&group->lock);
	for (i = 0; i < group->num_tz; i++) {
		if (group->tz[i] == tz) {
			group->low[i] = low;
			group->high[i] = high;
			continue;
		}
		if (READ_ONCE(group->tz[i]->mode) != THERMAL_DEVICE_ENABLED)
			continue;
		agg_low = max(agg_low, group->low[i]);
		agg_high = min(agg_high, group->high[i]);
	}
	ret = of_ops->set_trips(tz, agg_low, agg_high);
	mutex_unlock(&group->lock);

	return ret;
}

void thermal_of_group_update(struct thermal_zone_device *tz,
			     enum thermal_notify_event event,
			     void (*update)(struct thermal_zone_device *,
					    enum thermal_notify_event))
{
	struct thermal_of_group *group = tz->of_group;
	int i;

	for (i = 0; i < group->num_tz; i++)
		if (group->tz[i] != tz)
			update(group->tz[i], event);
}

static void __thermal_of_zone_unregister(struct thermal_zone_device *tz)
{
	struct thermal_trip *trips = tz->trips;
	struct thermal_zone_params *tzp = tz->tzp;
	struct thermal_zone_device_ops *ops = tz->ops;

	thermal_zone_device_disable(tz);
	thermal_zone_device_unregister(tz);
	kfree(trips);
	kfree(tzp);
	kfree(ops);
}

/**
 * thermal_of_zone_unregister - Cleanup the specific allocated ressources
 *
 * This function disables the thermal zone and frees the different
 * ressources allocated specific to the thermal OF. Zones registered for
 * the same sensor along with @tz are unregistered too.
 *
 * @tz: a pointer to the thermal zone structure
 */
void thermal_of_zone_unregister(struct thermal_zone_device *tz)
{
	struct thermal_of_group *group = tz->of_group;
	int i;

	if (group) {
		for (i = 0; i < group->num_tz; i++) {
			struct thermal_zone_device *t = group->tz[i];

			t->of_group = NULL;
			container_of(t->ops, struct thermal_of_ops, ops)->group = NULL;
		}
		for (i = 0; i < group->num_tz; i++)
			if (group->tz[i] != tz)
				__thermal_of_zone_unregister(group->tz[i]);
		thermal_of_group_free(group);
	}

	__thermal_of_zone_unregister(tz);
}
EXPORT_SYMBOL_GPL(thermal_of_zone_unregister);

static struct thermal_zone_device *
thermal_of_zone_register_np(struct device_node *np, struct device_node *sensor,
			    int id, void *data,
			    const struct thermal_zone_device_ops *ops,
			    struct thermal_of_group *group)
{
	struct thermal_zone_device *tz;
	struct thermal_trip *trips;
	struct thermal_zone_params *tzp;
	struct thermal_of_ops *of_ops;
	int delay, pdelay;
	int ntrips, mask;
	int ret;

	of_ops = kzalloc(sizeof(*of_ops), GFP_KERNEL);
	if (!of_ops)
		return ERR_PTR(-ENOMEM);
	of_ops->ops = *ops;

	trips = thermal_of_trips_init(np, &ntrips);
	if (IS_ERR(trips)) {
		pr_err("Failed to parse trip points for %pOFn id=%d\n", sensor, id);
		ret = PTR_ERR(trips);
		goto out_kfree_of_ops;
	}

	if (!trips)
		pr_info("No trip points found for %pOFn id=%d\n", sensor, id);

	ret = thermal_of_monitor_init(np, &delay, &pdelay);
	if (ret) {
		pr_err("Failed to initialize monitoring delays from %pOFn\n", np);
		goto out_kfree_trips;
	}

	tzp = thermal_of_parameters_init(np);
	if (IS_ERR(tzp)) {
		ret = PTR_ERR(tzp);
		pr_err("Failed to initialize parameter from %pOFn: %d\n", np, ret);
		goto out_kfree_trips;
	}

	of_ops->ops.get_trip_type = of_ops->ops.get_trip_type ? : of_thermal_get_trip_type;
	of_ops->ops.get_trip_temp = of_ops->ops.get_trip_temp ? : of_thermal_get_trip_temp;
	of_ops->ops.get_trip_hyst = of_ops->ops.get_trip_hyst ? : of_thermal_get_trip_hyst;
	of_ops->ops.set_trip_hyst = of_ops->ops.set_trip_hyst ? : of_thermal_set_trip_hyst;
	of_ops->ops.get_crit_temp = of_ops->ops.get_crit_temp ? : of_thermal_get_crit_temp;
	of_ops->ops.bind = thermal_of_bind;
	of_ops->ops.unbind = thermal_of_unbind;
	if (group && ops->set_trips) {
		of_ops->group = group;
		of_ops->set_trips = ops->set_trips;
		of_ops->ops.set_trips = thermal_of_group_set_trips;
	}

	mask = GENMASK_ULL((ntrips) - 1, 0);

	tz = thermal_zone_device_register_with_trips(np->name, trips, ntrips,
						     mask, data, &of_ops->ops, tzp,
						     pdelay, delay);
	if (IS_ERR(tz)) {
		ret = PTR_ERR(tz);
		pr_err("Failed to register thermal zone %pOFn: %d\n", np, ret);
		goto out_kfree_tzp;
	}

	/* QTI: the zone stays off until userspace enables it */
	if (of_property_read_bool(np, "disable-thermal-zone"))
		return tz;

	ret = thermal_zone_device_enable(tz);
	if (ret) {
		pr_err("Failed to enabled thermal zone '%s', id=%d: %d\n",
		       tz->type, tz->id, ret);
		__thermal_of_zone_unregister(tz);
		return ERR_PTR(ret);
	}

	return tz;

out_kfree_tzp:
	kfree(tzp);
out_kfree_trips:
	kfree(trips);
out_kfree_of_ops:
	kfree(of_ops);

	return ERR_PTR(ret);
}

/**
 * thermal_of_zone_register - Register a thermal zone with device node
 * sensor
 *
 * The thermal_of_zone_register() parses a device tree given a device
 * node sensor and identifier. It searches for the thermal zones
 * associated to the couple sensor/id and retrieves all the thermal
 * zone properties and registers new thermal zones with those
 * properties. When several zones use the couple sensor/id, all of them
 * are registered and the first one is returned.
 *
 * @sensor: A device node pointer corresponding to the sensor in the device tree
 * @id: An integer as sensor identifier
 * @data: A private data to be stored in the thermal zone dedicated private area
 * @ops: A set of thermal sensor ops
 *
 * Return: a valid thermal zone structure pointer on success.
 * 	- EINVAL: if the device tree thermal description is malformed
 *	- ENOMEM: if one structure can not be allocated
 *	- Other negative errors are returned by the underlying called functions
 */
struct thermal_zone_device *thermal_of_zone_register(struct device_node *sensor, int id, void *data,
						     const struct thermal_zone_device_ops *ops)
{
	struct thermal_of_group *group = NULL;
	struct thermal_zone_device *tz, *first = NULL;
	struct device_node *np, *next;
	int count = 0, i;

	np = of_thermal_zone_find(sensor, id);
	if (IS_ERR(np)) {
		if (PTR_ERR(np) != -ENODEV)
			pr_err("Failed to find thermal zone for %pOFn id=%d\n", sensor, id);
		return ERR_CAST(np);
	}

	for (next = of_node_get(np); !IS_ERR(next); count++) {
		struct device_node *cur = next;

		next = of_thermal_zone_find_from(sensor, id, cur);
		of_node_put(cur);
	}

	if (count > 1) {
		group = thermal_of_group_alloc(count);
		if (!group) {
			of_node_put(np);
			return ERR_PTR(-ENOMEM);
		}
	}

	while (!IS_ERR(np)) {
		tz = thermal_of_zone_register_np(np, sensor, id, data, ops, group);
		next = of_thermal_zone_find_from(sensor, id, np);
		of_node_put(np);
		np = next;

		if (IS_ERR(tz)) {
			if (!first) {
				while (!IS_ERR(np)) {
					next = np;
					np = of_thermal_zone_find_from(sensor, id, next);
					of_node_put(next);
				}
				thermal_of_group_free(group);
				return tz;
			}
			continue;
		}
		if (!first)
			first = tz;
		if (group) {
			mutex_lock(&group->lock);
			i = group->num_tz++;
			group->tz[i] = tz;
			group->low[i] = tz->prev_low_trip;
			group->high[i] = tz->prev_high_trip;
			mutex_unlock(&group->lock);
		}
	}

	if (group) {
		for (i = 0; i < group->num_tz; i++)
			group->tz[i]->of_group = group;
		/* program the sensor with the window of the whole group */
		thermal_zone_device_update(first, THERMAL_EVENT_UNSPECIFIED);
	}

	return first;
}
EXPORT_SYMBOL_GPL(thermal_of_zone_register);

static void devm_thermal_of_zone_release(struct device *dev, void *res)
{
	thermal_of_zone_unregister(*(struct thermal_zone_device **)res);
}

static int devm_thermal_of_zone_match(struct device *dev, void *res,
				      void *data)
{
	struct thermal_zone_device **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

/**
 * devm_thermal_of_zone_register - register a thermal tied with the sensor life cycle
 *
 * This function is the device version of the thermal_of_zone_register() function.
 *
 * @dev: a device structure pointer to sensor to be tied with the thermal zone OF life cycle
 * @sensor_id: the sensor identifier
 * @data: a pointer to a private data to be stored in the thermal zone 'devdata' field
 * @ops: a pointer to the ops structure associated with the sensor
 */
struct thermal_zone_device *devm_thermal_of_zone_register(struct device *dev, int sensor_id, void *data,
							  const struct thermal_zone_device_ops *ops)
{
	struct thermal_zone_device **ptr, *tzd;

	ptr = devres_alloc(devm_thermal_of_zone_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	tzd = thermal_of_zone_register(dev->of_node, sensor_id, data, ops);
	if (IS_ERR(tzd)) {
		devres_free(ptr);
		return tzd;
	}

	*ptr = tzd;
	devres_add(dev, ptr);

	return tzd;
}
EXPORT_SYMBOL_GPL(devm_thermal_of_zone_register);

/**
 * devm_thermal_of_zone_unregister - Resource managed version of
 *				thermal_of_zone_unregister().
 * @dev: Device for which which resource was allocated.
 * @tz: a pointer to struct thermal_zone where the sensor is registered.
 *
 * This function removes the sensor callbacks and private data from the
 * thermal zone device registered with devm_thermal_zone_of_sensor_register()
 * API. It will also silent the zone by remove the .get_temp() and .get_trend()
 * thermal zone device callbacks.
 * Normally this function will not need to be called and the resource
 * management code will ensure that the resource is freed.
 */
void devm_thermal_of_zone_unregister(struct device *dev, struct thermal_zone_device *tz)
{
	WARN_ON(devres_release(dev, devm_thermal_of_zone_release,
			       devm_thermal_of_zone_match, tz));
}
EXPORT_SYMBOL_GPL(devm_thermal_of_zone_unregister);
