#include "esphome.h"

#define GALLONS 10
#define LITERS 35

class WaterFlowSensor : public Component, public CustomAPIDevice {
 public:
 
  /** USER SPECIFIC SETTINGS **/
  #define UNITS GALLONS // If using Liters, change "GALLONS" to "LITERS". Everytime this amount of water is used, the total usage is written to the board, so the total can be restored if the board ever loses power
  const int PIN_NUMBER = 5; // Pin number to detect pulses on
  const double USAGE_PER_PULSE = .075; // Amount of water (in Gallons or Liters) that must flow for the water sensor to send one pulse
  const double MINIMUM_FLOW_RATE = .08; // Minimum flow rate (in Gallons or Liters per minute) that the flow sensor can recognize. Technically, the slowest consistent flow that the EKM HD meter can read is .22 gal/min. But mine was able to read a flow as slow as .08 gal/min fairly consistently (pulse every ~55 seconds: (60 / (.08/0.075) ).
  const double NEXT_PULSE_TIMEOUT_BUFFER_SEC = 2; // Buffer for waiting for next pulse to tell if water is still running. For example, if we expect the next pulse in 5 seconds, and we don't get a pulse in 5 + NEXT_PULSE_TIMEOUT_BUFFER_SEC seconds, it will report a flow rate of 0 to Home Assistant. If your USAGE_PER_PULSE value is much hight than .075, you will probably want to increase this a bit.
  /****************************/

  const int SENSOR_DEBOUNCE_MS = 50;
  double pulse_timeout_sec = 0;
  bool pulse_timed_out = false;
  int current_pin_state = 0;
  double flow_rate = 0; // Used to calculate the current flow rate in Gallons or Liters per minute
  double total_water_used = 0; // Used to keep a running count of the amount of water that has been used (Gallons or Liters)
  unsigned long last_pulse = 0;
  double delta_minutes = 0;
  int flowTriggerState = 0;
  unsigned long time = 0;
  Sensor *flow_rate_sensor = new Sensor();
  Sensor *total_water_used_sensor = new Sensor();

  void setup() override {
    pinMode(PIN_NUMBER, INPUT_PULLUP);

    // Register Home Assistant services
    register_service(&WaterFlowSensor::on_set_total_water_used, "set_total_water_used", {"value"});
    register_service(&WaterFlowSensor::on_power_cycle_restore, "power_cycle_restore");
  }

  void loop() override {    
    int pinState = digitalRead(PIN_NUMBER);
    
    // Don't do anything if sensor has not pulsed
    if (pinState == current_pin_state) { 
      if (!pulse_timed_out && ((millis() - last_pulse) > (pulse_timeout_sec * 1000))) {
        // Publish no flow after pulses have stopped
        flow_rate_sensor->publish_state(0);
        flowTriggerState = !current_pin_state; // to calculate the flow rate sooner, we will use the next pulse as the "first" pulse, whether it's high or low
        pulse_timed_out = true; // set this so we only publish once when the flow stops
      }
      return; 
    }
    pulse_timed_out = false;
    StateChanged(pinState);
  }

  // Pulse from meter
  void StateChanged(int pinState) {
    // Add a debounce filter
    time = millis();
    while (millis() - time < SENSOR_DEBOUNCE_MS) {
      if (digitalRead(PIN_NUMBER) != pinState) return; 
    }

    // Only increment the total_water_used on HIGH pulse
    if (!pinState) { total_water_used += USAGE_PER_PULSE; }

    if (pinState == flowTriggerState) {
      // Calculate flow rate
      delta_minutes = ((time - last_pulse) / 60000.0);
      flow_rate = (USAGE_PER_PULSE / delta_minutes);
      last_pulse = time;

      // If the calculated flow rate is less the the minimum flow that the meter can read, we wont say there is a real flow just yet.
      // This could be the first of a set of new pulses, or sometimes water just slowly moves back and forth in the pipes. 
      if (flow_rate >= MINIMUM_FLOW_RATE) {
        // publish flow_rate and total_water_used to Home Assistant
        flow_rate_sensor->publish_state(flow_rate);
        total_water_used_sensor->publish_state(total_water_used);

        // Set a timeout for the next expected pulse + a buffer, depending of the current flow rate.
        // For example, if the water is flowing at 1GPM, that is 13.33PPM, or a pulse every 4.5 seconds.
        // We would set the pulse_timeout_sec to 4.5 + NEXT_PULSE_TIMEOUT_BUFFER_SEC and if we don't get a pulse
        // by then, we say the flow has stopped.
        pulse_timeout_sec = (60/((1/USAGE_PER_PULSE) * flow_rate)) + NEXT_PULSE_TIMEOUT_BUFFER_SEC;
      }
    }
    current_pin_state = pinState;
  }

  // This can be called from home assistant to reset or adjust the total_water_used count on the esp
  void on_set_total_water_used(float value) {
    total_water_used = value;
    total_water_used_sensor->publish_state(total_water_used);
  }

  // This can be called from home assistant to restore the total_water_used (using an approximation) after a power cycle of the ESP
  void on_power_cycle_restore() {
    total_water_used = id(saved_usage_count) + (UNITS / 2);
    flow_rate_sensor->publish_state(0);
    total_water_used_sensor->publish_state(total_water_used);
  }
};
