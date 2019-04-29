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



//Probablr addresses where the client lm75a can be present. Set by making A0, A1, A2 high or low.
static const unsigned short addr_i2c[] = { 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, I2C_CLIENT_END };

//Register selection macro
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
	unsigned int		sample_time;
};


static inline long lm75_reg_to_mc(s16 temp, u8 resolution)
{
	return ((temp >> (16 - resolution)) * 1000) >> (resolution - 8);
}

static int lm75_read(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long *val)
{
	struct lm75_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client; 
	unsigned int regval;
	int err, reg;

	switch (type) {
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
		
		err = regmap_read(data->regmap, reg, &regval)/* i2c_smbus_read_word_data(client, reg) can also be used */;
		if (err < 0)
			return err;

		*val = lm75_reg_to_mc(regval, data->resolution);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const u32 lm75_chip_config[] = { HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL,0};

static const struct hwmon_channel_info lm75_chip = {
	.type = hwmon_chip,
};

static const struct hwmon_channel_info lm75_temp = {
	.type = hwmon_temp,
};

static const struct hwmon_channel_info *lm75_info[] = {
	&lm75_chip,
	&lm75_temp,
	NULL
};

static const struct hwmon_ops lm75_hwmon_ops = {
	.is_visible = 0444,
	.read = lm75_read,
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
};

static void lm75_remove(void *data)
{
	struct lm75_data *lm75 = data;
	struct i2c_client *client = lm75->client;

	i2c_smbus_write_byte_data(client, LM75_REG_CONF, lm75->orig_conf); //For writing back the original configuration into sensor (optional)
}

static int lm75_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct lm75_data *data;
	int status, err;
	u8 set_mask, clr_mask;
	int new;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -EIO;

	data = devm_kzalloc(dev, sizeof(struct lm75_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;

	data->regmap = devm_regmap_init_i2c(client, &lm75_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	set_mask = 0;
	clr_mask = LM75_SHUTDOWN;		

	data->resolution = 9;
	data->sample_time = MSEC_PER_SEC / 2; // = 500 ms
	   
	status = i2c_smbus_read_byte_data(client, LM75_REG_CONF);
	if (status < 0) 
	{
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

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data, &lm75_chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "%s: sensor '%s'\n", dev_name(hwmon_dev), client->name);

	return 0;
}



static const struct i2c_device_id lm75_id[] = { { "lm75a",0, }, {}, };
MODULE_DEVICE_TABLE(i2c, lm75_id);


#define LM75A_ID 0xA1


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

	if (i2c_smbus_read_byte_data(new_client, 7) == LM75A_ID) {
		
		if (i2c_smbus_read_byte_data(new_client, 4) != 0xff
		 || i2c_smbus_read_byte_data(new_client, 5) != 0xff
		 || i2c_smbus_read_byte_data(new_client, 6) != 0xff)
			return -ENODEV;
		is_lm75a = 1;
		hyst = i2c_smbus_read_byte_data(new_client, 2);
		os = i2c_smbus_read_byte_data(new_client, 3);
	} 
	if (hyst == 0 && os == 0)
		return -ENODEV;
	
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


static struct i2c_driver lm75a_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm75a",
			  },
	.probe		= lm75_probe,
	.id_table	= lm75_id,
	.detect		= lm75_detect,
	.address_list	= addr_i2c,
};

module_i2c_driver(lm75a_driver); //platform driver style

MODULE_AUTHOR(" sh ");
MODULE_DESCRIPTION("LM75A driver");
MODULE_LICENSE("GPL");
