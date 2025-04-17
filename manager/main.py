#!/usr/bin/env python3
from nicegui import ui, app, run, events
from collections import deque
from nicegui.events import ValueChangeEventArguments, GenericEventArguments, ClickEventArguments
import asyncio
from data import WifiAp, State, ManagerEvent, ScannerState 
import plotly.graph_objects as go
import plotly.colors as plot_cl
from mgr import Manager, MqttClient
import consts
import json
from file_picker import local_file_picker
import os
from typing import Callable, Awaitable
import time
class ScannerSettings:
    def __init__(self, band, path):
        self.chans_24 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]
        self.chans_5 = [36, 40, 44, 48]
        self.selected_chans : list[int] = self.chans_24
        self.selected_dir : str = path
        self.selected_band : int = band

    def import_options(self):
            band = None
            chans = None
            results_dir = None
            f = None
            try:
                f = open(consts.SETTINGS_FILE, "r")
            except:
                print("Settings file does not exist")
                return

            for line in f:
                parts = line.split("=")
                if len(parts) != 2:
                    continue
                try:
                    match parts[0]:
                        case "band":
                            band = int(parts[1].rstrip())
                        case "chans": #TODO actually validate range, now possible to pass illegal val
                            chans = []
                            for chan in parts[1].split(","):
                                chans.append(int(chan))
                        case "results_dir":
                            results_dir = parts[1].rstrip()
                except:
                    print("Invalid settings config, use defaults")
                    return
            
            if chans:
                self.selected_chans = chans
            if results_dir:
                self.selected_dir = results_dir
            if band:
                self.selected_band = band

    async def save_options(self):
        chans = [str(c) for c in self.selected_chans]
        with open(consts.SETTINGS_FILE, "w") as f:
            f.write(f"band={self.selected_band}\n")
            f.write(f"chans={",".join(chans)}\n")
            if self.selected_dir:
                f.write(f"results_dir={self.selected_dir}\n")
        ui.notify("Settings saved.")

class ScannerDialog:
    def __init__(self, manager: Manager, settings : ScannerSettings, on_select_ap : Callable[[], Awaitable[None]]):
        self.manager = manager 
        self.settings = settings
        self.select_ap = on_select_ap
        self.dialog = None
        self.scan_task = None

    def on_dialog_hide(self):
        self.manager.reset_scan_state
        if self.scan_task:
            self.scan_task.cancel()

    def start_scan(self):
        self.dialog = ui.dialog()
        self.dialog.on("hide", self.on_dialog_hide)
        self.scan_task = asyncio.create_task(self._start_scan())

    async def _start_scan(self):
        self.dialog.clear()
        self.manager.state = State.IDLE
        await self.manager.mqtt_send(consts.MANAGER_PUB_CMD_SCAN, json.dumps({"channels" : self.settings.selected_chans}))
        with self.dialog, ui.card().classes("items-center").props("flat"):
            spinner = ui.spinner(size="lg")
        
        self.dialog.open()
        
        res = await self.manager.common_aps_done(timeout=15)
        self.dialog.clear()

        if res:
            self.get_data()
        else:
            with self.dialog, ui.card().classes("items-center").props("flat"):
                ui.label("Scan timeout").classes("text-lg p-4")
        spinner.visible = False

    def get_data(self, args = None):
        if not self.dialog:
            return # ??????
        
        self.dialog.clear()  
        with self.dialog, ui.card().tight().classes("h-full md:h-1/2").props("flat bordered"):
            with ui.column():
                ui.label("Select AP").classes("text-2xl p-4")
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
                ui.on("select_ap", lambda: print("GOT AP EVNT"))
        self.dialog.open()
    
    def show_dialog(self):
        self.dialog = ui.dialog().classes("w-full")


class ScannerList:
    def __init__(self, manager: Manager, settings : ScannerSettings, dark):
        self.manager = manager 
        self.settings = settings
        self.manager.register_listener(ManagerEvent.CLIENT_REGISTER, self.update_scanners)
        self.manager.register_listener(ManagerEvent.CLIENT_UNREGISTER, self.update_scanners)
        self.manager.register_listener(ManagerEvent.PKT_DATA_RECV, self.update_scanners)
        self.manager.register_listener(ManagerEvent.PKT_DATA_RECV, self.update_plot_data)
        
        self.dialog = ScannerDialog(self.manager, settings, self.select_ap)
        self.rssi_bufs : dict[str, list[int]] = dict()
        self.var_bufs : dict[str, list[int]] = dict()
        self.ts_bufs : dict[str, list[int]] = dict()
        self.fig = go.Figure(layout={"template": "plotly_dark"})

        self.scanner_state_colors = {
            ScannerState.SCANNER_SCANNING : "green-8",
            ScannerState.SCANNER_IDLE : "orange-8",
            ScannerState.SCANNER_CRASHED : "red-8"
        }

        self.fig.update_layout(
            margin=dict(l=0, r=0, t=0, b=0),
            xaxis={"tickmode": "linear",
                   "dtick": 1000}, # 1s
            showlegend=False,
            xaxis_title=dict(text="Time")
        )

        self.fig.update_yaxes(range = [0, 200], visible=False)
        
        self.plot = None
        self.cover = None

        self.prev_target = None
        self.scanner_list = None
        self.data_type = 0

        self.saved_layout : go.Layout = None
        self.scanner_states : dict[str, bool] = dict()
        self.scanner_colors : dict[str, str] = dict()
        self.cover_visibility = True

    def display_list(self):
        self.scanner_list = ui.list().classes("w-full overflow-y-auto").props("separator")
        self.update_scanners()

    def display_plot(self):
        self.plot = ui.plotly(self.fig).classes("w-full h-full")
        self.plot.on("plotly_relayout", self.on_relayout)
        #hack for options, no way to do it with nicegui class wrapper unfortunately :/
        self.plot._props["options"]["config"] = {"modeBarButtonsToRemove": ["select", "lasso", "autoscale", "pan", "toImage", "resetScale", "zoom"],
                                                 "doubleClick" : False,
                                                 "scrollZoom" : True}
        
        self.cover = ui.card().classes("items-center justify-center absolute inset-0 z-10 opacity-85").props("flat")
        self.cover.bind_visibility_from(self.manager, "state", backward=lambda s: s != State.SCANNING)

        with self.cover:
             ui.label("Waiting for client data....").classes("black text-2xl")
    
    def update_scanners(self, args = None):
        print("UPDATE SCNANERS")
        if not self.scanner_list:
            return
        
        with self.scanner_list:
            self.scanner_list.clear()
            if (len(self.manager.scanners.keys()) == 0):
                ui.label("No clients registered.")
                self.scanner_states.clear()
                return
            
            # intersect = self.scanner_states.keys() & self.manager.scanners.keys()
            # for id in self.scanner_states.keys():
            #     if id not in intersect:
            #         self.scanner_states.pop(id)

            for i, id in enumerate(self.manager.scanners):
                if id not in self.scanner_states:
                    # self.rssi_bufs[id] = deque(maxlen=500_000)
                    # self.var_bufs[id] = deque(maxlen=500_000)
                    # self.ts_bufs[id] = deque(maxlen=500_000)
                    self.scanner_states[id] = True
                    self.scanner_colors[id] = plot_cl.qualitative.Plotly[i]
                    if len(self.settings.selected_dir) > 0:
                        self.manager.update_scanner_result_path(id, self.settings.selected_dir)
                scanner = self.manager.scanners[id]

                item = ui.item(f"{id}", on_click=lambda sender, scan_id=id: self.on_scanner_select(sender, scan_id)) \
                    .classes(f"bg-{self.scanner_state_colors[scanner.state]} font-bold text-white")
                if not self.scanner_states[id]:
                    item.classes.append("opacity-70")
                
                with item:
                    state_desc = ""
                    match scanner.state:
                        case ScannerState.SCANNER_IDLE:
                            state_desc = "Scanner is idle."
                        case ScannerState.SCANNER_CRASHED:
                            state_desc = "Scanner has crashed, waiting to recover..."
                        case ScannerState.SCANNER_SCANNING:
                            state_desc = "Scanner is active and capturing."
                    with ui.tooltip().classes("text-lg"):
                        ui.label(f"{state_desc}")
                        if scanner.state == ScannerState.SCANNER_SCANNING:
                            ui.label(f"Average RSSI: {scanner.stats.average}\n")
            
            self.scanner_list.update()
    
    def on_scanner_select(self, e : ClickEventArguments, id):
        self.scanner_states[id] = not self.scanner_states[id]
        print(self.scanner_states[id] )

        if self.scanner_states[id] == False:
            e.sender.classes.append("opacity-70")
        else:
            print("should clear")
            e.sender.classes.remove("opacity-70")
        e.sender.update()
        self.update_plot()


    def update_plot_data(self, id : str):
        print("UPDATE PLOT DATA")
        if not id:
            return
        if id not in self.scanner_states.keys():
            self.scanner_states[id] = True

        scanner = self.manager.scanners[id]
        # self.rssi_bufs[id].extend(list(scanner.stats.signal_buf))
        # self.var_bufs[id].extend(list(scanner.stats.variance_buf))
        # self.ts_bufs[id].extend(list(scanner.stats.ts_buf))
        self.cover_visibility = False
        self.update_plot()

    def update_plot(self):
        print("UPDATE PLOT")
        self.fig.data = []
        items = self.manager.scanners.items()
        xaxis = list()
        for id, scanner in items:
            if len(scanner.stats.signal_buf) == 0:
                continue
            
            if id not in  self.scanner_states.keys():
                continue

            if not self.scanner_states[id]:
                continue

            sig_buf, var_buf, ts_buf = self.manager.fetch_scanner_display_stats(id)
            scatter = self.fig.add_scatter(x=ts_buf,
                                y=var_buf if self.data_type == 0 else sig_buf,
                                marker=dict(color=self.scanner_colors[id]))
            if len(ts_buf) > len(xaxis):
                xaxis = ts_buf
        if len(items) > 0 and not self.saved_layout:
            if len(xaxis) > consts.X_AXIS_SPAN:
                self.fig.update_xaxes(range = [xaxis[-consts.X_AXIS_SPAN], xaxis[-1]])
           
        else:
            print("Saved layout not null")
            self.fig.layout = self.saved_layout
        
        self.plot.update()
    
    def change_plot_type(self, data_type : int):
        self.data_type = data_type
        self.saved_layout = None
        match data_type:
            case 0:
                self.fig.update_yaxes(range = [0, 200], visible=False, title=None)
                # self.saved_layout = self.fig.layout
            case 1:
                self.fig.update_yaxes(range = [-100, 0], visible=True,  title=dict(text="RSSI"))
    
                # self.saved_layout = self.fig.layout
        self.update_plot()
    
    def on_relayout(self, e : GenericEventArguments):
        print(e.args)
        if "xaxis.range[0]" in e.args:
            self.fig.update_xaxes( range = [e.args["xaxis.range[0]"], e.args["xaxis.range[1]"]])
            if  "yaxis.range[0]" in e.args:
                self.fig.update_yaxes(range = [e.args["yaxis.range[0]"], e.args["yaxis.range[1]"]])
            self.saved_layout = self.fig.layout

    def reset_axes(self):
        self.saved_layout = None
        self.change_plot_type(self.data_type)

    async def select_ap(self, e : events.GenericEventArguments):
        ap = e.args
        self.reset_data()
        self.cover_visibility = True
        print(ap)
        self.manager.selected_ap_obj = ap
        await self.manager.mqtt_send(consts.MANAGER_PUB_CMD_SELECT_AP, json.dumps(ap))  
        self.dialog.dialog.close()

    def reset_data(self):
        for id in self.rssi_bufs.keys():
            self.scanner_states[id] = True
        self.reset_axes()

    async def stop_capture(self):
        for id in self.scanner_states.keys():
            self.manager.update_scanner_display_stats(id, add=False, reset=True)
        self.scanner_states.clear()
        await self.manager.mqtt_send(consts.MANAGER_PUB_CMD_STOP)
        self.update_plot()
        
    def unregister_cbs(self):
        self.manager.remove_listener(ManagerEvent.CLIENT_REGISTER, self.update_scanners)
        self.manager.remove_listener(ManagerEvent.CLIENT_UNREGISTER, self.update_scanners)
        self.manager.remove_listener(ManagerEvent.PKT_DATA_RECV, self.update_scanners)
        self.manager.remove_listener(ManagerEvent.PKT_DATA_RECV, self.update_plot_data)
class GraphTab:
    def __init__(self, manager : Manager, settings: ScannerSettings, dark):
        self.manager = manager
        
        self.scanners = ScannerList(manager, settings, dark)

    def tab(self):
        # with ui.column().classes("w-full h-[calc(100vh-2rem)] grid grid-flow-row columns-2") as col:
        self.scanners.dialog.show_dialog()
            # with ui.row(align_items="center").classes("w-[95%] h-full fixed-center justify-center flex-wrap w-full gap-4 p-4"):
        with ui.card().tight().classes("w-full h-full col-span-6 md:col-span-5 row-span-2 md:row-span-1 relative").props("flat bordered") as graph_card:
            self.scanners.display_plot()
            with ui.row().classes("w-full justify-between p-2"):
                ui.toggle({0 : "Variance", 1 : "RSSI"}, value = 0, on_change=self.change_plot).set_value(self.scanners.data_type)
                ui.button("Reset axes").on_click(self.scanners.reset_axes)
        with ui.card().classes("w-full h-full col-span-6 md:col-span-1 row-span-1 md:row-span-1 flat bordered").props("flat bordered") as scan_card:
            with ui.row().classes("w-full justify-between"):
                ui.button("AP Scan", on_click= self.scanners.dialog.start_scan).bind_enabled_from(self.manager, "can_scan")
                ui.button("Stop capture", on_click=self.scanners.stop_capture) \
                .bind_visibility_from(self.manager, "state", backward=lambda s: s == State.SCANNING).props("flat")
            self.scanners.display_list()


    def change_plot(self, args : ValueChangeEventArguments):
        self.scanners.change_plot_type(args.value)
    
    def unregister_cbs(self):
        print("Unregister cbs")
        self.scanners.unregister_cbs()

class SettingsTab:
    def __init__(self, manager : Manager, settings : ScannerSettings):
        self.manager = manager
        self.settings = settings
        self.selected_opts = settings.chans_24

        self.el_select : ui.select = None
        self.el_toggle : ui.toggle = None
        self.selected_dir : str = ""
        self.dir_label : ui.label = None
        
        if os.path.exists(consts.SETTINGS_FILE):
            settings.import_options()

    def tab(self):
        self.settings.import_options() #HACK, ADD ADDITIONAL CHECKS TO NOT OVERRIDE LOCAL
        with ui.card().classes("w-full items-center md:items-start md:w-1/3 col-span-6 row-span-3 ").props("flat bordered dense"):
            
            with ui.row().classes("w-full justify-between"):
                ui.label("Scanner settings").classes("text-3xl")
                ui.button("Save", on_click=self.on_save)
            ui.separator()
            with ui.row().classes("w-full"):
                with ui.column().classes("w-full md:w-1/2 shrink-0"):
                    with ui.column().classes("justify-center items-center"):
                        #Ignore any other options for now,
                        #TODO 5GHz (maybe)
                        self.el_toggle = ui.toggle({0: "2.4 GHz", 1 : "5 GHz"}, value=self.settings.selected_band, on_change=self.update_band).tooltip("Scanned bandwidth")
                        self.el_toggle.disable()
                        with self.el_toggle:
                            ui.tooltip("Wi-Fi radio bandwidth for scanning.").classes("text-lg")
                   
                    with ui.column().classes("w-full"):
                        self.el_select = ui.select(self.selected_opts, validation= {"At least one channel required" : self.validate_select},
                            multiple=True, value=self.settings.selected_chans, label="Channels", 
                            on_change=self.update_selected_chans).classes("w-full").props("stack-label use-chips").without_auto_validation()
                        with self.el_select:
                            ui.tooltip("Channel list, that will be passed to scanners to search for APs.").classes("text-lg")

                with ui.column().classes("w-full md:w-1/3"):
                    with ui.button("Results location", on_click=self.select_file_path, icon="folder"):
                        ui.tooltip("Select directory where scanner data should be written.").classes("text-lg")
                    
                    self.dir_label = ui.label()
                    self.dir_label.set_text("Path not selected" if len(self.settings.selected_dir) == 0 else self.settings.selected_dir)
                    self.dir_label.bind_text_from(self.settings, "selected_dir")
                    clear = ui.button("Clear", on_click=self.clear_dir).props("flat")
                    clear.bind_visibility_from(self.settings, "selected_dir", backward= lambda d: len(d) != 0)
                    
    def clear_dir(self):
        self.settings.selected_dir = ""

    async def on_save(self):
        await self.settings.save_options()
        self.manager.update_results_path(self.settings.selected_dir)
        
    async def select_file_path(self):
        dirs = await local_file_picker('~')
        print(dirs)
        self.settings.selected_dir = dirs[0]
        self.dir_label.set_text(self.selected_dir)

    def validate_select(self, val):
        print("validate")
        return len(val) > 0
    
    def update_selected_chans(self, e : ValueChangeEventArguments):
        print("update")
        vals = e.value
        self.el_select.validate()
        if len(vals) == 0:
            vals.append(self.settings.selected_chans[-1])
        vals.sort()
        self.el_select.value = vals
        self.settings.selected_chans = vals
        self.el_select.update()
       
    def update_band(self, e : ValueChangeEventArguments):
        self.settings.selected_band = e.value
        match e.value:
            case 0:
                self.el_select.set_options(self.settings.chans_24, value=self.settings.chans_24)
                self.selected_opts = self.settings.chans_24
            case 1:
                self.el_select.set_options(self.settings.chans_5, value=self.settings.chans_5)
                self.selected_opts = self.settings.chans_5
        self.el_select.update()

def create_ui(manager: Manager, mqtt_client : MqttClient):
    @ui.page("/")
    def MainPage():
        ctx = ui.context
        
        settings = ScannerSettings(0, "")
        dark = ui.dark_mode(True)
        graph_tab = GraphTab(manager, settings, dark)
        settings_tab = SettingsTab(manager, settings)
        app.on_disconnect(graph_tab.unregister_cbs)
        main = None
        
        def graph():
            main.clear()
            with main:
                graph_tab.tab()
                
        def settings():
            main.clear()
            with main:
                settings_tab.tab()

        with ui.header().classes("h-[64px] justify-between bg-primary"):
            ui.label("RSSI analyzer").classes("text-lg md:text-3xl")
            ui.space()
            with ui.button(icon="ssid_chart").on_click(graph):
                ui.label("Graph").classes("max-sm:hidden")
            with ui.button(icon="settings").on_click(settings):
                ui.label("Settings").classes("max-sm:hidden")
            ui.space()
            # ui.switch("Dark mode").props('keep-color').bind_value(dark)

        with ui.element().classes('flex flex-col h-[calc(100vh-128px)] w-full'):
            main = ui.row().classes("flex-grow grid grid-cols-6 md:grid-rows-1 grid-rows-3 justify-items-center")

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