#!/usr/bin/env python3
from nicegui import ui, app
import asyncio
from manager import Manager, MqttClient
from settings_ui import SettingsTab
from graph_ui import GraphTab
from scanner_settings_ui import ScannerSettings
from confirm_dialog_ui import ConfirmDialog
from updates_ui import Updates

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

        def shutdown():
            mqtt_client.disconnect()
            task.cancel()
            
        app.on_shutdown(shutdown)
        app.on_disconnect(disconnect)
    except:
        print("Startup failed")
        app.shutdown()


app.on_startup(start)

ui.run(port=11000, title="wfan", favicon="files/wfan_icon.png", reload=False)
