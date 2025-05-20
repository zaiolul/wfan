from nicegui import ui
from nicegui.events import ValueChangeEventArguments
from data import ManagerState
from manager import Manager
from scanner_settings_ui import ScannerSettings
from scanner_dialog_ui import ScannerDialog
from scanner_ui import ScannerList
from updates_ui import Updates

class GraphTab:
    def __init__(self, manager: Manager, settings: ScannerSettings, dark):
        self.manager = manager
        self.settings = settings
        self.scanners = ScannerList(manager, settings, dark)

    async def start_ap_scan(self):
        await ScannerDialog(self.manager, self.settings, self.scanners._reset_data)

    def tab(self):

        with (
            ui.card()
            .tight()
            .classes(
                "w-full h-full col-span-6 md:col-span-5 row-span-2 md:row-span-1 relative"
            )
            .props("flat bordered")
        ):
            self.scanners.display_plot()
            with ui.row().classes("w-full justify-between p-2"):
                ui.toggle(
                    {0: "Variance", 1: "RSSI"}, value=0, on_change=self.change_plot
                ).set_value(self.scanners.data_type)

                with ui.row():
                    ssid_label = ui.label().classes("text-xl")
                    bssid_label = ui.label().classes("text-xl")

                    ssid_label.bind_text_from(self.manager, "selected_ap_obj", lambda ap: (
                        f"AP: {ap["ssid"]}" if ap else ""))

                    bssid_label.bind_text_from(self.manager, "selected_ap_obj", lambda ap: (
                        f"({ap["bssid"]})" if ap else ""))

                ui.button("Reset axes").on_click(self.scanners.reset_axes)

        with (
            ui.card()
            .classes(
                "w-full h-full col-span-6 md:col-span-1 row-span-1 md:row-span-1 flat bordered"
            )
            .props("flat bordered")
        ):
            with ui.row().classes("w-full justify-between"):
                ui.button(
                    "AP Scan", on_click=self.start_ap_scan
                ).bind_enabled_from(self.manager, "can_scan")
                ui.button(
                    "Stop capture", on_click=self.scanners.stop_capture
                ).bind_visibility_from(
                    self.manager, "state", backward=lambda s: s == ManagerState.SCANNING
                ).props(
                    "flat"
                )
            with ui.row().classes("w-full"):
                ui.label("System state")
                state_label = ui.label("--")
                state_tooltip = None
                with state_label:
                    state_tooltip = ui.tooltip().classes("text-lg")

                def _update_state():
                    state = self.manager.state.name
                    color = ""
                    tooltip = ""
                    text = state

                    match state:
                        case "IDLE" | "SELECTING":
                            color = "orange-8"
                            tooltip = "System is currently idle, waiting for scan."
                            text = "IDLE"
                        case "SCANNING":
                            color = "green-8"
                            tooltip = "System is active and collecting RSSI from access point."
                    state_label.set_text(f"{text}")
                    state_label.classes.clear()
                    state_label.classes(f"text-bold text-{color}")

                    state_tooltip.set_text(tooltip)

                _update_state()  # call it now as well, so it shows instantly, any updates later on handled in timer cb
                Updates.REGISTER_TIMER_CALLBACK(ui.context.client.id, _update_state)

            ui.separator()
            ui.label("Scanners").classes("text-bold")
            self.scanners.display_list()

    def change_plot(self, args: ValueChangeEventArguments):
        self.scanners.change_plot_type(args.value)
