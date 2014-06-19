var update_weather = function(position) {
	var req = new XMLHttpRequest();
	req.open('GET', 'http://api.openweathermap.org/data/2.5/weather?lat=' + position.coords.latitude + '&lon=' + position.coords.longitude + '&units=metric', true);
	req.onerror = function() {
		Pebble.sendAppMessage({
			"KEY_LATITUDE" : position.coords.latitude.toString(),
			"KEY_LONGITUDE" : position.coords.longitude.toString(),
			"KEY_TIMEZONE" : new Date().getTimezoneOffset() * 60,
		});	
	};
	req.onload = function(e) {
		if (req.readyState == 4 && req.status == 200) {
			var response = JSON.parse(req.responseText);
			Pebble.sendAppMessage({
				"KEY_WEATHER" : response.weather[0].main,
				"KEY_TEMPERATURE" : response.main.temp * 100,
				"KEY_TEMPERATURE_MIN" : response.main.temp_min * 100,
				"KEY_TEMPERATURE_MAX" : response.main.temp_max * 100,
				"KEY_LOCATION" : response.name,
				"KEY_UPDATE" : response.dt,
				"KEY_LATITUDE" : position.coords.latitude.toString(),
				"KEY_LONGITUDE" : position.coords.longitude.toString(),
				"KEY_TIMEZONE" : new Date().getTimezoneOffset() * 60,
			});
		} else {
			Pebble.sendAppMessage({
				"KEY_LATITUDE" : position.coords.latitude.toString(),
				"KEY_LONGITUDE" : position.coords.longitude.toString(),
				"KEY_TIMEZONE" : new Date().getTimezoneOffset() * 60,
			});			
		}
	};
	req.send(null);
};

var update = function() {
	navigator.geolocation.getCurrentPosition(update_weather, function() { Pebble.sendAppMessage({ "KEY_TIMEZONE" : new Date().getTimezoneOffset() * 60 }); });
};

Pebble.addEventListener("ready", function(e) {
	update();
});

Pebble.addEventListener("appmessage", function(e) {
	update();
});

Pebble.addEventListener("showConfiguration", function(e) {
	Pebble.openURL("https://dl.dropboxusercontent.com/u/167989/zenface-configuration.html");
});

Pebble.addEventListener("webviewclosed", function(e) {
	var configuration = JSON.parse(decodeURIComponent(e.response));
	Pebble.sendAppMessage({
		"KEY_SHOW_SECONDS": configuration.show_seconds,
		"KEY_INVERT": configuration.invert,
		"KEY_UNITS": configuration.units
	});
});
