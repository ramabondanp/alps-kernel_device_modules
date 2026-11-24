#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include "transsion_core.h"

extern struct tran_ts_core g_tran_ts;
extern struct tran_core_ops g_tpd_ops;

static void tran_set_ts_mode(int cmd, int val)
{
	struct tran_ts_controller *ctrl = g_tran_ts.controller;

	if (ctrl && ctrl->ops && ctrl->ops->set_ts_mode)
		ctrl->ops->set_ts_mode(cmd, val);
}

static void tp_state_recovery(void)
{
	tran_set_ts_mode(TRAN_TP_CMD_EDGE_MODE, g_tran_ts.edge_mode);
	if (g_tran_ts.curved_edge_amt_support)
		tran_set_ts_mode(TRAN_TP_CMD_EDGE_LEVEL, g_tran_ts.edge_level);
	if (g_tran_ts.esports_engine) {
		tran_set_ts_mode(TRAN_TP_CMD_TRANSIENT_RATE,
				 g_tran_ts.transient_report_rate);
		tran_set_ts_mode(TRAN_TP_CMD_LOCK_POINT, g_tran_ts.lock_point);
	}
	if (g_tran_ts.dynamic_report_rate_support)
		tran_set_ts_mode(TRAN_TP_CMD_REPORT_RATE,
				 g_tran_ts.tran_report_rate);

	TRAN_INFO("recover edge:%u rate:%u transient:%u lock:%u level:%u",
		  g_tran_ts.edge_mode, g_tran_ts.tran_report_rate,
		  g_tran_ts.transient_report_rate, g_tran_ts.lock_point,
		  g_tran_ts.edge_level);
}

int get_tpd_ops(struct tran_ts_controller *controller, struct tran_core_ops **ops)
{
	if (!controller || !ops)
		return 0;

	if (controller->tran_ts_data && controller->tran_ts_data->tpd_ops) {
		*ops = controller->tran_ts_data->tpd_ops;
		return 1;
	}

	TRAN_INFO("get ops fail !");
	return 0;
}
EXPORT_SYMBOL_GPL(get_tpd_ops);

static void dsp_event_callback(struct work_struct *work)
{
	struct tran_ts_controller *pController = g_tran_ts.controller;

	if (g_tran_ts.controller_init_state && pController && pController->ops &&
	    pController->ops->tran_ts_resume) {
		pController->ops->tran_ts_resume(&g_tran_ts.pdev->dev);
	} else {
		TRAN_INFO("vendor_probe_result is false!");
		mutex_unlock(&g_tran_ts.dsp_event_mutex);
		return;
	}

	g_tran_ts.ts_core_mode = TRAN_ACTIVE_MODE;
	tp_state_recovery();
	TRAN_INFO("core mode change to TRAN_ACTIVE_MODE");
	mutex_unlock(&g_tran_ts.dsp_event_mutex);
	TRAN_INFO("drm notify tp resume end");
}

static int check_support_ic(struct tran_ts_core *ts_core,
			    struct tran_ts_controller *controller, char *supInfo)
{
	int i;
	char **support_ic = NULL;

	if (controller) {
		support_ic = controller->support_ic;
		for (i = 0; support_ic[i] != NULL; i++) {
			TRAN_INFO("Check whether the supplier supports %s", support_ic[i]);
			if (strstr(supInfo, support_ic[i])) {
				controller->cur_ic = support_ic[i];
				controller->tran_ts_data = ts_core;
				ts_core->controller = controller;
				return 1;
			}
		}
	}

	ts_core->controller = NULL;
	return 0;
}

static int match_tp_controller(struct tran_ts_core *ts_core)
{
	char lcm_name[64] = {0};
	struct supplier_list *supList = NULL;
	struct controller_list *controllerList = NULL;
	struct tran_ts_platform *platData = ts_core->platform_data;

	if (platData && platData->ops && platData->ops->get_lcm_name) {
		platData->ops->get_lcm_name(lcm_name);
		TRAN_INFO("get_lcm_name %s", lcm_name);
	}

	list_for_each_entry(supList, &ts_core->supplier_head, next) {
		TRAN_INFO("check supplier-%d %s", supList->supplier_num, supList->supplier_info);
		if (strstr(supList->supplier_info, lcm_name)) {
			TRAN_INFO("apply supplier-%d %s", supList->supplier_num, supList->supplier_info);
			ts_core->supplier = supList;
			break;
		} else if (strstr(supList->supplier_info, "any")) {
			TRAN_INFO("apply supplier-%d %s", supList->supplier_num, supList->supplier_info);
			ts_core->supplier = supList;
			break;
		} else {
			ts_core->supplier = NULL;
		}
	}

	if (!ts_core->supplier) {
		TRAN_ERROR("no supplier match!!");
		return 0;
	}

	mutex_lock(&ts_core->controller_mutex);
	list_for_each_entry(controllerList, &ts_core->controller_head, next) {
		if (!controllerList->controller->vendor_name) {
			mutex_unlock(&ts_core->controller_mutex);
			return 0;
		}
		TRAN_INFO("try to find %s controller", controllerList->controller->vendor_name);
		if (strstr(supList->supplier_info, controllerList->controller->vendor_name))
			check_support_ic(ts_core, controllerList->controller, supList->supplier_info);
	}
	mutex_unlock(&ts_core->controller_mutex);

	if (ts_core->controller) {
		TRAN_INFO("start load controller vendor:%s ic:%s",
			  ts_core->controller->vendor_name, ts_core->controller->cur_ic);
		ts_core->controller->ops->tran_ts_init();
		return 1;
	}

	TRAN_ERROR("no controller match exit 0");
	return 0;
}

int register_ts_controller(struct tran_ts_controller *controller)
{
	struct controller_list *controllerList = NULL;

	if (!controller) {
		TRAN_INFO("Don't register reason controller is null");
		return -1;
	}

	controllerList = vmalloc(sizeof(*controllerList));
	if (!controllerList) {
		TRAN_ERROR("request controllerList mem failed");
		return -1;
	}

	memset(controllerList, 0, sizeof(*controllerList));
	controllerList->controller = controller;

	mutex_lock(&g_tran_ts.controller_mutex);
	list_add_tail(&controllerList->next, &g_tran_ts.controller_head);
	mutex_unlock(&g_tran_ts.controller_mutex);

	if (!queue_work(g_tran_ts.tpd_init_workqueue, &g_tran_ts.tpd_match_work))
		TRAN_ERROR("touch device queue_work failed");
	return 0;
}
EXPORT_SYMBOL_GPL(register_ts_controller);

int unregister_ts_controller(struct tran_ts_controller *controller)
{
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_ts_controller);

static int tran_parse_dt(struct device *dev, struct tran_ts_core *core_data)
{
	int i, len, ret;
	int supplier_num = 0;
	u32 resolution_multiples = 1;
	const char *supplier_info = NULL;
	const char *supplier_fw = NULL;
	const char *supplier_testfw = NULL;
	const char *supplier_vendor = NULL;
	struct supplier_list *tmpList = NULL;
	char supplierx[32] = {0};
	struct device_node *np = dev->of_node;

	TRAN_FUNC_ENTER();

	if (of_property_read_u32(np, "touchscreen,esports-engine",
				 &core_data->esports_engine) ||
	    core_data->esports_engine >= 5)
		core_data->esports_engine = 0;

	core_data->dynamic_report_rate_support =
		of_property_read_bool(np, "touchscreen,dynamic-report-rate-support");
	core_data->pocket_mode_amt_support =
		of_property_read_bool(np, "touchscreen,pocket-mode-amt-support");
	core_data->fingerprint_support =
		of_property_read_bool(np, "touchscreen,fingerprint-support");
	core_data->curved_edge_amt_support =
		of_property_read_bool(np, "touchscreen,curved-edge-amt-support");
	TRAN_INFO("dt features: dyn_rate=%d pocket=%d fp=%d edge=%d esports=%u",
		  core_data->dynamic_report_rate_support,
		  core_data->pocket_mode_amt_support,
		  core_data->fingerprint_support,
		  core_data->curved_edge_amt_support,
		  core_data->esports_engine);

	ret = of_property_read_u32(np, "supplier-num", &supplier_num);
	if (ret < 0) {
		TRAN_ERROR("supplier-num: can not find!");
		return ret;
	}
	TRAN_INFO("supplier-num: %d", supplier_num);

	for (i = 0; i < supplier_num; i++) {
		tmpList = NULL;
		supplier_info = NULL;
		supplier_vendor = NULL;
		memset(supplierx, 0, sizeof(supplierx));

		snprintf(supplierx, sizeof(supplierx), "supplier-%d", i);
		supplier_info = of_get_property(np, supplierx, &len);
		if (!supplier_info) {
			TRAN_ERROR("supplier-%d: can not find!", i);
			return -EINVAL;
		}
		TRAN_INFO("supplier-%d: %s", i, supplier_info);

		snprintf(supplierx, sizeof(supplierx), "supplier-vendor-fwname-%d", i);
		supplier_fw = of_get_property(np, supplierx, &len);
		if (!supplier_fw)
			TRAN_ERROR("supplier-vendor-fwname-%d: can not find!", i);
		else
			TRAN_INFO("supplier-vendor-fwname-%d: %s", i, supplier_fw);

		snprintf(supplierx, sizeof(supplierx), "supplier-vendor-testname-%d", i);
		supplier_testfw = of_get_property(np, supplierx, &len);
		if (!supplier_testfw)
			TRAN_ERROR("supplier-vendor-testname-%d: can not find!", i);
		else
			TRAN_INFO("supplier-vendor-testname-%d: %s", i, supplier_testfw);

		snprintf(supplierx, sizeof(supplierx), "supplier-vendor-%d", i);
		supplier_vendor = of_get_property(np, supplierx, &len);
		if (!supplier_vendor)
			TRAN_INFO("supplier-vendor-%d: can not find!", i);
		else
			TRAN_INFO("supplier-vendor-%d: %s", i, supplier_vendor);

		snprintf(supplierx, sizeof(supplierx), "supplier-resolution-%d", i);
		ret = of_property_read_u32(np, supplierx, &resolution_multiples);
		if (ret < 0) {
			TRAN_ERROR("supplier-resolution-%d: can not find!", i);
			resolution_multiples = 1;
		}
		TRAN_INFO("supplier-resolution-%d: %d", i, resolution_multiples);

		tmpList = vmalloc(sizeof(*tmpList));
		if (!tmpList) {
			TRAN_ERROR("request supplier_list mem failed!");
			return -ENOMEM;
		}

		tmpList->supplier_num = i;
		tmpList->supplier_info = (char *)supplier_info;
		tmpList->supplier_fw = (char *)supplier_fw;
		tmpList->supplier_testfw = (char *)supplier_testfw;
		tmpList->supplier_vendor = (char *)supplier_vendor;
		tmpList->resolution_multiples = resolution_multiples;
		list_add_tail(&tmpList->next, &core_data->supplier_head);
	}

	TRAN_FUNC_EXIT();
	return 0;
}

static void controller_init_finish(void)
{
	g_tran_ts.controller_init_state = true;
	complete(&g_tran_ts.comp);
	TRAN_INFO("controller load finish");
}

static void controller_uninit_finish(void)
{
	g_tran_ts.controller_init_state = false;
	TRAN_INFO("controller remove finish");
}

static void controller_match_work(struct work_struct *work)
{
	int ret;
	int retry;

	if (g_tran_ts.controller) {
		TRAN_INFO("Not allow to match controller");
		return;
	}

	/*
	 * adaptive-ts is loaded before the vendor touch driver, and the vendor
	 * only calls register_ts_controller() from its module_init. Retry so a
	 * match is not lost when this work runs before that registration, which
	 * is what happens on a recovery boot.
	 */
	for (retry = 0; retry < 10; retry++) {
		if (match_tp_controller(&g_tran_ts))
			break;
		TRAN_INFO("no controller yet, retry %d", retry);
		msleep(200);
	}

	if (!g_tran_ts.controller) {
		TRAN_ERROR("no controller match after retries");
		return;
	}

	ret = wait_for_completion_timeout(&g_tran_ts.comp, msecs_to_jiffies(8000));
	if (ret == 0) {
		TRAN_ERROR("init controller timeout!");
		g_tran_ts.controller = NULL;
		return;
	}
	TRAN_INFO("load controller success!");
	if (g_tran_ts.platform_data && g_tran_ts.platform_data->ops &&
	    g_tran_ts.platform_data->ops->plat_init)
		g_tran_ts.platform_data->ops->plat_init(&g_tran_ts);
	if (g_tran_ts.controller->ops && g_tran_ts.controller->ops->tran_gesture_enable)
		g_tran_ts.controller->ops->tran_gesture_enable(1);
}

static int g_tran_gesture_mode = 1;
static u8 g_tran_gesture_code;

struct tran_gesture_map {
	char cmd[2];
	u8 unused;
	u8 enabled;
	u8 code;
	u32 type;
};

static struct tran_gesture_map g_tran_gesture_maps[] = {
	{ {'a', 'l'}, 0, 1, 0xff, 3 },
	{ {'c', 'c'}, 0, 1, 0xcc, 0 },
	{ {'a', 'a'}, 0, 0, 0xaa, 1 },
	{ {'b', 'b'}, 0, 0, 0xbb, 1 },
	{ {'a', 'b'}, 0, 0, 0xab, 1 },
	{ {'b', 'a'}, 0, 0, 0xba, 1 },
	{ {'5', 'e'}, 0, 0, 0x5e, 4 },
	{ {'3', 'e'}, 0, 0, 0x3e, 4 },
	{ {'f', 'p'}, 0, 1, 0xbe, 4 },
	{ {'w', '0'}, 0, 0, 'w',  2 },
	{ {'o', '0'}, 0, 0, 'o',  2 },
	{ {'m', '0'}, 0, 0, 'm',  2 },
	{ {'e', '0'}, 0, 0, 'e',  2 },
	{ {'c', '0'}, 0, 0, 'c',  2 },
	{ {'z', '0'}, 0, 0, 'z',  2 },
	{ {'s', '0'}, 0, 0, 's',  2 },
	{ {'v', '0'}, 0, 0, 'v',  2 },
};

void tran_gesture_set_code(u8 code)
{
	g_tran_gesture_code = code;
}
EXPORT_SYMBOL_GPL(tran_gesture_set_code);

static struct proc_dir_entry *g_gesture_data_procs[4];
static const char *const g_gesture_data_proc_names[4] = {
	"main_gesture_function",
	"gesture_function",
	"main_gesture_data",
	"gesture_data",
};

static struct proc_dir_entry *g_gesture_state_procs[2];
static const char *const g_gesture_state_proc_names[2] = {
	"main_gesture_state",
	"gesture_state",
};

static struct proc_dir_entry *g_gesture_flag_procs[3];
static const char *const g_gesture_flag_proc_names[3] = {
	"main_gesture_flag",
	"gesture_flag",
	"touch_gesture",
};

static ssize_t tran_gesture_data_read(struct file *file, char __user *buf,
				      size_t count, loff_t *ppos)
{
	char tmp[8];
	u8 code = g_tran_gesture_code;
	char ch = (code == 0xcc) ? 'f' : (char)code;
	int len;
	ssize_t ret;

	len = scnprintf(tmp, sizeof(tmp), "%c\n", ch);
	ret = simple_read_from_buffer(buf, count, ppos, tmp, len);
	if (ret > 0)
		TRAN_INFO("CTP gesture ret:%zd ppos:%lld ges:%02x char:%c",
			  ret, *ppos, code, ch);
	return ret;
}

static ssize_t tran_gesture_data_write(struct file *file, const char __user *buf,
				       size_t count, loff_t *ppos)
{
	char tmp[16];
	size_t n = min(count, sizeof(tmp) - 1);
	int i, en;

	if (!n)
		return -EINVAL;
	if (copy_from_user(tmp, buf, n))
		return -EFAULT;
	tmp[n] = '\0';

	if (n >= 3 && (tmp[2] == '1' || tmp[2] == '2')) {
		en = (tmp[2] == '1') ? 1 : 0;
		for (i = 0; i < ARRAY_SIZE(g_tran_gesture_maps); i++) {
			if (g_tran_gesture_maps[i].cmd[0] == tmp[0] &&
			    g_tran_gesture_maps[i].cmd[1] == tmp[1]) {
				g_tran_gesture_maps[i].enabled = en;
				TRAN_INFO("gesture_data_write %c%c code[0x%02x] enable[%d]",
					  tmp[0], tmp[1], g_tran_gesture_maps[i].code, en);
				if (g_tran_gesture_maps[i].code == 0xcc) {
					g_tran_gesture_mode = en;
					if (g_tran_ts.controller && g_tran_ts.controller->ops &&
					    g_tran_ts.controller->ops->tran_gesture_enable)
						g_tran_ts.controller->ops->tran_gesture_enable(en);
				}
				return count;
			}
		}
	}

	en = (tmp[0] != '0' && tmp[0] != '2' && tmp[0] != '\0' && tmp[0] != '\n') ? 1 : 0;
	g_tran_gesture_mode = en;
	TRAN_INFO("gesture data write fallback: '%c' -> enable=%d", tmp[0], en);
	if (g_tran_ts.controller && g_tran_ts.controller->ops &&
	    g_tran_ts.controller->ops->tran_gesture_enable)
		g_tran_ts.controller->ops->tran_gesture_enable(en);
	return count;
}

static ssize_t tran_gesture_status_read(struct file *file, char __user *buf,
					size_t count, loff_t *ppos)
{
	char tmp[256];
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(g_tran_gesture_maps); i++) {
		len += scnprintf(tmp + len, sizeof(tmp) - len, "%c%c : %d\n",
				 g_tran_gesture_maps[i].cmd[0],
				 g_tran_gesture_maps[i].cmd[1],
				 g_tran_gesture_maps[i].enabled);
	}
	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static ssize_t tran_gesture_proc_read(struct file *file, char __user *buf,
				      size_t count, loff_t *ppos)
{
	char tmp[8];
	int len;

	len = scnprintf(tmp, sizeof(tmp), "%d\n", g_tran_gesture_mode);
	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static ssize_t tran_gesture_proc_write(struct file *file, const char __user *buf,
				       size_t count, loff_t *ppos)
{
	char tmp[16];
	size_t n = min(count, sizeof(tmp) - 1);
	int en;

	if (!n)
		return -EINVAL;
	if (copy_from_user(tmp, buf, n))
		return -EFAULT;
	tmp[n] = '\0';
	en = (tmp[0] != '0' && tmp[0] != '2' && tmp[0] != '\0' && tmp[0] != '\n') ? 1 : 0;
	g_tran_gesture_mode = en;
	TRAN_INFO("gesture proc write: '%c' -> enable=%d", tmp[0], en);
	if (g_tran_ts.controller && g_tran_ts.controller->ops &&
	    g_tran_ts.controller->ops->tran_gesture_enable)
		g_tran_ts.controller->ops->tran_gesture_enable(en);
	return count;
}

static const struct proc_ops tran_gesture_data_proc_ops = {
	.proc_read = tran_gesture_data_read,
	.proc_write = tran_gesture_data_write,
};

static const struct proc_ops tran_gesture_state_proc_ops = {
	.proc_read = tran_gesture_status_read,
	.proc_write = tran_gesture_proc_write,
};

static const struct proc_ops tran_gesture_flag_proc_ops = {
	.proc_read = tran_gesture_proc_read,
	.proc_write = tran_gesture_proc_write,
};

static void tran_gesture_proc_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_gesture_data_proc_names); i++) {
		g_gesture_data_procs[i] = proc_create(g_gesture_data_proc_names[i],
						      0666, NULL,
						      &tran_gesture_data_proc_ops);
		if (!g_gesture_data_procs[i])
			TRAN_ERROR("Failed to create proc entry for %s",
				   g_gesture_data_proc_names[i]);
	}
	for (i = 0; i < ARRAY_SIZE(g_gesture_state_proc_names); i++) {
		g_gesture_state_procs[i] = proc_create(g_gesture_state_proc_names[i],
						       0666, NULL,
						       &tran_gesture_state_proc_ops);
		if (!g_gesture_state_procs[i])
			TRAN_ERROR("Failed to create proc entry for %s",
				   g_gesture_state_proc_names[i]);
	}
	for (i = 0; i < ARRAY_SIZE(g_gesture_flag_proc_names); i++) {
		g_gesture_flag_procs[i] = proc_create(g_gesture_flag_proc_names[i],
						      0666, NULL,
						      &tran_gesture_flag_proc_ops);
		if (!g_gesture_flag_procs[i])
			TRAN_ERROR("Failed to create proc entry for %s",
				   g_gesture_flag_proc_names[i]);
	}
}

static void tran_gesture_proc_deinit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_gesture_data_proc_names); i++) {
		if (g_gesture_data_procs[i]) {
			proc_remove(g_gesture_data_procs[i]);
			g_gesture_data_procs[i] = NULL;
		}
	}
	for (i = 0; i < ARRAY_SIZE(g_gesture_state_proc_names); i++) {
		if (g_gesture_state_procs[i]) {
			proc_remove(g_gesture_state_procs[i]);
			g_gesture_state_procs[i] = NULL;
		}
	}
	for (i = 0; i < ARRAY_SIZE(g_gesture_flag_proc_names); i++) {
		if (g_gesture_flag_procs[i]) {
			proc_remove(g_gesture_flag_procs[i]);
			g_gesture_flag_procs[i] = NULL;
		}
	}
}

enum tran_feat_gate {
	TRAN_FEAT_ALWAYS = 0,
	TRAN_FEAT_ESPORTS,
	TRAN_FEAT_CURVED_EDGE,
	TRAN_FEAT_DYNAMIC_RATE,
};

struct tran_mode_node {
	const char *name;
	int cmd;
	enum tran_feat_gate feat;
};

static const struct tran_mode_node g_tran_mode_nodes[] = {
	{ "border_suppression", TRAN_TP_CMD_EDGE_MODE, TRAN_FEAT_ALWAYS },
	{ "tp_report_rate_switch", TRAN_TP_CMD_REPORT_RATE, TRAN_FEAT_DYNAMIC_RATE },
	{ "tp_transient_report_rate_switch", TRAN_TP_CMD_TRANSIENT_RATE, TRAN_FEAT_ESPORTS },
	{ "game_mode", TRAN_TP_CMD_LOCK_POINT, TRAN_FEAT_ESPORTS },
	{ "game_state", TRAN_TP_CMD_GAME_STATE, TRAN_FEAT_ESPORTS },
	{ "tran_edge_level", TRAN_TP_CMD_EDGE_LEVEL, TRAN_FEAT_CURVED_EDGE },
};

static struct proc_dir_entry *g_tran_mode_procs[ARRAY_SIZE(g_tran_mode_nodes)];

static bool tran_is_feat_supported(enum tran_feat_gate feat)
{
	switch (feat) {
	case TRAN_FEAT_ALWAYS:
		return true;
	case TRAN_FEAT_ESPORTS:
		return g_tran_ts.esports_engine != 0;
	case TRAN_FEAT_CURVED_EDGE:
		return g_tran_ts.curved_edge_amt_support;
	case TRAN_FEAT_DYNAMIC_RATE:
		return g_tran_ts.dynamic_report_rate_support;
	default:
		return false;
	}
}

static int tran_mode_get(int cmd)
{
	switch (cmd) {
	case TRAN_TP_CMD_GAME_STATE:
		return g_tran_ts.game_state;
	case TRAN_TP_CMD_EDGE_MODE:
		return g_tran_ts.edge_mode;
	case TRAN_TP_CMD_LOCK_POINT:
		return g_tran_ts.lock_point;
	case TRAN_TP_CMD_REPORT_RATE:
		return g_tran_ts.tran_report_rate;
	case TRAN_TP_CMD_TRANSIENT_RATE:
		return g_tran_ts.transient_report_rate;
	case TRAN_TP_CMD_EDGE_LEVEL:
		return g_tran_ts.edge_level;
	default:
		return -EINVAL;
	}
}

static int tran_mode_update(int cmd, int val)
{
	switch (cmd) {
	case TRAN_TP_CMD_GAME_STATE:
		if (val < 0 || val > 1)
			return -EINVAL;
		g_tran_ts.game_state = val;
		break;
	case TRAN_TP_CMD_EDGE_MODE:
		if (val < 0 || val > 4)
			return -EINVAL;
		g_tran_ts.edge_mode = val;
		break;
	case TRAN_TP_CMD_LOCK_POINT:
		if (val < 0 || val > 5)
			return -EINVAL;
		g_tran_ts.lock_point = val;
		break;
	case TRAN_TP_CMD_REPORT_RATE:
		if (val < 0 || val > 5)
			return -EINVAL;
		g_tran_ts.tran_report_rate = val;
		break;
	case TRAN_TP_CMD_TRANSIENT_RATE:
		if (val < 0 || val > 1)
			return -EINVAL;
		g_tran_ts.transient_report_rate = val;
		break;
	case TRAN_TP_CMD_EDGE_LEVEL:
		if (val != 0 && val != 2 && val != 3)
			return -EINVAL;
		g_tran_ts.edge_level = val;
		break;
	default:
		return -EINVAL;
	}

	if (!g_tran_ts.controller) {
		TRAN_ERROR("no controller running");
		return -ENODEV;
	}
	if (g_tran_ts.ts_core_mode == TRAN_SUSPEND_MODE) {
		TRAN_INFO("tp in suspend mode, defer cmd 0x%x val %d", cmd, val);
		return 0;
	}

	tran_set_ts_mode(cmd, val);
	return 0;
}

static ssize_t tran_mode_proc_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	const struct tran_mode_node *node = pde_data(file_inode(file));
	char tmp[16];
	int val, len;

	if (!node)
		return -EINVAL;

	val = tran_mode_get(node->cmd);
	if (val < 0)
		return val;

	len = scnprintf(tmp, sizeof(tmp), "%d\n", val);
	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static ssize_t tran_mode_proc_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	const struct tran_mode_node *node = pde_data(file_inode(file));
	char tmp[16];
	size_t n = min(count, sizeof(tmp) - 1);
	int val, ret;

	if (!node || !n)
		return -EINVAL;
	if (copy_from_user(tmp, buf, n))
		return -EFAULT;
	tmp[n] = '\0';

	if (sscanf(tmp, "%d", &val) != 1)
		return -EINVAL;

	ret = tran_mode_update(node->cmd, val);
	return ret < 0 ? ret : count;
}

static const struct proc_ops tran_mode_proc_ops = {
	.proc_read = tran_mode_proc_read,
	.proc_write = tran_mode_proc_write,
};

static void tran_mode_proc_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_tran_mode_nodes); i++) {
		if (!tran_is_feat_supported(g_tran_mode_nodes[i].feat))
			continue;
		g_tran_mode_procs[i] = proc_create_data(g_tran_mode_nodes[i].name,
							0666, NULL,
							&tran_mode_proc_ops,
							(void *)&g_tran_mode_nodes[i]);
		if (!g_tran_mode_procs[i])
			TRAN_ERROR("Failed to create proc entry for %s",
				   g_tran_mode_nodes[i].name);
	}
}

static void tran_mode_proc_deinit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_tran_mode_nodes); i++) {
		if (g_tran_mode_procs[i]) {
			proc_remove(g_tran_mode_procs[i]);
			g_tran_mode_procs[i] = NULL;
		}
	}
}

struct tp_ioctl_data {
	int len;
	char data[256];
};

#define TRAN_TP_IOC_MAGIC		't'
#define TP_IOC_R_ROTATION		_IOR(TRAN_TP_IOC_MAGIC, 0x0a, struct tp_ioctl_data)
#define TP_IOC_W_ROTATION		_IOW(TRAN_TP_IOC_MAGIC, 0x0a, struct tp_ioctl_data)
#define TP_IOC_R_REPORT_RATE		_IOR(TRAN_TP_IOC_MAGIC, 0x0b, struct tp_ioctl_data)
#define TP_IOC_W_REPORT_RATE		_IOW(TRAN_TP_IOC_MAGIC, 0x0b, struct tp_ioctl_data)
#define TP_IOC_R_EDGE_LEVEL		_IOR(TRAN_TP_IOC_MAGIC, 0x0d, struct tp_ioctl_data)
#define TP_IOC_W_EDGE_LEVEL		_IOW(TRAN_TP_IOC_MAGIC, 0x0d, struct tp_ioctl_data)
#define TP_IOC_R_GAME_MODE		_IOR(TRAN_TP_IOC_MAGIC, 0x12, struct tp_ioctl_data)
#define TP_IOC_W_GAME_MODE		_IOW(TRAN_TP_IOC_MAGIC, 0x12, struct tp_ioctl_data)
#define TP_IOC_R_GAME_STATE		_IOR(TRAN_TP_IOC_MAGIC, 0x13, struct tp_ioctl_data)
#define TP_IOC_W_GAME_STATE		_IOW(TRAN_TP_IOC_MAGIC, 0x13, struct tp_ioctl_data)
#define TP_IOC_R_TRANSIENT_RATE		_IOR(TRAN_TP_IOC_MAGIC, 0x14, struct tp_ioctl_data)
#define TP_IOC_W_TRANSIENT_RATE		_IOW(TRAN_TP_IOC_MAGIC, 0x14, struct tp_ioctl_data)

static dev_t tp_poll_devt;
static struct cdev tp_poll_cdev;
static struct class *tp_poll_class;
static struct device *tp_poll_device;
static bool tp_poll_registered;

static long tp_poll_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct tp_ioctl_data kdata;
	int val = 0;
	int tp_cmd;
	bool is_read = false;

	memset(&kdata, 0, sizeof(kdata));

	if (cmd == TP_IOC_R_REPORT_RATE || cmd == TP_IOC_W_REPORT_RATE) {
		if (!g_tran_ts.dynamic_report_rate_support)
			return -EOPNOTSUPP;
		if (cmd == TP_IOC_R_REPORT_RATE &&
		    copy_to_user((void __user *)arg, &kdata, sizeof(kdata)))
			return -EFAULT;
		return 0;
	}

	switch (cmd) {
	case TP_IOC_R_ROTATION:
		is_read = true;
		fallthrough;
	case TP_IOC_W_ROTATION:
		tp_cmd = TRAN_TP_CMD_EDGE_MODE;
		break;
	case TP_IOC_R_EDGE_LEVEL:
		is_read = true;
		fallthrough;
	case TP_IOC_W_EDGE_LEVEL:
		if (!g_tran_ts.curved_edge_amt_support)
			return -EOPNOTSUPP;
		tp_cmd = TRAN_TP_CMD_EDGE_LEVEL;
		break;
	case TP_IOC_R_GAME_MODE:
		is_read = true;
		fallthrough;
	case TP_IOC_W_GAME_MODE:
		if (!g_tran_ts.esports_engine)
			return -EOPNOTSUPP;
		tp_cmd = TRAN_TP_CMD_LOCK_POINT;
		break;
	case TP_IOC_R_GAME_STATE:
		is_read = true;
		fallthrough;
	case TP_IOC_W_GAME_STATE:
		if (!g_tran_ts.esports_engine)
			return -EOPNOTSUPP;
		tp_cmd = TRAN_TP_CMD_GAME_STATE;
		break;
	case TP_IOC_R_TRANSIENT_RATE:
		is_read = true;
		fallthrough;
	case TP_IOC_W_TRANSIENT_RATE:
		if (!g_tran_ts.esports_engine)
			return -EOPNOTSUPP;
		tp_cmd = TRAN_TP_CMD_TRANSIENT_RATE;
		break;
	default:
		return -ENOTTY;
	}

	if (!g_tran_ts.controller)
		return -ENODEV;

	if (is_read) {
		val = tran_mode_get(tp_cmd);
		if (val < 0)
			return val;
		kdata.len = scnprintf(kdata.data, sizeof(kdata.data), "%d\n", val) + 1;
		if (copy_to_user((void __user *)arg, &kdata, sizeof(kdata)))
			return -EFAULT;
		return 0;
	}

	if (copy_from_user(&kdata, (void __user *)arg, sizeof(kdata)))
		return -EFAULT;
	kdata.data[sizeof(kdata.data) - 1] = '\0';

	if (sscanf(kdata.data, "%d", &val) != 1)
		return -EINVAL;

	return tran_mode_update(tp_cmd, val);
}

static const struct file_operations tp_poll_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = tp_poll_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static int tp_poll_drv_init(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(struct tp_ioctl_data) != 260);

	ret = alloc_chrdev_region(&tp_poll_devt, 0, 1, "tppolldrv");
	if (ret < 0) {
		TRAN_ERROR("Failed to allocate chrdev region for tppolldrv: %d", ret);
		return ret;
	}

	cdev_init(&tp_poll_cdev, &tp_poll_fops);
	tp_poll_cdev.owner = THIS_MODULE;
	ret = cdev_add(&tp_poll_cdev, tp_poll_devt, 1);
	if (ret < 0) {
		TRAN_ERROR("Failed to add cdev for tppolldrv: %d", ret);
		goto err_unreg;
	}

	tp_poll_class = class_create("tppollclass");
	if (IS_ERR(tp_poll_class)) {
		ret = PTR_ERR(tp_poll_class);
		tp_poll_class = NULL;
		TRAN_ERROR("Failed to create tppollclass: %d", ret);
		goto err_cdev;
	}

	tp_poll_device = device_create(tp_poll_class, NULL, tp_poll_devt, NULL, "tppolldrv");
	if (IS_ERR(tp_poll_device)) {
		ret = PTR_ERR(tp_poll_device);
		tp_poll_device = NULL;
		TRAN_ERROR("Failed to create /dev/tppolldrv: %d", ret);
		goto err_class;
	}

	tp_poll_registered = true;
	return 0;

err_class:
	class_destroy(tp_poll_class);
	tp_poll_class = NULL;
err_cdev:
	cdev_del(&tp_poll_cdev);
err_unreg:
	unregister_chrdev_region(tp_poll_devt, 1);
	return ret;
}

static void tp_poll_drv_exit(void)
{
	if (!tp_poll_registered)
		return;
	device_destroy(tp_poll_class, tp_poll_devt);
	class_destroy(tp_poll_class);
	cdev_del(&tp_poll_cdev);
	unregister_chrdev_region(tp_poll_devt, 1);
	tp_poll_device = NULL;
	tp_poll_class = NULL;
	tp_poll_registered = false;
}

static int tpd_probe(struct platform_device *pdev)
{
	struct tran_ts_platform *tran_pdata = g_tran_ts.platform_data;

	TRAN_FUNC_ENTER();

	if (!tran_pdata) {
		TRAN_ERROR("tran platform is NULL!");
		return 0;
	}

	INIT_WORK(&g_tran_ts.tpd_match_work, controller_match_work);
	INIT_WORK(&g_tran_ts.tpd_dsp_work, dsp_event_callback);
	init_completion(&g_tran_ts.comp);

	tran_parse_dt(&pdev->dev, &g_tran_ts);
	tran_gesture_proc_init();
	tran_mode_proc_init();
	tp_poll_drv_init();

	g_tran_ts.pdev = pdev;
	g_tran_ts.ts_core_mode = TRAN_ACTIVE_MODE;
	g_tran_ts.tpd_workqueue = create_singlethread_workqueue("tpd_work_queue");

	queue_work(g_tran_ts.tpd_init_workqueue, &g_tran_ts.tpd_match_work);

	TRAN_FUNC_EXIT();
	return 0;
}

static void tpd_remove(struct platform_device *pdev)
{
	struct tran_ts_platform *pdata = g_tran_ts.platform_data;

	tp_poll_drv_exit();
	tran_mode_proc_deinit();
	tran_gesture_proc_deinit();
	if (pdata && pdata->ops && pdata->ops->plat_deinit)
		pdata->ops->plat_deinit(&g_tran_ts);
}

static const struct dev_pm_ops tpd_pm_ops = {
	.suspend = NULL,
	.resume = NULL,
};

static const struct of_device_id touch_of_match[] = {
	{ .compatible = "mediatek,touch", },
	{ .compatible = "transsion,touch", },
	{},
};

static void tran_shutdown(struct platform_device *pdev)
{
	struct tran_ts_platform *pdata = g_tran_ts.platform_data;

	TRAN_FUNC_ENTER();
	if (pdata && pdata->ops && pdata->ops->plat_deinit)
		pdata->ops->plat_deinit(&g_tran_ts);
}

static struct platform_driver tpd_driver = {
	.remove = tpd_remove,
	.shutdown = tran_shutdown,
	.probe = tpd_probe,
	.driver = {
		.name = TPD_DEVICE,
		.pm = &tpd_pm_ops,
		.owner = THIS_MODULE,
		.of_match_table = touch_of_match,
	},
};

struct tran_core_ops g_tpd_ops = {
	.controller_init_completion = controller_init_finish,
	.controller_uninit_completion = controller_uninit_finish,
};

struct tran_ts_core g_tran_ts = {
	.supplier_head = LIST_HEAD_INIT(g_tran_ts.supplier_head),
	.controller_head = LIST_HEAD_INIT(g_tran_ts.controller_head),
	.controller_mutex = __MUTEX_INITIALIZER(g_tran_ts.controller_mutex),
	.dsp_event_mutex = __MUTEX_INITIALIZER(g_tran_ts.dsp_event_mutex),
	.tpd_ops = &g_tpd_ops,
};
EXPORT_SYMBOL_GPL(g_tran_ts);

unsigned char g_tran_tp_provide;
EXPORT_SYMBOL(g_tran_tp_provide);

static int __init tpd_device_init(void)
{
	TRAN_FUNC_ENTER();
	init_tran_platform_info();

	g_tran_ts.tpd_init_workqueue = create_singlethread_workqueue("tpd_init_queue");

	if (platform_driver_register(&tpd_driver) != 0)
		TRAN_ERROR("unable to register touch panel driver.");

	TRAN_FUNC_EXIT();
	return 0;
}

static void __exit tpd_device_exit(void)
{
	TRAN_FUNC_ENTER();
	platform_driver_unregister(&tpd_driver);
	TRAN_FUNC_EXIT();
}

module_init(tpd_device_init);
module_exit(tpd_device_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Transsion touch panel driver");
MODULE_AUTHOR("Transsion");
