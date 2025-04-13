#!/usr/bin/env python3
from nicegui import ui, app, run, events
from collections import deque
from nicegui.events import ValueChangeEventArguments, GenericEventArguments, ClickEventArguments
import asyncio
from data import WifiAp, State, ManagerEvent
import plotly.graph_objects as go
import plotly.colors as plot_cl
from mgr import Manager, MqttClient
import consts
import json

class ScannerDialog:
    def __init__(self, manager: Manager):
        self.dialog = None
        self.manager = manager 
        self.manager.register_listener(ManagerEvent.AP_SELECT, self.get_data)

    def display_dialog(self):
        self.dialog = ui.dialog().classes("w-full")
        self.dialog.on("hide", self.manager.reset_scan_state)

    async def start_scan(self):
        self.dialog.clear()
        self.manager.state = State.IDLE
        await self.manager.mqtt_send(consts.MANAGER_PUB_CMD_SCAN, "a")
        with  self.dialog, ui.card().classes("w-1/2 items-center").props("flat"):
            spinner = ui.spinner(size="lg")
        
        self.dialog.open()
        
        await self.manager.common_aps_done()
        spinner.visible = False

    async def select_ap(self, e : events.GenericEventArguments):
        ap = e.args
        await self.manager.mqtt_send(consts.MANAGER_PUB_CMD_SELECT_AP, json.dumps(ap))  
        self.dialog.close()

    def get_data(self, args = None):
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
                
                #Below is needed for the button in each table row. Really no better way to do it?
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
    def __init__(self, manager: Manager, dark):
        self.manager = manager 
        self.manager.register_listener(ManagerEvent.CLIENT_REGISTER, self.update_scanners)
        self.manager.register_listener(ManagerEvent.CLIENT_UNREGISTER, self.update_scanners)
        # self.manager.register_listener(ManagerEvent.PKT_DATA_RECV, self.update_scanners)
        self.manager.register_listener(ManagerEvent.PKT_DATA_RECV, self.update_plot_data)
        
        self.rssi_bufs : dict[str, list[int]] = dict()
        self.var_bufs : dict[str, list[int]] = dict()
        self.ts_bufs : dict[str, list[int]] = dict()
        self.fig = go.Figure(layout={"template": "plotly_dark" if dark.value else "plotly"})

        self.fig.update_layout(
            margin=dict(l=0, r=0, t=0, b=0),
            xaxis={"tickmode": "linear",
                   "dtick": 1000}, # 1s
            showlegend=False
        )

        self.fig.update_yaxes(range = [0, 200])
        
        self.plot = None
        self.cover = None

        self.prev_target = None
        self.scanner_list = None
        self.data_type = 0

        self.saved_layout : go.Layout = None
        self.scanner_states : dict[str, bool] = dict()
        self.scanner_colors : dict[str, str] = dict()

    def display_list(self):
        self.scanner_list = ui.list().classes("overflow-y-auto").props("separator")
        self.update_scanners()

    def display_plot(self):
        self.plot = ui.plotly(self.fig).classes("w-full h-full")
        self.plot.on("plotly_relayout", self.on_relayout)
        #hack for options, no way to do it with nicegui
        self.plot._props["options"]["config"] = {"modeBarButtonsToRemove": ["zoomIn", "zoomOut", "select", "lasso", "autoscale", "pan", "toImage", "resetScale", "zoom"],
                                                 "doubleClick" : False,
                                                 "scrollZoom" : True}
        
        self.cover = ui.card().classes("items-center justify-center absolute inset-0 z-10 opacity-85").props("flat")
        with self.cover:
             ui.label("Waiting for client data....").classes("black text-2xl")

    def update_scanners(self, args = None):
        with self.scanner_list:
            self.scanner_list.clear()
            if (len(self.manager.scanners.keys()) == 0):
                ui.label("No clients registered.")
            # intersect = self.rssi_bufs.keys() & self.manager.scanners.keys()
            for i, id in enumerate(self.manager.scanners):
                if id not in self.rssi_bufs.keys():
                    self.rssi_bufs[id] = deque(maxlen=500_000)
                    self.var_bufs[id] = deque(maxlen=500_000)
                    self.ts_bufs[id] = deque(maxlen=500_000)
                    self.scanner_states[id] = True
                    self.scanner_colors[id] = plot_cl.qualitative.Plotly[i]
                scanner = self.manager.scanners[id]

                with ui.item(f"{id}", on_click=lambda sender, scan_id=id: self.on_scanner_select(sender, scan_id)).classes("font-bold bg-primary"):
                    with ui.item_section().props("avatar"):
                        if scanner.scanning:
                            ui.element("div").classes("h-3 w-3 box scanning")
                        else:
                            ui.element("div").classes("h-3 w-3 box idle")
                    # with ui.item_section():
                    #     ui.label(f"{id}")
            self.scanner_list.update()
    
    def on_scanner_select(self, e : ClickEventArguments, id):
        self.scanner_states[id] = not self.scanner_states[id]
        print(self.scanner_states[id] )

        if self.scanner_states[id] == True:
            # e.sender.classes("bg-primary")
            e.sender.classes.append("bg-primary")
        else:
            print("should clear")
            e.sender.classes.remove("bg-primary")
            # e.sender.classes("bg-gray-600")
        e.sender.update()
        self.update_plot()


    def update_plot_data(self, id : str):
        if not id:
            return
        if id not in  self.rssi_bufs.keys():
            self.scanner_states[id] = True

        scanner = self.manager.scanners[id]
        lents = len(self.ts_bufs[id])
        self.rssi_bufs[id].extend(list(scanner.stats.signal_buf))
        self.var_bufs[id].extend(list(scanner.stats.variance_buf))
        self.ts_bufs[id].extend(list(scanner.stats.ts_buf))
        self.update_plot()

    def update_plot(self):
        self.fig.data = []
        items = self.manager.scanners.items()
        for id, scanner in items:
            if id not in self.rssi_bufs.keys():
                continue
            if len(scanner.stats.signal_buf) == 0:
                continue
            self.cover.set_visibility(False)

            if not self.scanner_states[id]:
                continue
            scatter = self.fig.add_scatter(x=list(self.ts_bufs[id]),
                                y=list(self.var_bufs[id]) if self.data_type == 0 else list(self.rssi_bufs[id]),
                                 marker=dict(color=self.scanner_colors[id]))

        if len(items) > 0 and not self.saved_layout:
            xaxis = list(self.ts_bufs.values())[0]
            if len(xaxis) > consts.X_AXIS_SPAN:
                self.fig.update_xaxes(range = [xaxis[-consts.X_AXIS_SPAN], xaxis[-1]])
        else:
            self.fig.layout = self.saved_layout
        self.plot.update()
    
    def change_plot_type(self, data_type : int):
        self.data_type = data_type
        match data_type:
            case 0:
                self.fig.update_yaxes(range = [0, 200])
                # self.saved_layout = self.fig.layout
            case 1:
                self.fig.update_yaxes(range = [-100, 0])
                # self.saved_layout = self.fig.layout
        self.update_plot()
    
    def on_relayout(self, e : GenericEventArguments):
        print(e.args)
        if "xaxis.range[0]" in e.args:
            self.fig.update_xaxes( range = [e.args["xaxis.range[0]"], e.args["xaxis.range[1]"]])
            if  "yaxis.range[0]" in e.args:
                self.fig.update_yaxes(range = [e.args["yaxis.range[0]"], e.args["yaxis.range[1]"]])
            self.saved_layout = self.fig.layout

        # elif "xaxis.autorange" in e.args:
        #     self.saved_layout = None
            # items = self.manager.scanners.items()
            # if len(items) > 0:
            #     xaxis = list(self.ts_bufs.values())[0]
            #     if len(xaxis) > consts.X_AXIS_SPAN:
            #         print(f"SET X {xaxis[-consts.X_AXIS_SPAN]} - {xaxis[-1]}")
            #         self.fig.update_xaxes(range = [xaxis[-consts.X_AXIS_SPAN], xaxis[-1]])
            #     else:
            #         print(f"SET X AUTO")
            #         self.fig.update_xaxes(autorange = True)
        # self.plot.update()

    def reset_axes(self):
        self.saved_layout = None
        self.change_plot_type(self.data_type)
    
    def reset_data(self):
        self.rssi_bufs.clear()
        self.var_bufs.clear()
        self.ts_bufs.clear()
        self.scanner_states.clear()
class GraphTab:
    def __init__(self, manager : Manager, dark):
        self.manager = manager
        self.dialog = ScannerDialog(self.manager)
        self.scanners = ScannerList(self.manager, dark)

    def tab(self):
        # with ui.column().classes("w-full h-[calc(100vh-2rem)] grid grid-flow-row columns-2") as col:
        self.dialog.display_dialog()
            # with ui.row(align_items="center").classes("w-[95%] h-full fixed-center justify-center flex-wrap w-full gap-4 p-4"):
        with ui.card().tight().classes("w-full h-full col-span-6 md:col-span-5 row-span-2 md:row-span-1 relative").props("flat bordered") as graph_card:
            self.scanners.display_plot()
            with ui.row().classes("w-full justify-between p-2"):
                ui.toggle({0 : "Variance", 1 : "RSSI"}, value = 0, on_change=self.change_plot).set_value(self.scanners.data_type)
                ui.button("Reset axes").on_click(self.scanners.reset_axes)
        with ui.card().classes("w-full h-full col-span-6 md:col-span-1 row-span-1 md:row-span-1 flat bordered").props("flat bordered") as scan_card:
            ui.button("AP Scan", on_click= lambda: self.dialog.start_scan()).bind_enabled_from(self.manager, "can_scan")
            self.scanners.display_list()


    def change_plot(self, args : ValueChangeEventArguments):
        self.scanners.change_plot_type(args.value)
           

class SettingsTab:
    def __init__(self, manager : Manager):
        self.manager = manager

    def tab(self):
        ui.label("setting tab")


def create_ui(manager: Manager, mqtt_client : MqttClient):
    @ui.page("/")
    def MainPage():
        dark = ui.dark_mode(True)
        graph_tab = GraphTab(manager, dark)
        settings_tab = SettingsTab(manager)
        main = None
        
        def graph():
            main.clear()
            with main:
                graph_tab.tab()
                
        def settings():
            main.clear()
            with main:
                settings_tab.tab()

        # with ui.row().classes("w-full grid grid-cols-6 bg-primary"):
        #     ui.label("RSSI analyzer").classes("col-span-6 md:col-span-2 text-3xl")
        #     ui.button("Graph").on_click(graph).classes("col-span-2 md:col-span-2")
        #     ui.button("Settings").on_click(settings).classes("col-span-2 md:col-span-2")
        #     dark = ui.dark_mode(True)
            # ui.switch("Dark mode").bind_value(dark).classes("col-span-2 md:col-span-1")
        with ui.header().classes("h-[64px] justify-between bg-primary"):
            ui.label("RSSI analyzer").classes("text-3xl")
            ui.space()
            ui.button("Graph").on_click(graph)
            ui.button("Settings").on_click(settings)
            ui.space()
            ui.switch("Dark mode").props('keep-color').bind_value(dark)

        with ui.element().classes('flex flex-col h-[calc(100vh-128px)] w-full'):
            main = ui.row().classes("flex-grow grid grid-cols-6 md:grid-rows-1 grid-rows-3 ")

        ui.colors(primary="#6E93D6", secondary="#53B689", accent="#111B1E", positive="#53B689", dark="#121212")
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
       
        graph()

async def manager_work_loop(manager: Manager):
    while True:
        await manager.receive_next()

async def start_ui():
    mqtt_client = MqttClient()
    manager = Manager(mqtt_client)
    task = asyncio.create_task(manager_work_loop(manager))
    create_ui(manager, mqtt_client)
   
app.on_startup(start_ui)

ui.run()