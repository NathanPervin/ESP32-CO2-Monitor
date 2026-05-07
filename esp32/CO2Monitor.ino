// GUI:
// Configuration files and base code from: https://randomnerdtutorials.com/lvgl-cheap-yellow-display-esp32-2432s028r
// LVGL documentation: https://docs.lvgl.io/

// includes for GUI
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// includes for WIFI
#include <WiFi.h>
#include <time.h>
#include <secrets.h>
#include <HTTPClient.h>

/*
    GUI Variables
*/
// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

#define TFT_BL 21 // backlight

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

const char* top_text = "CO2 Monitor";
const char* enter_building_text = "Building";
const char* enter_room_number_text = "Room Number";
const char* ambient_text = "Ambient";
const char* session_text = "Session";
const char* CO2_plot_title = "CO2 Concentration (ppm)";
const char* time_scale_text = "Time Scale";

bool is_ambient = true; // default value is to record as an ambient, if false, mode is session

static lv_obj_t* start_screen;
static lv_obj_t* recording_screen;

static lv_obj_t * ta1; // text area for the building input
static lv_obj_t * ta2; // text area for the room input

#define MAX_BUILDING_LENGTH 31  
#define MAX_ROOM_LENGTH 11      

//#define MAX_INPUT_TEXT_LENGTH 50
char building_text[MAX_BUILDING_LENGTH];
char room_text[MAX_ROOM_LENGTH];

static lv_obj_t * chart;
static lv_chart_series_t * ser;

// the width of the x axis, in seconds 
static short int time_scale = 60; 

// determines how many point will be displayed at once,
const short int number_plot_points = 60; 
const short int CO2_plot_rate = 1000; // ms
static short int CO2_min = 400; // ppm
static short int CO2_max = 5000; // ppm

static lv_style_t style_radio;
static lv_style_t style_radio_chk;

const char* time_scales[] = {
  "60 s",
  "5 m",
  "1 hr",
  "24 hr",
  NULL
};

typedef enum {
  t_60s,
  t_5mins,
  t_1hr,
  t_24hrs
} TimeScale;

TimeScale current_time_scale = t_60s;

const int screen_sleep_after_time = 300000; // ms, 5 mins

lv_obj_t * CO2_level_label;
char CO2_level_text[30];

lv_obj_t * time_label;
char time_text[32];

lv_obj_t * error_label;
char error_text[32];

lv_obj_t * wifi_dropdown = NULL;
lv_obj_t * wifi_icon_button;

/*
//  CO2 Sensor Variables
*/
#define CO2_PWM_PIN 35

volatile unsigned long previous_low_start = 0;  // us
volatile unsigned long previous_high_start = 0; //us
volatile unsigned long TL = 0; // us
volatile unsigned long TH = 0; // us

const short int CO2_log_rate = 1000; // ms

/*
    WIFI Variables
*/
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;   // EST
const int daylightOffset_sec = 3600;  // 1 hour for daylight saving
bool wifi_initialized = false;

unsigned long last_wifi_init_attempt = 0; // timestamp of last attempt
const unsigned long attempt_wifi_init_every = 5000; // ms
static int max_init_attempts = 5;

#define MAX_SSID_LEN 64
#define MAX_PASSWORD_LEN 64

char WIFI_SSID[MAX_SSID_LEN];
char WIFI_PASSWORD[MAX_PASSWORD_LEN];

/*
    System Variables
*/
// buffers that will store the last 60 points
// which are an average of the available time intervals
static int32_t buffer_60secs[number_plot_points];
static int32_t buffer_5min[number_plot_points];
static int32_t buffer_1hr[number_plot_points];
static int32_t buffer_24hrs[number_plot_points];

// store the index where the next value should be placed
int i_60secs = 0;
int i_5mins = 0;
int i_1hr = 0;
int i_24hrs = 0;

// variables to find the average CO2 ppm over an interval
float sum_last_5s_CO2 = 0;
float count_5s = 0;
float sum_last_60s_CO2 = 0;
float count_60s = 0;
float sum_last_1440s_CO2 = 0;
float count_1440s = 0;

const short int ppm_threshold = 2000; // CO2 ppm above this will make the plot line red
static float CO2_value = 0;

bool on_start_screen = false;
bool on_recording_screen = false;

#define LOG_BUFFER_SIZE 300 // 300 JSON lines, 1s per line = 5 minutes of data

// {"mode":"session","building":"","room_number":"","unix_timestamp":1700000000,"CO2_ppm":5000}
// fixed chars:  ~64  +  max building: 30  +  max room: 10  +  null: 1  =  105, use 128 for extra room
#define MAX_JSON_LINE 128
char log_buffer[LOG_BUFFER_SIZE][MAX_JSON_LINE];
int log_buffer_index = 0;

/*
    CO2 Sensor Functions
*/
// Software interrupt on IO35 Change
void ARDUINO_ISR_ATTR read_PWM() {

  // Get level to determine if the ISR was triggered on rising on falling edge
  int level = digitalRead(CO2_PWM_PIN);

  // get the current microseconds since program start or last overflow
  unsigned long now = micros(); 

  // rising edge, the duration of the low time can be calculated by
  // subtracting the previous falling edge's time from the current time
  if (level == HIGH) {
    TL = now - previous_low_start;
    previous_high_start = now;
  } else { 
    // falling edge, the duration of the high time preceding this can be found by 
    // subtracting the previous rising edge's time from the current time
    TH = now - previous_high_start;
    previous_low_start = now;
  }
}

/*
    WIFI Functions
*/
// connects to wifi and configures NTP for unix time retrieval
void initialize_wifi() {
  Serial.printf("Connecting to Wi-Fi %s ...\n", WIFI_SSID);

  // SSID and password must be inside of secrets.h
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // try to connect to the wifi
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < max_init_attempts) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if(WiFi.status() != WL_CONNECTED){
    Serial.println("\nFailed to connect to Wi-Fi");
    wifi_initialized = false;
    error_handler();
    return;
  } 
  else {
    wifi_initialized = true;
    error_handler();
  }

  Serial.println("\nWi-Fi connected!");
  wifi_initialized = true;
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

// Handles wifi connection attempts if wifi connection fails,
// attempts to estabish a new connection on set intervals until
// a successful connection is made
bool handle_wifi_reinitialization() {
  if (!wifi_initialized) {

    // check that the time interval between connections has passed
    if (millis() - last_wifi_init_attempt >= attempt_wifi_init_every) {
      
      initialize_wifi();
      last_wifi_init_attempt = millis(); // reset timestamp
      return wifi_initialized;
    }
    else { // time interval for new connection attempt has no yet passed
      return false;
    }
  } // wifi is already initialized
  return true;
}

// returns the unix time 
time_t get_unix_time() {

  // if the wifi is not connected, then the unix time cannot be establisheds
  if (!handle_wifi_reinitialization()) return -1;

  time_t now = time(nullptr);
  int retry = 0;
  const int retry_count = 10;

  // ensure unix time is valid, otherwise try again
  while (now < 1000000000 && retry < retry_count) { 
    delay(500);
    now = time(nullptr);
    retry++;
  }

  if(now < 1000000000) return 0;
  return now;
}

/*
    System Functions
*/
// saves a CO2 level and unix timestamp to a JSON string and adds it to the buffer 
// that stores all of the JSON lines prior to sending the POST request
// also calls function that will POST when buffer is full
void log_data(int32_t CO2_ppm, time_t unix_time) {

  // synthesize the JSON string that will be stored in the buffer 
  snprintf(log_buffer[log_buffer_index], MAX_JSON_LINE,
    "{\"mode\":\"%s\",\"building\":\"%s\",\"room_number\":\"%s\",\"unix_timestamp\":%ld,\"CO2_ppm\":%d}",
    is_ambient ? "ambient" : "session",
    building_text,
    room_text,
    (long)unix_time,
    (int)CO2_ppm
  );

  // each time a line is added to the buffer, increment a counter and
  // call function to POST the data when the buffer is full
  log_buffer_index++;
  if (log_buffer_index >= LOG_BUFFER_SIZE) {
    upload_data();
    log_buffer_index = 0;
  }
}

// makes the POST request to the SERVER_URL in secrets.h
// handles inputting the api token into the header for authentification
bool upload_data() {

  // ensure that wifi is connected
  if (!handle_wifi_reinitialization()) {
    Serial.println("Cannot upload: WiFi not connected");
    return false;
  }

  // calculate payload size upfront to avoid reallocation
  int payload_len = 2; // [ ]
  for (int i = 0; i < log_buffer_index; i++) {
    payload_len += strlen(log_buffer[i]);
    if (i < log_buffer_index - 1) payload_len += 1; // comma
  }

  String payload;
  payload.reserve(payload_len + 1);
  payload = "[";
  for (int i = 0; i < log_buffer_index; i++) {
    payload += log_buffer[i];
    if (i < log_buffer_index - 1) payload += ",";
  }
  payload += "]";

  WiFiClient client;
  HTTPClient http;
  http.begin(client, SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(API_TOKEN));

  Serial.printf("Uploading %d readings...\n", log_buffer_index);
  int response_code = http.POST(payload);

  if (response_code > 0) {
    Serial.printf("POST response: %d\n", response_code);
    Serial.println(http.getString());
    http.end();
    return true;
  } else {
    Serial.printf("POST failed: %s\n", http.errorToString(response_code).c_str());
    http.end();
    return false;
  }
}

// called each time a CO2 value is collected (every 1 second),
// values get averaged and stored in all buffers to allow for
// past data to be displayed for new timescale 
void load_buffers(float CO2_ppm) {
  
  int32_t val = (int32_t)CO2_ppm;
  time_t time = get_unix_time();

  // display nothing to the user if time is not valid
  // usually when wifi isn't initialized yet
  if (time == -1 || time == 0) {
    snprintf(time_text, sizeof(time_text), "");
  } 
  else {
    convert_time(time); // saves time to global var time_text

    // only log the value if we are able to receive a time stamp
    log_data(val, time);
  }

  // load 60 seconds buffer, get next index
  buffer_60secs[i_60secs] = val;
  i_60secs = (i_60secs + 1) % number_plot_points; // index of 60 loops back to 0

  // avoid duplicate plot updates
  if (current_time_scale == t_60s) {
    update_plot();
  }

  // calculate sums and advance counters
  sum_last_5s_CO2 += CO2_ppm;
  count_5s++;
  sum_last_60s_CO2 += CO2_ppm;
  count_60s++;
  sum_last_1440s_CO2 += CO2_ppm;
  count_1440s++;

  // push the last 5 second average into the 5 minute buffer 
  if(count_5s >= 5) {

    int32_t avg = (int32_t)(sum_last_5s_CO2 / count_5s);

    // load buffer, get next index 
    buffer_5min[i_5mins] = avg;
    i_5mins = (i_5mins + 1) % number_plot_points;

    sum_last_5s_CO2 = 0;
    count_5s = 0;
    update_plot();
  }

  // push the last 60 second average into the 1 hour buffer 
  if(count_60s >= 60) {

    int32_t avg = (int32_t)(sum_last_60s_CO2 / count_60s);

    // load buffer, get next index 
    buffer_1hr[i_1hr] = avg;
    i_1hr = (i_1hr + 1) % number_plot_points;

    sum_last_60s_CO2 = 0;
    count_60s = 0;
    update_plot();
  }

  // push the last 1440 second average into the 24 hour buffer 
  if(count_1440s >= 1440) {

    int32_t avg = (int32_t)(sum_last_1440s_CO2 / count_1440s);

    // load buffer, get next index 
    buffer_24hrs[i_24hrs] = avg;
    i_24hrs = (i_24hrs + 1) % number_plot_points;

    sum_last_1440s_CO2 = 0;
    count_1440s = 0;
    update_plot();
  }

}

// clears all buffers and related variables
// used when user creates a new recording session
void clear_buffers() {

  // initialize the buffers so that the plot will be empty on start
  for(int i = 0; i < number_plot_points; i++) {
    buffer_60secs[i] = LV_CHART_POINT_NONE;
    buffer_5min[i] = LV_CHART_POINT_NONE;
    buffer_1hr[i] = LV_CHART_POINT_NONE;
    buffer_24hrs[i] = LV_CHART_POINT_NONE;
  }

  i_60secs = 0;
  i_5mins = 0;
  i_1hr = 0;
  i_24hrs = 0;

  sum_last_5s_CO2 = 0;
  count_5s = 0;
  sum_last_60s_CO2 = 0;
  count_60s = 0;
  sum_last_1440s_CO2 = 0;
  count_1440s = 0;

  log_buffer_index = 0;

}

// Gets the building and room number text from the text areas, checks that the they are valid
bool get_location() {
  const char* building_text_const = lv_textarea_get_text(ta1); 
  const char* room_text_const = lv_textarea_get_text(ta2); 

  // enforce maximum building and room length in case lvgl max
  // input limit fails to avoid buffer overflow when creating JSON lines
  if (strlen(building_text_const) >= MAX_BUILDING_LENGTH) {
    Serial.println("Error: Building name too long!");
    return false;
  }
  if (strlen(room_text_const) >= MAX_ROOM_LENGTH) {
    Serial.println("Error: Room number too long!");
    return false;
  }

  // if no building was entered, save as 'debug'
  if (strlen(building_text_const) == 0) {
    strncpy(building_text, "debug", MAX_BUILDING_LENGTH - 1);
  } else {
    strncpy(building_text, building_text_const, MAX_BUILDING_LENGTH - 1);
  }
  building_text[MAX_BUILDING_LENGTH - 1] = '\0';

  // if no room was entered, save as '0'
  if (strlen(room_text_const) == 0) {
    strncpy(room_text, "0", MAX_ROOM_LENGTH - 1);
  } else {
    strncpy(room_text, room_text_const, MAX_ROOM_LENGTH - 1);
  }
  room_text[MAX_ROOM_LENGTH - 1] = '\0';

  return true;
}

// called when start button is pressed, gets the wifi option that
// the user selected from the dropdown on the start screen
void get_wifi_option() {
  uint16_t sel = lv_dropdown_get_selected(wifi_dropdown);
  if(sel < NUMBER_WIFI_OPTIONS) {
    snprintf(WIFI_SSID, sizeof(WIFI_SSID), "%s", WIFI_SSIDS[sel]);
    snprintf(WIFI_PASSWORD, sizeof(WIFI_PASSWORD), "%s", WIFI_PASSWORDS[sel]);
  }
}

// called to update the error text, handles displaying multiple errors
static void error_handler() {
  if (!wifi_initialized) {
    snprintf(error_text, sizeof(error_text), "%s", "Failed to connect to Wi-Fi");
  }
  else { // TMP, save old errors later
    snprintf(error_text, sizeof(error_text), "%s", "");
  }
}

// Get the Touchscreen data
void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {

  // Checks if Touchscreen was touched, and prints X, Y and Pressure (Z)
  if(touchscreen.tirqTouched() && touchscreen.touched()) {
    
    // turn on backlight to make screen visable
    digitalWrite(TFT_BL, HIGH);

    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();

    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;

    data->state = LV_INDEV_STATE_PRESSED;

    // Set the coordinates
    data->point.x = x;
    data->point.y = y;
  }
  else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// called when user clicks into a text area
static void ta_event_cb(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * ta = lv_event_get_target_obj(e);
  lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data(e);

  // clicked into text area
  if(code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);

    // when the user clicks into a text area, the dropdown should be hidden
    // so as to not overlay the keyboard
    if (wifi_dropdown == NULL) return;
    if (!lv_obj_has_flag(wifi_dropdown, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_add_flag(wifi_dropdown, LV_OBJ_FLAG_HIDDEN);
    } 
  }

  // clicked out of text area
  if(code == LV_EVENT_DEFOCUSED) {
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}

// Creates text centered at the top of the screen
void lv_top_text(lv_obj_t* screen) {

  lv_obj_t * text_label = lv_label_create(screen);
  lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);    
  lv_label_set_text(text_label, top_text);
  lv_obj_set_width(text_label, 150);    // width of the text (wraps if text is longer than the input number)
  lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(text_label, LV_ALIGN_CENTER, 0, -105);
}

// Creates the keyboard when a text area is selected
// Allows the user to enter a building and room number
void lv_keyboard(lv_obj_t* screen) {

  // Create a keyboard, automatically displays when a text box is clicked on
  lv_obj_t * kb = lv_keyboard_create(screen);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  // Enter Building Text (Above the text box)
  lv_obj_t * building_text_label = lv_label_create(screen);
  lv_label_set_long_mode(building_text_label, LV_LABEL_LONG_WRAP);    // Breaks the long lines
  lv_label_set_text(building_text_label, enter_building_text);
  lv_obj_set_width(building_text_label, 150);    // Set smaller width to make the lines wrap
  lv_obj_set_style_text_align(building_text_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(building_text_label, LV_ALIGN_TOP_LEFT, 45, 30);

  // text area for the building entry. The keyboard will write here
  ta1 = lv_textarea_create(screen);
  lv_obj_align(ta1, LV_ALIGN_TOP_LEFT, 10, 50);
  lv_obj_add_event_cb(ta1, ta_event_cb, LV_EVENT_ALL, kb);
  lv_textarea_set_placeholder_text(ta1, "COE");
  lv_obj_set_size(ta1, 140, 35);
  lv_textarea_set_max_length(ta1, MAX_BUILDING_LENGTH-1);
  
  // Room Number Text (Above the text box)
  lv_obj_t * room_number_text_label = lv_label_create(screen);
  lv_label_set_long_mode(room_number_text_label, LV_LABEL_LONG_WRAP);    // Breaks the long lines
  lv_label_set_text(room_number_text_label, enter_room_number_text);
  lv_obj_set_width(room_number_text_label, 200);    // Set smaller width to make the lines wrap
  lv_obj_set_style_text_align(room_number_text_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(room_number_text_label, LV_ALIGN_TOP_LEFT, 185, 30);

  // text area for the room number, the keyboard will write here
  ta2 = lv_textarea_create(screen);
  lv_obj_align(ta2, LV_ALIGN_TOP_RIGHT, -10, 50);
  lv_obj_add_event_cb(ta2, ta_event_cb, LV_EVENT_ALL, kb);
  lv_textarea_set_placeholder_text(ta2, "306");
  lv_obj_set_size(ta2, 140, 35);
  lv_textarea_set_max_length(ta2, MAX_ROOM_LENGTH-1);

  lv_keyboard_set_textarea(kb, ta1);

}

// Called when the Ambient/Session switch is toggled,
// used for specifying the mode
static void switch_event_handler(lv_event_t * e) {

  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * obj = lv_event_get_target_obj(e); 
  LV_UNUSED(obj);
  if(code == LV_EVENT_VALUE_CHANGED) {

    // if its set to "ON", its set to a session, otherwise its set to ambient   
    is_ambient = !(lv_obj_has_state(obj, LV_STATE_CHECKED)); 
  }
}
// Creates the ambient/session toggle switch
// located on the left, below the text areas
void lv_switch(lv_obj_t* screen) {

  // Ambient Text (to the left of the switch)
  lv_obj_t * ambient_text_label = lv_label_create(screen);
  lv_label_set_long_mode(ambient_text_label, LV_LABEL_LONG_WRAP);    // Breaks the long lines
  lv_label_set_text(ambient_text_label, ambient_text);
  lv_obj_set_width(ambient_text_label, 200);    // Set smaller width to make the lines wrap
  lv_obj_set_style_text_align(ambient_text_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(ambient_text_label, LV_ALIGN_TOP_LEFT, 10, 95);

  // Session Text (to the right of the switch)
  lv_obj_t * session_text_label = lv_label_create(screen);
  lv_label_set_long_mode(session_text_label, LV_LABEL_LONG_WRAP);    // Breaks the long lines
  lv_label_set_text(session_text_label, session_text);
  lv_obj_set_width(session_text_label, 200);    // Set smaller width to make the lines wrap
  lv_obj_set_style_text_align(session_text_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(session_text_label, LV_ALIGN_TOP_LEFT, 135, 95);

  // toggle switch (centered between texts)
  lv_obj_t * sw;
  sw = lv_switch_create(screen);
  lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 80, 90);
  lv_obj_add_event_cb(sw, switch_event_handler, LV_EVENT_ALL, NULL);
  lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);

}

// Called when the Start button is pressed
static void start_button_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {

    // if the location text is valid, advance to the recording screen
    if (!get_location()) return;
    get_wifi_option();
    lv_scr_load(recording_screen);

    on_start_screen = false;
    on_recording_screen = true;
    clear_buffers();

    // initialize wifi is not called here since it blocks the screen loading,
    // it is instead called when the buffer is loaded and time is retrieved 
  }
}

// Creates the start button
void lv_start_button(lv_obj_t* screen) {

  // Button, located to the right of toggle switch
  lv_obj_t * label;
  lv_obj_t * btn1 = lv_button_create(screen);
  lv_obj_add_event_cb(btn1, start_button_event, LV_EVENT_ALL, NULL);
  lv_obj_align(btn1, LV_ALIGN_TOP_LEFT, 245, 85);
  lv_obj_remove_flag(btn1, LV_OBJ_FLAG_PRESS_LOCK);
  
  // Text inside the button
  label = lv_label_create(btn1);
  lv_label_set_text(label, "Start");
  lv_obj_center(label);

}

// Creates the chart for CO2 values
void lv_chart(lv_obj_t* screen) {
  chart = lv_chart_create(screen);
  lv_obj_set_size(chart, 200, 140); // 200x140 pixels
  lv_obj_align(chart, LV_ALIGN_CENTER, 60, -30);

  lv_obj_refresh_ext_draw_size(chart);

  ser = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, CO2_min, CO2_max);
  lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, time_scale);
  lv_chart_set_point_count(chart, number_plot_points);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(chart, 5, 5); // (...,5 horizontal lines, 5 vertical lines)

  // Text for the plot title
  lv_obj_t * label = lv_label_create(screen);
  lv_label_set_text(label, CO2_plot_title);
  lv_obj_align_to(label, chart, LV_ALIGN_OUT_TOP_MID, -2, 0);

  // Text for the top (max) CO2 axis label
  lv_obj_t * y_label_top = lv_label_create(screen);
  lv_label_set_text_fmt(y_label_top, "%d", CO2_max);
  lv_obj_set_style_text_font(y_label_top, &lv_font_montserrat_10, 0);
  lv_obj_align_to(y_label_top, chart, LV_ALIGN_OUT_LEFT_TOP, 0, 5);

  // Text for between the top and middle CO2 axis label 
  lv_obj_t * y_label_1 = lv_label_create(screen);
  lv_label_set_text_fmt(y_label_1, "%d", CO2_min + (CO2_max - CO2_min)*3/4);
  lv_obj_set_style_text_font(y_label_1, &lv_font_montserrat_10, 0);
  lv_obj_align_to(y_label_1, chart, LV_ALIGN_OUT_LEFT_TOP, 0, lv_obj_get_height(chart)/4);

  // Text for the middle CO2 axis label
  lv_obj_t * y_label_mid = lv_label_create(screen);
  lv_label_set_text_fmt(y_label_mid, "%d", (CO2_min + CO2_max)/2);
  lv_obj_set_style_text_font(y_label_mid, &lv_font_montserrat_10, 0);
  lv_obj_align_to(y_label_mid, chart, LV_ALIGN_OUT_LEFT_MID, 0, 0);

  // Text for between the middle and bottom CO2 axis label 
  lv_obj_t * y_label_3 = lv_label_create(screen);
  lv_label_set_text_fmt(y_label_3, "%d", CO2_min + (CO2_max - CO2_min)*1/4);
  lv_obj_set_style_text_font(y_label_3, &lv_font_montserrat_10, 0);
  lv_obj_align_to(y_label_3, chart, LV_ALIGN_OUT_LEFT_BOTTOM, 0, -lv_obj_get_height(chart)/4);

  // Text for the bottom (min) CO2 axis label
  lv_obj_t * y_label_bottom = lv_label_create(screen);
  lv_label_set_text_fmt(y_label_bottom, "%d", CO2_min);
  lv_obj_set_style_text_font(y_label_bottom, &lv_font_montserrat_10, 0);
  lv_obj_align_to(y_label_bottom, chart, LV_ALIGN_OUT_LEFT_BOTTOM, 0, -5);

}

// called when the user changes the option on the wifi dropdown
static void wifi_dropdown_handler(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * obj = lv_event_get_target_obj(e);

  if(code == LV_EVENT_VALUE_CHANGED) {
    get_wifi_option();
  }
}

// Creates the dropdown list that contains all of the wifi networks
// saved in secrets.h
void lv_dropdown(lv_obj_t* screen)
{
    wifi_dropdown = lv_dropdown_create(screen);

    // Build newline-separated options
    static char options_str[256];
    options_str[0] = '\0';
    for(int i = 0; i < NUMBER_WIFI_OPTIONS; i++) {
        strcat(options_str, WIFI_SSIDS[i]);
        if(i < NUMBER_WIFI_OPTIONS - 1) strcat(options_str, "\n");
    }

    lv_dropdown_set_options(wifi_dropdown, options_str);
    lv_obj_align(wifi_dropdown, LV_ALIGN_TOP_RIGHT, -80, 120);
    lv_obj_add_event_cb(wifi_dropdown, wifi_dropdown_handler, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(wifi_dropdown, LV_OBJ_FLAG_HIDDEN); // dropdown hidden initially
}

// called when the user presses the button with the wifi icon on the start page
// shows or hides the dropdown list of wifi options
static void wifi_icon_button_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED) return;

  // Toggle popup visibility
  if (lv_obj_has_flag(wifi_dropdown, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_remove_flag(wifi_dropdown, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(wifi_dropdown, LV_OBJ_FLAG_HIDDEN);
  }
}

// Creates the button with the wifi icon on the start page
void lv_wifi_icon_button(lv_obj_t * screen) {

  // Small WiFi icon button, placed to the right of the ambient/session switch area
  lv_obj_t * btn = lv_button_create(screen);
  lv_obj_set_size(btn, 40, 30);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -80, 85);
  lv_obj_add_event_cb(btn, wifi_icon_button_event, LV_EVENT_ALL, NULL);

  lv_obj_t * label = lv_label_create(btn);
  lv_label_set_text(label, LV_SYMBOL_WIFI);
  lv_obj_center(label);
}

// Called when the user selects a radio button option
// for changing the plot timescale
static void event_radio_button(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target_obj(e);

  // check that the checkbox is checked (radio button is selected)
  // since this event is called twice for the unchecking of the previous
  // option and the checking of a new option
  if(!lv_obj_has_state(obj, LV_STATE_CHECKED)) {
    return;
  }

  // determine which timescale the user has selected:
  // 60 seconds timescale
  if (strcmp(lv_checkbox_get_text(obj), time_scales[0]) == 0) {
    current_time_scale = t_60s;
  }
  else if (strcmp(lv_checkbox_get_text(obj), time_scales[1]) == 0) {
    current_time_scale = t_5mins;
  }
  else if (strcmp(lv_checkbox_get_text(obj), time_scales[2]) == 0) {
    current_time_scale = t_1hr;
  }
  else if (strcmp(lv_checkbox_get_text(obj), time_scales[3]) == 0) {
    current_time_scale = t_24hrs;
  }
  else {
    Serial.print("Unable to resolve time scale from radio box selection!");
  }
  Serial.println("mac address: ");
  Serial.println(WiFi.macAddress());
  update_plot();
}

// Creates checkboxes as radio buttons for the time scale selection
void lv_radio_buttons(lv_obj_t* screen) {
  lv_style_init(&style_radio);
  lv_style_set_radius(&style_radio, LV_RADIUS_CIRCLE);
  lv_style_init(&style_radio_chk);
  lv_style_set_bg_image_src(&style_radio_chk, NULL);

  lv_obj_t * cont = lv_obj_create(screen);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(cont, LV_ALIGN_CENTER, -115, -30);

  // Create the selection options from the time_scales list of strings
  uint32_t i;
  char buf[32];
  for(int i = 0; time_scales[i] != NULL; i++) {

    lv_snprintf(buf, sizeof(buf), time_scales[i]);

    lv_obj_t * obj = lv_checkbox_create(cont);
    lv_checkbox_set_text(obj, buf);

    lv_obj_add_event_cb(obj, event_radio_button, LV_EVENT_VALUE_CHANGED, NULL);

    // This makes the checkboxes act as radio buttons
    lv_obj_set_radio_button(obj, true);

    lv_obj_add_style(obj, &style_radio, LV_PART_INDICATOR);
    lv_obj_add_style(obj, &style_radio_chk, LV_PART_INDICATOR | LV_STATE_CHECKED);
  }

  // Make the first checkbox checked
  lv_obj_add_state(lv_obj_get_child(cont, 0), LV_STATE_CHECKED);

  lv_obj_t * label = lv_label_create(screen);
  lv_label_set_text(label, time_scale_text);
  lv_obj_align_to(label, cont, LV_ALIGN_OUT_TOP_MID, -2, 0);
}

// Called when the Stop button is pressed
// brings user back to the starting screen
static void stop_button_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {

    lv_scr_load(start_screen);
    on_start_screen = true;
    on_recording_screen = false;
  }
}

// Creates the stop button
void lv_stop_button(lv_obj_t* screen) {

  // Button, located to the right of toggle switch
  lv_obj_t * label;
  lv_obj_t * btn2 = lv_button_create(screen);
  lv_obj_add_event_cb(btn2, stop_button_event, LV_EVENT_ALL, NULL);
  lv_obj_align(btn2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_remove_flag(btn2, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_size(btn2, 100, 50);
  
  // Text inside the button
  label = lv_label_create(btn2);
  lv_label_set_text(label, "Stop");
  lv_obj_center(label);

}

// converts unix time to a human-readable time stamp
void convert_time(time_t t) {
  struct tm * timeinfo;
  timeinfo = localtime(&t); // converts to local time

  // save to gloal variable
  //strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", timeinfo); // military time
  strftime(time_text, sizeof(time_text), "%Y-%m-%d %I:%M:%S %p", timeinfo);
}

// Creates the text for the CO2 level, time, and errors
void lv_data_text(lv_obj_t* screen) {

  // Text for the current CO2 level
  CO2_level_label = lv_label_create(screen);
  lv_label_set_text(CO2_level_label, "");
  lv_obj_align_to(CO2_level_label, chart, LV_ALIGN_OUT_BOTTOM_LEFT, 40, 0);

  // Text for the current time
  time_label = lv_label_create(screen);
  lv_label_set_text(time_label, "");
  lv_obj_align_to(time_label, chart, LV_ALIGN_OUT_BOTTOM_LEFT, -115, 0);
  lv_obj_set_width(time_label, 90);

  // Text for the active error messages
  error_label = lv_label_create(screen);
  lv_label_set_text(error_label, "");
  lv_obj_align_to(error_label, chart, LV_ALIGN_OUT_BOTTOM_LEFT, -115, 60);
  lv_obj_set_style_text_color(error_label, lv_palette_main(LV_PALETTE_RED), 0);

}

// Creates the GUI elements for the start and recording screens
void lv_create_main_gui(void) {

  // initialize the two screens
  start_screen = lv_obj_create(NULL);
  recording_screen = lv_obj_create(NULL);

  // call functions to create start screen
  lv_top_text(start_screen);
  lv_keyboard(start_screen);
  lv_switch(start_screen);
  lv_start_button(start_screen);
  lv_wifi_icon_button(start_screen);
  lv_dropdown(start_screen);

  lv_obj_clear_flag(start_screen, LV_OBJ_FLAG_SCROLLABLE); // prevent the screen fron scrolling when trying to enter info

  // call functions to create recording screen
  lv_chart(recording_screen);
  lv_radio_buttons(recording_screen);
  lv_stop_button(recording_screen);
  lv_data_text(recording_screen);

  // set the start screen as the active scren
  lv_scr_load(start_screen);
  on_start_screen = true;
}

// Called from load_buffers, when new value in inserted into a buffer 
void update_plot() {

  // reload entire buffer when plot is updated
  if (current_time_scale == t_60s) {
    lv_chart_set_series_ext_y_array(chart, ser, buffer_60secs);
    lv_chart_set_x_start_point(chart, ser, i_60secs);
  }
  else if (current_time_scale == t_5mins) {
    lv_chart_set_series_ext_y_array(chart, ser, buffer_5min);
    lv_chart_set_x_start_point(chart, ser, i_5mins);
  }
  else if (current_time_scale == t_1hr) {
    lv_chart_set_series_ext_y_array(chart, ser, buffer_1hr);
    lv_chart_set_x_start_point(chart, ser, i_1hr);
  }
  else if (current_time_scale == t_24hrs) {
    lv_chart_set_series_ext_y_array(chart, ser, buffer_24hrs);
    lv_chart_set_x_start_point(chart, ser, i_24hrs);
  }
  else {
    Serial.print("Invalid current_time_scale state!");
    return;
  }

  // turn the plot red if the CO2 ppm goes above the threshold or green if its below
  if (CO2_value > ppm_threshold) {
    lv_chart_set_series_color(chart, ser, lv_palette_main(LV_PALETTE_RED));
  } 
  else {
      lv_chart_set_series_color(chart, ser, lv_palette_main(LV_PALETTE_GREEN));
  }

  // update CO2 level label with the newest value
  snprintf(CO2_level_text, sizeof(CO2_level_text), "CO2 Level: %d (ppm)", (int32_t)CO2_value);
  lv_label_set_text(CO2_level_label, CO2_level_text);

  // update the other texts
  lv_label_set_text(time_label, time_text);
  lv_label_set_text(error_label, error_text);

  lv_chart_refresh(chart);
}

void setup() {
  Serial.begin(115200);

  // Initialize CO2 Sensor PWM pin and assign software interrupt
  pinMode(CO2_PWM_PIN, INPUT);
  attachInterrupt(CO2_PWM_PIN, read_PWM, CHANGE);

  // Start LVGL
  lv_init();

  // Start the SPI for the touchscreen and init the touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);

  // Set the Touchscreen rotation in landscape mode
  // Note: in some displays, the touchscreen might be upside down, so you might need to set the rotation to 0: touchscreen.setRotation(0);
  touchscreen.setRotation(2);

  // Create a display object
  lv_display_t * disp;

  // Initialize the TFT display using the TFT_eSPI library
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    
  // Initialize an LVGL input device object (Touchscreen)
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);

  // Set the callback function to read Touchscreen input
  lv_indev_set_read_cb(indev, touchscreen_read);

  // Function to draw the GUI (text, buttons and sliders)
  lv_create_main_gui();
}

void loop() {
  lv_task_handler();  // let the GUI do its work
  lv_tick_inc(5);     // tell LVGL how much time has passed
  delay(5);           // let this time pass

  // CO2 sensor elapsed time
  static unsigned long previous_log_time = 0;
  unsigned long now = millis();

  // CO2 sensor ppm calculation and logging
  if (now - previous_log_time >= CO2_log_rate) { // update every 1 second
    previous_log_time = now;

    // TH + TL should sum to above 4000 us to prevent a divide
    // by zero or negative ppm (can occur while sensor is heating)
    if (TH + TL > 4000) {

      // Using formula from datasheet: Cppm = 5000 * (TH - 2) / (TH + TL - 4),
      // converted to microseconds
      float ppm = 5000.0 * (TH - 2000) / (TH + TL - 4000);
      ppm = constrain(ppm, 400, 5000); // the sensor's range is 400-5000 ppm
      CO2_value = ppm;

      if (on_recording_screen) {
        load_buffers(ppm);
      }
    } else {
      Serial.println("Invalid PWM Received");
    }
  }
  
  // Turns the backlight off after some time of inactivity
  if(lv_disp_get_inactive_time(NULL) > screen_sleep_after_time) {
    digitalWrite(TFT_BL, LOW);
  }

}