#include "Lasers.hpp"
#include "dsp/digital.hpp"

#include <stdio.h> 
#include <unistd.h> 
#include <fcntl.h>  
#include <termios.h> 
#include <iostream>
#include <string.h>
#include <string>

TextField *textField;

// http://forum.arduino.cc/index.php?topic=41162.0
// https://chrisheydrick.com/2012/06/17/how-to-read-serial-data-from-an-arduino-in-linux-with-c-part-3/

struct DoubleLasers : Module {
	enum ParamIds {
		CONNECT_BUTTON,
		DISCONNECT_BUTTON,
		MIN_1,
		MAX_1,
		SMOOTH_1,
		MIN_2,
		MAX_2,
		SMOOTH_2,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_1,
		OUTPUT_2,
		OUTPUT_BOTH,
		NUM_OUTPUTS
	};
	enum LightIds {
		CONNECT_LIGHT,
		DISCONNECT_LIGHT,
		SMOOTH_LIGHT_1,
		SMOOTH_LIGHT_2,
		LIGHT_1,
		LIGHT_2,
		NUM_LIGHTS
	};

	SchmittTrigger connectTrigger;
	SchmittTrigger disconnectTrigger;
	SchmittTrigger smooth1Trigger;
	SchmittTrigger smooth2Trigger;
	
	char serial_port;
	int fd;
	int connected=false;
	unsigned char buf[255];
	int res;
	float range1;
	float range2;
	int target_values[2];
	int values[2];
	float lerp_amount = 0.1;
	bool smooth1 = false;
	bool smooth2 = false;
	const char * separator = " ";
	
	
	DoubleLasers() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}
	void step() override;

	void onDelete() override {
		disconnect();
	}
	
	
	void connect() {
		lights[CONNECT_LIGHT].setBrightnessSmooth(1.0f);
		// Use ls /dev/cu.*
		fd = open(textField->text.c_str(), O_RDWR | O_NOCTTY | O_NDELAY); //open("/dev/tty.usbserial-14110", O_RDWR | O_NOCTTY | O_NDELAY);
		init_port(&fd);
		connected=true;
	}

	void disconnect() {
		connected=false;
		lights[CONNECT_LIGHT].setBrightness(0.0f);
		close(fd);
	}
	
	void init_port(int *fd) {
		struct termios options;
		tcgetattr(*fd,&options);
		
		// Set baud rate to 115200
		cfsetispeed(&options,B115200);
		cfsetospeed(&options,B115200);
		
		// 8 bits, no parity, no stop bits
		options.c_cflag &= ~PARENB;
		options.c_cflag &= ~CSTOPB;
		options.c_cflag &= ~CSIZE;
		options.c_cflag |= CS8;

		// no hardware flow control
		options.c_cflag &= ~CRTSCTS;

		// enable receiver, ignore status lines
		options.c_cflag |= (CREAD | CLOCAL);

		// disable input/output flow control, disable restart chars
		options.c_iflag &= ~(IXON | IXOFF | IXANY);

		// Disable canonical input, disable echo, disable visually erase chars
		// disable terminal-generated signals
		options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

		//disable output processing
		options.c_oflag &= ~OPOST;

		// Wait for 2 bytes to come in before read returns
		options.c_cc[VMIN] = 2;

		// No minimum wait time before read returns
		options.c_cc[VTIME] = 0;

		// Set options
		tcsetattr(*fd,TCSANOW,&options);

		tcflush(*fd, TCIFLUSH);
	}

	int read_until(int fd, char until) {
		char b[1];
		int i=0;
		do {
			int n = read(fd, b, 1);
			if (n==-1) return -1;
			if (n==0) {
				usleep(10*1000);
				continue;
			}
			buf[i] = b[0]; i++;
		} while (b[0] != until);

		buf[i] = 0;
		return 0;
	}

	// https://stackoverflow.com/questions/4353525/floating-point-linear-interpolation
	float lerp(float a, float b, float f)
	{
		float out_value = a;

		out_value += (b-a) * f;

		return out_value;
	}
};


void DoubleLasers::step() {
	

	// Connect
	if (connectTrigger.process(params[CONNECT_BUTTON].value)) {
		connect();
	}

	// Disconnect
	if (disconnectTrigger.process(params[DISCONNECT_BUTTON].value)) {
		disconnect();
	}

	// Smooth trigger
	if (smooth1Trigger.process(params[SMOOTH_1].value)) {
		smooth1 = !smooth1;
	}

	if (smooth2Trigger.process(params[SMOOTH_2].value)) {
		smooth2 = !smooth2;
	}

	lights[SMOOTH_LIGHT_1].setBrightnessSmooth(smooth1);
	lights[SMOOTH_LIGHT_2].setBrightnessSmooth(smooth2);


	lights[DISCONNECT_LIGHT].setBrightnessSmooth(disconnectTrigger.isHigh());

	if (connected==true) {
		// Read data from serial port		
		read_until(fd, '\n');
		
		char * buf_converted = reinterpret_cast<char*>(buf);
		char * split_string;
		split_string = strtok (buf_converted, separator);
		int current = 0;
		while (split_string != NULL)
		{
			int new_value = atoi(split_string);//boost::lexical_cast<int>(split_string); 
			target_values[current] = new_value;
			current +=1;
			split_string = strtok (NULL, separator);
		}

		if (smooth1) {
			values[0] = lerp(values[0], target_values[0], lerp_amount);
		} else {
			values[0] = target_values[0];
		}

		if (smooth2) {
			values[1] = lerp(values[1], target_values[1], lerp_amount);
		} else {
			values[1] = target_values[1];
		}


		range1 = params[MAX_1].value-params[MIN_1].value;
		range2 = params[MAX_2].value-params[MIN_2].value;

		outputs[OUTPUT_1].value = params[MIN_1].value+((range1/256.0f)*values[0]);
		outputs[OUTPUT_2].value = params[MIN_2].value+((range2/256.0f)*values[1]);
		outputs[OUTPUT_BOTH].value = clampf(outputs[OUTPUT_1].value + outputs[OUTPUT_2].value, -10.f, 10.f);

		lights[LIGHT_1].value = (1.0f/256.0f)*values[0];
		lights[LIGHT_2].value = (1.0f/256.0f)*values[1];

	} else {
		lights[LIGHT_1].setBrightnessSmooth(0.0f);
		lights[LIGHT_2].setBrightnessSmooth(0.0f);
		outputs[OUTPUT_1].value = 0.0f;
		outputs[OUTPUT_2].value = 0.0f;
	}
	
}


struct DoubleLasersWidget : ModuleWidget {
	

	DoubleLasersWidget(DoubleLasers *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/DoubleLasers.svg")));

		//object_textfield = new PortTextField();
		textField = Widget::create<LedDisplayTextField>(Vec(12, 40));
		//object_textfield->box.pos = Vec(12, 40);
		textField->box.size = Vec(126, 40);
		addChild(textField);

		addParam(ParamWidget::create<LEDButton>(Vec(50, 88), module, DoubleLasers::CONNECT_BUTTON, 0.0f, 1.0f, 0.0f));
		addParam(ParamWidget::create<LEDButton>(Vec(120, 88), module, DoubleLasers::DISCONNECT_BUTTON, 0.0f, 1.0f, 0.0f));
		addChild(ModuleLightWidget::create<MediumLight<GreenLight>>(Vec(54, 92), module, DoubleLasers::CONNECT_LIGHT));
		addChild(ModuleLightWidget::create<MediumLight<GreenLight>>(Vec(124, 92), module, DoubleLasers::DISCONNECT_LIGHT));

		addParam(ParamWidget::create<RoundBlackKnob>(Vec(15, 163), module, DoubleLasers::MIN_1, -10.0f, 10.0f, 0.0f));
		addParam(ParamWidget::create<RoundBlackKnob>(Vec(104, 163), module, DoubleLasers::MAX_1, -10.0f, 10.0f, 0.0f));
		addParam(ParamWidget::create<RoundBlackKnob>(Vec(15, 237), module, DoubleLasers::MIN_2, -10.0f, 10.0f, 0.0f));
		addParam(ParamWidget::create<RoundBlackKnob>(Vec(104, 237), module, DoubleLasers::MAX_2, -10.0f, 10.0f, 0.0f));

		addParam(ParamWidget::create<LEDButton>(Vec(66, 169), module, DoubleLasers::SMOOTH_1, 0.0f, 1.0f, 0.0f));
		addParam(ParamWidget::create<LEDButton>(Vec(66, 243), module, DoubleLasers::SMOOTH_2, 0.0f, 1.0f, 0.0f));
		addChild(ModuleLightWidget::create<MediumLight<GreenLight>>(Vec(70, 173), module, DoubleLasers::SMOOTH_LIGHT_1));
		addChild(ModuleLightWidget::create<MediumLight<GreenLight>>(Vec(70, 247), module, DoubleLasers::SMOOTH_LIGHT_2));

		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH,0)));
		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addOutput(Port::create<PJ301MPort>(Vec(18, 321), Port::OUTPUT, module, DoubleLasers::OUTPUT_1));
		addOutput(Port::create<PJ301MPort>(Vec(62, 321), Port::OUTPUT, module, DoubleLasers::OUTPUT_BOTH));
		addOutput(Port::create<PJ301MPort>(Vec(106, 321), Port::OUTPUT, module, DoubleLasers::OUTPUT_2));

		addChild(ModuleLightWidget::create<MediumLight<GreenLight>>(Vec(48, 329), module, DoubleLasers::LIGHT_1));
		addChild(ModuleLightWidget::create<MediumLight<GreenLight>>(Vec(92, 329), module, DoubleLasers::LIGHT_2));


	}
};


Model *modelDoubleLasers = Model::create<DoubleLasers, DoubleLasersWidget>("Lasers", "DoubleLasers", "Double Lasers", EXTERNAL_TAG);
