#!/usr/bin/env python3
from nicegui import ui, app
import asyncio
from manager import Manager, MqttClient
from typing import Callable
from settings_tab import SettingsTab
from graph_tab import GraphTab
from scanner_settings import ScannerSettings
from confirm_dialog import ConfirmDialog

# used with ui.timer, define callbacks for ui updates
# id: nicegui context id, list: list of callbacks
class Updates:
    def __init__(self):
        self.timer_cbs = dict[str, list[Callable]]()

    @staticmethod
    def REGISTER_TIMER_CALLBACK(self, id: str, cb: Callable):
        if id not in self.timer_cbs:
            self.timer_cbs[id] = []

        # overwrite old callback, noticed some problems if functions are weirdly defined
        # inside other funcs (see _update_state), as it would call "older version" of the function
        # probably cause this code is absolute trash :/
        for i, ex in enumerate(self.timer_cbs[id]):
            if ex.__name__ == cb.__name__:
                del self.timer_cbs[id][i]
                break

        self.timer_cbs[id].append(cb)

    @staticmethod
    def UNREGISTER_ID_CALLBACK(self, id: str, cb: Callable):
        if id in self.timer_cbs:
            del self.timer_cbs[id]
    
    def run(self):
        for cb_list in self.timer_cbs.values():
            for cb in cb_list:
                cb()


async def create_ui(manager: Manager, mqtt_client: MqttClient):

    @ui.page("/")
    async def MainPage():
        updates = Updates()
        
        settings = ScannerSettings(0, "")
        dark = ui.dark_mode(True)
        graph_tab = GraphTab(manager, settings, dark)
        settings_tab = SettingsTab(manager, settings)
        main = None  # this is used as base element for the UI

        # loop over callbacks for UI updates
        ui.timer(1.0, updates.run, immediate=False)

        with ui.element().classes("flex flex-col h-[calc(100vh-128px)] w-full"):
            main = ui.row().classes(
                "flex-grow grid grid-cols-6 md:grid-rows-1 grid-rows-3 justify-items-center"
            )

        async def graph():
            if settings_tab.changes_made:
                res = await ConfirmDialog("Changes have been made, exit without saving?")
                if not res:
                    return

            main.clear()
            with main:
                graph_tab.tab()

        async def settings():
            main.clear()
            with main:
                settings_tab.tab()

        async def shutdown():
            with main:
                res = await ConfirmDialog("Shut down?")

            if res:
                with main:
                    with ui.dialog() as dialog, ui.card().props("flat"):

                        dialog.open()
                        ui.label("Shutting down...").classes(
                            "text-2xl text-bold")
                        ui.label("This tab can be closed.").classes("text-xl")
                app.shutdown()

        with ui.header().classes("h-[64px] justify-between bg-primary"):
            ui.label("RSSI analyzer").classes("text-lg md:text-3xl")
            ui.space()
            ui.button("Graph", icon="ssid_chart").classes(
                "max-sm:hidden").on_click(graph)
            ui.button("Settings", icon="settings").classes(
                "max-sm:hidden").on_click(settings)

            ui.space()
            ui.button(icon="power_settings_new").classes(
                "bg-red").on_click(shutdown)
            # ui.switch("Dark mode").props('keep-color').bind_value(dark)

        ui.colors(
            primary="#6E93D6",
            secondary="#53B689",
            accent="#111B1E",
            positive="#53B689",
            dark="#121212",
        )
        ui.add_css(
            """
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
        """
        )
        ui.add_head_html("""
            <style>
            @media (min-width: 768px) {
                .q-table thead th {
                    font-size: 1.125rem;
                }
                .q-table tbody td {
                    font-size: 1.125rem;
                }
            }
            </style>
            """
                         )

        await graph()


async def manager_work_loop(manager: Manager):
    while True:
        await manager.receive_next()


async def start():
    try:
        mqtt_client = MqttClient()
        manager = Manager(mqtt_client)
        task = asyncio.create_task(manager_work_loop(manager))

        
        await create_ui(manager, mqtt_client)

        def disconnect():
           Updates.UNREGISTER_ID_CALLBACK(ui.context.client.id, manager)

        app.on_disconnect(disconnect)
    except:
        print("Startup failed")
        app.shutdown()


app.on_startup(start)

ui.run()
