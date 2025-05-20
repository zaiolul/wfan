from nicegui import ui, run
from nicegui.events import ValueChangeEventArguments
from manager import Manager
import consts
from file_picker_ui import local_file_picker
import os
from scanner_settings_ui import ScannerSettings
from updates_ui import Updates


class SettingsTab:
    def __init__(self, manager: Manager, settings: ScannerSettings):
        self.manager: Manager = manager
        self.settings: ScannerSettings = settings
        self.selected_opts = settings.chans_24

        self.el_select: ui.select = None
        self.el_toggle: ui.toggle = None
        self.selected_dir: str = ""
        self.dir_label: ui.label = None

        self.changes_made = False

        self.mqtt_status: bool
        self.mqtt_status_label: ui.label = None

        self.status_colors = {
            False: "red-8",
            True: "green-8"
        }

        if os.path.exists(consts.SETTINGS_FILE):
            settings.import_options()
        self.prev_mqtt_state = False
        ui.timer(1, self._check_mqtt_conn)

    def tab(self):
        self.changes_made = False
        self.settings.import_options()  # HACK, ADD ADDITIONAL CHECKS TO NOT OVERRIDE LOCAL
        with (
            ui.card()
            .classes(
                "w-full items-center md:items-start md:w-1/2 col-span-6 row-span-3 "
            )
            .props("flat bordered dense")
        ):

            with ui.row().classes("w-full justify-between"):
                ui.label("Scanner settings").classes("text-3xl")
                ui.button("Save", on_click=self._on_save)
            ui.separator()
            with ui.row().classes("w-full"):
                with ui.column().classes("w-full md:w-1/2"):
                    with ui.column().classes("justify-center items-center"):
                        # Ignore any other options for now,
                        # TODO 5GHz (maybe)
                        self.el_toggle = ui.toggle(
                            {0: "2.4 GHz"},
                            value=self.settings.selected_band,
                            on_change=self.update_band,
                        ).tooltip("Scanned bandwidth").props('no-caps')
                        self.el_toggle.disable()
                        with self.el_toggle:
                            ui.tooltip("Wi-Fi radio bandwidth for scanning. (prototype version only has 2.4 GHz)").classes(
                                "text-lg"
                            )

                    with ui.column().classes("w-full"):
                        self.el_select = (
                            ui.select(
                                self.selected_opts,
                                validation={
                                    "At least one channel required": self._validate_select
                                },
                                multiple=True,
                                value=self.settings.selected_chans,
                                label="Channels",
                                on_change=self._update_selected_chans,
                            )
                            .classes("w-full")
                            .props("stack-label use-chips")
                            .without_auto_validation()
                        )
                        with self.el_select:
                            ui.tooltip(
                                "Channel list, that will be passed to scanners to search for APs."
                            ).classes("text-lg")

                with ui.column().classes("w-full md:w-1/3"):
                    with ui.row():
                        with ui.button(
                            "Set results path",
                            on_click=self._select_file_path,
                            icon="folder",
                        ).props('no-caps'):
                            ui.tooltip(
                                "Select directory where scanner data should be written."
                            ).classes("text-lg")

                        clear = ui.button(
                            "Clear", on_click=self._clear_dir).props("flat")
                        clear.bind_visibility_from(
                            self.settings, "selected_dir", backward=lambda d: len(d) != 0
                        )

                    self.dir_label = ui.label()
                    self.dir_label.set_text(
                        "Path not selected"
                        if len(self.settings.selected_dir) == 0
                        else self.settings.selected_dir
                    )
                    self.dir_label.bind_text_from(
                        self.settings, "selected_dir")

            ui.separator()
            with ui.row().classes("w-full"):
                ui.label("MQTT broker connection").classes("w-full text-3xl")

                self.mqtt_ip = ui.input("Broker IP", value=self.settings.mqtt_ip, validation={
                    "Not a valid IPv4 address": self._validate_ip
                }, on_change=self._on_mqtt_host_change)

                self.mqtt_port = ui.input("Port", value=self.settings.mqtt_port, validation={
                    "Not a valid port (0-65535)": self._validate_port
                }, on_change=self._on_mqtt_port_change)
                ui.input("Username", value=self.settings.mqtt_user,
                         on_change=self._on_mqtt_user_change)
                ui.input("Password", value=self.settings.mqtt_password, password=True,
                         on_change=self._on_mqtt_password_change)

                ui.label("Connection status:").classes("text-lg")

                self.mqtt_status_label = ui.label()
                self._update_mqtt_status()
                Updates.REGISTER_TIMER_CALLBACK(
                    ui.context.client.id, self._update_mqtt_status)

    async def _check_mqtt_conn(self):
        state = self.manager.client.get_status()
        if not state:
            await self._conn_mqtt()
        if self.prev_mqtt_state and not state:
            ui.notify("MQTT disconnected, trying to connect", color="negative")
        elif not self.prev_mqtt_state and state:
            ui.notify("MQTT connected", color="positive")
        self.prev_mqtt_state = state

    async def _conn_mqtt(self):
        def func():
            self.manager.client.try_connect(
                self.settings.mqtt_ip, self.settings.mqtt_port, 
                self.settings.mqtt_user, self.settings.mqtt_password
            )

        await run.io_bound(func)

    def _on_mqtt_user_change(self, e: ValueChangeEventArguments):
        self.settings.mqtt_user = e.value
        self.changes_made = True

    def _on_mqtt_password_change(self, e: ValueChangeEventArguments):
        self.settings.mqtt_password = e.value
        self.changes_made = True

    def _on_mqtt_host_change(self, e: ValueChangeEventArguments):
        self.settings.mqtt_ip = e.value
        self.changes_made = True

    def _on_mqtt_port_change(self, e: ValueChangeEventArguments):
        self.settings.mqtt_port = e.value
        self.changes_made = True

    def _validate_ip(self, value: str) -> bool:
        from ipaddress import ip_address
        try:
            if len(value) == 0:
                return False
            ip_address(value)
            return True
        except ValueError:
            return False

    def _validate_port(self, value: str) -> bool:
        try:
            port = int(value)
            if 0 < port < 65536:
                return True
        except ValueError:
            return False

    def _update_mqtt_status(self):
        self.mqtt_status = self.manager.client.get_status()
        self.mqtt_status_label.set_text(
            "Connected" if self.mqtt_status else "Disconnected")
        self.mqtt_status_label.classes.clear()
        self.mqtt_status_label.classes(
            f"text-{self.status_colors[self.mqtt_status]} text-lg")
        self.mqtt_status_label.update()

    def _clear_dir(self):
        self.settings.selected_dir = ""
        self.changes_made = True

    async def _on_save(self):
        self.changes_made = False
        await self.settings.save_options()
        await self._conn_mqtt()
        self.manager.update_results_path(self.settings.selected_dir)

    async def _select_file_path(self):
        dirs = await local_file_picker("~")
        if not dirs:
            return
        self.settings.selected_dir = dirs[0]
        self.dir_label.set_text(self.selected_dir)
        self.changes_made = True

    def _validate_select(self, val) -> bool:
        print("validate")
        return len(val) > 0

    def _update_selected_chans(self, e: ValueChangeEventArguments):
        self.mqtt_status = self.manager.client.get_status()

        vals = e.value
        self.el_select.validate()
        if len(vals) == 0:
            vals.append(self.settings.selected_chans[-1])
        vals.sort()
        self.el_select.value = vals
        self.settings.selected_chans = vals
        self.el_select.update()
        self.changes_made = True

    def update_band(self, e: ValueChangeEventArguments):
        self.settings.selected_band = e.value
        match e.value:
            case 0:
                self.el_select.set_options(
                    self.settings.chans_24, value=self.settings.chans_24
                )
                self.selected_opts = self.settings.chans_24
            case 1:
                self.el_select.set_options(
                    self.settings.chans_5, value=self.settings.chans_5
                )
                self.selected_opts = self.settings.chans_5
        self.el_select.update()
