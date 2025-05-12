from nicegui import ui
from scanner_settings_ui import ScannerSettings
from data import ScannerState
from manager import Manager
from plotly import graph_objects as go
import consts
from nicegui.events import (

    GenericEventArguments,
    ClickEventArguments,
)
import plotly.graph_objects as go
import plotly.colors as plot_cl
from updates_ui import Updates

class ScannerList:
    def __init__(self, manager: Manager, settings: ScannerSettings, dark):
        self.manager: Manager = manager
        self.settings: ScannerSettings = settings

        self.fig = go.Figure(layout={"template": "plotly_dark"})

        self.scanner_state_colors = {
            ScannerState.SCANNER_SCANNING: "green-8",
            ScannerState.SCANNER_IDLE: "orange-8",
            ScannerState.SCANNER_CRASHED: "red-8",
        }

        self.fig.update_layout(
            margin=dict(l=10, r=0, t=20, b=0),
            xaxis_title=dict(text="Time"),
            showlegend=False,
        )

        self.fig.update_yaxes(range=[0, consts.Y_VAR_MAX], visible=False)

        self.plot: ui.plotly = None
        self.cover: ui.card = None

        self.scanner_list: ui.list = None
        self.data_type = 0

        self.saved_layout: go.Layout = None
        self.scanner_states: dict[str, bool] = dict()
        self.scanner_colors: dict[str, str] = dict()

    def display_list(self):
        self.scanner_list = (
            ui.list().classes("w-full overflow-y-auto").props("separator")
        )
        self.scanner_states.clear()
        self.update_scanners()
        Updates.REGISTER_TIMER_CALLBACK(ui.context.client.id, self.update_scanners)

    def display_plot(self):
        self.plot = ui.plotly(self.fig).classes("w-full h-full")
        self.plot.on("plotly_relayout", self._on_relayout)

        # hack for options, no way to do it with nicegui class wrapper unfortunately :/
        self.plot._props["options"]["config"] = {
            "modeBarButtonsToRemove": [
                "select",
                "lasso",
                "autoscale",
                "pan",
                "toImage",
                "resetScale",
                "zoom",
            ],
            "doubleClick": False,
            "scrollZoom": True,
            "showTips": False,
        }

        self.cover = (
            ui.card()
            .classes("items-center justify-center absolute inset-0 z-10 opacity-85")
            .props("flat")
        )
        self.cover.bind_visibility_from(
            self.manager, "selected_ap_obj", backward=lambda s: s == None
        )

        with self.cover:
            ui.label("Waiting for client data....").classes("black text-2xl")
        Updates.REGISTER_TIMER_CALLBACK(ui.context.client.id, self._update_plot)

    def update_scanners(self, args=None):
        if len(self.scanner_states.keys()) == len(self.manager.scanners.keys()):
            return

        # find what changed (client added/removed), update scanner list and plot entries accordingly
        intersect = self.scanner_states.keys() & self.manager.scanners.keys()
        to_remove = [id for id in self.scanner_states.keys()
                     if id not in intersect]
        new_fig_data = list(self.fig.data)
        for id in to_remove:
            self.scanner_states.pop(id)
            for i, data in enumerate(new_fig_data):
                if data.name == id:
                    new_fig_data.pop(i)
                    break
        self.fig.data = new_fig_data

        self.scanner_list.clear()
        with self.scanner_list:
            if len(self.manager.scanners.keys()) == 0:
                ui.label("No clients registered.")
                self.scanner_states.clear()
                return

            for i, id in enumerate(self.manager.scanners):
                if id not in self.scanner_states:
                    self.scanner_states[id] = True
                    self.scanner_colors[id] = plot_cl.qualitative.Plotly[i]
                    if len(self.settings.selected_dir) > 0:
                        self.manager.update_scanner_result_path(
                            id, self.settings.selected_dir
                        )
                    if id not in [data.name for data in self.fig.data]:
                        self.fig.add_scatter(
                            x=[],
                            y=[],
                            marker=dict(color=self.scanner_colors[id]),
                            name=id,
                        )

                scanner = self.manager.scanners[id]

                item = ui.item(
                    on_click=lambda sender, scan_id=id: self._on_scanner_select(
                        sender, scan_id
                    ),
                ).classes("w-full")

                if not self.scanner_states[id]:
                    item.classes.append("opacity-70")

                with item:
                    ui.element().style(
                        f"width: 10px; background-color:{self.scanner_colors[id]}"
                    ).classes("mr-2")
                    with ui.item_section():
                        ui.item_label(f"{id}").classes(
                            f"font-bold text-lg p-2")

                        ui.item_label().bind_text_from(
                            scanner,
                            "stats",
                            lambda s: f"Average RSSI (dBm): {int(s.average) if scanner.state == ScannerState.SCANNER_SCANNING else '--'}",
                        ).props("caption")

                        ui.item_label().bind_text_from(
                            scanner,
                            "stats",
                            lambda s: f"Maximum RSSI (dBm): {int(s.maximum) if scanner.state == ScannerState.SCANNER_SCANNING else '--'}",
                        ).props("caption")

                        ui.item_label().bind_text_from(
                            scanner,
                            "stats",
                            lambda s: f"Minimum RSSI (dBm): {int(s.minimum) if scanner.state == ScannerState.SCANNER_SCANNING else '--'}",
                        ).props("caption")

                    with ui.item_section().props("side"):
                        ui.icon("circle")

                    def _update_scanner_state_icon():
                        for child in self.scanner_list.descendants():
                            if child.props.get("name") == "circle":
                                child.props["color"] = self.scanner_state_colors[
                                    scanner.state
                                ]
                            child.update()

                    _update_scanner_state_icon()
                    Updates.REGISTER_TIMER_CALLBACK(
                        ui.context.client.id, _update_scanner_state_icon
                    )

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
                            ui.label(
                                f"Average RSSI: {scanner.stats.average}\n")

    def _on_scanner_select(self, e: ClickEventArguments, id):
        self.scanner_states[id] = not self.scanner_states[id]

        if self.scanner_states[id] == False:
            e.sender.classes.append("opacity-70")
        else:
            e.sender.classes.remove("opacity-70")
        e.sender.update()
        self._update_plot()

    def _update_plot(self):
        print("UPDATE PLOT")
        # self.fig.data = []
        items = self.manager.scanners.items()
        xaxis = list()
        for id, scanner in items:
            if len(scanner.stats.signal_buf) == 0:
                continue

            if id not in self.scanner_states.keys():
                self.scanner_states[id] = True

            if not self.scanner_states[id]:
                self.fig.update_traces(selector=dict(name=id), visible=False)
                continue

            sig_buf, var_buf, ts_buf = self.manager.fetch_scanner_display_stats(
                id)

            self.fig.update_traces(
                x=ts_buf,
                y=var_buf if self.data_type == 0 else sig_buf,
                visible=True,
                selector=dict(name=id),
            )

            if len(ts_buf) > len(xaxis):
                xaxis = ts_buf

        print(self.saved_layout)
        if len(items) > 0 and not self.saved_layout:
            if len(xaxis) > consts.X_AXIS_SPAN:
                self.fig.update_xaxes(
                    range=[xaxis[-consts.X_AXIS_SPAN], xaxis[-1]])
            elif len(xaxis) > 0:
                self.fig.update_xaxes(range=[xaxis[0], xaxis[-1]])
        elif self.saved_layout:
            self.fig.layout = self.saved_layout

        self.plot.update()

    def change_plot_type(self, data_type: int):
        self.data_type = data_type
        self.saved_layout = None
        match data_type:
            case 0:
                self.fig.update_yaxes(
                    range=[0, consts.Y_VAR_MAX], visible=False, title=None
                )
                # self.saved_layout = self.fig.layout
            case 1:
                self.fig.update_yaxes(
                    range=[-100, 0], visible=True, title=dict(text="RSSI")
                )

                # self.saved_layout = self.fig.layout
        self._update_plot()

    def _on_relayout(self, e: GenericEventArguments):
        print(e.args)
        if "xaxis.range[0]" in e.args:
            self.fig.update_xaxes(
                range=[e.args["xaxis.range[0]"], e.args["xaxis.range[1]"]]
            )
        if "yaxis.range[0]" in e.args:
            self.fig.update_yaxes(
                range=[e.args["yaxis.range[0]"], e.args["yaxis.range[1]"]]
            )
        self.saved_layout = self.fig.layout

    def reset_axes(self):
        self.saved_layout = None
        self.change_plot_type(self.data_type)

    def _reset_data(self):
        for id in self.scanner_states.keys():
            self.scanner_states[id] = True
        for data in self.fig.data:
            print(data.name)
            data.x = []
            data.y = []
        self.reset_axes()

    async def stop_capture(self):
        self.manager.do_capture_stop()
        self.scanner_states.clear()
        await self.manager.mqtt_send(consts.MANAGER_PUB_CMD_STOP)
        self._update_plot()
