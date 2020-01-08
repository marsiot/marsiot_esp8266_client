
#include <ESP8266WiFi.h>
#include <Ticker.h>

/*
 * 需要安装PubSubClient库用来支持MQTT，tools->Manage->Libraries search PubSubClient
*/
#include <PubSubClient.h>

/*
 * 【以下改动非常重要】
 * PubSubClient库下载之后，找到PubSubClient.h
 * 将MQTT_MAX_PACKET_SIZE值修改为2500
 * #define MQTT_MAX_PACKET_SIZE 2500
*/

/*
 * 需要安装ArduinoJson库用来支持JSON，tools->Manage->Libraries search ArduinoJson
*/
#include <ArduinoJson.h>

/*
 * 附近要连接的WIFI路由器的名称和密码
*/
const char *my_ssid = "ittim_2g";
const char *my_password = "";

/*
 * 【获取你自己的SITE TOKEN】
 * 访问www.marsiot.com,使用微信登陆开发平台
 * 在开发平台首页显示你自己SITE TOKEN信息,例如
 * Site Token: 6c1a99b5-dd3b-4d55-bb88-xxxxxxxxxx
*/
const char *marsiot_sitetoken = "6c1a99b5-dd3b-4d55-bb88-08dd0aedee2d";

/*
 * 模块名称和描述用户可以自定义
*/
const char *model_name = "esp8266";
const char *model_description = "test";

unsigned char my_mac[6];
char my_hardware_id[20];

Ticker my_timer;
int my_timer_count = 0;

WiFiClient espClient;
PubSubClient client(espClient);


void setup() {
  Serial.begin(115200);

  wifi_set_event_handler_cb(wifi_event_handler_cb);

  WiFi.mode(WIFI_STA);
  WiFi.begin(my_ssid, my_password);

  Serial.printf("\r\n");
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.printf(".");
  }
  Serial.printf("\r\n");

  Serial.printf("\r\nwifi connected !\r\nip: ");
  Serial.println(WiFi.localIP());

  //MQTT的服务器地址和端口号
  client.setServer("www.marsiot.com", 1883);
  //MQTT订阅事件处理
  client.setCallback(mqtt_callback);

  while (!client.connected()) {
    Serial.printf("\r\nconnecting to www.marsiot.com ...");

    if (client.connect("ESP8266Client", "", "" )) {
      Serial.println("\r\nmqtt connected !");
    } else {
      Serial.print("\r\nfailed with state ");
      Serial.println(client.state());
      delay(2000);
    }
  }

  //向MARSIOT平台注册设备
  marsiot_do_register();
}

void loop() {
  client.loop();
}

void wifi_event_handler_cb(System_Event_t * event) {
  switch (event->event) {
    case EVENT_STAMODE_CONNECTED:
      //获取MAC地址，后续作为注册时的HARDWARE ID
      memcpy(&my_mac[0], event->event_info.sta_connected.mac, 6);
      break;
    case EVENT_STAMODE_DISCONNECTED:
      break;
    case EVENT_STAMODE_AUTHMODE_CHANGE:
      break;
    case EVENT_STAMODE_GOT_IP:
      break;
    case EVENT_SOFTAPMODE_STACONNECTED:
    case EVENT_SOFTAPMODE_STADISCONNECTED:
      break;
  }
}

void marsiot_do_register() {
#define JSON_BUFF_BUF_MAX 350
  char json_buff[350];

  memset(&my_hardware_id[0], 0, sizeof(my_hardware_id));
  snprintf(my_hardware_id, 20, "%02x%02x%02x%02x%02x%02x", my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);

  snprintf(json_buff, JSON_BUFF_BUF_MAX, "marsiot/commands/%s", my_hardware_id);
  client.subscribe(json_buff);

  snprintf(json_buff, JSON_BUFF_BUF_MAX, "marsiot/system/%s", my_hardware_id);
  client.subscribe(json_buff);

  snprintf(json_buff, JSON_BUFF_BUF_MAX,
           "{\"request\": {\"siteToken\": \"%s\", \"specificationToken\": \"05cad420-e359-4685-b5ca-721d1d8b473c\", \"hardwareId\": \"%s\", \"metadata\": {\"model-name\": \"%s\", \"model-description\": \"%s\"}}, \"type\": \"RegisterDevice\", \"hardwareId\": \"%s\"}",
           marsiot_sitetoken, my_hardware_id, model_name, model_description, my_hardware_id);
  client.publish("marsiot/input/json", json_buff) ;
}

void marsiot_send_message(String type, String message) {
  StaticJsonDocument<500> doc;

  JsonObject request = doc.createNestedObject("request");
  request["message"] = message.c_str();
  request["type"] = type.c_str();
  doc["type"] = "DeviceAlert";
  doc["hardwareId"] = my_hardware_id;

  String json_str;
  serializeJson(doc, json_str);

  client.publish("marsiot/input/json", json_str.c_str()) ;
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  DynamicJsonDocument doc(2500);
  String my_topic = topic;

  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  //here help to use ArduinoJson
  //https://arduinojson.org/v6/assistant/

  if (my_topic.startsWith("marsiot/system/")) {
    marsiot_do_system(doc["systemCommand"]);
  }

  if (my_topic.startsWith("marsiot/commands/")) {
    String cmd_name = doc["command"]["command"]["name"];
    JsonArray cmd_parameters = doc["command"]["command"]["parameters"];
    JsonObject cmd_parameterValues = doc["command"]["invocation"]["parameterValues"];
    marsiot_do_command(cmd_name, cmd_parameters, cmd_parameterValues);
  }
}

void marsiot_do_system(JsonObject systemJsonObj) {
  String reason = systemJsonObj["reason"];

  if (reason == "NewRegistration") {
    Serial.print("\r\nregister to www.marsiot.com success!");
    Serial.printf("\r\nhardware id: %s", my_hardware_id);
    Serial.printf("\r\nmodel name: %s", model_name);
    Serial.printf("\r\nmodel description: %s\r\n", model_description);
    marsiot_send_message("message", "regster ok");

    //注册成功后，每隔20秒，调用一次timer_do
    my_timer.attach(20, timer_do);
  }
}

void marsiot_do_command(String cmd_name, JsonArray cmd_parameters, JsonObject cmd_parameterValues) {
  Serial.print("\r\n" + cmd_name);

  Serial.print("(");
  for (int i = 0; i < cmd_parameters.size(); i++)  {
    JsonObject para = cmd_parameters[i];
    String name = para["name"];
    String value = cmd_parameterValues[name].as<char *>();
    Serial.print(name + ":" + value);
    if (i < (cmd_parameters.size() - 1)) {
      Serial.print(",");
    }
  }
  Serial.print(")\r\n");

  //用户可以在www.marsiot.com开发平台上，自定义命令
  //在这里参考helloWorld的处理，添加相应自定义命令的处理过程
  if (cmd_name.equals("helloWorld")) {
    String greeting = cmd_parameterValues["greeting"];
    bool loud = cmd_parameterValues["loud"] == "true" ? true : false;
    helloWorld(greeting, loud);
  }

  if (cmd_name.equals("setGpio")) {
    String gpio = cmd_parameterValues["gpio"];
    bool high = cmd_parameterValues["high"] == "true" ? true : false;
    setGpio(gpio, high);
  }
}

//定期被调用，在这里可以向平台发送消息，用于显示图表显示
void timer_do() {
  StaticJsonDocument<500> doc;

  //Y轴纵坐标温度和内存值，用户可自定义
  JsonArray y = doc.createNestedArray("y");
  JsonObject y_0 = y.createNestedObject();
  y_0["name"] = "温度(摄氏度)";
  y_0["value"] = "41.9";
  JsonObject y_1 = y.createNestedObject();
  y_1["name"] = "使用内存(M)";
  y_1["value"] = "948";

  //X轴横坐标显示当前计数
  doc["x"] = my_timer_count++;

  String json_str;
  serializeJson(doc, json_str);
  marsiot_send_message("mychart1", json_str);

  Serial.printf("\r\n%s\r\n", json_str.c_str());
}

void helloWorld(String greeting, bool loud) {
    //向平台发送一个消息
    marsiot_send_message("test", "i got the message");
}

void setGpio(String gpio, bool high) {
  //在这个完成设置GPIO等操作
  //digitalWrite(LED_BUILTIN, high?HIGH:LOW);
}
