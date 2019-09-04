#include "plugin.hpp"
#include "components.hpp"


struct MiniBus : Module {
	enum ParamIds {
		ON_PARAM,
		ENUMS(LEVEL_PARAMS, 3),
		NUM_PARAMS
	};
	enum InputIds {
		ON_CV_INPUT,
		MP_INPUT,
		BUS_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		BUS_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ON_LIGHT,
		NUM_LIGHTS
	};

	dsp::SchmittTrigger on_trigger;
	dsp::SchmittTrigger on_cv_trigger;

	bool input_on = true;
	float onramp = 0.0;   // when starts at 0 creates pop filter on startup

	MiniBus() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(ON_PARAM, 0.f, 1.f, 0.f, "Input on");
		configParam(LEVEL_PARAMS + 0, 0.f, 1.f, 0.f, "Level to blue bus");
		configParam(LEVEL_PARAMS + 1, 0.f, 1.f, 0.f, "Level to orange bus");
		configParam(LEVEL_PARAMS + 2, 0.f, 1.f, 1.f, "Level to red bus");
	}

	void process(const ProcessArgs &args) override {

		// on off button with level ramp to filter pops
		if (on_trigger.process(params[ON_PARAM].getValue()) + on_cv_trigger.process(inputs[ON_CV_INPUT].getVoltage())) {
			if (input_on) {
				input_on = false; onramp = 1;
			} else {
				input_on = true; onramp = 0;
			}
		}

		if (input_on) {   // calculate pop filter speed with current sampleRate
			if (onramp < 1) onramp += 50 / args.sampleRate;
		} else {
			if (onramp > 0) onramp -= 50 / args.sampleRate;
		}

		lights[ON_LIGHT].value = onramp;

		// set bus outputs for 3 stereo buses out
		outputs[BUS_OUTPUT].setChannels(6);

		// process inputs and bus outputs
		float mono_in = inputs[MP_INPUT].getVoltageSum() * onramp;
		for (int sb = 0; sb < 3; sb++) {   // sb = stereo bus
			float in_level = params[LEVEL_PARAMS + sb].getValue();
			for (int c = 0; c < 2; c++) {
				int bus_channel = (2 * sb) + c;
				outputs[BUS_OUTPUT].setVoltage((mono_in * in_level) + inputs[BUS_INPUT].getPolyVoltage(bus_channel), bus_channel);
			}
		}
	}

	// save on button state
	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "input_on", json_integer(input_on));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *input_onJ = json_object_get(rootJ, "input_on");
		if (input_onJ) input_on = json_integer_value(input_onJ);
	}
};


struct MiniBusWidget : ModuleWidget {
	MiniBusWidget(MiniBus *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MiniBus.svg")));

		addChild(createWidget<ScrewUp>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewUp>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<BlackButton>(mm2px(Vec(7.62, 15.20)), module, MiniBus::ON_PARAM));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(7.62, 15.20)), module, MiniBus::ON_LIGHT));
		addParam(createParamCentered<BlueKnob>(mm2px(Vec(7.62, 51.5)), module, MiniBus::LEVEL_PARAMS + 0));
		addParam(createParamCentered<OrangeKnob>(mm2px(Vec(7.62, 67.25)), module, MiniBus::LEVEL_PARAMS + 1));
		addParam(createParamCentered<RedKnob>(mm2px(Vec(7.62, 83.0)), module, MiniBus::LEVEL_PARAMS + 2));

		addInput(createInputCentered<KeyPort>(mm2px(Vec(7.62, 23.20)), module, MiniBus::ON_CV_INPUT));
		addInput(createInputCentered<NutPort>(mm2px(Vec(7.62, 35.4)), module, MiniBus::MP_INPUT));
		addInput(createInputCentered<NutPort>(mm2px(Vec(7.62, 103.85)), module, MiniBus::BUS_INPUT));

		addOutput(createOutputCentered<NutPort>(mm2px(Vec(7.62, 114.1)), module, MiniBus::BUS_OUTPUT));
	}
};


Model *modelMiniBus = createModel<MiniBus, MiniBusWidget>("MiniBus");