#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include "lm75.h"



enum lm75_type {		
	lm75a,
};


static const unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b, 0x4c,
					0x4d, 0x4e, 0x4f, I2C_CLIENT_END };


#define LM75_REG_TEMP		0x00
#define LM75_REG_CONF		0x01
#define LM75_REG_HYST		0x02
#define LM75_REG_MAX		0x03


struct lm75_data {
	struct i2c_client	*client;
	struct regmap		*regmap;
	u8			orig_conf;
	u8			resolution;	
	u8			resolution_limits;
	unsigned int		sample_time;	/* In ms */
};

/*-----------------------------------------------------------------------*/

static inline long lm75_reg_to_mc(s16 temp, u8 resolution)
{
	return ((temp >> (16 - resolution)) * 1000) >> (resolution - 8);
}

static int lm75_read(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long *val)
{
	struct lm75_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int err, reg;

	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			*val = data->sample_time;
			break;
		default:
			return -EINVAL;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			reg = LM75_REG_TEMP;
			break;
		case hwmon_temp_max:
			reg = LM75_REG_MAX;
			break;
		case hwmon_temp_max_hyst:
			reg = LM75_REG_HYST;
			break;
		default:
			return -EINVAL;
		}
		err = regmap_read(data->regmap, reg, &regval);
		if (err < 0)
			return err;

		*val = lm75_reg_to_mc(regval, data->resolution);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int lm75_write(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long temp)
{
	struct lm75_data *data = dev_get_drvdata(dev);
	u8 resolution;
	int reg;

	if (type != hwmon_temp)
		return -EINVAL;

	switch (attr) {
	case hwmon_temp_max:
		reg = LM75_REG_MAX;
		break;
	case hwmon_temp_max_hyst:
		reg = LM75_REG_HYST;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Resolution of limit registers is assumed to be the same as the
	 * temperature input register resolution unless given explicitly.
	 */
	if (data->resolution_limits)
		resolution = data->resolution_limits;
	else
		resolution = data->resolution;

	temp = clamp_val(temp, LM75_TEMP_MIN, LM75_TEMP_MAX);
	temp = DIV_ROUND_CLOSEST(temp  << (resolution - 8),
				 1000) << (16 - resolution);

	return regmap_write(data->regmap, reg, temp);
}

static umode_t lm75_is_visible(const void *data, enum hwmon_sensor_types type,
			       u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0444;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		case hwmon_temp_max:
		case hwmon_temp_max_hyst:
			return 0644;
		}
		break;
	default:
		break;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/

/* device probe and removal */

/* chip configuration */

static const u32 lm75_chip_config[] = {
	HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL,
	0
};

static const struct hwmon_channel_info lm75_chip = {
	.type = hwmon_chip,
	.config = lm75_chip_config,
};

static const u32 lm75_temp_config[] = {
	HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST,
	0
};

static const struct hwmon_channel_info lm75_temp = {
	.type = hwmon_temp,
	.config = lm75_temp_config,
};

static const struct hwmon_channel_info *lm75_info[] = {
	&lm75_chip,
	&lm75_temp,
	NULL
};

static const struct hwmon_ops lm75_hwmon_ops = {
	.is_visible = lm75_is_visible,
	.read = lm75_read,
	.write = lm75_write,
};

static const struct hwmon_chip_info lm75_chip_info = {
	.ops = &lm75_hwmon_ops,
	.info = lm75_info,
};

static bool lm75_is_writeable_reg(struct device *dev, unsigned int reg)
{
	return reg != LM75_REG_TEMP;
}

static bool lm75_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == LM75_REG_TEMP;
}

static const struct regmap_config lm75_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = LM75_REG_MAX,
	.writeable_reg = lm75_is_writeable_reg,
	.volatile_reg = lm75_is_volatile_reg,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.cache_type = REGCACHE_RBTREE,
	//.use_single_read = true,
	//.use_single_write = true,
};

static void lm75_remove(void *data)
{
	struct lm75_data *lm75 = data;
	struct i2c_client *client = lm75->client;

	i2c_smbus_write_byte_data(client, LM75_REG_CONF, lm75->orig_conf);
}

static int
lm75_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct lm75_data *data;
	int status, err;
	u8 set_mask, clr_mask;
	int new;
	enum lm75_type kind;

	if (client->dev.of_node)
		kind = (enum lm75_type)of_device_get_match_data(&client->dev);
	else
		kind = id->driver_data;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -EIO;

	data = devm_kzalloc(dev, sizeof(struct lm75_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;

	data->regmap = devm_regmap_init_i2c(client, &lm75_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	/* Set to LM75 resolution (9 bits, 1/2 degree C) and range.
	 * Then tweak to be more precise when appropriate.
	 */
	set_mask = 0;
	clr_mask = LM75_SHUTDOWN;		/* continuous conversions */

	switch (kind) {
	
	case lm75a:
		data->resolution = 9;
		data->sample_time = MSEC_PER_SEC / 2;
	    break;
	
}    
  

	status = i2c_smbus_read_byte_data(client, LM75_REG_CONF);
	if (status < 0) {
		dev_dbg(dev, "Can't read config? %d\n", status);
		return status;
	}
	data->orig_conf = status;
	new = status & ~clr_mask;
	new |= set_mask;
	if (status != new)
		i2c_smbus_write_byte_data(client, LM75_REG_CONF, new);

	err = devm_add_action_or_reset(dev, lm75_remove, data);
	if (err)
		return err;

	dev_dbg(dev, "Config %02x\n", new);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data, &lm75_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "%s: sensor '%s'\n", dev_name(hwmon_dev), client->name);

	return 0;
}

static const struct i2c_device_id lm75_ids[] = {
	
	{ "lm75a", lm75a, },
	
	{},
};
MODULE_DEVICE_TABLE(i2c, lm75_ids);

static const struct of_device_id lm75_of_match[] = {
	
	{
		.compatible = "national,lm75a",
		.data = (void *)lm75a
	},
	
	{ },
};
MODULE_DEVICE_TABLE(of, lm75_of_match);

#define LM75A_ID 0xA1

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm75_detect(struct i2c_client *new_client,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	int i;
	int conf, hyst, os;
	bool is_lm75a = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	conf = i2c_smbus_read_byte_data(new_client, 1);
	if (conf & 0xe0)
		return -ENODEV;

	/* First check for LM75A */
	if (i2c_smbus_read_byte_data(new_client, 7) == LM75A_ID) {
		/* LM75A returns 0xff on unused registers so
		   just to be sure we check for that too. */
		if (i2c_smbus_read_byte_data(new_client, 4) != 0xff
		 || i2c_smbus_read_byte_data(new_client, 5) != 0xff
		 || i2c_smbus_read_byte_data(new_client, 6) != 0xff)
			return -ENODEV;
		is_lm75a = 1;
		hyst = i2c_smbus_read_byte_data(new_client, 2);
		os = i2c_smbus_read_byte_data(new_client, 3);
	} /*else { /* Traditional style LM75 detection */
		/* Unused addresses 
		hyst = i2c_smbus_read_byte_data(new_client, 2);
		if (i2c_smbus_read_byte_data(new_client, 4) != hyst
		 || i2c_smbus_read_byte_data(new_client, 5) != hyst
		 || i2c_smbus_read_byte_data(new_client, 6) != hyst
		 || i2c_smbus_read_byte_data(new_client, 7) != hyst)
			return -ENODEV;
		os = i2c_smbus_read_byte_data(new_client, 3);
		if (i2c_smbus_read_byte_data(new_client, 4) != os
		 || i2c_smbus_read_byte_data(new_client, 5) != os
		 || i2c_smbus_read_byte_data(new_client, 6) != os
		 || i2c_smbus_read_byte_data(new_client, 7) != os)
			return -ENODEV;
	} */
	/*
	 * It is very unlikely that this is a LM75 if both
	 * hysteresis and temperature limit registers are 0.
	 */
	if (hyst == 0 && os == 0)
		return -ENODEV;

	/* Addresses cycling */
	for (i = 8; i <= 248; i += 40) {
		if (i2c_smbus_read_byte_data(new_client, i + 1) != conf
		 || i2c_smbus_read_byte_data(new_client, i + 2) != hyst
		 || i2c_smbus_read_byte_data(new_client, i + 3) != os)
			return -ENODEV;
		if (is_lm75a && i2c_smbus_read_byte_data(new_client, i + 7)
				!= LM75A_ID)
			return -ENODEV;
	}

	strlcpy(info->type, is_lm75a ? "lm75a" : "lm75", I2C_NAME_SIZE);

	return 0;
}

#ifdef CONFIG_PM
static int lm75_suspend(struct device *dev)
{
	int status;
	struct i2c_client *client = to_i2c_client(dev);
	status = i2c_smbus_read_byte_data(client, LM75_REG_CONF);
	if (status < 0) {
		dev_dbg(&client->dev, "Can't read config? %d\n", status);
		return status;
	}
	status = status | LM75_SHUTDOWN;
	i2c_smbus_write_byte_data(client, LM75_REG_CONF, status);
	return 0;
}

static int lm75_resume(struct device *dev)
{
	int status;
	struct i2c_client *client = to_i2c_client(dev);
	status = i2c_smbus_read_byte_data(client, LM75_REG_CONF);
	if (status < 0) {
		dev_dbg(&client->dev, "Can't read config? %d\n", status);
		return status;
	}
	status = status & ~LM75_SHUTDOWN;
	i2c_smbus_write_byte_data(client, LM75_REG_CONF, status);
	return 0;
}

static const struct dev_pm_ops lm75_dev_pm_ops = {
	.suspend	= lm75_suspend,
	.resume		= lm75_resume,
};
#define LM75_DEV_PM_OPS (&lm75_dev_pm_ops)
#else
#define LM75_DEV_PM_OPS NULL
#endif /* CONFIG_PM */

static struct i2c_driver lm75_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm75",
		.of_match_table = of_match_ptr(lm75_of_match),
		.pm	= LM75_DEV_PM_OPS,
	},
	.probe		= lm75_probe,
	.id_table	= lm75_ids,
	.detect		= lm75_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm75_driver);

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>");
MODULE_DESCRIPTION("LM75 driver");
MODULE_LICENSE("GPL");
