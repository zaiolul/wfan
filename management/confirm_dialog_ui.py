from nicegui import ui


class ConfirmDialog(ui.dialog):
    def __init__(self, title: str):
        super().__init__()
        with self, ui.card().props("flat"):
            ui.label(title).classes("text-2xl text-bold")
            with ui.row().classes("w-full justify-end"):
                ui.button("Yes", on_click=lambda: self.submit(True))
                ui.button("No", on_click=lambda: self.submit(False)).props(
                    "outline"
                )
