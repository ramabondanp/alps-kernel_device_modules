// SPDX-License-Identifier: GPL-2.0
/*
 * AW8601AF VCM driver.
 *
 * Register sequence and power handling follow the vendor binary shipped in
 * X6871 vendor_dlkm (checked against its disassembly).
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#define DRIVER_NAME "aw8601af"
#define AW8601AF_NAME "aw8601af"
#define AW8601AF_I2C_SLAVE_ADDR 0x0c

#define LOG_INF(format, args...)                                               \
	pr_info(DRIVER_NAME " [%s] " format, __func__, ##args)

#define AW8601AF_MAX_FOCUS_POS			1023
#define AW8601AF_ORIGIN_FOCUS_POS		0
#define AW8601AF_FOCUS_STEPS			1
#define AW8601AF_SET_POSITION_ADDR		0x03

#define AW8601AF_CTRL_DELAY_US			10000
#define AW8601AF_MOVE_STEPS			100
#define AW8601AF_MOVE_DELAY_US			5000

struct aw8601af_device {
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_subdev sd;
	struct v4l2_ctrl *focus;
	struct regulator *vin;
	struct regulator *vdd;
	struct pinctrl *vcamaf_pinctrl;
	struct pinctrl_state *vcamaf_on;
	struct pinctrl_state *vcamaf_off;
};

static inline struct aw8601af_device *to_aw8601af_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct aw8601af_device, ctrls);
}

static inline struct aw8601af_device *sd_to_aw8601af_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct aw8601af_device, sd);
}

static int aw8601af_set_position(struct aw8601af_device *aw8601af, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&aw8601af->sd);

	return i2c_smbus_write_word_data(client, AW8601AF_SET_POSITION_ADDR,
					 swab16(val));
}

static int aw8601af_release(struct aw8601af_device *aw8601af)
{
	int ret;
	int val = aw8601af->focus->val;
	int diff_dac = AW8601AF_ORIGIN_FOCUS_POS - val;
	int nStep_count = abs(diff_dac) / AW8601AF_MOVE_STEPS;
	int i;

	for (i = 0; i < nStep_count; ++i) {
		val += (diff_dac < 0 ? -AW8601AF_MOVE_STEPS : AW8601AF_MOVE_STEPS);
		ret = aw8601af_set_position(aw8601af, val);
		if (ret) {
			LOG_INF("%s I2C failure: %d", __func__, ret);
			return ret;
		}
		usleep_range(AW8601AF_MOVE_DELAY_US,
			     AW8601AF_MOVE_DELAY_US + 1000);
	}

	ret = aw8601af_set_position(aw8601af, AW8601AF_ORIGIN_FOCUS_POS);
	if (ret) {
		LOG_INF("%s I2C failure: %d", __func__, ret);
		return ret;
	}

	i2c_smbus_write_byte_data(v4l2_get_subdevdata(&aw8601af->sd), 0x02, 0x01);

	return 0;
}

static int aw8601af_init(struct aw8601af_device *aw8601af)
{
	struct i2c_client *client = v4l2_get_subdevdata(&aw8601af->sd);
	int ret = 0;
	char puSendCmdArray[8][2] = {
		{0x02, 0x01}, {0x02, 0x00}, {0xFE, 0xFE},
		{0x02, 0x02}, {0x06, 0x40}, {0x07, 0x7b},
		{0x03, 0x01}, {0x04, 0x77},
	};
	unsigned char cmd_number;

	LOG_INF("+\n");

	client->addr = AW8601AF_I2C_SLAVE_ADDR;

	for (cmd_number = 0; cmd_number < 8; cmd_number++) {
		if (puSendCmdArray[cmd_number][0] != 0xFE) {
			ret = i2c_smbus_write_byte_data(client,
					puSendCmdArray[cmd_number][0],
					puSendCmdArray[cmd_number][1]);
			if (ret < 0)
				return -1;
		} else {
			msleep(1);
		}
	}

	LOG_INF("-\n");

	return ret;
}

static int aw8601af_power_off(struct aw8601af_device *aw8601af)
{
	int ret;

	LOG_INF("+\n");

	ret = aw8601af_release(aw8601af);
	if (ret)
		LOG_INF("aw8601af release failed!\n");

	ret = regulator_disable(aw8601af->vin);
	if (ret)
		return ret;

	ret = regulator_disable(aw8601af->vdd);
	if (ret)
		return ret;

	if (aw8601af->vcamaf_pinctrl && aw8601af->vcamaf_off)
		ret = pinctrl_select_state(aw8601af->vcamaf_pinctrl,
					aw8601af->vcamaf_off);

	LOG_INF("-\n");

	return ret;
}

static int aw8601af_power_on(struct aw8601af_device *aw8601af)
{
	int ret;

	LOG_INF("+\n");

	ret = regulator_enable(aw8601af->vin);
	if (ret < 0)
		return ret;

	ret = regulator_enable(aw8601af->vdd);
	if (ret < 0)
		return ret;

	if (aw8601af->vcamaf_pinctrl && aw8601af->vcamaf_on)
		ret = pinctrl_select_state(aw8601af->vcamaf_pinctrl,
					aw8601af->vcamaf_on);

	if (ret < 0)
		return ret;

	usleep_range(AW8601AF_CTRL_DELAY_US, AW8601AF_CTRL_DELAY_US + 100);

	ret = aw8601af_init(aw8601af);
	if (ret < 0)
		goto fail;

	LOG_INF("-\n");

	return 0;

fail:
	regulator_disable(aw8601af->vin);
	regulator_disable(aw8601af->vdd);
	if (aw8601af->vcamaf_pinctrl && aw8601af->vcamaf_off)
		pinctrl_select_state(aw8601af->vcamaf_pinctrl,
				aw8601af->vcamaf_off);

	return ret;
}

static int aw8601af_set_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct aw8601af_device *aw8601af = to_aw8601af_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		LOG_INF("pos(%d)\n", ctrl->val);
		ret = aw8601af_set_position(aw8601af, ctrl->val);
		if (ret) {
			LOG_INF("%s I2C failure: %d", __func__, ret);
			return ret;
		}
	}
	return 0;
}

static const struct v4l2_ctrl_ops aw8601af_vcm_ctrl_ops = {
	.s_ctrl = aw8601af_set_ctrl,
};

static int aw8601af_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int ret;
	struct aw8601af_device *aw8601af = sd_to_aw8601af_vcm(sd);

	ret = aw8601af_power_on(aw8601af);
	if (ret < 0) {
		LOG_INF("power on fail, ret = %d\n", ret);
		return ret;
	}

	return 0;
}

static int aw8601af_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct aw8601af_device *aw8601af = sd_to_aw8601af_vcm(sd);

	aw8601af_power_off(aw8601af);

	return 0;
}

static const struct v4l2_subdev_internal_ops aw8601af_int_ops = {
	.open = aw8601af_open,
	.close = aw8601af_close,
};

static const struct v4l2_subdev_ops aw8601af_ops = { };

static void aw8601af_subdev_cleanup(struct aw8601af_device *aw8601af)
{
	v4l2_async_unregister_subdev(&aw8601af->sd);
	v4l2_ctrl_handler_free(&aw8601af->ctrls);
#if IS_ENABLED(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&aw8601af->sd.entity);
#endif
}

static int aw8601af_init_controls(struct aw8601af_device *aw8601af)
{
	struct v4l2_ctrl_handler *hdl = &aw8601af->ctrls;
	const struct v4l2_ctrl_ops *ops = &aw8601af_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	aw8601af->focus = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
			  0, AW8601AF_MAX_FOCUS_POS, AW8601AF_FOCUS_STEPS, 0);

	if (hdl->error)
		return hdl->error;

	aw8601af->sd.ctrl_handler = hdl;

	return 0;
}

static int aw8601af_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct aw8601af_device *aw8601af;
	int ret;

	LOG_INF("+\n");

	aw8601af = devm_kzalloc(dev, sizeof(*aw8601af), GFP_KERNEL);
	if (!aw8601af)
		return -ENOMEM;

	aw8601af->vin = devm_regulator_get(dev, "vin");
	if (IS_ERR(aw8601af->vin)) {
		ret = PTR_ERR(aw8601af->vin);
		if (ret != -EPROBE_DEFER)
			LOG_INF("cannot get vin regulator\n");
		return ret;
	}

	aw8601af->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(aw8601af->vdd)) {
		ret = PTR_ERR(aw8601af->vdd);
		if (ret != -EPROBE_DEFER)
			LOG_INF("cannot get vdd regulator\n");
		return ret;
	}

	aw8601af->vcamaf_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(aw8601af->vcamaf_pinctrl)) {
		ret = PTR_ERR(aw8601af->vcamaf_pinctrl);
		aw8601af->vcamaf_pinctrl = NULL;
		LOG_INF("cannot get pinctrl\n");
	} else {
		aw8601af->vcamaf_on = pinctrl_lookup_state(
			aw8601af->vcamaf_pinctrl, "vcamaf_on");

		if (IS_ERR(aw8601af->vcamaf_on)) {
			ret = PTR_ERR(aw8601af->vcamaf_on);
			aw8601af->vcamaf_on = NULL;
			LOG_INF("cannot get vcamaf_on pinctrl\n");
		}

		aw8601af->vcamaf_off = pinctrl_lookup_state(
			aw8601af->vcamaf_pinctrl, "vcamaf_off");

		if (IS_ERR(aw8601af->vcamaf_off)) {
			ret = PTR_ERR(aw8601af->vcamaf_off);
			aw8601af->vcamaf_off = NULL;
			LOG_INF("cannot get vcamaf_off pinctrl\n");
		}
	}

	v4l2_i2c_subdev_init(&aw8601af->sd, client, &aw8601af_ops);
	aw8601af->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	aw8601af->sd.internal_ops = &aw8601af_int_ops;

	ret = aw8601af_init_controls(aw8601af);
	if (ret)
		goto err_cleanup;

#if IS_ENABLED(CONFIG_MEDIA_CONTROLLER)
	ret = media_entity_pads_init(&aw8601af->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	aw8601af->sd.entity.function = MEDIA_ENT_F_LENS;
#endif

	ret = v4l2_async_register_subdev(&aw8601af->sd);
	if (ret < 0)
		goto err_cleanup;

	LOG_INF("-\n");

	return 0;

err_cleanup:
	aw8601af_subdev_cleanup(aw8601af);
	return ret;
}

static void aw8601af_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct aw8601af_device *aw8601af = sd_to_aw8601af_vcm(sd);

	LOG_INF("+\n");

	aw8601af_subdev_cleanup(aw8601af);

	LOG_INF("-\n");
}

static const struct i2c_device_id aw8601af_id_table[] = {
	{ AW8601AF_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, aw8601af_id_table);

static const struct of_device_id aw8601af_of_table[] = {
	{ .compatible = "mediatek,aw8601af" },
	{ },
};
MODULE_DEVICE_TABLE(of, aw8601af_of_table);

static struct i2c_driver aw8601af_i2c_driver = {
	.driver = {
		.name = AW8601AF_NAME,
		.of_match_table = aw8601af_of_table,
	},
	.probe  = aw8601af_probe,
	.remove = aw8601af_remove,
	.id_table = aw8601af_id_table,
};

module_i2c_driver(aw8601af_i2c_driver);

MODULE_AUTHOR("Transsion");
MODULE_DESCRIPTION("AW8601AF VCM driver");
MODULE_LICENSE("GPL v2");
