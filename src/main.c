#include <pebble.h>
	
#define TEMP(x) (metric_units ? (x / 100) : (int)((double)(x) / 100.0 * 9.0 / 5.0 + 32.0))

Window *window;
TextLayer *text_layer;
TextLayer *time_layer;
//TextLayer *date_layer;
InverterLayer *inverter_layer;
static char battery[8];
static char latitude[24];
static char longitude[24];
static char location[24];
static char weather[24];
static int temperature = -999999;
static int temperature_min = -999999;
static int temperature_max = -999999;
static bool show_seconds = false;
static bool invert_face = false;
static time_t last_update = -1;
static int timezone_offset = 0;
static bool metric_units = true;
static bool bluetooth = false;
static time_t last_attempt = -1;

enum {
	KEY_TEMPERATURE = 0,
	KEY_LATITUDE = 1,
	KEY_LONGITUDE = 2,
	KEY_SHOW_SECONDS = 3,
	KEY_INVERT = 4,
	KEY_TIMEZONE = 5,
	KEY_LOCATION = 6,
	KEY_WEATHER = 7,
	KEY_UPDATE = 8,
	KEY_UNITS = 9,
	KEY_TEMPERATURE_MIN = 10,
	KEY_TEMPERATURE_MAX = 11,
};

enum {
	CMD_UPDATE = 0,
};

void handle_battery_change(BatteryChargeState charge) {
	snprintf(battery, sizeof(battery), "%u%%%s%s", charge.charge_percent, charge.is_charging ? "+" : "", charge.is_plugged  ? "*" : "");
}

void handle_bluetooth_change(bool connected) {
	bluetooth = connected;
}

void process_tuple(Tuple *t) {
	int key = t->key;
	char *p = 0;
	
	last_attempt = -1;
	
	switch(key) {
		case KEY_LATITUDE:
			strncpy(latitude, t->value->cstring, sizeof(latitude));
			p = strchr(latitude, '.');
			if(p) p[7] = 0;
			break;
		case KEY_LONGITUDE:
			strncpy(longitude, t->value->cstring, sizeof(longitude));
			p = strchr(longitude, '.');
			if(p) p[7] = 0;
			break;
		case KEY_LOCATION:
			strncpy(location, t->value->cstring, sizeof(location));
			break;
		case KEY_WEATHER:
			strncpy(weather, t->value->cstring, sizeof(weather));
			break;
		case KEY_TEMPERATURE:
			temperature = t->value->int32;
			break;
		case KEY_TEMPERATURE_MIN:
			temperature_min = t->value->int32;
			break;
		case KEY_TEMPERATURE_MAX:
			temperature_max = t->value->int32;
			break;
		case KEY_TIMEZONE:
			timezone_offset = t->value->int32;
			break;
		case KEY_UPDATE:
			last_update = t->value->int32;
			break;
		
		// settings:
		case KEY_SHOW_SECONDS:
		{
			show_seconds = false;
			if(!strcmp(t->value->cstring, "on"))
				show_seconds = true;
			persist_write_bool(KEY_SHOW_SECONDS, show_seconds);
			break;
		}
		case KEY_INVERT:
		{
			invert_face = false;
			if(!strcmp(t->value->cstring, "on"))
				invert_face = true;
			persist_write_bool(KEY_INVERT, invert_face);
			layer_set_hidden(inverter_layer_get_layer(inverter_layer), invert_face);
			break;
		}
		case KEY_UNITS:
		{
			metric_units = false;
			if(!strcmp(t->value->cstring, "metric"))
				metric_units = true;
			persist_write_bool(KEY_UNITS, metric_units);
			break;
		}
	}	
}

void in_received_handler(DictionaryIterator *iter, void *context) {
	Tuple *t = dict_read_first(iter);
	if(t)
		process_tuple(t);
	
	while(t) {
		t = dict_read_next(iter);
		if(t)
			process_tuple(t);
	}
}

void send_int(uint8_t key, uint8_t cmd) {
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);

	time_t current_time = time(0);
	if(last_attempt != -1 && current_time - last_attempt < 15) return; // don't try too often
	last_attempt = current_time;
	
	Tuplet value = TupletInteger(key, cmd);
	dict_write_tuplet(iter, &value);
	
	app_message_outbox_send();
}

void handle_timechanges(struct tm *tick_time, TimeUnits units_changed) {
	static char time_buffer[256];
	static char clock_buffer[64];
//	static char day_buffer[10];
//	static char date_buffer[10];
	static char ampm[3];
	static char buf[64];
	char *p = 0;
	strftime(ampm, sizeof(ampm), "%p", tick_time);
	time_t current_time = time(0);
	time_t since = current_time + timezone_offset - last_update;

	if(tick_time->tm_sec == 0 && tick_time->tm_min % 15 == 0)
		send_int(KEY_TEMPERATURE, CMD_UPDATE);
	else if(bluetooth && last_update == -1)
		send_int(KEY_TEMPERATURE, CMD_UPDATE);

	static char time_format[16];
	snprintf(time_format, sizeof(time_format), "%%l:%%M");
	if(clock_is_24h_style()) snprintf(time_format, sizeof(time_format), "%%H:%%M");
	if(show_seconds) strncat(time_format, ":%S", sizeof(time_format));
	
	if(clock_is_24h_style())
		strftime(clock_buffer, sizeof(clock_buffer), time_format, tick_time);
	else {
		strftime(clock_buffer, sizeof(clock_buffer), time_format, tick_time);
		snprintf(buf, sizeof(buf), "%c", ampm[0] == 'A' ? 'a' : 'p');
		strncat(clock_buffer, buf, sizeof(time_buffer));
	}
	
	p = clock_buffer;
	if(p[0] == ' ') p++;
	text_layer_set_text(time_layer, p);

	//strncat(time_buffer, "\n", sizeof(time_buffer));	
	strftime(time_buffer, sizeof(time_buffer), "%A\n%B ", tick_time);
	//strncat(time_buffer, buf, sizeof(time_buffer));	
	
	strftime(buf, sizeof(buf), "%e, %Y", tick_time);
	p = buf;
	if(p[0] == ' ') p++;
	strncat(time_buffer, p, sizeof(time_buffer));	
	
	snprintf(buf, sizeof(buf), "\nepoch: %ld", current_time + timezone_offset);
	strncat(time_buffer, buf, sizeof(time_buffer));

	snprintf(buf, sizeof(buf), "\nbattery: %s", battery);
	strncat(time_buffer, buf, sizeof(time_buffer));

	snprintf(buf, sizeof(buf), "\nconnected: %s", bluetooth ? "yes" : "no");
	strncat(time_buffer, buf, sizeof(time_buffer));

	// only show weather and location if we're connected and it has been updated
	if(bluetooth && last_update != -1) {
		if(latitude[0] != 0) {
			snprintf(buf, sizeof(buf), "\nlatitude: %s", latitude);
			strncat(time_buffer, buf, sizeof(time_buffer));
		}
		if(longitude[0] != 0) {
			snprintf(buf, sizeof(buf), "\nlongitude: %s", longitude);
			strncat(time_buffer, buf, sizeof(time_buffer));
		}
		if(location[0] != 0) {
			snprintf(buf, sizeof(buf), "\nlocation: %s", location);
			strncat(time_buffer, buf, sizeof(time_buffer));
		}
		if(weather[0] != 0) {
			snprintf(buf, sizeof(buf), "\nweather: %s", weather);
			strncat(time_buffer, buf, sizeof(time_buffer));
		}
	
		if(temperature != -999999) {
			snprintf(buf, sizeof(buf), "\ntemp: %d\u00B0%c (%d/%d)", TEMP(temperature), metric_units ? 'C' : 'F', TEMP(temperature_min), TEMP(temperature_max));
			strncat(time_buffer, buf, sizeof(time_buffer));
		}
		
		char unit = 's';
		if(since > 3600) {
			since /= 3600;
			unit = 'h';
		} else if(since > 60) {
			since /= 60;
			unit = 'm';
		}
		snprintf(buf, sizeof(buf), "\nupdated: %ld%c ago", since, unit);
		if(unit == 's') snprintf(buf, sizeof(buf), "\nupdated: <1m ago");
		strncat(time_buffer, buf, sizeof(time_buffer));
	}
	
	p = time_buffer;
	if(p[0] == ' ') p++;
	text_layer_set_text(text_layer, p);

	//strftime(date_buffer, sizeof(date_buffer), "%b %e", tick_time);
	//text_layer_set_text(date_layer, date_buffer);
}

#define TIME_HEIGHT 36

void handle_init(void) {
	// Create a window and text layer
	window = window_create();
	text_layer = text_layer_create(GRect(0, TIME_HEIGHT, 144, 168 - TIME_HEIGHT));

	show_seconds = persist_read_bool(KEY_SHOW_SECONDS);
	invert_face = persist_read_bool(KEY_INVERT);
	metric_units = persist_read_bool(KEY_UNITS);
	
	latitude[0] = 0;
	longitude[0] = 0;
	location[0] = 0;
	weather[0] = 0;
	// Set the text, font, and text alignment
	//text_layer_set_text(text_layer, "");
	// http://www.openweathermap.org/current
	// https://developer.getpebble.com/blog/2013/07/24/Using-Pebble-System-Fonts/
	// https://github.com/JvetS/PebbleSimulator/blob/master/PebbleSimulator/pebble_fonts.h
	text_layer_set_font(text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	// https://developer.getpebble.com/2/api-reference/group___graphics_types.html
	text_layer_set_text_alignment(text_layer, GTextAlignmentLeft);

	// Add the text layer to the window
	layer_add_child(window_get_root_layer(window), text_layer_get_layer(text_layer));

//	date_layer = text_layer_create(GRect(0, 112, 144, 56));
//	text_layer_set_text_alignment(date_layer, GTextAlignmentCenter);
//	text_layer_set_font(date_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
//	layer_add_child(window_get_root_layer(window), text_layer_get_layer(date_layer));
	time_layer = text_layer_create(GRect(0, 0, 144, TIME_HEIGHT));
	text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
	text_layer_set_font(time_layer, fonts_get_system_font(FONT_PRO_32));
	layer_add_child(window_get_root_layer(window), text_layer_get_layer(time_layer));

	inverter_layer = inverter_layer_create(GRect(0, 0, 144, 168));
	layer_add_child(window_get_root_layer(window), inverter_layer_get_layer(inverter_layer));
	layer_set_hidden(inverter_layer_get_layer(inverter_layer), invert_face);
	
	battery_state_service_subscribe(handle_battery_change);
	handle_battery_change(battery_state_service_peek());

	bluetooth_connection_service_subscribe(handle_bluetooth_change);
	handle_bluetooth_change(bluetooth_connection_service_peek());
	
	app_message_register_inbox_received(in_received_handler);
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

	time_t current_time = time(0);
	handle_timechanges(localtime(&current_time), SECOND_UNIT);
	
	tick_timer_service_subscribe(SECOND_UNIT, handle_timechanges);
	
	// Push the window
	window_stack_push(window, true);

	// App Logging!
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "Hello World from the applogs!");
}

void handle_deinit(void) {
	text_layer_destroy(text_layer);
	text_layer_destroy(time_layer);
	//text_layer_destroy(date_layer);
	inverter_layer_destroy(inverter_layer);
	
	battery_state_service_unsubscribe();
	bluetooth_connection_service_unsubscribe();
	tick_timer_service_unsubscribe();
	
	window_destroy(window);
}

int main(void) {
	handle_init();
	app_event_loop();
	handle_deinit();
}