#include "plugin.hpp"
#include "gtgComponents.hpp"
#include "gtgDSP.hpp"

struct BusDepot : Module {
	enum ParamIds {
		ON_PARAM,
		AUX_PARAM,
		LEVEL_PARAM,
		FADE_PARAM,
		FADE_IN_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ON_CV_INPUT,
		LEVEL_CV_INPUT,
		LMP_INPUT,
		R_INPUT,
		BUS_INPUT,
		FADE_CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		LEFT_OUTPUT,
		RIGHT_OUTPUT,
		BUS_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ON_LIGHT,
		ENUMS(LEFT_LIGHTS, 9),
		ENUMS(RIGHT_LIGHTS, 9),
		NUM_LIGHTS
	};

	int color_theme = 0;

	dsp::VuMeter2 vu_meters[2];
	dsp::ClockDivider vu_divider;
	dsp::ClockDivider light_divider;
	dsp::SchmittTrigger on_trigger;
	dsp::SchmittTrigger on_cv_trigger;
	AutoFader depot_fader;
	SimpleSlewer level_smoother;

	const int level_speed = 26;   // for level cv filter
	float peak_left = 0;
	float peak_right = 0;
	bool level_cv_filter = true;
	int fade_cv_mode = 0;

	BusDepot() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(ON_PARAM, 0.f, 1.f, 0.f, "Output on");   // depot_fader defaults to on and creates a quick fade up
		configParam(AUX_PARAM, 0.f, 1.f, 1.f, "Aux level in");
		configParam(LEVEL_PARAM, 0.f, 1.f, 1.f, "Master level");
		configParam(FADE_PARAM, 26, 17000, 26, "Fade out automation in milliseconds");
		configParam(FADE_IN_PARAM, 26, 17000, 26, "Fade in automation in milliseconds");
		vu_meters[0].lambda = 25.f;
		vu_meters[1].lambda = 25.f;
		vu_divider.setDivision(512);
		light_divider.setDivision(64);
		depot_fader.setSpeed(26);
		level_smoother.setSlewSpeed(level_speed);   // for level cv filter
		color_theme = loadDefaultTheme();
	}

	void process(const ProcessArgs &args) override {
		// on off button with fader that filters pops
		if (on_trigger.process(params[ON_PARAM].getValue()) + on_cv_trigger.process(inputs[ON_CV_INPUT].getVoltage())) {
			depot_fader.on = !depot_fader.on;
		}

		depot_fader.process();

		// process sound
		float summed_out[2] = {0.f, 0.f};
		if (depot_fader.getFade() > 0) {   // don't need to process sound when silent

			// get param levels
			float aux_level = params[AUX_PARAM].getValue();
			float master_level = clamp(inputs[LEVEL_CV_INPUT].getNormalVoltage(10.0f) * 0.1f, 0.0f, 1.0f) * params[LEVEL_PARAM].getValue();
			if (level_cv_filter) master_level = level_smoother.slew(master_level);
			float fade = depot_fader.getExpFade(2.5);   // exponential fade for fade automation

			// get aux inputs
			float stereo_in[2] = {0.f, 0.f};
			if (inputs[R_INPUT].isConnected()) {   // get a channel from each cable
				stereo_in[0] = inputs[LMP_INPUT].getVoltage() * aux_level;
				stereo_in[1] = inputs[R_INPUT].getVoltage() * aux_level;
			} else {   // get mono polyphonic cable sum from LMP
				float lmp_in = inputs[LMP_INPUT].getVoltageSum() * aux_level;
				for (int c = 0; c < 2; c++) {
					stereo_in[c] = lmp_in;
				}
			}

			// bus inputs with levels
			float bus_in[6] = {};

			// get blue and orange buses with levels
			for (int c = 0; c < 4; c++) {
				bus_in[c] = inputs[BUS_INPUT].getPolyVoltage(c) * master_level * fade;
			}

			// get red levels and add aux inputs
			for (int c = 4; c < 6; c++) {
				bus_in[c] = (stereo_in[c - 4] + inputs[BUS_INPUT].getPolyVoltage(c)) * master_level * fade;
			}

			// set bus outputs
			for (int c = 0; c < 6; c++) {
				outputs[BUS_OUTPUT].setVoltage(bus_in[c], c);
			}

			// set three stereo bus outputs on bus out
			outputs[BUS_OUTPUT].setChannels(6);

			// sum stereo mix for stereo outputs and light levels
			for (int c = 0; c < 2; c++) {
				summed_out[c] = bus_in[c] + bus_in[c + 2] + bus_in[c + 4];
			}

			// set stereo mix out
			outputs[LEFT_OUTPUT].setVoltage(summed_out[0]);
			outputs[RIGHT_OUTPUT].setVoltage(summed_out[1]);
		}

		// hit peak lights accurately by polling every sample
		if (summed_out[0] > 10.f) peak_left = 1.f;
		if (summed_out[1] > 10.f) peak_right = 1.f;

		// get levels for lights
		if (vu_divider.process()) {   // check levels infrequently
			for (int i = 0; i < 2; i++) {
				vu_meters[i].process(args.sampleTime * vu_divider.getDivision(), summed_out[i] / 10.f);
			}
		}

		if (light_divider.process()) {   // set lights and fade speed infrequently

			// set fade speed infrequently
			if (inputs[FADE_CV_INPUT].isConnected()) {
				if (depot_fader.on) {   // fade in with CV
					if (fade_cv_mode == 0 || fade_cv_mode == 1) {
						depot_fader.setSpeed(std::round((clamp(inputs[FADE_CV_INPUT].getNormalVoltage(0.0f) * 0.1f, 0.0f, 1.0f) * 16974.f) + 26.f));   // 26 to 17000 milliseconds
					} else {
						if (params[FADE_IN_PARAM].getValue() != depot_fader.last_speed) {
							depot_fader.setSpeed(params[FADE_IN_PARAM].getValue());
						}
					}
				} else {   // fade out with CV
					if (fade_cv_mode == 0 || fade_cv_mode == 2) {
						depot_fader.setSpeed(std::round((clamp(inputs[FADE_CV_INPUT].getNormalVoltage(0.0f) * 0.1f, 0.0f, 1.0f) * 16974.f) + 26.f));   // 26 to 17000 milliseconds
					} else {
						if (params[FADE_PARAM].getValue() != depot_fader.last_speed) {
							depot_fader.setSpeed(params[FADE_PARAM].getValue());
						}
					}
				}
			} else {   // fade without CV
				if (params[FADE_IN_PARAM].getValue() != depot_fader.last_speed) {
					if (depot_fader.on) {   // calculate fade in speed
						depot_fader.setSpeed(params[FADE_IN_PARAM].getValue());
					} else {   // calculate fade out speed
						depot_fader.setSpeed(params[FADE_PARAM].getValue());
					}
				}
			}

			// set on light
			if (depot_fader.getFade() > 0 && depot_fader.getFade() < depot_fader.getGain()) {
				lights[ON_LIGHT].value = 0.3f * (depot_fader.getFade() / depot_fader.getGain()) + 0.25f;   // dim light shows click and fade progress
			} else {
				lights[ON_LIGHT].value = depot_fader.getFade();
			}

			// make peak lights stay on when hit
			if (peak_left > 0) peak_left -= 15.f / args.sampleRate; else peak_left = 0.f;
			if (peak_right > 0) peak_right -= 15.f / args.sampleRate; else peak_right = 0.f;
			lights[LEFT_LIGHTS + 0].setBrightness(peak_left);
			lights[RIGHT_LIGHTS + 0].setBrightness(peak_right);

			// green and yellow lights
			for (int i = 1; i < 9; i++) {
				lights[LEFT_LIGHTS + i].setBrightness(vu_meters[0].getBrightness((-6 * i), -6 * (i - 1)));
				lights[RIGHT_LIGHTS + i].setBrightness(vu_meters[1].getBrightness((-6 * i), -6 * (i - 1)));
			}
		}
	}

	// save on button state
	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "input_on", json_integer(depot_fader.on));
		json_object_set_new(rootJ, "level_cv_filter", json_integer(level_cv_filter));
		json_object_set_new(rootJ, "color_theme", json_integer(color_theme));
		json_object_set_new(rootJ, "fade_cv_mode", json_integer(fade_cv_mode));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *input_onJ = json_object_get(rootJ, "input_on");
		if (input_onJ) depot_fader.on = json_integer_value(input_onJ);
		json_t *level_cv_filterJ = json_object_get(rootJ, "level_cv_filter");
		if (level_cv_filterJ) {
			level_cv_filter = json_integer_value(level_cv_filterJ);
		} else {
			if (input_onJ) level_cv_filter = false;   // do not change existing patches
		}
		json_t *color_themeJ = json_object_get(rootJ, "color_theme");
		if (color_themeJ) color_theme = json_integer_value(color_themeJ);
		json_t *fade_cv_modeJ = json_object_get(rootJ, "fade_cv_mode");
		if (fade_cv_modeJ) {
			fade_cv_mode = json_integer_value(fade_cv_modeJ);
		} else {
			if (input_onJ) {
				params[FADE_IN_PARAM].setValue(params[FADE_PARAM].getValue());   // same behavior on patches saved before fade in knob existed
			}
		}
	}

	void onSampleRateChange() override {
		depot_fader.setSpeed(params[FADE_PARAM].getValue());
		level_smoother.setSlewSpeed(level_speed);
	}

	void onReset() override {
		depot_fader.on = true;
		depot_fader.setGain(1.f);
		level_cv_filter = true;
		fade_cv_mode = 0;
	}
};


struct BusDepotWidget : ModuleWidget {
	SvgPanel* night_panel;

	BusDepotWidget(BusDepot *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/BusDepot.svg")));

		// load night panel if not preview
		if (module) {
			night_panel = new SvgPanel();
			night_panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/BusDepot_Night.svg")));
			night_panel->visible = false;
			addChild(night_panel);
		}

		addChild(createThemedWidget<gtgScrewUp>(Vec(RACK_GRID_WIDTH, 0), module ? &module->color_theme : NULL));
		addChild(createThemedWidget<gtgScrewUp>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0), module ? &module->color_theme : NULL));
		addChild(createThemedWidget<gtgScrewUp>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH), module ? &module->color_theme : NULL));
		addChild(createThemedWidget<gtgScrewUp>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH), module ? &module->color_theme : NULL));

		addParam(createThemedParamCentered<gtgBlackButton>(mm2px(Vec(15.24, 15.20)), module, BusDepot::ON_PARAM, module ? &module->color_theme : NULL));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(15.24, 15.20)), module, BusDepot::ON_LIGHT));
		addParam(createThemedParamCentered<gtgBlackTinyKnob>(mm2px(Vec(15.24, 60.48)), module, BusDepot::AUX_PARAM, module ? &module->color_theme : NULL));
		addParam(createThemedParamCentered<gtgBlackKnob>(mm2px(Vec(15.24, 83.88)), module, BusDepot::LEVEL_PARAM, module ? &module->color_theme : NULL));
		addParam(createThemedParamCentered<gtgGrayTinySnapKnob>(mm2px(Vec(15.24, 42.54)), module, BusDepot::FADE_PARAM, module ? &module->color_theme : NULL));
		addParam(createThemedParamCentered<gtgGrayTinySnapKnob>(mm2px(Vec(15.24, 26.15)), module, BusDepot::FADE_IN_PARAM, module ? &module->color_theme : NULL));

		addInput(createThemedPortCentered<gtgKeyPort>(mm2px(Vec(23.6, 21.1)), true, module, BusDepot::ON_CV_INPUT, module ? &module->color_theme : NULL));
		addInput(createThemedPortCentered<gtgKeyPort>(mm2px(Vec(15.24, 71.63)), true, module, BusDepot::LEVEL_CV_INPUT, module ? &module->color_theme : NULL));
		addInput(createThemedPortCentered<gtgNutPort>(mm2px(Vec(6.95, 21.1)), true, module, BusDepot::LMP_INPUT, module ? &module->color_theme : NULL));
		addInput(createThemedPortCentered<gtgNutPort>(mm2px(Vec(6.95, 31.2)), true, module, BusDepot::R_INPUT, module ? &module->color_theme : NULL));
		addInput(createThemedPortCentered<gtgNutPort>(mm2px(Vec(7.45, 114.1)), true, module, BusDepot::BUS_INPUT, module ? &module->color_theme : NULL));
		addInput(createThemedPortCentered<gtgKeyPort>(mm2px(Vec(23.6, 31.2)), true, module, BusDepot::FADE_CV_INPUT, module ? &module->color_theme : NULL));

		addOutput(createThemedPortCentered<gtgNutPort>(mm2px(Vec(23.1, 103.85)), false, module, BusDepot::LEFT_OUTPUT, module ? &module->color_theme : NULL));
		addOutput(createThemedPortCentered<gtgNutPort>(mm2px(Vec(23.1, 114.1)), false, module, BusDepot::RIGHT_OUTPUT, module ? &module->color_theme : NULL));
		addOutput(createThemedPortCentered<gtgNutPort>(mm2px(Vec(7.45, 103.85)), false, module, BusDepot::BUS_OUTPUT, module ? &module->color_theme : NULL));

		// create vu lights
		for (int i = 0; i < 9; i++) {
			float spacing = i * 5.25;
			float top = 50.0;
			if (i < 1 ) {
				addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(5.45, top + spacing)), module, BusDepot::LEFT_LIGHTS + i));
				addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(25.1, top + spacing)), module, BusDepot::RIGHT_LIGHTS + i));
			} else {
				if (i < 2) {
					addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(5.45, top + spacing)), module, BusDepot::LEFT_LIGHTS + i));
					addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(25.1, top + spacing)), module, BusDepot::RIGHT_LIGHTS + i));
				} else {
					addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(5.45, top + spacing)), module, BusDepot::LEFT_LIGHTS + i));
					addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(25.1, top + spacing)), module, BusDepot::RIGHT_LIGHTS + i));
				}
			}
			}
	}

	// build the menu
	void appendContextMenu(Menu* menu) override {
		BusDepot* module = dynamic_cast<BusDepot*>(this->module);

		struct ThemeItem : MenuItem {
			BusDepot* module;
			int theme;
			void onAction(const event::Action& e) override {
				module->color_theme = theme;
			}
		};

		struct LevelCvFilterItem : MenuItem {
			BusDepot* module;
			void onAction(const event::Action& e) override {
				module->level_cv_filter = !module->level_cv_filter;
			}
		};

		struct FadeCvModeItem : MenuItem {
			BusDepot* module;
			int cv_mode;
			void onAction(const event::Action& e) override {
				module->fade_cv_mode = cv_mode;
			}
		};

		struct DefaultThemeItem : MenuItem {
			BusDepot* module;
			void onAction(const event::Action &e) override {
				saveDefaultTheme(rightText.empty());
			}
		};

		menu->addChild(new MenuEntry);
		menu->addChild(createMenuLabel("Color Theme"));

		std::string themeTitles[2] = {"70's Cream", "Night Ride"};
		for (int i = 0; i < 2; i++) {
			ThemeItem* themeItem = createMenuItem<ThemeItem>(themeTitles[i]);
			themeItem->rightText = CHECKMARK(module->color_theme == i);
			themeItem->module = module;
			themeItem->theme = i;
			menu->addChild(themeItem);
		}

		// CV filters
		menu->addChild(new MenuEntry);
		menu->addChild(createMenuLabel("CV Input Filters"));

		LevelCvFilterItem* levelCvFilterItem = createMenuItem<LevelCvFilterItem>("Smoothing on level CV");
		levelCvFilterItem->rightText = CHECKMARK(module->level_cv_filter);
		levelCvFilterItem->module = module;
		menu->addChild(levelCvFilterItem);

		// Fade CV filters
		menu->addChild(new MenuEntry);
		menu->addChild(createMenuLabel("Fade CV Mode"));

		std::string fadeCvModeTitles[3] = {"Both fade in and fade out speeds", "Fade in speed only", "Fade out speed only"};
		for (int i = 0; i < 3; i++) {
			FadeCvModeItem* fadeCvModeItem = createMenuItem<FadeCvModeItem>(fadeCvModeTitles[i]);
			fadeCvModeItem->rightText = CHECKMARK(module->fade_cv_mode == i);
			fadeCvModeItem->module = module;
			fadeCvModeItem->cv_mode = i;
			menu->addChild(fadeCvModeItem);
		}

		menu->addChild(new MenuEntry);
		menu->addChild(createMenuLabel("All Modular Bus Mixers"));

		DefaultThemeItem* defaultThemeItem = createMenuItem<DefaultThemeItem>("Default Night Ride theme");
		defaultThemeItem->rightText = CHECKMARK(loadDefaultTheme());
		defaultThemeItem->module = module;
		menu->addChild(defaultThemeItem);
	}

	// display the panel based on the theme
	void step() override {
		if (module) {
			panel->visible = ((((BusDepot*)module)->color_theme) == 0);
			night_panel->visible = ((((BusDepot*)module)->color_theme) == 1);
		}
		Widget::step();
	}
};


Model *modelBusDepot = createModel<BusDepot, BusDepotWidget>("BusDepot");
