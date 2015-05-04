#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#include <linux/pinctrl/consumer.h>

#include <linux/atmel-ssc.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#include "atmel-pcm.h"
#include "atmel_ssc_dai.h"

#include "../codecs/tlv320aic3x.h"

#define MCLK_RATE 12000000
#define DAI_FORMAT	(SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM)

static struct clk* mclk;

static int atmel_asoc_tlv320aic3107_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *const rtd = substream->private_data;
	struct snd_soc_dai *const codec_dai = rtd->codec_dai;
	struct snd_soc_dai *const cpu_dai = rtd->cpu_dai;
	int ret;

	/* Set the CODEC DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, DAI_FORMAT);
	if (ret < 0)
		return ret;

	/* Set the CPU DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, DAI_FORMAT);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops audio_operations = {
	.hw_params = atmel_asoc_tlv320aic3107_hw_params,
};

static int at91sam9n12_set_bias_level(struct snd_soc_card *card,
					struct snd_soc_dapm_context *dapm,
					enum snd_soc_bias_level level)
{
	static int mclk_on;
	int ret = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		if (!mclk_on)
			ret = clk_enable(mclk);
		if (ret == 0)
			mclk_on = 1;
		break;

	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		if (mclk_on)
			clk_disable(mclk);
		mclk_on = 0;
		break;
	}

	return ret;
}

static const struct snd_soc_dapm_widget atmel_soc_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Front Left", NULL),
	SND_SOC_DAPM_LINE("Left Capture", NULL),
};

static const struct snd_soc_dapm_route intercon[] = {
	{ "Left Class-D Out", NULL, "Front Left" },
	{ "LINE1L", "differential", "Left Capture" },
};

static int initialize(struct snd_soc_pcm_runtime *rtd)
{
	int ret;

	ret = snd_soc_dai_set_sysclk(rtd->codec_dai, CLKIN_MCLK, MCLK_RATE, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static struct snd_soc_dai_link atmel_asoc_tlv320aic3107_dai = {
	.name = "TLV320AIC3107",
	.stream_name = "TLV320AIC3107 PCM",
	.codec_dai_name = "tlv320aic3x-hifi",
	.dai_fmt = DAI_FORMAT,
	.init = initialize,
	.codec_name = "tlv320aic3x-codec.0-0018",
	.ops = &audio_operations,
};

static struct snd_soc_card asoc_tlv320aic3107_card = {
	.name = "asoc_tlv320aic3107",
	.owner = THIS_MODULE,
	.dai_link = &atmel_asoc_tlv320aic3107_dai,
	.num_links = 1,
	.set_bias_level = at91sam9n12_set_bias_level,

	.dapm_widgets = atmel_soc_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(atmel_soc_dapm_widgets),
	.dapm_routes = intercon,
	.num_dapm_routes = ARRAY_SIZE(intercon),
};

static int atmel_snd_soc_parse_card(struct snd_soc_card *const card)
{
	int result;

	result = snd_soc_of_parse_card_name(card, "atmel,model");
	if (result)
	{
		dev_err(card->dev, "Failed to parse the card name\n");
		return result;
	}

	result = snd_soc_of_parse_audio_routing(card, "atmel,audio-routing");
	if (result)
	{
		dev_err(card->dev, "Failed to parse audio-routing\n");
		return result;
	}

	return 0;
}

static int atmel_snd_soc_parse_node(struct platform_device *const pdev, struct snd_soc_dai_link *const dai_link)
{
	struct device_node* node = pdev->dev.of_node;
	struct device_node* codec_node;
	struct device_node* cpu_node;

	/* Parse the CODEC info */
	dai_link->codec_name = NULL;
	codec_node = of_parse_phandle(node, "atmel,audio-codec", 0);
	if (!codec_node)
	{
		dev_err(&pdev->dev, "CODEC info missing\n");
		return -EINVAL;
	}
	dai_link->codec_of_node = codec_node;

	/* Parse the DAI and platform info */
	dai_link->cpu_dai_name = NULL;
	dai_link->platform_name = NULL;
	cpu_node = of_parse_phandle(node, "atmel,ssc-controller", 0);
	if (!cpu_node)
	{
		dev_err(&pdev->dev, "DAI and PCM info missing\n");
		return -EINVAL;
	}
	dai_link->cpu_of_node = cpu_node;
	dai_link->platform_of_node = cpu_node;

	of_node_put(codec_node);
	of_node_put(cpu_node);

	return 0;
}

static int atmel_audio_probe(struct platform_device *pdev)
{
	struct device_node *const np = pdev->dev.of_node;
	struct snd_soc_card *const card = &asoc_tlv320aic3107_card;

	struct snd_soc_dai_link *const dai_link = &atmel_asoc_tlv320aic3107_dai;
	int id = of_alias_get_id(np, "ssc");
	struct clk *pllb;
	struct pinctrl *pinctrl;

	int ret;

	if( id < 0 )
		id = 0;

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		dev_err(&pdev->dev, "Failed to request pinctrl for mck\n");
		return PTR_ERR(pinctrl);
	}

	ret = atmel_ssc_set_audio(id);
	if (ret) {
		dev_err(&pdev->dev, "ssc channel %d is not valid\n", id);
		return -EINVAL;
	}

	/*
	 * Codec MCLK is supplied by PCK0 - set it up.
	 */
	mclk = clk_get(NULL, "pck0");
	if (IS_ERR(mclk)) {
		printk(KERN_ERR "ASoC: Failed to get MCLK\n");
		ret = PTR_ERR(mclk);
		goto err;
	}

	pllb = clk_get(NULL, "pllb");
	if (IS_ERR(pllb)) {
		printk(KERN_ERR "ASoC: Failed to get PLLB\n");
		ret = PTR_ERR(pllb);
		goto err_mclk;
	}
	ret = clk_set_parent(mclk, pllb);
	clk_put(pllb);
	if (ret != 0) {
		printk(KERN_ERR "ASoC: Failed to set MCLK parent\n");
		goto err_mclk;
	}

	clk_set_rate(mclk, MCLK_RATE);

	card->dev = &pdev->dev;

	ret = atmel_snd_soc_parse_card(card);
	if( ret )
		goto err;

	/* Parse device node info */
	if (np)
	{
		ret = atmel_snd_soc_parse_node(pdev, dai_link);
		if( ret )
			goto err;
	}

	ret = snd_soc_register_card(card);
	if (ret) {
		printk(KERN_ERR "ASoC: snd_soc_register_card() failed\n");
		goto err;
	}

	return ret;

err_mclk:
	clk_put(mclk);
	mclk = NULL;
err:
	printk(KERN_ERR "ASoC: releasing ssc...\n");
	atmel_ssc_put_audio(id);
	return ret;
}

static int atmel_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_dai_link *const dai_link = &atmel_asoc_tlv320aic3107_dai;
	struct device_node *const node = (struct device_node *)dai_link->cpu_of_node;

	clk_disable(mclk);
	mclk = NULL;

	snd_soc_unregister_card(platform_get_drvdata(pdev));
	atmel_ssc_put_audio(of_alias_get_id(node, "ssc"));

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id atmel_asoc_tlv320aic3107_dt_ids[] = {
	{ .compatible = "atmel,asoc-tlv320aic3107", },
	{ }
};
MODULE_DEVICE_TABLE(of, at91sam9n12_tlv320aic3x_dt_ids);
#endif

static struct platform_driver atmel_audio_driver = {
	.driver = {
		.name	= "atmel-tlv320aic3107-audio",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(atmel_asoc_tlv320aic3107_dt_ids),
	},
	.probe	= atmel_audio_probe,
	.remove	= atmel_audio_remove,
};

module_platform_driver(atmel_audio_driver);

/* Module information */
MODULE_AUTHOR("Nick Knudson <nick@sproutling.com>");
MODULE_DESCRIPTION("ALSA SoC machine driver for Atmel with TLV320AIC3107 CODEC");
MODULE_ALIAS("platform:at91sam9n12-audio");
MODULE_LICENSE("GPL");

