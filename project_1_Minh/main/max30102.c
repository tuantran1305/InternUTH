#include <stdio.h>
#include "max30102.h"
#include "max30102_registers.h"
#include <string.h>

esp_err_t max30102_init( max30102_config_t* this,
                         i2c_port_t i2c_num,
                         max30102_mode_t mode,
                         max30102_sampling_rate_t sampling_rate,
                         max30102_pulse_width_t pulse_width,
                         max30102_current_t ir_current,
                         max30102_current_t start_red_current,
                         uint8_t mean_filter_size,
                         uint8_t pulse_bpm_sample_size,
                         max30102_adc_range_t high_res_mode,
                         max30102_sample_averaging_t sample_averaging,
                         bool overflow_enable,
                         max30102_almost_full_t almost_full,
                         bool debug )
{
    this->i2c_num = i2c_num;
    
    this->acceptable_intense_diff = MAX30102_DEFAULT_ACCEPTABLE_INTENSITY_DIFF;
    this->red_current_adj_ms = MAX30102_DEFAULT_RED_LED_CURRENT_ADJUSTMENT_MS;
    this->reset_spo2_pulse_n = MAX30102_DEFAULT_RESET_SPO2_EVERY_N_PULSES;
    this->dc_alpha = MAX30102_DEFAULT_ALPHA;
    this->pulse_min_threshold = MAX30102_DEFAULT_PULSE_MIN_THRESHOLD;
    this->pulse_max_threshold = MAX30102_DEFAULT_PULSE_MAX_THRESHOLD;
    
    this->mean_filter_size = mean_filter_size;
    this->pulse_bpm_sample_size = pulse_bpm_sample_size;

    this->debug = debug;
    this->current_pulse_detector_state = MAX30102_PULSE_IDLE;

    this->mean_diff_ir.values = NULL;
    this->values_bpm = NULL;
    this->mean_diff_ir.values = malloc(sizeof(float)*mean_filter_size);
    this->values_bpm = malloc(sizeof(float)*pulse_bpm_sample_size);

    if(!(this->values_bpm) || !(this->mean_diff_ir.values)) return ESP_ERR_INVALID_RESPONSE;

    esp_err_t ret = max30102_set_mode(this, mode);
    if(ret != ESP_OK) return ret;

    ret = max30102_set_sampling_rate(this, sampling_rate);
    if(ret != ESP_OK) return ret;
    ret = max30102_set_pulse_width(this, pulse_width);
    if(ret != ESP_OK) return ret;

    this->red_current = (uint8_t)start_red_current;
    this->last_red_current_check = 0;

    this->ir_current = ir_current;
    ret = max30102_set_led_current(this, this->red_current, ir_current);
    if(ret != ESP_OK) return ret;
    ret = max30102_set_high_res(this, high_res_mode);
    if(ret != ESP_OK) return ret;

    ret = max30102_set_sample_averaging(this, sample_averaging);
    if(ret != ESP_OK) return ret;

    ret = max30102_set_roll_over(this, overflow_enable);
    if(ret != ESP_OK) return ret;

    ret = max30102_set_almost_full(this, almost_full);
    if(ret != ESP_OK) return ret;

    this->dc_filter_ir.w = 0;
    this->dc_filter_ir.result = 0;

    this->dc_filter_red.w = 0;
    this->dc_filter_red.result = 0;



    this->lpb_filter_ir.v[0] = 0;
    this->lpb_filter_ir.v[1] = 0;
    this->lpb_filter_ir.result = 0;

    memset(this->mean_diff_ir.values, 0, sizeof(float)*mean_filter_size);
    this->mean_diff_ir.index = 0;
    this->mean_diff_ir.sum = 0;
    this->mean_diff_ir.count = 0;


    memset(this->values_bpm, 0, sizeof(float)*pulse_bpm_sample_size);
    this->values_bpm_sum = 0;
    this->values_bpm_count = 0;
    this->bpm_index = 0;


    this->ir_ac_sq_sum = 0;
    this->red_ac_sq_sum = 0;
    this->samples_recorded = 0;
    this->pulses_detected = 0;
    this->current_spO2 = 0;

    this->last_beat_threshold = 0;
    return ESP_OK;
}

esp_err_t max30102_update(max30102_config_t* this, max30102_data_t* data) {
    data->pulse_detected = false;
    data->heart_bpm = 0.0;
    data->ir_cardiogram = 0.0;
    data->ir_dc_value = 0.0;
    data->red_dc_value = 0.0;
    data->spO2 = this->current_spO2;
    data->last_beat_threshold = 0;
    data->dc_filtered_ir = 0.0;
    data->dc_filtered_red = 0.0;

    max30102_fifo_t raw_data;
    esp_err_t ret = max30102_read_fifo(this, &raw_data);
    if(ret != ESP_OK) return ret;

    this->dc_filter_ir = max30102_dc_removal( (float)raw_data.raw_ir,
                                              this->dc_filter_ir.w,
                                              this->dc_alpha );
    this->dc_filter_red = max30102_dc_removal( (float)raw_data.raw_red,
                                               this->dc_filter_red.w,
                                               this->dc_alpha );

    float mean_diff_res_ir = max30102_mean_diff( this,
                                                 this->dc_filter_ir.result );

    max30102_lpb_filter(this, mean_diff_res_ir/*-dcFilterIR.result*/);

    this->ir_ac_sq_sum += this->dc_filter_ir.result * this->dc_filter_ir.result;
    this->red_ac_sq_sum+= this->dc_filter_red.result*this->dc_filter_red.result;
    this->samples_recorded++;

    if(max30102_detect_pulse(this, this->lpb_filter_ir.result) && this->samples_recorded)
    {
        data->pulse_detected=true;
        this->pulses_detected++;

        float ratio_rms = log( sqrt( this->red_ac_sq_sum /
                                     (float)this->samples_recorded ) ) /
                          log( sqrt( this->ir_ac_sq_sum /
                                     (float)this->samples_recorded ) );

        if(this->debug)
            printf("RMS Ratio: %f\n", ratio_rms);

        //This is my adjusted standard model, so it shows 0.89 as 94% saturation.
        //It is probably far from correct, requires proper empircal calibration.
        this->current_spO2 = 110.0 - 18.0 * ratio_rms;
        data->spO2 = this->current_spO2;

        if(!(this->pulses_detected % this->reset_spo2_pulse_n)) {
            this->ir_ac_sq_sum = 0;
            this->red_ac_sq_sum = 0;
            this->samples_recorded = 0;
        }
    }

    ret = max30102_balance_intensities( this,
                                        this->dc_filter_red.w,
                                        this->dc_filter_ir.w   );
    if(ret != ESP_OK) return ret;


    data->heart_bpm = this->current_bpm;
    data->ir_cardiogram = this->lpb_filter_ir.result;
    data->ir_dc_value = this->dc_filter_ir.w;
    data->red_dc_value = this->dc_filter_red.w;
    data->last_beat_threshold = this->last_beat_threshold;
    data->dc_filtered_ir = this->dc_filter_ir.result;
    data->dc_filtered_red = this->dc_filter_red.result;

    return ESP_OK;
}

bool max30102_detect_pulse(max30102_config_t* this, float sensor_value) {
    static float prev_sensor_value = 0;
    static uint8_t values_went_down = 0;
    static uint32_t current_beat = 0;
    static uint32_t last_beat = 0;

    if(sensor_value > this->pulse_max_threshold) {
        this->current_pulse_detector_state = MAX30102_PULSE_IDLE;
        prev_sensor_value = 0;
        last_beat = 0;
        current_beat = 0;
        values_went_down = 0;
        this->last_beat_threshold = 0;
        return false;
    }

    switch(this->current_pulse_detector_state) {
    case MAX30102_PULSE_IDLE:
        if(sensor_value >= this->pulse_min_threshold) {
            this->current_pulse_detector_state = MAX30102_PULSE_TRACE_UP;
            values_went_down = 0;
        }
        break;
    case MAX30102_PULSE_TRACE_UP:
        if(sensor_value > prev_sensor_value) {
            current_beat = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            this->last_beat_threshold = sensor_value;
        } else {
            if(this->debug) {
                printf("Peak reached: %f %f\n",
                       sensor_value,
                       prev_sensor_value);
            }

            uint32_t beat_duration = current_beat - last_beat;
            last_beat = current_beat;

            float raw_bpm = 0;
            if(beat_duration)
                raw_bpm = 60000.0 / (float)beat_duration;

            if(this->debug){
                printf("Beat duration: %lu\n", beat_duration);
                printf("Raw BPM: %f\n", raw_bpm);
            }

            this->current_pulse_detector_state = MAX30102_PULSE_TRACE_DOWN;

            // Reset filter after a while without pulses
            if(beat_duration > 2500){ // 2.5 seconds
                memset(this->values_bpm, 0, sizeof(float)*this->pulse_bpm_sample_size);
                this->values_bpm_sum = 0;
                this->values_bpm_count = 0;
                this->bpm_index = 0;

                if(this->debug)
                    printf("Moving avg. resetted\n");
            }

            // Test if out of bounds
                if(raw_bpm < 50 || raw_bpm > 220){
                if(this->debug)
                    printf("BPM out of bounds. Not adding to Moving Avg.\n");

                return false;
            }

            // Optimized filter
            this->values_bpm_sum -= this->values_bpm[this->bpm_index];
            this->values_bpm[this->bpm_index] = raw_bpm;
            this->values_bpm_sum += this->values_bpm[this->bpm_index++];

            this->bpm_index %= this->pulse_bpm_sample_size;

            if(this->values_bpm_count < this->pulse_bpm_sample_size)
                this->values_bpm_count++;

            this->current_bpm = this->values_bpm_sum / this->values_bpm_count;

            if(this->debug) {
                printf("CurrentMoving Avg: ");

                for(int i = 0; i < this->values_bpm_count; i++)
                    printf("%f ", this->values_bpm[i]);

                printf(" \n");
                printf("AVg. BPM: %f\n", this->current_bpm);
            }

            return true;
        }
        break;
    case MAX30102_PULSE_TRACE_DOWN:
        if(sensor_value < prev_sensor_value)
            values_went_down++;

        if(sensor_value < this->pulse_min_threshold)
            this->current_pulse_detector_state = MAX30102_PULSE_IDLE;

        break;
    }

    prev_sensor_value = sensor_value;
    return false;
}

esp_err_t max30102_balance_intensities( max30102_config_t* this,
                                        float red_dc,
                                        float ir_dc )
{
    if( (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) -
        this->last_red_current_check >= this->red_current_adj_ms )
    {
        //printf("%f\n", red_dc - ir_dc);
        if( ir_dc - red_dc > this->acceptable_intense_diff &&
            this->red_current < MAX30102_LED_CURRENT_50MA )
        {
            this->red_current++;
            esp_err_t ret = max30102_set_led_current( this,
                                                      this->red_current,
                                                      this->ir_current );
            if(ret != ESP_OK) return ret;
            if(this->debug)
                printf("RED LED Current +\n");

        } else if( red_dc - ir_dc > this->acceptable_intense_diff &&
                   this->red_current > 0 )
        {
            this->red_current--;
            esp_err_t ret = max30102_set_led_current( this,
                                                      this->red_current,
                                                      this->ir_current );
            if(ret != ESP_OK) return ret;
            if(this->debug)
                printf("RED LED Current -\n");
        }

        this->last_red_current_check = (uint32_t)(xTaskGetTickCount() *
                                       portTICK_PERIOD_MS );

    }
    return ESP_OK;
}

// Writes val to address register on device
esp_err_t max30102_write_register( max30102_config_t* this,
                                   uint8_t address,
                                   uint8_t val      )
{
    // start transmission to device
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX30102_DEVICE << 1) | I2C_MASTER_WRITE, true);

    i2c_master_write_byte(cmd, address, true); // send register address
    i2c_master_write_byte(cmd, val, true); // send value to write

    // end transmission
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin( this->i2c_num,
                                          cmd,
                                          1000 / portTICK_PERIOD_MS );
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t max30102_read_register( max30102_config_t* this,
                                  uint8_t address,
                                  uint8_t* reg     )
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX30102_DEVICE << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, address, true);

    //i2c_master_stop(cmd);
    i2c_master_start(cmd);

    i2c_master_write_byte(cmd, (MAX30102_DEVICE << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, reg, 1); //1 is NACK

    // end transmission
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin( this->i2c_num,
                                          cmd,
                                          1000 / portTICK_PERIOD_MS );

    i2c_cmd_link_delete(cmd);
    return ret;
}

// Reads num bytes starting from address register on device in to _buff array
esp_err_t max30102_read_from( max30102_config_t* this,
                              uint8_t address,
                              uint8_t* reg,
                              uint8_t size )
{
    if(!size)
        return ESP_OK;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX30102_DEVICE << 1) | I2C_MASTER_WRITE, 1);
    i2c_master_write_byte(cmd, address, true);

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX30102_DEVICE << 1) | I2C_MASTER_READ, true);

    if(size > 1)
        i2c_master_read(cmd, reg, size-1, 0); //0 is ACK

    i2c_master_read_byte(cmd, reg+size-1, 1); //1 is NACK

    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin( this->i2c_num,
                                          cmd,
                                          1000 / portTICK_PERIOD_MS );
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t max30102_set_mode(max30102_config_t* this, max30102_mode_t mode) {
    uint8_t current_mode_reg;
    //Tratar erros
    esp_err_t ret = max30102_read_register( this,
                                            MAX30102_MODE_CONF,
                                            &current_mode_reg );
    if(ret != ESP_OK) return ret;
    printf("Setting the mode...");
    printf("%x\n", (current_mode_reg & 0xF8) | mode );
    return max30102_write_register( this,
                                    MAX30102_MODE_CONF,
                                    (current_mode_reg & 0xF8) | mode );
}

/* Lets Keep this just in case we break something

esp_err_t max30102_set_high_res(max30102_config_t* this, bool enabled) {
    uint8_t previous;

    //Tratar erros
    esp_err_t ret = max30102_read_register(this, MAX30102_SPO2_CONF, &previous);
    if(ret != ESP_OK) return ret;
    if(enabled) {
        return max30102_write_register( this,
                                        MAX30102_SPO2_CONF,
                                        previous | MAX30102_SPO2_HI_RES_EN );
    } else {
        return max30102_write_register( this,
                                        MAX30102_SPO2_CONF,
                                        previous & ~MAX30102_SPO2_HI_RES_EN );
    }
}
*/

esp_err_t max30102_set_high_res(max30102_config_t* this, max30102_adc_range_t adc_range) {
    uint8_t current_spO2_reg;

    //Tratar erros
    esp_err_t ret = max30102_read_register(this, MAX30102_SPO2_CONF, &current_spO2_reg);
    if(ret != ESP_OK) return ret;
    printf("Setting the ADC range...");
    printf("%x\n", (current_spO2_reg & 0x9F) | (adc_range<<5) );
    return max30102_write_register( this,
                                    MAX30102_SPO2_CONF,
                                    (current_spO2_reg & 0x9F) | (adc_range<<5) );
    
}

esp_err_t max30102_set_sampling_rate( max30102_config_t* this,
                                      max30102_sampling_rate_t rate )
{
    uint8_t current_spO2_reg;

    //Tratar erros
    esp_err_t ret = max30102_read_register( this,
                                            MAX30102_SPO2_CONF,
                                            &current_spO2_reg );
    if(ret != ESP_OK) return ret;
    printf("Setting the sampling rate...");
    printf("%x\n", (current_spO2_reg & 0xE3) | (rate<<2) );
    return max30102_write_register( this,
                                    MAX30102_SPO2_CONF,
                                    (current_spO2_reg & 0xE3) | (rate<<2) );
}

esp_err_t max30102_set_pulse_width( max30102_config_t* this,
                                    max30102_pulse_width_t pw )
{
    uint8_t current_spO2_reg;

    //Tratar erros
    esp_err_t ret = max30102_read_register( this,
                                            MAX30102_SPO2_CONF,
                                            &current_spO2_reg );
    if(ret != ESP_OK) return ret;
    printf("Setting the pulse width...");
    printf("%x\n", (current_spO2_reg & 0xFC) | pw );
    return max30102_write_register( this,
                                    MAX30102_SPO2_CONF,
                                    (current_spO2_reg & 0xFC) | pw );
}

esp_err_t max30102_set_led_current( max30102_config_t* this,
                                    max30102_current_t red_current,
                                    max30102_current_t ir_current )
{
    //Tratar erros
    /*return max30102_write_register( this,
                                    MAX30102_LED_CONF,
                                    (red_current << 4) | ir_current );
    */
   esp_err_t ret = max30102_write_register(this, MAX30102_LED_CONF, red_current);
   if(ret != ESP_OK) return ret;
   return max30102_write_register(this, MAX30102_LED_CONF_2, ir_current);
}

esp_err_t max30102_set_sample_averaging(max30102_config_t* this, 
                                       max30102_sample_averaging_t sample_averaging)
{
    uint8_t current_fifo_conf_reg;

    //Tratar erros
    esp_err_t ret = max30102_read_register( this,
                                            MAX30102_FIFO_CONF,
                                            &current_fifo_conf_reg );
    if(ret != ESP_OK) return ret;
    printf("Setting the sample averaging...");
    printf("%x\n", (current_fifo_conf_reg & 0x1F) | (sample_averaging<<5) );
    return max30102_write_register( this,
                                    MAX30102_FIFO_CONF,
                                    (current_fifo_conf_reg & 0x1F) | (sample_averaging<<5) );

}


esp_err_t max30102_set_roll_over(max30102_config_t* this, bool enabled) {
    uint8_t previous;

    //Tratar erros
    esp_err_t ret = max30102_read_register(this, MAX30102_FIFO_CONF, &previous);
    if(ret != ESP_OK) return ret;
    if(enabled) {
        return max30102_write_register( this,
                                        MAX30102_FIFO_CONF,
                                        previous | MAX30102_FIFO_ROLL_OVER_EN );
    } else {
        return max30102_write_register( this,
                                        MAX30102_FIFO_CONF,
                                        previous & ~MAX30102_FIFO_ROLL_OVER_EN );
    }
}

esp_err_t max30102_set_almost_full(max30102_config_t* this, 
                                       max30102_almost_full_t almost_full)
{
    uint8_t current_fifo_conf_reg;

    //Tratar erros
    esp_err_t ret = max30102_read_register( this,
                                            MAX30102_FIFO_CONF,
                                            &current_fifo_conf_reg );
    if(ret != ESP_OK) return ret;
    printf("Setting the almost full value...");
    printf("%x\n", (current_fifo_conf_reg & 0xF0) | almost_full );
    return max30102_write_register( this,
                                    MAX30102_FIFO_CONF,
                                    (current_fifo_conf_reg & 0xF0) | almost_full );

}

esp_err_t max30102_set_acceptable_intense_difff( max30102_config_t* this, 
                                                 uint32_t acceptable_intense_diff )
{
    //Add possible error check
    this->acceptable_intense_diff = acceptable_intense_diff;
    return ESP_OK;
}
esp_err_t max30102_set_red_current_adj_ms(max30102_config_t* this, uint32_t red_current_adj_ms) {
    //Add possible error check
    this->red_current_adj_ms = red_current_adj_ms;
    return ESP_OK;
}

esp_err_t max30102_set_reset_spo2_pulse_n(max30102_config_t* this, uint8_t reset_spo2_pulse_n) {
    //Add possible error check
    this->reset_spo2_pulse_n = reset_spo2_pulse_n;
    return ESP_OK;
}

esp_err_t max30102_set_dc_alpha(max30102_config_t* this, float dc_alpha) {
    //Add possible error check
    this->dc_alpha = dc_alpha;
    return ESP_OK;
}

esp_err_t max30102_set_pulse_min_threshold(max30102_config_t* this, uint16_t pulse_min_threshold) {
    //Add possible error check
    this->pulse_min_threshold = pulse_min_threshold;
    return ESP_OK;
}

esp_err_t max30102_set_pulse_max_threshold(max30102_config_t* this, uint16_t pulse_max_threshold) {
    //Add possible error check
    this->pulse_max_threshold = pulse_max_threshold;
    return ESP_OK;
}

esp_err_t max330100_read_temperature(max30102_config_t* this, float* temperature) {
    uint8_t current_mode_reg;
    //Tratar erros
    esp_err_t ret = max30102_read_register( this,
                                            MAX30102_INT_STATUS_2,
                                            &current_mode_reg );
    if(ret != ESP_OK) return ret;
    ret = max30102_write_register( this,
                                   MAX30102_INT_ENABLE_2,
                                   current_mode_reg | MAX30102_MODE_TEMP_EN );
    if(ret != ESP_OK) return ret;
    //This can be changed to a while loop, (with interrupt flag!)
    //there is an interrupt flag for when temperature has been read.
    vTaskDelay(100/portTICK_PERIOD_MS);

    int8_t temp;
    //Tratar erros
    ret = max30102_read_register(this, MAX30102_TEMP_INT, (uint8_t*)&temp);
    if(ret != ESP_OK) return ret;

    float temp_fraction;
    ret = max30102_read_register( this,
                                  MAX30102_TEMP_FRACTION,
                                  (uint8_t*)&temp_fraction);
    if(ret != ESP_OK) return ret;
    temp_fraction *= 0.0625;
    *temperature = (float)temp+temp_fraction;
    return ESP_OK;
}

/* Lets also keep this even though its practically useless for 30102
esp_err_t max30102_read_fifo(max30102_config_t* this, max30102_fifo_t* fifo) {
    uint8_t buffer[4];
    //Testar erros
    esp_err_t ret = max30102_read_from(this, MAX30102_FIFO_DATA, buffer, 4);
    if(ret != ESP_OK) return ret;
    fifo->raw_ir = ((uint16_t)buffer[0] << 8) | buffer[1];
    fifo->raw_red = ((uint16_t)buffer[2] << 8) | buffer[3];

    return ESP_OK;
}
*/

esp_err_t max30102_read_fifo(max30102_config_t* this, max30102_fifo_t* fifo) {
    uint8_t buffer[6];
    //Testar erros
    esp_err_t ret = max30102_read_from(this, MAX30102_FIFO_DATA, buffer, 6);
    if(ret != ESP_OK) return ret;
    fifo->raw_red = ((uint32_t)buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
    fifo->raw_ir = ((uint32_t)buffer[3] << 16) | (buffer[4] << 8) | buffer[5];

    return ESP_OK;
}

max30102_dc_filter_t max30102_dc_removal( float x,
                                          float prev_w,
                                          float alpha )
{
    max30102_dc_filter_t filtered = {};
    filtered.w = x + alpha * prev_w;
    filtered.result = filtered.w - prev_w;

    return filtered;
}

void max30102_lpb_filter( max30102_config_t* this, float x )
{
    this->lpb_filter_ir.v[0] = this->lpb_filter_ir.v[1];

    //Fs = 100Hz and Fc = 10Hz
    this->lpb_filter_ir.v[1] = (2.452372752527856026e-1 * x) +
                               ( 0.50952544949442879485 * this->lpb_filter_ir.v[0] );
    
    //Fs = 100Hz and Fc = 4Hz
    /*this->lpb_filter_ir.v[1] = (1.367287359973195227e-1 * x)
                      + (0.72654252800536101020 * this->lpb_filter_ir.v[0]);
    //Very precise butterworth filter*/

    this->lpb_filter_ir.result = this->lpb_filter_ir.v[0] +
                                 this->lpb_filter_ir.v[1];
}

float max30102_mean_diff( max30102_config_t* this, float M )
{
    float avg = 0;

    this->mean_diff_ir.sum -= this->mean_diff_ir.values[this->mean_diff_ir.index];
    this->mean_diff_ir.values[this->mean_diff_ir.index] = M;
    this->mean_diff_ir.sum += this->mean_diff_ir.values[this->mean_diff_ir.index++];

    this->mean_diff_ir.index = this->mean_diff_ir.index % this->mean_filter_size;

    if(this->mean_diff_ir.count < this->mean_filter_size)
        this->mean_diff_ir.count++;

    avg = this->mean_diff_ir.sum / this->mean_diff_ir.count;
    return avg - M;
}

esp_err_t max30102_print_registers(max30102_config_t* this)
{
    uint8_t int_status, int_enable, fifo_write, fifo_ovf_cnt, fifo_read;
    uint8_t fifo_data, mode_conf, sp02_conf, led_conf, temp_int, temp_frac;
    uint8_t rev_id, part_id;
    esp_err_t ret;

    ret = max30102_read_register(this, MAX30102_INT_STATUS, &int_status);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_INT_ENABLE, &int_enable);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_FIFO_WRITE, &fifo_write);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register( this,
                                  MAX30102_FIFO_OVERFLOW_COUNTER,
                                  &fifo_ovf_cnt );
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_FIFO_READ, &fifo_read);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_FIFO_DATA, &fifo_data);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_MODE_CONF, &mode_conf);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_SPO2_CONF, &sp02_conf);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_LED_CONF, &led_conf);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_TEMP_INT, &temp_int);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_TEMP_FRACTION, &temp_frac);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_REV_ID, &rev_id);
    if(ret != ESP_OK) return ret;
    ret = max30102_read_register(this, MAX30102_PART_ID, &part_id);
    if(ret != ESP_OK) return ret;

    printf("%x\n", int_status);
    printf("%x\n", int_enable);
    printf("%x\n", fifo_write);
    printf("%x\n", fifo_ovf_cnt);
    printf("%x\n", fifo_read);
    printf("%x\n", fifo_data);
    printf("%x\n", mode_conf);
    printf("%x\n", sp02_conf);
    printf("%x\n", led_conf);
    printf("%x\n", temp_int);
    printf("%x\n", temp_frac);
    printf("%x\n", rev_id);
    printf("%x\n", part_id);

    return ESP_OK;
}