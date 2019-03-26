#include <Adafruit_NeoPixel.h> /* Adafruit NeoPixel 1.1.8 */


// hardware setup
#define BUTTON_PIN 2
#define BUTTON_PIN_SECOND 3
#define LED_STRIP_DATA_PIN 7
#define NUM_LEDS 500


// debug options
//#define RAMPUP_ENABLE 0
//#define BOOT_MARKER_ENABLE 0
#define DEBUG




enum app_mode : uint8_t {
#ifdef RAMPUP_ENABLE
    ramp_up,
#endif
    //black,
    rainbow2,
    white,
    rainbow20,
    rainbow20_stopped,
    horsemode,
    num_modes,
    disabled,
};

struct app;

struct preemption_guard {
    app_mode my_mode;
    struct app* app;
    // poll to detect preemption (app mode switch)
    bool preempted();
};

struct app {
    void preempt_and_switch_to_next_mode();
    void ENABLE();
    // get a guard object for the current mode
    preemption_guard begin_preemptible();
    volatile app_mode mode;   
    volatile app_mode lastmode;
};

struct rainbow_state {
    void resume(uint8_t wait, preemption_guard g);
    static uint32_t wheel(byte wheel_pos);
    uint8_t wait;
    uint16_t i, j;
};

struct horsemode_state {
    unsigned long switch_every_millis;

    unsigned long last_millis;
    uint8_t offset;
    void resume(preemption_guard guard);

    horsemode_state(unsigned long switch_every_millis)
        : switch_every_millis(switch_every_millis),
          last_millis(0), offset(0) 
          {};
};

struct white_state {
    void resume(preemption_guard g, uint32_t color);
};

// Parameter 1 = number of pixels in strip
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_STRIP_DATA_PIN, NEO_GRB + NEO_KHZ800);

app a;
struct rainbow_state st_rainbow = {0};
struct horsemode_state st_horsemode(100);
struct white_state st_white ;

void button_rise_isr();
void button_fall_isr();
unsigned long timepressed = 0;
bool isOn = false;
bool isOnOther = true;
//unsigned long temp = 0;
void setup() {
#ifdef DEBUG
    Serial.begin(9600);
#endif
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);
    pinMode(BUTTON_PIN_SECOND, INPUT);
    strip.begin();
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_rise_isr, RISING);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_SECOND), button_fall_isr, FALLING);
    strip.show();
    digitalWrite(LED_BUILTIN, HIGH);
    
}

// button_isr bounce handling
//volatile int lastButtonPush = 0;
void button_rise_isr()
{
    if((timepressed - millis()) < 50) {
      return;
    }
#ifdef DEBUG
    Serial.print("rise \n");
#endif

    timepressed = millis();
}

void button_fall_isr(){
    noInterrupts();
    unsigned long timeWasPressed = millis() - timepressed;
        if (abs(timeWasPressed) > 500) { 
#ifdef DEBUG
            Serial.print("long \n");  
#endif       
            a.ENABLE();
        } else if(abs(timeWasPressed) > 20) {
#ifdef DEBUG
            Serial.print("short \n");
#endif
            a.preempt_and_switch_to_next_mode();        
        } 
        interrupts();           
}

bool didStartInitSeq = false;
void loop() {    


#ifdef BOOT_MARKER_ENABLE
    if (!didStartInitSeq) {
        didStartInitSeq = true;
        for (int r = 0; r < 6; r++) {
            digitalWrite(LED_BUILTIN, r % 2);
            delay(1000);
        }
        digitalWrite(LED_BUILTIN, LOW);
    }
#endif /* BOOT_MARKER_ENABLE */

    auto guard = a.begin_preemptible();

    switch (a.mode) {
        case app_mode::rainbow2:
#ifdef DEBUG
  Serial.print("rainbow2 \n");
#endif
            st_rainbow.resume(2, guard);
            break;
        case app_mode::rainbow20:
#ifdef DEBUG
  Serial.print("rainbow20 \n");
#endif
            
            st_rainbow.resume(20, guard);
            break;
        case app_mode::rainbow20_stopped:
            
            //static_assert(app_mode::rainbow20_stopped == app_mode::rainbow20 + 1);
            break;
        case app_mode::horsemode:
#ifdef DEBUG
  Serial.print("horse \n");
#endif
            
            st_horsemode.resume(guard);
            break;
        case app_mode::white:
#ifdef DEBUG
  Serial.print("white \n");
#endif
            
            st_white.resume(guard, strip.Color(255, 255, 255));
            break;
        case app_mode::disabled:
#ifdef DEBUG
   Serial.print("disabeld \n");
#endif
            
            st_white.resume(guard, strip.Color(0, 0, 0));
            
            break;
            /*
        case app_mode::black:
            st_white.resume(guard, strip.Color(0, 0, 0));
            break;
            */
#ifdef RAMPUP_ENABLE
        case app_mode::ramp_up:
            for (int r = 0; r < 3 && !guard.preempted(); r++) {
                digitalWrite(LED_BUILTIN, r % 2);
                for (int i = 0; i < strip.numPixels() && !guard.preempted(); i++) {
                    strip.setPixelColor(i, r, r, r);
                }
                strip.show();
                delay(1000);
            }
#endif /* RAMPUP_ENABLE */

        default:
            // inidicate missing code by blinking fast for a.mode times
            for (int i = 0; i < a.mode && !guard.preempted(); i++) {
                digitalWrite(LED_BUILTIN, 1);
                delay(200);
                digitalWrite(LED_BUILTIN, 0);
                delay(200);
            }
            digitalWrite(LED_BUILTIN, 0);
            delay(1000);
    }
}

bool preemption_guard::preempted() {
    return app->mode != my_mode;
}

void app::ENABLE(){    
    if (mode != app_mode::disabled) {
        lastmode = mode;
        mode = app_mode(app_mode::disabled);
    } else {
        mode = app_mode(lastmode);
    }    
}

void app::preempt_and_switch_to_next_mode() {
    if (mode == app_mode::disabled) {
        return;
    }     
    int newmode = mode + 1;
    if (newmode == app_mode::num_modes) {
        newmode = 0;
    }
    mode = app_mode(newmode);
}

preemption_guard app::begin_preemptible() {
    return preemption_guard{mode, this};
}


void rainbow_state::resume(uint8_t wait, preemption_guard g) {
    this->wait = wait;
    for(; j<256 && !g.preempted(); j++) {
        for(; i<strip.numPixels() && !g.preempted(); i++) {
            strip.setPixelColor(i, wheel((i*1+j) & 255));
        }
        if (g.preempted()) {
            return;
        }
        i = 0;
        strip.show();
        delay(wait);
    }
    if (g.preempted()) {
        return;
    }
    j = 0;
}

void horsemode_state::resume(preemption_guard g) {
    if (abs(millis() - last_millis) > switch_every_millis) {
        offset++;
        last_millis = millis();
        const int stride = 20;
        for (int s = 0; s < strip.numPixels() - stride && !g.preempted(); s += stride) {
            for (int i = 0; i < stride && !g.preempted(); i++) {
                strip.setPixelColor(
                        s + i,
                        255 * ((s + offset) % 3 == 0),
                        255 * ((s + offset) % 3 == 1),
                        255 * ((s + offset) % 3 == 2)
                        );
            }
        }
        if (!g.preempted()) {
            strip.show();
        }
    }
}

// The colours are a transition r - g - b - back to r.
// Input a value 0 to 255 to get a color value.
uint32_t rainbow_state::wheel(byte wheel_pos) {
    if(wheel_pos < 85) {
        return strip.Color(wheel_pos * 3, 255 - wheel_pos * 3, 0);
    }
    else if(wheel_pos < 170) {
        wheel_pos -= 85;
        return strip.Color(255 - wheel_pos * 3, 0, wheel_pos * 3);
    }
    else {
        wheel_pos -= 170;
        return strip.Color(0, wheel_pos * 3, 255 - wheel_pos * 3);
    }
}

void white_state::resume(preemption_guard g, uint32_t color) {
    for (int i = 0; i < strip.numPixels() && !g.preempted(); i++) {
        strip.setPixelColor(i, color);
    }
    if (!g.preempted()) {
        strip.show();
    }
};
