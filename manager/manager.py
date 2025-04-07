from nicegui import ui
import paho.mqtt.client as mqtt
import threading
import time
import consts
from data import *
from queue import Queue, Empty
import json

message_queue = Queue()

def handle_data(client : mqtt.Client, manager : Manager, id : str, payload : str):
    json_data = json.loads(payload)

    for item in json_data:
        print(type(json_data[item]))

    match PayloadType(json_data["type"]):
        case PayloadType.AP_LIST:
            print(f"Received AP list from {id}")

        case PayloadType.PKT_LIST:
            print(f"Received packet list from {id}")


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
    print("0: exit 1: scan")
    selection = int(input("> "))

    match selection:
        case 0:
            print("Exiting...")
            return False
        case 1:
            print("Scanning...")
            client.publish(consts.MANAGER_PUB_CMD_SCAN, "", 1)
            manager.state = State.SCANNING

def main():
    manager = Manager()
    client = setup_mqtt_client()
    client.loop_start()
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
                    pass

        except Empty:
            pass
        except KeyboardInterrupt:
            break
    message_queue.join()
    client.loop_stop()
    # Start the NiceGUI app
    # ui.run()

if __name__ == "__main__":
    main()

# GUI Elements



# Start NiceGUI
