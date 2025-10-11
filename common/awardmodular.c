// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 Dang Huynh
 *
 * Simulates the Award Modular BIOS bootup sequence, like it's 1995.
 */

#include <vsprintf.h>
#include <dm.h>
#include <video_console.h>
#include <config.h>
#include <console.h>
#include <display_options.h>
#include <awardmodular.h>

#if defined(CONFIG_VIDEO_FONT_8X16) && !defined(CONFIG_AWARDMODULAR_START_16X32)
#include <modular_awardlogo_8x16.h>
#include <modular_epalogo_8x16.h>
#endif

#if defined(CONFIG_VIDEO_FONT_16X32)
#include <modular_awardlogo_16x32.h>
#include <modular_epalogo_16x32.h>
#endif

#include <bmp_layout.h>
#include <cpu.h>
#include <video.h>
#include <ram.h>
#include <init.h>
#include <version.h>
#include <sysinfo.h>

#define ERR_RET(x) { ret = (x); if (ret < 0) return ret; }

struct modular_bios {
	bool initialized;

	struct udevice *cdev;
	struct udevice *vdev;

	/* RAM size */
	unsigned long ram_size;

	/* Board Information */
	const char *model;
	char model_buf[80];

	/* CPU Info */
	const char *cpumodel;
	char cpumodel_buf[80];
	int cpucount;
	unsigned long cpufreq;

	/* EPA Logo */
	struct bmp_image *epa_logo;
	int epa_pos[2];

	/* Award Logo */
	struct bmp_image *award_logo;
};

static struct modular_bios modular_bios_priv;

static void modular_position_curline(struct udevice *cdev, int style)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(cdev);
	int y = priv->ycur / priv->y_charsize;
	int x = 0;

	switch (style) {
	case 1:
		x = priv->cols / 2;
		break;
	case 2:
		x = priv->cols - 1;
		break;
	default:
		break;
	}

	vidconsole_position_cursor(cdev, x, y);
}

static int modular_put_string_center(struct udevice *cdev, const char *str)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(cdev);
	int x, y;

	y = priv->ycur / priv->y_charsize;
	x = priv->cols / 2;

	vidconsole_position_cursor(cdev, x - (strlen(str) / 2), y);

	return vidconsole_put_string(cdev, str);
}

static int get_modular_board_model(void)
{
	struct modular_bios *priv = &modular_bios_priv;
	struct udevice *dev = NULL;
	int ret;

	priv->model = priv->model_buf;

	if (IS_ENABLED(CONFIG_SYSINFO)) {
		sysinfo_get(&dev);
		if (dev) {
			ret = sysinfo_get_str(dev, SYSID_BOARD_MODEL,
					      sizeof(priv->model_buf), priv->model_buf);
			if (ret == 0)
				return 0;
		}
	}

	if (IS_ENABLED(CONFIG_OF_CONTROL)) {
		priv->model = fdt_getprop(gd->fdt_blob, 0, "model", NULL);
		if (priv->model)
			return 0;
	}

	return -ENODEV;
}

static int get_modular_cpu_info(void)
{
	struct modular_bios *priv = &modular_bios_priv;
	int cpucount = 1;

	priv->cpumodel = priv->cpumodel_buf;

#if IS_ENABLED(CONFIG_CPU)
	struct udevice *cpudev = NULL;
	struct cpu_info cpuinfo;
	int ret;

	cpudev = cpu_get_current_dev();
	if (cpudev) {
		cpucount = cpu_get_count(cpudev);

		ret = cpu_get_info(cpudev, &cpuinfo);
		if (!ret) {
			priv->cpufreq = cpuinfo.cpu_freq;
			priv->cpucount = cpucount;
		}

		ret = cpu_get_desc(cpudev, priv->cpumodel_buf, sizeof(priv->cpumodel_buf));
		if (ret == 0)
			return 0;
	}
#endif

	priv->cpumodel = "Unknown";
	priv->cpucount = cpucount;
	priv->cpufreq = 0;

	/* Convert to MHz */
	if (priv->cpufreq > 0)
		priv->cpufreq /= 1000000;

	return 0;
}

static int get_modular_ram_size(void)
{
	struct modular_bios *priv = &modular_bios_priv;
	unsigned long ram_size = 0;

#if IS_ENABLED(CONFIG_RAM)
	struct udevice *ramdev;
	struct ram_info ram;
	int ret;

	uclass_get_device(UCLASS_RAM, 0, &ramdev);

	if (ramdev) {
		ret = ram_get_info(ramdev, &ram);
		if (ret == 0)
			ram_size = ram.size;
	}
#endif

	/*
	 * If obtaining RAM size from RAM udevice fails, we'll get the
	 * RAM size reported by U-Boot.
	 */
	if (ram_size == 0)
		ram_size = get_effective_memsize();

	/* convert bytes to kbytes */
	ram_size /= 1024;

	priv->ram_size = ram_size;

	return 0;
}

void epa_logo_fade(void)
{
	struct modular_bios *priv = &modular_bios_priv;
	struct bmp_image *epa_logo = priv->epa_logo;
	u8 *data;
	u32 image_size;
	int i, j;

	if (!priv->initialized)
		return;

	data = (void *)epa_logo + epa_logo->header.data_offset;
	image_size = epa_logo->header.image_size;

	for (i = 0; i < 255; i++) {
		for (j = 0; j < image_size; j++) {
			if (data[j] > 0)
				data[j] -= 1;
		}

		video_bmp_display(priv->vdev, (ulong)epa_logo,
				  priv->epa_pos[0], priv->epa_pos[1], false);
		video_sync(priv->vdev, true);
	}
}

static int print_award_cpu(void)
{
	struct modular_bios *priv = &modular_bios_priv;
	char buf[80];

	if (!priv->initialized)
		return -ENOMEM;

	snprintf(buf, sizeof(buf), "Main Processor : %s %ldMHz, %d CPU(s)\n",
		 priv->cpumodel, priv->cpufreq, priv->cpucount);

	return vidconsole_put_string(priv->cdev, buf);
}

static int print_award_memory_test(void)
{
	struct modular_bios *priv = &modular_bios_priv;
	struct vidconsole_priv *cdev_priv;
	unsigned long ram_size;
	char memstr[16];
	int x, y;
	int i, j;

	if (!priv->initialized)
		return -ENODEV;

	if (priv->ram_size == 0)
		return -EINVAL;

	ram_size = priv->ram_size;

	cdev_priv = dev_get_uclass_priv(priv->cdev);

	vidconsole_put_string(priv->cdev, "Memory Testing : ");

	y = cdev_priv->ycur / cdev_priv->y_charsize;
	x = VID_TO_PIXEL(cdev_priv->xcur_frac - cdev_priv->xstart_frac) /
		cdev_priv->x_charsize;

	for (j = 0; j < 3; j++) {
		i = 0;

		while (i < ram_size) {
			if (tstc())
				goto skip_memtest;

			i += 2048;

			/* prevent overflow */
			if (i > ram_size)
				i = ram_size;

			snprintf(memstr, ARRAY_SIZE(memstr), "%8dK OK", i);

			vidconsole_position_cursor(priv->cdev, x, y);
			vidconsole_put_string(priv->cdev, memstr);
			video_sync(priv->vdev, true);
		}
	}

skip_memtest:
	vidconsole_put_string(priv->cdev, "\n\n");

	vidconsole_position_cursor(priv->cdev, 0, y + 2);

	return 0;
}

int print_modular_bios(void)
{
	struct modular_bios *priv = &modular_bios_priv;
	struct vidconsole_priv *cdev_priv;
	struct udevice *cdev;
	struct udevice *vdev;
	struct bmp_header *bmphdr;
	char plainver[ARRAY_SIZE(PLAIN_VERSION)];
	int ret;

	if (!priv->initialized)
		return -ENOMEM;

	cdev = priv->cdev;
	vdev = priv->vdev;

	if (IS_ENABLED(CONFIG_AWARDMODULAR_START_16X32))
		vidconsole_select_font(priv->cdev, "16x32", 0);

	ERR_RET(vidconsole_clear_and_reset(cdev));

	/* convert auto generated plain version to uppercase */
	str_to_upper(PLAIN_VERSION, plainver, sizeof(plainver));

	cdev_priv = dev_get_uclass_priv(cdev);

	vidconsole_position_cursor(cdev, 3, 1);

	/* print logo */
	bmphdr = &priv->epa_logo->header;
	priv->epa_pos[0] = video_get_xsize(vdev) - bmphdr->width;
	priv->epa_pos[1] = cdev_priv->ycur;
	ERR_RET(video_bmp_display(vdev, (ulong)priv->epa_logo,
				  priv->epa_pos[0], priv->epa_pos[1], false));

	ERR_RET(video_bmp_display(vdev, (ulong)priv->award_logo,
				  0, cdev_priv->ycur, false));

	ERR_RET(vidconsole_put_string(cdev, "Award Modular BIOS v6.00PG, An Energy Star Ally\n"
					"Copyright (C) 1984-2000, Award Software, Inc.\n\n"));

	/* Print the footer */
	vidconsole_position_cursor(cdev, 0, cdev_priv->rows - 2);
	vidconsole_put_string(cdev, "Press DEL to enter SETUP\n");
	vidconsole_put_string(cdev, plainver);

	vidconsole_position_cursor(cdev, 0, 4);

	vidconsole_put_string(cdev, priv->model);
	vidconsole_put_string(cdev, "\n\n");

	video_sync(priv->vdev, true);

	ERR_RET(print_award_cpu());
	ERR_RET(print_award_memory_test());

	return 0;
}

int print_modular_bios_second(void)
{
	struct modular_bios *priv = &modular_bios_priv;
	struct vidconsole_priv *cdev_priv;
	struct udevice *vdev;
	struct udevice *cdev;
	char buf[80];
	int ret;
	int i;

	if (!priv->initialized)
		return -ENOMEM;

	cdev = priv->cdev;
	vdev = priv->vdev;

	if (IS_ENABLED(CONFIG_AWARDMODULAR_START_16X32))
		vidconsole_select_font(priv->cdev, NULL, 0);

	ERR_RET(vidconsole_clear_and_reset(cdev));

	cdev_priv = dev_get_uclass_priv(cdev);

	ERR_RET(modular_put_string_center(cdev, "Award Software, Inc.\n"));
	ERR_RET(modular_put_string_center(cdev, "System Configurations\n"));

	modular_position_curline(cdev, 0);

	/* TODO: this part is very ugly */
	ERR_RET(vidconsole_put_string(cdev, "╔"));
	for (i = 0; i < cdev_priv->cols - 2; i++)
		ERR_RET(vidconsole_put_string(cdev, "═"));

	ERR_RET(vidconsole_put_string(cdev, "╗"));
	modular_position_curline(cdev, 0);

	/* start specs */
	ERR_RET(vidconsole_put_string(cdev, "║"));

	snprintf(buf, sizeof(buf), " %-20s: %s", "CPU Type", priv->cpumodel);
	ERR_RET(vidconsole_put_string(cdev, buf));

	modular_position_curline(cdev, 1);
	snprintf(buf, sizeof(buf), "%-20s: %s", "Base Memory", "640K");
	ERR_RET(vidconsole_put_string(cdev, buf));

	modular_position_curline(cdev, 2);
	ERR_RET(vidconsole_put_string(cdev, "║"));
	modular_position_curline(cdev, 0);

	ERR_RET(vidconsole_put_string(cdev, "║"));

	snprintf(buf, sizeof(buf), " %-20s: %s", "Co-Processor", "Installed");
	ERR_RET(vidconsole_put_string(cdev, buf));

	modular_position_curline(cdev, 1);
	snprintf(buf, sizeof(buf), "%-20s: %ldK", "Extended Memory", priv->ram_size);
	ERR_RET(vidconsole_put_string(cdev, buf));

	modular_position_curline(cdev, 2);
	ERR_RET(vidconsole_put_string(cdev, "║"));
	modular_position_curline(cdev, 0);

	/* end specs */

	ERR_RET(vidconsole_put_string(cdev, "╚"));
	for (i = 0; i < cdev_priv->cols - 2; i++)
		ERR_RET(vidconsole_put_string(cdev, "═"));

	ERR_RET(vidconsole_put_string(cdev, "╝"));

	ERR_RET(vidconsole_put_string(cdev, "\n\n"));

	modular_position_curline(cdev, 0);

	return 0;
}

void init_modular_bios(void)
{
	struct modular_bios *priv = &modular_bios_priv;

	priv->initialized = false;

	if (!video_is_active())
		return;

	uclass_get_device(UCLASS_VIDEO, 0, &priv->vdev);
	uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &priv->cdev);

	if (!priv->vdev || !priv->cdev)
		return;

	get_modular_board_model();
	get_modular_cpu_info();
	get_modular_ram_size();

#if IS_ENABLED(CONFIG_AWARDMODULAR_START_16X32)
	priv->epa_logo = (void *)&epalogo_16x32_bitmap;
	priv->award_logo = (void *)&awardlogo_16x32_bitmap;
#else
	unsigned int fontsize;

	if (vidconsole_get_font_size(priv->cdev, NULL, &fontsize) < 0)
		return;

	switch (fontsize) {
#if defined(CONFIG_VIDEO_FONT_8X16)
	case 8:
		priv->epa_logo = (void *)&epalogo_8x16_bitmap;
		priv->award_logo = (void *)&awardlogo_8x16_bitmap;
		break;
#endif
#if defined(CONFIG_VIDEO_FONT_16X32)
	case 16:
		priv->epa_logo = (void *)&epalogo_16x32_bitmap;
		priv->award_logo = (void *)&awardlogo_16x32_bitmap;
		break;
#endif
	default:
		break;
	}
#endif

	if (!priv->epa_logo || !priv->award_logo)
		return;

	priv->initialized = true;
}
