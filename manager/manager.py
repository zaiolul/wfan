from nicegui import ui
import paho.mqtt.client as mqtt
import threading
import time
import consts
from data import *
from queue import Queue, Empty
import json
import sys
import io
import os
import matplotlib.pyplot as plt
import numpy as np

message_queue = Queue()
input_queue = Queue()
# Initialize plot

# plt.ion()
# fig, ax = plt.subplots()
# line, = ax.plot([], [], 'r-')
# ax.set_xlim(0, 100)
# ax.set_ylim(0, 200)
# xdata, ydata = [], []
# window_size = 100  # Define the window size
# x = 0
# def extend_data(new_data):

#     global xdata, ydata, x
#     if len(xdata) > window_size:
#         x -= 1
#         ydata = ydata[1:]
#     else:
#         x += 1
#         xdata.append(x)
   
#     ydata.append(new_data)
#     print(ydata)
#     # Keep only the most recent data within the window size


# def update_plot():
#     global xdata, ydata
#     line.set_xdata(xdata)
#     line.set_ydata(ydata)
#     ax.relim()
#     ax.autoscale_view()
#     plt.draw()
#     plt.pause(0.01)


def select_ap(ap_list : list[WifiAp]) -> WifiAp:
    print(f"Select AP\n{"No":2} {"SSID":32} (BSSID)")
    for i, ap in enumerate(ap_list):
        print(f"{i:2}: {ap.ssid if len(ap.ssid) > 0 else "<empty>":32} ({ap.bssid})")
    
    try:
        select = input_queue.get(timeout=10)
    except Empty:
        return None

    if select < 0 or select >= len(ap_list):
        print("Invalid selection")
        return None
    return ap_list[select]
   
def update_scanner_stats(client: ScannerClient, data):
    if not client.stats.done:
        for item in data:
            radio_obj = item["radio"]
            # ap_obj = item["ap"]
            radio = RadioInfo(radio_obj["channel_freq"], radio_obj["antenna_signal"], radio_obj["noise"])
            # ap = WifiAp(["ssid"], item["bssid"], item["channel"])
            client.stats.signal_buf.append(radio.signal)

        if len(client.stats.signal_buf) == consts.PKT_STATS_BUF_SIZE:
            client.stats.done = True
        else:
            return
    for item in data:
        radio_obj = item["radio"]
        radio = RadioInfo(radio_obj["channel_freq"], radio_obj["antenna_signal"], radio_obj["noise"])
        client.stats.signal_buf.append(radio.signal)
        avg = sum(client.stats.signal_buf) / len(client.stats.signal_buf)
        client.stats.average = avg
        var = (radio.signal - avg) ** 2
        client.stats.variance_buf.append(var)
        # extend_data(sum(client.stats.variance_buf))
        with open (client.outfile, "a") as f:
            f.write(f"{radio.signal};{avg};{sum(client.stats.variance_buf)}\n")
    # update_plot()
    
def handle_data(client : mqtt.Client, manager : Manager, id : str, payload : str):
    json_data = json.loads(payload)

    data = json_data["data"]

    match PayloadType(json_data["type"]):
        case PayloadType.AP_LIST:
            manager.ap_counters.clear()
            
            for item in data:
                ap = WifiAp(item["ssid"], item["bssid"], item["channel"])
                manager.clients[id].ap_list.append(ap)
                if ap not in manager.ap_counters:
                    manager.ap_counters[ap] = 1
                else:
                    manager.ap_counters[ap] += 1

            manager.clients[id].finished_scan = True

            if all(c.finished_scan for c in manager.clients.values()):
                print("All clients finished scanning")
                manager.state = State.SELECTING
                
        case PayloadType.PKT_LIST:
            print(f"Received packet list from {id} {payload}")
            update_scanner_stats(manager.clients[id], data)


def handle_cmd(client : mqtt.Client, manager : Manager, cmd : str, id: str):
    print(f"Handling command {cmd} for {id}")
    
    if len(id) == 0:
        print("Invalid id")
        return
    
    topic_regack = f"{consts.TOPIC_CMD_BASE}/{id}/{consts.SCANNER_REG_ACK}"

    match cmd:
        case consts.CMD_REGISTER:
            print(f"Registering {id}")
            if id in manager.clients:
                scanner = manager.clients[id]
                if scanner.crash_timer is not None:
                    scanner.crash_timer.cancel()
                    scanner.crash_timer = None
                    client.publish(topic_regack, None, 1)
                else:
                    manager.clients.pop(id)
                return
            
            manager.clients[id] = ScannerClient(id)
            manager.clients[id].stats.signal_buf = deque(maxlen=consts.PKT_STATS_BUF_SIZE)
            manager.clients[id].stats.variance_buf = deque(maxlen=5)

            t = time.gmtime(time.time())
            manager.clients[id].outfile = \
                f"{consts.OUTPUT_DIR}/{id}_{t.tm_year}{t.tm_mon}{t.tm_mday}{t.tm_hour}{t.tm_min}.csv"
            client.publish(topic_regack, None, 1)

        case consts.CMD_CRASH:
            print(f"Crashing {id}")
            if id not in manager.clients:
                return
            scanner = manager.clients[id]
            scanner.crash_timer = threading.Timer(consts.SCAN_CRASH_WAIT, lambda: manager.clients.pop(id))
            scanner.crash_timer.start()

def parse_mqtt_message(client : mqtt.Client, manager : Manager, topic : str, payload : str):
    topic_parts = topic.split("/")
    if mqtt.topic_matches_sub(consts.MANAGER_SUB_DATA, topic):
        handle_data(client, manager, topic_parts[1], payload)
    elif mqtt.topic_matches_sub(consts.MANAGER_SUB_CMD_ID, topic):
        handle_cmd(client, manager, topic_parts[1], topic_parts[2])

def on_connect(client : mqtt.Client, userdata : Manager, flags : any, rc: int, properties : any = None):
    print("Connected with result code " + str(rc))
    client.subscribe([(consts.MANAGER_SUB_DATA, 1),
                      (consts.MANAGER_SUB_CMD_REGISTER, 1),
                      (consts.MANAGER_SUB_CMD_STOP, 1),
                      (consts.MANAGER_SUB_CMD_CRASH, 1)])

def on_message(client : mqtt.Client, userdata : Manager, msg : mqtt.MQTTMessage):
    payload = msg.payload.decode()
    message_queue.put((msg.topic, payload))

def setup_mqtt_client() -> mqtt.Client:
    manager = Manager()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.will_set("cmd/all/stop", "", 0, False)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect("localhost", 1883, 60)
    return client

def on_idle(client : mqtt.Client, manager : Manager):
    print("Idle state")
    # Implement idle state logic here

    if len(manager.clients.keys()) == 0:
        return
    
    try:
        selection = input_queue.get()
    except Empty:
        return
    
    match selection:
        case 0:
            return
        case 1:
            print("Scanning...")
            client.publish(consts.MANAGER_PUB_CMD_SCAN, "", 1)
            manager.state = State.SCANNING

def on_select_ap(client : mqtt.Client, manager : Manager):
    
    common_aps = [bssid for bssid, count in manager.ap_counters.items() if count == len(manager.clients)]
    if len(common_aps) == 0:
        print("No common APs found")
        return
    print(common_aps)
    ap = select_ap(common_aps)
    if ap is None:
        print("No AP selected")
        return
    
    manager.selected_ap = ap

    ap_obj = {
        "ssid" : ap.ssid,
        "bssid": ap.bssid,
        "channel": ap.channel,
    }

    client.publish(consts.MANAGER_PUB_CMD_SELECT_AP, json.dumps(ap_obj), 1)
    manager.state = State.SCANNING

def get_int_input():
    while True:
        try:
            val = int(input("> "))
            input_queue.put(val)
        except ValueError:
            print("Invalid input, try again")

def main():
    manager = Manager()
    client = setup_mqtt_client()
    input_thread = threading.Thread(target=get_int_input)
    input_thread.start()
    client.loop_start()
    
    os.makedirs(consts.OUTPUT_DIR, exist_ok=True)
    while True:
        try:
            topic, payload = message_queue.get(timeout=1)
            parse_mqtt_message(client, manager, topic, payload)

            match manager.state:
                case State.IDLE:
                    on_idle(client, manager)
                case State.SCANNING:
                    pass
                case State.SELECTING:
                    on_select_ap(client, manager)

        except Empty:
            pass
        except KeyboardInterrupt:
            break
    message_queue.join()
    input_queue.join()

    input_thread.join()
    client.loop_stop()
    # Start the NiceGUI app
    # ui.run()

if __name__ == "__main__":
    main()

# GUI Elements



# Start NiceGUI
