#include "esphome.h"

class WaterFlowSensor : public Component, public CustomAPIDevice {
 public:
  Sensor *flow_rate_gpm_sensor = new Sensor();
  Sensor *gallons_used_sensor = new Sensor();

  const int SENSOR_DEBOUNCE_MS = 50;
  const double GALLONS_PER_PULSE = .075;
  const double MINIMUM_FLOW_GPM = .08; // Technically, the slowest consistent flow the meter can read is .22 gal/min. But mine was able to read a flow as slow as .08 gal/min fairly consistently (pulse every ~55 seconds: (60 / (.08/0.075) ).
  const double NEXT_PULSE_TIMEOUT_BUFFER_SEC = 2; // buffer for waiting for next pulse. For example, if we expect the next pulse in 5 seconds, and we don't get a pulse in 5 + NEXT_PULSE_TIMEOUT_BUFFER_SEC seconds, we will say water flow has stopped
  double pulse_timeout_sec = 0;
  bool pulse_timed_out = false;
  int current_pin_state = 0;
  double flow_rate_gpm = 0;
  double gallons_used = 0;
  unsigned long last_pulse = 0;
  double delta_minutes = 0;
  int flowTriggerState = 0;
  unsigned long time = 0;

  void setup() override {
    pinMode(5, INPUT_PULLUP);

    // Register Home Assistant services
    register_service(&WaterFlowSensor::on_set_gallons_used, "set_gallons_used", {"gallons"});
    register_service(&WaterFlowSensor::on_power_cycle_restore, "power_cycle_restore");
  }

  void loop() override {    
    int pinState = digitalRead(5);
    
    // Don't do anything if sensor has not pulsed
    if (pinState == current_pin_state) { 
      if (!pulse_timed_out && ((millis() - last_pulse) > (pulse_timeout_sec * 1000))) {
        // Publish no flow after pulses have stopped
        flow_rate_gpm_sensor->publish_state(0);
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
      if (digitalRead(5) != pinState) return; 
    }

    // Only increment the gallons used on HIGH pulse
    if (!pinState) { gallons_used += GALLONS_PER_PULSE; }

    if (pinState == flowTriggerState) {
      // Calculate flow rate
      delta_minutes = ((time - last_pulse) / 60000.0);
      flow_rate_gpm = (GALLONS_PER_PULSE / delta_minutes);
      last_pulse = time;

      // If the calculated flow rate is less the the minimum flow that the meter can read, we wont say there is a real flow just yet.
      // This could be the first of a set of new pulses, or sometimes water just slowly moves back and forth in the pipes. 
      if (flow_rate_gpm >= MINIMUM_FLOW_GPM) {
        // publish flow rate and total gallons to Home Assistant
        flow_rate_gpm_sensor->publish_state(flow_rate_gpm);
        gallons_used_sensor->publish_state(gallons_used);

        // Set a timeout for the next expected pulse + a buffer, depending of the current flow rate.
        // For example, if the water is flowing at 1GPM, that is 13.33PPM, or a pulse every 4.5 seconds.
        // We would set the pulse_timeout_sec to 4.5 + NEXT_PULSE_TIMEOUT_BUFFER_SEC and if we don't get a pulse
        // by then, we say the flow has stopped.
        pulse_timeout_sec = (60/((1/GALLONS_PER_PULSE) * flow_rate_gpm)) + NEXT_PULSE_TIMEOUT_BUFFER_SEC;
      }
    }
    current_pin_state = pinState;
  }

  // This can be called from home assistant to reset or adjust the gallons count on the esp
  void on_set_gallons_used(float gallons) {
    gallons_used = gallons;
    gallons_used_sensor->publish_state(gallons_used);
  }

  // This can be called from home assistant to restore the gallons_used (using an approximation) after a power cycle of the ESP
  void on_power_cycle_restore() {
    gallons_used = id(saved_gallons_count) + 5;
    flow_rate_gpm_sensor->publish_state(0);
    gallons_used_sensor->publish_state(gallons_used);
  }
};