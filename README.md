# CO2 Monitor
![](images/img_1.jpeg)

### Description
An ESP-32 device that monitors the CO2 level in the room and sends the information to a database in the cloud. A website will display the data.

### Impact
[Studies](https://www.sciencedirect.com/science/article/pii/S036013232300358X) have shown that short-term exposure to high levels of CO2 can reduce cognitive performance and negatively impact learning. Therefore, it is important to be ensure that CO2 levels remain low in an academic setting.

### Components
MH-Z19C CO2 Sensor ([more info](https://www.winsen-sensor.com/d/files/infrared-gas-sensor/mh-z19c-pins-type-co2-manual-ver1_0.pdf)) \
[by EC Buying](https://www.amazon.com/EC-Buying-Monitoring-Concentration-Detection/dp/B0CRKH5XVX)

ESP32 Cheap Yellow Display ([more info](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display))\
[by Hosyond](https://www.amazon.com/gp/product/B0D92C9MMH)

3.7V LiPo Battery\
[by MakerHawk](https://www.amazon.com/gp/aw/d/B0D7MC714N)

### Features
* Screen automatically sleeps after 5 minutes of inactivity
* Zero the CO2 sensor to 400 ppm by depressing the button for 7 seconds (only do this when outdoors)

### Dependencies
* Django for web app, API endpoint, and PostgreSQL database
* LVGL for ESP-32 GUI
* TFT_eSPI
* XPT2046_Touchscreen
ESP32 CYD GUI configuration files can be found in [this guide](https://randomnerdtutorials.com/lvgl-cheap-yellow-display-esp32-2432s028r)

### Data Collection and Upload

Samples will be taken every 1 second. The timestamp will be updated after each data POST request to keep the timestamps accurate over long periods of time. POST requests will be made every 5 minutes to reduce power usage and live data is already displayed on the CYD. A PostgreSQL database hosted in the cloud will be used to store the data.

#### `POST /api/log/`

Logs one or more CO2 readings to the database.

**Authentication:** Bearer token via `Authorization` header (set `CO2_API_TOKEN` in `.env`)

**Request body:** A single JSON object or an array of objects:
```json
[
  {"mode": "ambient", "building": "COE", "room_number": 306, "unix_timestamp": 1678886400, "CO2_ppm": 750},
  {"mode": "ambient", "building": "COE", "room_number": 306, "unix_timestamp": 1678886401, "CO2_ppm": 760}
]
```

| Field | Type | Max Length | Description |
|-------|------|------------|-------------|
| `mode` | string | 50 chars | `"ambient"` or `"session"` |
| `building` | string | 30 chars | Building identifier (e.g. `"COE"`) |
| `room_number` | string | 10 chars | Room number (e.g. "301A")|
| `unix_timestamp` | int |  | Unix timestamp of the reading |
| `CO2_ppm` | int |  | CO2 concentration in parts per million |

**Responses:**

| Status | Meaning |
|--------|---------|
| `201` | Success — returns `{"success": true, "records_saved": N}` |
| `400` | Malformed JSON or missing field |
| `401` | Missing or invalid token |

### Pinout
| Pin | Use |
|-----|-----|
| IO35 | PWM for CO2 Sensor |
| 5V (UART PORT) | Vin for CO2 Sensor |
| GND | GND for CO2 Sensor |
| GND | GND for button -> CO2 Sensor HD Pin |

### CAD
Modified [ghfisanotti's CYD Case on Thingiverse](https://www.thingiverse.com/thing:7047135), licensed under CC BY-SA 3.0. See CAD folder in this repo for the STL and STEP files. 

### Required Parts and Assembly
* 1x ESP-32 CYD
* 1x MH-Z19C CO2 Sensor
* 2x 1.25mm 4-pin JST to Dupont connectors
* 1x CYD_LID (3D printed)
* 1x CYD_BASE (3D printed)
* 4x M3 heat-set inserts
* 4x M3x8 bolts
* 1x [push button](https://www.amazon.com/Waterproof-Momentary-Button-Switch-Colors/dp/B07F24Y1TB) *optional, for zeroing the sensor
* 1x 5-pin female pin header *optional, for securing the sensor to the base

The 5-pin female pin header is secured by melting the plastic around it with a soldering iron. On the female pin header that the MH-Z19C's HD pin will plug into, solder a wire (this will connect to one pin of the button). Connect the other pin of the button to a GND pin on the CYD.

###  How to run
Create a virtual environment
```bash
python -m venv venv
```

Start virtual environment
```bash
source venv/bin/activate
```

Install dependencies
```bash
pip install -r requirements.txt
```

Install PostgreSQL
```bash
sudo apt install postgresql postgresql-contrib
```

Run a Docker Container for PostgreSQL if you only want local data storage
```bash
sudo apt install docker.io
```

```bash
docker run -d \
  --name co2_postgres \
  -e POSTGRES_DB=co2db \
  -e POSTGRES_USER=co2user \
  -e POSTGRES_PASSWORD=yourpassword \
  -p 5432:5432 \
  postgres:15
```

Set environment variables in the file Django/CO2_Dashboard/.env, see the .env.example file in that directory for the required variables. Also set the environment variables in testing/.env, see the .env.example file in that folder for the required variables.

```bash
cd Django
```
create the database tables
```bash
python manage.py makemigrations
```

```bash
python manage.py migrate
```

Run locally using:
```bash
python manage.py runserver 0.0.0.0:8000
```

### Unit Testing
Run the external API unit tests
```bash
cd testing
```
```bash
pytest testing_api.py 
```

Run the Django internal unit tests
```bash
cd Django/
```
```bash
python manage.py test api -v 2
```

### Miscellaneous Info
Create User
```bash
python manage.py shell
```
```bash
from django.contrib.auth.models import User
```

```bash
User.objects.create_user(username='myuser', password='mypassword')
```

Delete User
```bash
User.objects.get(username='myuser').delete()
```

#### Testing Database Entry Labels
* building of 'debug', room 0 for esp32 test insertions
* building of 'debug', room 1 for unit testing