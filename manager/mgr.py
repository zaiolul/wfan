#!/usr/bin/env python3
import paho.mqtt.client as mqtt
import threading
import consts
from data import *
import json
import os
import math
import asyncio
import datetime
import copy


class MqttClient:
    def __init__(self, config: str):
        self.queue = asyncio.Queue()
        self.mqtt_client = self._setup_mqtt_client(config)
        self._event_loop = asyncio.get_event_loop()
        self.mqtt_client.loop_start()

    def _on_connect(
        self, client: mqtt.Client, userdata, flags: any, rc: int, properties: any = None
    ):
        print("Connected with result code " + str(rc))
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

    def _setup_mqtt_client(self, config: str) -> mqtt.Client:
        if config == "":
            config = consts.MQTT_CONF
        conf = self._parse_mqtt_conf(config)
        if not conf:
            raise Exception("MQTT config file not found or invalid")

        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        client.will_set(consts.MANAGER_PUB_CMD_END, "", 0, False)
        client.on_connect = self._on_connect
        client.on_message = self._on_message
        client.username_pw_set(conf["USERNAME"], conf["PASSWORD"])
        host = conf["HOST"]
        client.connect(conf["HOST"], int(conf["PORT"]), 60)
        return client

    def _parse_mqtt_conf(self, path: str) -> dict[str, str]:
        f = None
        config = {"HOST": None, "USERNAME": None, "PORT": None, "PASSWORD": None}
        try:
            f = open(path, "r")
            for line in f:
                parts = line.split("=")
                if parts[0] not in config.keys():
                    print(f"Unknown field: {parts[0]}")
                    continue
                config[parts[0]] = parts[1].rstrip()
        except:
            return None

        f.close()
        return config


class Manager:
    def __init__(self, client: MqttClient):
        self.client: MqttClient = client
        self.queue: asyncio.Queue = self.client.queue
        self.scanners: dict[str, ScannerClient] = dict()
        self.ap_counters: dict[WifiAp, int] = dict()
        self.common_aps: list[WifiAp] = list()
        self.selected_ap_obj: dict = None
        self.state = ManagerState.IDLE
        self.listeners: dict[str, dict[ManagerEvent, list[callable]]] = dict()

        # these fetched by UI for display
        self.rssi_bufs: dict[str, deque] = dict()
        self.var_bufs: dict[str, deque] = dict()
        self.ts_bufs: dict[str, deque] = dict()
        self.can_scan = False  # BINDS FROM UI

        # os.makedirs(consts.OUTPUT_DIR, exist_ok=True)

    def update_scanner_result_path(self, id: str, path: str):
        if len(path) == 0:
            self.scanners[id].outfile = ""
        else:
            self.scanners[id].outfile = f"{path}/{id}_{datetime.date.today()}.csv"
        print(f"Updated path for {id}: {self.scanners[id].outfile}")

    def update_results_path(self, path: str):
        for id in self.scanners.keys():
            self.update_scanner_result_path(id, path)

    def update_scanner_display_stats(
        self, id: str, reset: bool = False, add: bool = True, count=-1
    ):
        if reset:
            self.rssi_bufs[id] = deque(maxlen=256_000)
            self.var_bufs[id] = deque(maxlen=256_000)
            self.ts_bufs[id] = deque(maxlen=256_000)

        if add:
            self.rssi_bufs[id].extend(
                self.scanners[id].stats.signal_buf
                if count == -1
                else list(self.scanners[id].stats.signal_buf)[-count:]
            )
            self.var_bufs[id].extend(
                self.scanners[id].stats.variance_disp_buf
                if count == -1
                else list(self.scanners[id].stats.variance_disp_buf)[-count:]
            )
            self.ts_bufs[id].extend(
                self.scanners[id].stats.ts_buf
                if count == -1
                else list(self.scanners[id].stats.ts_buf)[-count:]
            )

    def fetch_scanner_display_stats(self, id: str):
        return (
            list(self.rssi_bufs[id]),
            list(self.var_bufs[id]),
            list(self.ts_bufs[id]),
        )

    def _is_outlier(self, scanner: ScannerClient, entry: int) -> bool:
        # variance can be 0 if RSSI is very stable (or the window is atleast, so Z score calc fails, assume
        # there's always some variance in such case
        var = max(scanner.stats.variance, 1)
        z = (entry - scanner.stats.average) / math.sqrt(var)

        # hopefully filters out much higher values, found some random one time spikes of 2x weaker RSSi
        return abs(z) > 5

    def _update_scanner_stats(self, id: str, data):
        client = self.scanners[id]
        for item in data:
            radio_obj = item["radio"]
            radio = RadioInfo(
                radio_obj["channel_freq"],
                radio_obj["antenna_signal"],
                radio_obj["noise"],
            )
            ap_obj = item["ap"]

            val = radio.signal

            # so I saw massive spikes sometimes, better remove it as it's not caused by atennuation methinks
            if client.stats.done and self._is_outlier(client, radio.signal):
                val = int(client.stats.average)

            if client.stats.maximum < val or client.stats.maximum == 0:
                client.stats.maximum = val
            if client.stats.minimum > val:
                client.stats.minimum = val

            client.stats.signal_buf.append(val)
            client.stats.ts_buf.append(
                datetime.datetime.fromtimestamp(int(ap_obj["timestamp"]) / 1000)
            )

            avg = sum(client.stats.signal_buf) / len(client.stats.signal_buf)
            client.stats.average = avg

            # single point variance
            var = (val - avg) ** 2

            # "smooth" out using prev variance values
            client.stats.variance_calc_buf.append(var)
            client.stats.variance = sum(client.stats.variance_calc_buf) / len(
                client.stats.variance_calc_buf
            )

            # take last 5 variance values to "smooth" change over time
            var_sum = sum(list(client.stats.variance_calc_buf)[-5:])

            # just clamp value if its too extreme, don't really care about the value itself
            if var_sum > consts.Y_VAR_MAX:
                var_sum = consts.Y_VAR_MAX
            client.stats.variance_disp_buf.append(var_sum)

            if len(client.stats.signal_buf) == consts.PKT_STATS_BUF_SIZE:
                client.stats.done = True
        return len(data)

    async def _write_pkt_data(self, scanner: ScannerClient):
        f = None
        if not scanner.outfile:
            return

        exists = os.path.exists(scanner.outfile)
        f = open(scanner.outfile, "a")
        if not exists:
            print(f"{id} does not have a results file, create")
            f.write(
                f"{self.selected_ap_obj["ssid"]};{self.selected_ap_obj["bssid"]};{self.selected_ap_obj["channel"]}\n"
            )
        signals = list(scanner.stats.signal_buf)
        variances = list(scanner.stats.variance_disp_buf)
        timestamps = list(scanner.stats.ts_buf)

        for i in range(len(signals)):
            f.write(f"{timestamps[i]};{variances[i]:2};{signals[i]}\n")
        f.close()

    async def _handle_data(self, id: str, payload: str):
        json_data = json.loads(payload)

        data = json_data["data"]

        match PayloadType(json_data["type"]):
            case PayloadType.AP_LIST:
                for item in data:
                    ap = WifiAp(
                        item["ssid"] if item["ssid"] != "" else "<HIDDEN>",
                        item["bssid"],
                        item["channel"],
                    )
                    self.scanners[id].ap_list.append(ap)
                    if ap not in self.ap_counters:
                        self.ap_counters[ap] = 1
                    else:
                        self.ap_counters[ap] += 1
                self.scanners[id].finished_scan = True
                self.scanners[id].stats.done = False
                if all(c.finished_scan for c in self.scanners.values()):
                    print("All scanners finished scanning")
                    self.common_aps = [
                        bssid
                        for bssid, count in self.ap_counters.items()
                        if count == len(self.scanners)
                    ]
                    self.state = ManagerState.SELECTING
                    # self._call_listeners(ManagerEvent.AP_SELECT)

            case PayloadType.PKT_LIST:
                self.state = ManagerState.SCANNING
                self.scanners[id].state = ScannerState.SCANNER_SCANNING
                count = self._update_scanner_stats(id, data)
                self.update_scanner_display_stats(id, count=count)

                # if self.scanners[id].stats.done:
                # self._call_listeners(ManagerEvent.PKT_DATA_RECV, id)
                await self._write_pkt_data(copy.deepcopy(self.scanners[id]))

    def _handle_client_crash(self, id: str):
        self.scanners.pop(id)
        # self._call_listeners(ManagerEvent.CLIENT_UNREGISTER)

    def _handle_cmd(self, cmd: str, id: str):
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
                        scanner.state = ScannerState.SCANNER_IDLE
                        self.can_scan = True
                        self.client.mqtt_client.publish(topic_regack, None, 1)
                        if self.state == ManagerState.SCANNING:
                            self.client.mqtt_client.publish(
                                consts.MANAGER_PUB_CMD_SELECT_AP,
                                json.dumps(self.selected_ap_obj),
                            )
                    else:
                        print(f"Unregister scanner {id}")
                        self.scanners[id].scanning = False
                        self.scanners.pop(id)
                        if len(self.scanners.keys()) == 0:
                            self.state = ManagerState.IDLE
                            self.can_scan = False
                    # self._call_listeners(ManagerEvent.CLIENT_UNREGISTER)
                    return

                self.scanners[id] = ScannerClient(id)
                self.scanners[id].stats.signal_buf = deque(
                    maxlen=consts.PKT_STATS_BUF_SIZE
                )
                self.scanners[id].stats.variance_calc_buf = deque(
                    maxlen=consts.PKT_STATS_BUF_SIZE
                )
                self.scanners[id].stats.variance_disp_buf = deque(
                    maxlen=consts.PKT_STATS_BUF_SIZE
                )  # adjust for "smoothness"
                self.scanners[id].stats.ts_buf = deque(maxlen=consts.PKT_STATS_BUF_SIZE)

                self.client.mqtt_client.publish(topic_regack, None, 1)
                self.update_scanner_display_stats(id, reset=True, add=False)
                # self._call_listeners(ManagerEvent.CLIENT_REGISTER)

            case consts.CMD_CRASH:
                print(f"Crashing {id}")
                if id not in self.scanners:
                    return
                scanner = self.scanners[id]
                self.scanners[id].state = ScannerState.SCANNER_CRASHED
                scanner.crash_timer = asyncio.create_task(
                    self._handle_scanner_crash(id)
                )
                if all(
                    s.state == ScannerState.SCANNER_CRASHED
                    for s in self.scanners.values()
                ):
                    self.can_scan = False
                # self._call_listeners(ManagerEvent.CLIENT_REGISTER)
            case consts.CMD_READY:
                print(f"{id} READY")
                self.can_scan = True

    async def _handle_scanner_crash(self, id: str):
        await asyncio.sleep(consts.SCAN_CRASH_WAIT)
        print(f"Client {id} never recovered, clean")
        self.scanners.pop(id)
        if len(self.scanners) == 0:
            self.state = ManagerState.IDLE
        # self._call_listeners(ManagerEvent.CLIENT_UNREGISTER)

    async def _message_handler(self, topic: str, payload: str):
        topic_parts = topic.split("/")
        if mqtt.topic_matches_sub(consts.MANAGER_SUB_DATA, topic):
            await self._handle_data(topic_parts[1], payload)
        elif mqtt.topic_matches_sub(consts.MANAGER_SUB_CMD_ID, topic):
            self._handle_cmd(topic_parts[1], topic_parts[2])

    async def receive_next(self):
        try:
            topic, payload = await self.queue.get()
            await self._message_handler(topic, payload)
        except asyncio.QueueEmpty:
            return

    async def _common_aps_done(self):
        while True:
            if self.state == ManagerState.SELECTING:
                return True
            await asyncio.sleep(0.5)

    async def common_aps_done(self, timeout: int = 15):
        try:
            await asyncio.wait_for(self._common_aps_done(), timeout=timeout)
            return True
        except asyncio.TimeoutError:
            return False

    async def reset_scan_state(self):
        self.state = ManagerState.IDLE

    # def register_listener(self, id: str, event: ManagerEvent, callback: callable):
    #     if id not in self.listeners.keys():
    #         self.listeners[id] = dict()

    #     if event not in self.listeners[id].keys():
    #         self.listeners[id] = {event: [callback]}
    #     else:
    #         self.listeners[id][event].append(callback)

    # def _call_listeners(self, event: ManagerEvent, args=None):
    #     for id, cbs in self.listeners.items():
    #         if event not in cbs.keys():
    #             continue

    #         for callback in cbs[event]:
    #             callback(args)

    # def remove_listeners(self, id: str):
    #     if id in self.listeners.keys():
    #         self.listeners.pop(id)
    def do_capture_start(self):
        self.update_scanner_display_stats("", reset=True, add=False)
            
    def do_capture_stop(self):
        for id, scanner in self.scanners.items():
            scanner.state = ScannerState.SCANNER_IDLE
            scanner.finished_scan = False
            scanner.scanning = False
        self.state = ManagerState.IDLE

    async def mqtt_send(self, topic: str, payload: str = None, qos: int = 1):
        # clean up for upcoming states
        self.common_aps.clear()
        self.ap_counters.clear()
        for scanner in self.scanners.values():
            scanner.finished_scan = False
            scanner.state = ScannerState.SCANNER_IDLE
        self.client.mqtt_client.publish(
            topic=topic, payload=payload, qos=qos, retain=False
        )
