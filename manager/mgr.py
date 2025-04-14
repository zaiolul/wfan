#!/usr/bin/env python3
import paho.mqtt.client as mqtt
import threading
import consts
from data import *
import json
import os
import numpy as np
import asyncio
import datetime

class MqttClient:
    def __init__(self):
        self.queue = asyncio.Queue()
        self.mqtt_client = self._setup_mqtt_client()
        self._event_loop = asyncio.get_event_loop()
        self.mqtt_client.loop_start()

    def _on_connect(self, client : mqtt.Client, userdata, flags : any, rc: int, properties : any = None):
        print("Connected with result code " + str(rc))
        client.subscribe([(consts.MANAGER_SUB_DATA, 1),
                        (consts.MANAGER_SUB_CMD_REGISTER, 1),
                        (consts.MANAGER_SUB_CMD_STOP, 1),
                        (consts.MANAGER_SUB_CMD_CRASH, 1)])

    def _on_message(self, client : mqtt.Client, userdata, msg : mqtt.MQTTMessage):
        payload = msg.payload.decode()
        asyncio.run_coroutine_threadsafe(
            self.queue.put(item=(msg.topic, msg.payload)), self._event_loop)

    def _setup_mqtt_client(self) -> mqtt.Client:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        client.will_set("cmd/all/stop", "", 0, False)
        client.on_connect = self._on_connect
        client.on_message = self._on_message
        
        client.connect("localhost", 1883, 60)
        return client

class Manager:
    def __init__(self, client : MqttClient):
        self.client = client
        self.queue = self.client.queue 
        self.scanners : dict[str, ScannerClient] = dict()
        self.can_scan = False #BINDS FROM UI
        self.ap_counters : dict[WifiAp, int] = dict()
        self.common_aps : list[WifiAp] = list()
        self.selected_ap : WifiAp = None
        self.state = State.IDLE
        self.listeners = dict[ManagerEvent, list[callable]]()

        os.makedirs(consts.OUTPUT_DIR, exist_ok=True)

    #ok, so I cant really do iqr method, cause most of the time iqr = 0 in stable environment
    #need a better way than just comparing it like this
    def is_outlier(self, scanner: ScannerClient, entry : int) -> bool: 
        return entry < 1.75 * scanner.stats.average

    async def update_scanner_stats(self, client: ScannerClient, data):
        if not client.stats.done:
            for item in data:
                radio_obj = item["radio"]
                ap_obj = item["ap"]
                radio = RadioInfo(radio_obj["channel_freq"], radio_obj["antenna_signal"], radio_obj["noise"])
                print(ap_obj)
                client.stats.signal_buf.append(radio.signal)
                client.stats.ts_buf.append(ap_obj["timestamp"]) #dont need the object here tbh, might change for radio case as well

            if len(client.stats.signal_buf) == consts.PKT_STATS_BUF_SIZE:
                client.stats.done = True
                client.stats.average = np.average(client.stats.signal_buf)
                client.stats.variance = np.var(client.stats.signal_buf)
            else:
                return
            
        for item in data:
            radio_obj = item["radio"]
            radio = RadioInfo(radio_obj["channel_freq"], radio_obj["antenna_signal"], radio_obj["noise"])
            ap_obj = item["ap"]

            val = radio.signal
            if self.is_outlier(client, radio.signal):
                val = client.stats.average 

            client.stats.signal_buf.append(val)
            client.stats.ts_buf.append(datetime.datetime.fromtimestamp(int(ap_obj["timestamp"]) / 1000))
            avg = np.average(client.stats.signal_buf)
            client.stats.average = avg

            var = (val - avg) ** 2
            client.stats.variance_tmp_buf.append(var)
            var_sum = sum(client.stats.variance_tmp_buf)

            client.stats.variance_buf.append(var_sum)

        client.stats.variance = np.var(client.stats.signal_buf)
        
    async def handle_data(self, id : str, payload : str):
        json_data = json.loads(payload)

        data = json_data["data"]

        match PayloadType(json_data["type"]):
            case PayloadType.AP_LIST:                
                for item in data:
                    ap = WifiAp(item["ssid"], item["bssid"], item["channel"])
                    self.scanners[id].ap_list.append(ap)
                    if ap not in self.ap_counters:
                        self.ap_counters[ap] = 1
                    else:
                        self.ap_counters[ap] += 1
                self.scanners[id].finished_scan = True

                if all(c.finished_scan for c in self.scanners.values()):
                    print("All scanners finished scanning")
                    self.common_aps = [bssid for bssid, count in self.ap_counters.items() if count == len(self.scanners)]
                    print(self.common_aps)
                    self.state = State.SELECTING
                    self.call_listeners(ManagerEvent.AP_SELECT)

            case PayloadType.PKT_LIST:
                self.scanners[id].scanning = True
                await self.update_scanner_stats(self.scanners[id], data)
                if self.scanners[id].stats.done:
                    self.call_listeners(ManagerEvent.PKT_DATA_RECV, id)

    def handle_client_crash(self, id: str):
        self.scanners.pop(id)
        self.call_listeners(ManagerEvent.CLIENT_UNREGISTER)
        
    def handle_cmd(self, cmd : str, id: str):
        print(f"Handling command {cmd} for {id}")
        
        if len(id) == 0:
            print("Invalid id")
            return
        
        topic_regack = f"{consts.TOPIC_CMD_BASE}/{id}/{consts.SCANNER_REG_ACK}"
        
        match cmd:
            case consts.CMD_REGISTER:
                print(f"Registering {id}")
                if id in self.scanners:
                    scanner = self.scanners[id]
                    if scanner.crash_timer is not None:
                        scanner.crash_timer.cancel()
                        scanner.crash_timer = None
                        self.client.mqtt_client.publish(topic_regack, None, 1)
                    else:
                        self.scanners[id].scanning = False
                        self.scanners.pop(id)
                        if len(self.scanners.keys()) == 0:
                            self.can_scan = False

                    self.call_listeners(ManagerEvent.CLIENT_UNREGISTER)
                    return
                
                self.scanners[id] = ScannerClient(id)
                self.scanners[id].stats.signal_buf = deque(maxlen=consts.PKT_STATS_BUF_SIZE)
                self.scanners[id].stats.variance_buf = deque(maxlen=consts.PKT_STATS_BUF_SIZE)
                self.scanners[id].stats.variance_tmp_buf = deque(maxlen=5)
                self.scanners[id].stats.ts_buf = deque(maxlen=consts.PKT_STATS_BUF_SIZE)

                self.client.mqtt_client.publish(topic_regack, None, 1)
                self.can_scan = True
                self.call_listeners(ManagerEvent.CLIENT_REGISTER)

            case consts.CMD_CRASH:
                print(f"Crashing {id}")
                if id not in self.scanners:
                    return
                scanner = self.scanners[id]
                self.scanners[id].scanning = False
                #TODO fix, asyncio task?
                # scanner.crash_timer = threading.Timer(consts.SCAN_CRASH_WAIT, lambda: self.handle_client_crash(id))
                # scanner.crash_timer.start()

    async def message_handler(self, topic: str, payload: str):
        topic_parts = topic.split("/")
        if mqtt.topic_matches_sub(consts.MANAGER_SUB_DATA, topic):
            await self.handle_data(topic_parts[1], payload)
        elif mqtt.topic_matches_sub(consts.MANAGER_SUB_CMD_ID, topic):
            self.handle_cmd(topic_parts[1], topic_parts[2])

    async def receive_next(self):
        try:
            topic, payload = await self.queue.get()
            await self.message_handler(topic, payload)
        except asyncio.QueueEmpty:
            return
        
    async def _common_aps_done(self):
        while True:
            if self.state == State.SELECTING:
                return True
            await asyncio.sleep(0.5)

    async def common_aps_done(self, timeout: int = 10):
        try:
            await asyncio.wait_for(self._common_aps_done(), timeout=timeout)
            return True
        except asyncio.TimeoutError:
            return False

    async def reset_scan_state(self):
      
        self.state = State.IDLE

    def register_listener(self, event: ManagerEvent, callback: callable):
        if event not in self.listeners:
            self.listeners[event] = []
        self.listeners[event].append(callback)
    
    def call_listeners(self, event: ManagerEvent, args = None):
        if event not in self.listeners:
            return
        for callback in self.listeners[event]:
            asyncio.get_event_loop().call_soon_threadsafe(callback, args)

    async def mqtt_send(self, topic: str, payload: str = None, qos : int = 1):
        #clean up for upcoming states
        self.common_aps.clear()
        self.ap_counters.clear()

        self.client.mqtt_client.publish(topic=topic, payload=payload, qos=qos, retain=False)

class Scanners:
    def __init__(self):
        self.scanners : dict[str, ScannerClient] = dict()
        self.lock = asyncio.Lock()
        self.queue = asyncio.Queue(maxsize=consts.MAX_CLIENTS)
        self.cb = list()

    async def register(self, id : str):
        async with self.lock:
            if id in self.scanners:
                return
            self.scanners[id] = ScannerClient(id)
            self.on_change()
    
    async def unregister(self, s : ScannerClient):
        async with self.lock:
            if s not in self.scanners:
                return
            self.scanners.remove(s)
            self.on_change()

    def on_change(self):
        for cb in self.cb:
            cb(self.scanners)
