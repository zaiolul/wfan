#!/usr/bin/env python3

from nicegui import ui, app, run, events
from collections import deque
from nicegui.events import ValueChangeEventArguments, GenericEventArguments
import asyncio
from data import WifiAp, State, ManagerEvent
import plotly.graph_objects as go
from mgr import Manager, MqttClient
import consts
import json

class ScannerDialog:
    def __init__(self, manager: Manager):
        self.dialog = ui.dialog().classes("w-full")
        self.manager = manager 
        self.dialog.on("hide", manager.reset_scan_state)
        self.manager.register_listener(ManagerEvent.AP_SELECT, self.get_data)

    async def start_scan(self):
        self.dialog.clear()
        self.manager.state = State.IDLE
        await self.manager.client.mqtt_send(consts.MANAGER_PUB_CMD_SCAN, "a")
        with  self.dialog, ui.card().classes("w-1/2 items-center").props("flat"):
            spinner = ui.spinner(size="lg")
        
        self.dialog.open()
        
        await self.manager.common_aps_done()
        spinner.visible = False

    async def select_ap(self, e : events.GenericEventArguments):
        ap = e.args
        await self.manager.client.mqtt_send(consts.MANAGER_PUB_CMD_SELECT_AP, json.dumps(ap))  
        self.dialog.close()

    def get_data(self):
        self.dialog.clear()  
        with self.dialog, ui.card().classes("w-full").props("flat bordered"):
            with ui.column():
                ui.label("Select AP").classes("text-2xl")
            with ui.column().classes("overflow-y-auto"):
                rows =[{"ssid" : ap.ssid, "bssid" : ap.bssid, "channel" : ap.channel} for ap in self.manager.common_aps]
                table = ui.table(rows=rows, row_key="ssid", 
                    column_defaults={
                        "align": "left",
                        "headerClasses": "uppercase text-primary text-lg font-bold",
                        }).classes("w-full")
                
                table.add_slot("header", r'''
                    <q-tr :props="props">
                        <q-th v-for="col in props.cols" :key="col.name" :props="props">
                            {{ col.label }}
                        </q-th>
                        <q-th auto-width />
                    </q-tr>
                ''')

                table.add_slot('body', r'''
                    <q-tr :props="props">
                        <q-td v-for="col in props.cols" :key="col.name" :props="props">
                            {{ col.value }}
                        </q-td>
                        <q-td auto-width>
                            <q-btn color="primary" @click="$parent.$emit('select_ap', props.row)">
                                Select AP
                            </q-btn>
                        </q-td>
                    </q-tr>
                ''')
                table.on("select_ap", self.select_ap)
        self.dialog.open()
    
    def show_dialog(self):
        self.dialog = ui.dialog()

class ScannerList:
    def __init__(self, manager: Manager):
        self.manager = manager 
        self.manager.register_listener(ManagerEvent.CLIENT_REGISTER, self.update_scanners)
        self.manager.register_listener(ManagerEvent.CLIENT_UNREGISTER, self.update_scanners)
        self.manager.register_listener(ManagerEvent.PKT_DATA_RECV, self.update_scanners)
        self.manager.register_listener(ManagerEvent.PKT_DATA_RECV, self.update_plot)
        
        self.rssi_bufs : dict[str, list[int]] = dict()
        self.var_bufs : dict[str, list[int]] = dict()
        self.ts_bufs : dict[str, list[int]] = dict()
        self.fig = go.Figure()
        self.fig.update_layout(
            margin=dict(l=10, r=5, t=10, b=10),
        )
        self.fig.update_yaxes(range = [0, 200])
        
        self.plot = None
        self.cover = None

        self.prev_target = None
        self.scanner_list = None
        self.data_type = 0

    def display_list(self):
        self.scanner_list = ui.list().classes("overflow-y-auto") 
        self.update_scanners()

    def display_plot(self):
        self.plot = ui.plotly(self.fig).classes("w-full h-full")
        self.cover = ui.card().classes("items-center justify-center absolute inset-0 z-10 opacity-85").props("flat")

        with self.cover:
             ui.label("Waiting for client data....").classes("black text-2xl")

    def update_scanners(self):

        with self.scanner_list:
            self.scanner_list.clear()
            if (len(self.manager.scanners.keys()) == 0):
                ui.label("No clients registered.")
            # intersect = self.rssi_bufs.keys() & self.manager.scanners.keys()
            for id in self.manager.scanners:
                if id not in self.rssi_bufs.keys():
                    self.rssi_bufs[id] = deque(maxlen=500_000)
                    self.var_bufs[id] = deque(maxlen=500_000)
                    self.ts_bufs[id] = deque(maxlen=500_000)

                scanner = self.manager.scanners[id]
                with ui.item(on_click=lambda: ui.notify(f"selected {id}")):
                    with ui.item_section().props("avatar"):
                        if scanner.scanning:
                            ui.element("div").classes("h-3 w-3 box scanning")
                        else:
                            ui.element("div").classes("h-3 w-3 box idle")
                    with ui.item_section():
                        ui.label(f"{id}")
            self.scanner_list.update()
    
    def update_plot(self):
        self.fig.data = []
        count = 0
        for id, scanner in self.manager.scanners.items():
            if id not in self.rssi_bufs.keys():
                continue
            if len(scanner.stats.signal_buf) == 0:
                continue
            self.cover.set_visibility(False)
            self.rssi_bufs[id].extend(list(scanner.stats.signal_buf))
            self.var_bufs[id].extend(list(scanner.stats.variance_buf))
            self.ts_bufs[id].extend(list(scanner.stats.ts_buf))
            scatter = self.fig.add_scatter(x=list(self.ts_bufs[id]),
                                y=list(self.var_bufs[id]) if self.data_type == 0 else list(self.rssi_bufs[id]))
            count = len(self.var_bufs[id])

        self.plot.update()
    
    def change_plot_type(self, data_type : int):
        self.data_type = data_type
        match data_type:
            case 0:
                self.fig.update_yaxes(range = [0, 200])
            case 1:
                self.fig.update_yaxes(range = [-100, 0])
        self.update_plot()

class GraphTab:
    def __init__(self, manager : Manager):
        self.manager = manager
        self.dialog = ScannerDialog(self.manager)
        self.scanners = ScannerList(self.manager)

    def tab(self):
        with ui.column().classes("w-full h-[calc(100vh-2rem)]") as col:
            with ui.row(align_items="center").classes("w-[95%] h-full fixed-center justify-center no-wrap"):
                with ui.card().classes("w-3/4 h-3/4 items-end relative").props("flat bordered") as graph_card:
                    self.scanners.display_plot()
                    with ui.toggle({0 : "Motion detection", 1 : "RSSI"}, value = 0, on_change=self.change_plot):
                        pass
                with ui.card().classes("w-1/4 h-3/4 flat bordered").props("flat bordered") as scan_card:
                    ui.button("AP Scan", on_click= lambda: self.dialog.start_scan()).bind_enabled_from(self.manager, "can_scan")
                    self.scanners.display_list()
                    self.scanners.update_plot()

    def change_plot(self, args : ValueChangeEventArguments):
        self.scanners.change_plot_type(args.value)
           

class SettingsTab:
    def __init__(self, manager : Manager):
        self.manager = manager

    def tab(self):
        ui.label("setting tab")

def create_ui(manager: Manager, mqtt_client : MqttClient):
    # router = Router()
    graph_tab = GraphTab(manager)
    settings_tab = SettingsTab(manager)
    main = ui.column()

    def graph():
        main.clear()
        with main:
            graph_tab.tab()
            
    def settings():
        main.clear()
        with main:
            settings_tab.tab()

    with ui.header().classes("no-wrap justify-between bg-primary"):
        ui.label("RSSI analyzer").classes("text-3xl")
        ui.space()
        ui.button("Graph").on_click(graph)
        ui.button("Settings").on_click(settings)
        ui.space()
        dark = ui.dark_mode(True)
        ui.switch("Dark mode").bind_value(dark)
   
    with ui.footer().classes("justify-between bg-dark"):
        ui.label("test")
    graph()

async def manager_work_loop(manager: Manager):
    while True:
        await manager.receive_next()
        print("Manager work loop")

async def start_ui():
    mqtt_client = MqttClient()
    manager = Manager(mqtt_client)
    create_ui(manager, mqtt_client)
    task = asyncio.create_task(manager_work_loop(manager))

ui.colors(primary="#6E93D6", secondary="#53B689", accent="#111B1E", positive="#53B689", dark="#121212")

app.on_startup(start_ui)
ui.add_css('''
    .box {
        border: 1px solid black;
        height: 20px;
        width: 20px;
    }
    .box.idle {
        background-color: yellow;
    }
    .box.scanning {
        background-color: green;
    }      
''')


ui.run()