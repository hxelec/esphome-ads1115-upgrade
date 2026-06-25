#include "ads1115.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::ads1115 {
/*

Note from hxelec:

This is my first ever time not blindly using Arduino libraries. I am a complete beginner in embedded dev, other than very basic Arduino which I wouldn't count lol.
I am by no means experienced ennough to make judgements... but I think the overall purpose of certain sections in code could be better explained.
Either that or I'm just misinterpreting the use case of this development environment or haven't looked hard enough I suppose.
Anyways, I used exercism.org to do 20 C++ activities and binge-watched The Cherno in preparation for this.

I wanted to implement the comparator function using an action but decided against it, since I don't know how the python would work for that.
On top of that, I am using the comparator to ask whether the new reading has deviated from the previous saved one by a certain amount.
It makes more sense to do it within this code than to ask for an absolute value and have to deal with that logic in the .yaml.
Especially since the priorities and execution order of certain functions is ambiguous. It's the same reason I didn't use an i2c lambda.
I didn't want to set this comparator externally and then this program comes in and resets it right before going to sleep again.

So overall this program sets the comparator on each reading, even though it's useless until just the one before the shutdown. Oh well, lol.

*/ 

static const char *const TAG = "ads1115";
static const uint8_t ADS1115_REGISTER_CONVERSION = 0x00;
static const uint8_t ADS1115_REGISTER_CONFIG = 0x01;
// add registers to enable comparator usage
static const uint8_t ADS1115_REGISTER_LO_THRESH = 0x02;
static const uint8_t ADS1115_REGISTER_HI_THRESH = 0x03;

void ADS1115Component::setup() {
  // check if reading conversion register works, I think.
  uint16_t value;
  if (!this->read_byte_16(ADS1115_REGISTER_CONVERSION, &value)) {
    this->mark_failed();
    return;
  }

  // set placeholder config values, I think.

  uint16_t config = 0;
  // Clear single-shot bit
  //        0b0xxxxxxxxxxxxxxx
  config |= 0b0000000000000000;
  // Setup multiplexer
  //        0bx000xxxxxxxxxxxx
  config |= ADS1115_MULTIPLEXER_P0_N1 << 12;

  // Setup Gain
  //        0bxxxx000xxxxxxxxx
  config |= ADS1115_GAIN_6P144 << 9;

  if (this->continuous_mode_) {
    // Set continuous mode
    //        0bxxxxxxx0xxxxxxxx
    config |= 0b0000000000000000;
  } else {
    // Set singleshot mode
    //        0bxxxxxxx1xxxxxxxx
    config |= 0b0000000100000000;
  }

  // Set data rate - 860 samples per second
  //        0bxxxxxxxx111xxxxx
  config |= ADS1115_860SPS << 5;

  // Set comparator mode - window
  //        0bxxxxxxxxxxx1xxxx
  config |= 0b0000000000010000;

  // Set comparator polarity - active low
  //        0bxxxxxxxxxxxx0xxx
  config |= 0b0000000000000000;

  // Set comparator latch enabled - true
  //        0bxxxxxxxxxxxxx1xx
  config |= 0b0000000000000100;

  // Set comparator que mode - assert after 4 conversions
  //        0bxxxxxxxxxxxxxx10
  config |= 0b0000000000000010;

  if (!this->write_byte_16(ADS1115_REGISTER_CONFIG, config)) {
  // Sidenote: why are we using 'this->' in this setup function?
  // Wouldn't it be looking for member functions in 'this' (ADS1115Component class which inherited I2CDevice and Component member functions) anyways?
    this->mark_failed();
    return;
  }
  this->prev_config_ = config; // set setup config
}
void ADS1115Component::dump_config() {
  ESP_LOGCONFIG(TAG, "ADS1115:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, ESP_LOG_MSG_COMM_FAIL);
  }
}
float ADS1115Component::request_measurement(ADS1115Multiplexer multiplexer, ADS1115Gain gain,
                                            ADS1115Resolution resolution, ADS1115Samplerate samplerate, uint16_t threshold_offset) {
  uint16_t config = this->prev_config_; // load setup config
  // Multiplexer
  //        0bxBBBxxxxxxxxxxxx
  config &= 0b1000111111111111; // clear multiplexer bits
  config |= (multiplexer & 0b111) << 12; // put in new multiplexer bits...
  // why do we have '& 0b111' here?

  // Gain
  //        0bxxxxBBBxxxxxxxxx
  config &= 0b1111000111111111; // clear PGA bits
  config |= (gain & 0b111) << 9; // put in new PGA bits

  // Sample rate
  //        0bxxxxxxxxBBBxxxxx
  config &= 0b1111111100011111; // clear data rate bits
  config |= (samplerate & 0b111) << 5; // put in new data rate bits

  if (!this->continuous_mode_) { // trigger single shot conversion (not applicable)
    // Start conversion
    config |= 0b1000000000000000;
  }

  // if in single-shot mode or configuration changed
  if (!this->continuous_mode_ || this->prev_config_ != config) {
    // write new config register and warn if failed
    if (!this->write_byte_16(ADS1115_REGISTER_CONFIG, config)) {
      this->status_set_warning();
      return NAN;
    }
    // update prev config variable
    this->prev_config_ = config;

    // Delay calculated as: ceil((1000/SPS)+.5)
    if (resolution == ADS1015_12_BITS) {
      switch (samplerate) {
        case ADS1115_8SPS:
          delay(9);
          break;
        case ADS1115_16SPS:
          delay(5);
          break;
        case ADS1115_32SPS:
          delay(3);
          break;
        case ADS1115_64SPS:
        case ADS1115_128SPS:
          delay(2);
          break;
        default:
          delay(1);
          break;
      }
    } else {
      switch (samplerate) {
        case ADS1115_8SPS:
          delay(126);  // NOLINT
          break;
        case ADS1115_16SPS:
          delay(63);  // NOLINT
          break;
        case ADS1115_32SPS:
          delay(32);
          break;
        case ADS1115_64SPS:
          delay(17);
          break;
        case ADS1115_128SPS:
          delay(9);
          break;
        case ADS1115_250SPS:
          delay(5);
          break;
        case ADS1115_475SPS:
          delay(3);
          break;
        case ADS1115_860SPS:
          delay(2);
          break;
      }
    }

    // in continuous mode, conversion will always be running, rely on the delay
    // to ensure conversion is taking place with the correct settings
    // can we use the rdy pin to trigger when a conversion is done?
    // > yeah, we could, but ESPHome doesn't seem to be built for this and I also suck at coding 😅

    // single shot code (ignore for me)
    if (!this->continuous_mode_) {
      uint32_t start = millis();
      while (this->read_byte_16(ADS1115_REGISTER_CONFIG, &config) && (config >> 15) == 0) {
        if (millis() - start > 100) {
          ESP_LOGW(TAG, "Reading ADS1115 timed out");
          this->status_set_warning();
          return NAN;
        }
        yield();
      }
    }
  }

  uint16_t raw_conversion;
  if (!this->read_byte_16(ADS1115_REGISTER_CONVERSION, &raw_conversion)) {
    this->status_set_warning();
    return NAN;
  }

  if (multiplexer == 0b100) { // our water tank sensor pins
    // set lower threshold
    if (threshold_offset > raw_conversion) {
      if (!this->write_byte_16(ADS1115_REGISTER_LO_THRESH, 0x0000)) {
        this->status_set_warning();
        return NAN;
      }
    } else {
      if (!this->write_byte_16(ADS1115_REGISTER_LO_THRESH, static_cast<uint16_t>(raw_conversion - threshold_offset))) {
        this->status_set_warning();
        return NAN;
      }
    }

    // set upper threshold
    if (threshold_offset > (0xFFFF - raw_conversion)) {
      if (!this->write_byte_16(ADS1115_REGISTER_HI_THRESH, 0xFFFF)) {
        this->status_set_warning();
        return NAN;
      }
    } else {
      if (!this->write_byte_16(ADS1115_REGISTER_HI_THRESH, static_cast<uint16_t>(raw_conversion + threshold_offset))) {
        this->status_set_warning();
        return NAN;
      }
    }
  }


  if (resolution == ADS1015_12_BITS) {
    // ADS1015 returns 12-bit value left-justified in 16 bits; shift right and sign-extend
    raw_conversion = static_cast<uint16_t>(static_cast<int16_t>(raw_conversion) >> (16 - ADS1015_12_BITS));
  }

  auto signed_conversion = static_cast<int16_t>(raw_conversion);

  float millivolts;
  float divider = (resolution == ADS1115_16_BITS) ? 32768.0f : 2048.0f;
  switch (gain) {
    case ADS1115_GAIN_6P144:
      millivolts = (signed_conversion * 6144) / divider;
      break;
    case ADS1115_GAIN_4P096:
      millivolts = (signed_conversion * 4096) / divider;
      break;
    case ADS1115_GAIN_2P048:
      millivolts = (signed_conversion * 2048) / divider;
      break;
    case ADS1115_GAIN_1P024:
      millivolts = (signed_conversion * 1024) / divider;
      break;
    case ADS1115_GAIN_0P512:
      millivolts = (signed_conversion * 512) / divider;
      break;
    case ADS1115_GAIN_0P256:
      millivolts = (signed_conversion * 256) / divider;
      break;
    default:
      millivolts = NAN;
  }

  this->status_clear_warning();
  return millivolts / 1e3f;
}

}  // namespace esphome::ads1115
