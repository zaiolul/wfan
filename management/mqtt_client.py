import paho.mqtt.client as mqtt
import asyncio
import consts


class MqttClient:
    def __init__(self):
        self.queue = asyncio.Queue()
        self.mqtt_client = self._setup_mqtt_client()
        self._event_loop = asyncio.get_event_loop()

    def _on_connect(
        self, client: mqtt.Client, userdata, flags: any, rc: int, properties: any = None
    ):
        print("Connected to MQTT broker with result code")
        client.subscribe(
            [
                (consts.MANAGER_SUB_DATA, 1),
                (consts.MANAGER_SUB_CMD_REGISTER, 1),
                (consts.MANAGER_SUB_CMD_STOP, 1),
                (consts.MANAGER_SUB_CMD_CRASH, 1),
                (consts.MANAGER_SUB_CMD_READY, 1),
            ]
        )

    def _on_message(self, client: mqtt.Client, userdata, msg: mqtt.MQTTMessage):
        payload = msg.payload.decode()
        asyncio.run_coroutine_threadsafe(
            self.queue.put(item=(msg.topic, msg.payload)), self._event_loop
        )

    def _on_disconnect(
        self, client: mqtt.Client, userdata, flags: any, rc: int, properties: any = None
    ):
        print("Disconnected from MQTT broker")

    def try_connect(self, host: str, port: int, username: str, password: str):
        try:
            if self.mqtt_client.is_connected():
                self.mqtt_client.disconnect()
                self.mqtt_client.loop_stop()

            self.mqtt_client.username_pw_set(username, password)
            self.mqtt_client.connect(host, port, 10)
            self.mqtt_client.loop_start()
        except:
            print("Failed to connect to MQTT broker")

    def disconnect(self):
        self.mqtt_client.publish(consts.MANAGER_PUB_CMD_END, None, 1)
        self.mqtt_client.disconnect()
        self.mqtt_client.loop_stop()

    def get_status(self):
        return self.mqtt_client.is_connected()

    def _setup_mqtt_client(self) -> mqtt.Client:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        client.will_set(consts.MANAGER_PUB_CMD_END, "", 0, False)
        client.on_connect = self._on_connect
        client.on_message = self._on_message
        client.on_disconnect = self._on_disconnect

        return client
