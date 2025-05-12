from nicegui import ui, events
from typing import Callable, Awaitable
import asyncio
from manager import Manager
from scanner_settings_ui import ScannerSettings
import json
import consts
from data import ManagerState


class ScannerDialog(ui.dialog):
    def __init__(
        self,
        manager: Manager,
        settings: ScannerSettings,
        on_select_ap: Callable[[], Awaitable[None]],
    ):
        super().__init__()
        self.manager: Manager = manager
        self.settings: ScannerSettings = settings
        self.on_select_ap = on_select_ap
        self.dialog: ui.Dialog = None

        self.scan_task = asyncio.create_task(self.start_scan())
        self.on("hide", self._on_close)

    def _on_close(self):
        if self.scan_task:
            self.scan_task.cancel()
        self.submit(None)

    async def _select_ap(self, e: events.GenericEventArguments):
        ap = e.args
        self.on_select_ap()
        ap["bssid"] = ap["bssid"].lower()
        self.manager.selected_ap_obj = ap
        self.manager.do_capture_start()
        await self.manager.mqtt_send(consts.MANAGER_PUB_CMD_SELECT_AP, json.dumps(ap))
        self.submit(True)

    async def start_scan(self):
        # self.dialog = ui.dialog()
        with self, ui.card().classes("max-h-2/3").style("max-width: none").props("flat bordered"):
            with ui.row().classes("w-full justify-between"):
                ui.label("Select AP").classes("text-2xl ")
                ui.button("Close", on_click=self._on_close).props("flat")
                ui.separator()
            with ui.row().classes("w-full overflow-y-auto justify-center items-center"):
                await self._start_scan()

    async def _start_scan(self):
        self.manager.state = ManagerState.IDLE
        await self.manager.mqtt_send(
            consts.MANAGER_PUB_CMD_SCAN,
            json.dumps({"channels": self.settings.selected_chans}),
        )

        spinner = ui.spinner(size="lg")
        res = await self.manager.common_aps_done(timeout=15)

        if res:
            self._get_data()
        else:
            ui.label("Scan timeout").classes("text-bold text-lg p-4")
            ui.label(
                "Did not receive any response from scanners, ensure they are online and try again."
            ).classes("text-lg p-4")
        spinner.visible = False

    def _get_data(self, args=None):

        rows = [
            {"ssid": ap.ssid, "bssid": ap.bssid.upper(), "channel": ap.channel}
            for ap in self.manager.common_aps
        ]
        columns = [
            {"name": "channel", "label": "SSID",
                "field": "ssid", "sortable": True},
            # , "classes" : "blur-sm hover:blur-none"},
            {"name": "bssid", "label": "BSSID", "field": "bssid", "sortable": True},
            {"name": "channel", "label": "Channel",
                "field": "channel", "sortable": True},
        ]
        rows.sort(key=lambda x: x["channel"])

        table = ui.table(
            rows=rows,
            columns=columns,
            row_key="bssid",
            column_defaults={
                "align": "left",
            }
        ).classes("w-full text-lg")

        # Below is needed for the button in each table row. Really no better way to do it?
        table.add_slot(
            "header",
            r"""
            <q-tr :props="props">
                <q-th v-for="col in props.cols" :key="col.name" :props="props">
                    {{ col.label }}
                </q-th>
                <q-th auto-width />
            </q-tr>
        """,
        )

        table.add_slot(
            "body",
            r"""
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
        """,
        )
        table.add_slot('body-cell-bssid', '''
            <q-td key="bssid" :props="props">
                <q-badge :color="green">
                    {{ props.value }}
                </q-badge>
            </q-td>
        ''')
        table.on("select_ap", self._select_ap)
