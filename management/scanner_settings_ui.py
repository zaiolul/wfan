from nicegui import ui
import consts

class ScannerSettings:
    def __init__(self, band, path):
        self.chans_24 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]
        self.chans_5 = [36, 40, 44, 48]
        self.selected_chans: list[int] = self.chans_24
        self.selected_dir: str = path
        self.selected_band: int = band
        self.mqtt_ip: str = "127.0.0.1"
        self.mqtt_port: int = 1883
        self.mqtt_user: str = ""
        self.mqtt_password : str = ""

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
                    case (
                        "chans"
                    ):  # TODO actually validate range, now possible to pass illegal val
                        chans = []
                        for chan in parts[1].split(","):
                            chans.append(int(chan))
                    case "results_dir":
                        results_dir = parts[1].rstrip()
                    case "mqtt_ip":
                        self.mqtt_ip = parts[1].rstrip()
                    case "mqtt_port":
                        self.mqtt_port = int(parts[1].rstrip())
                    case "mqtt_user":
                        self.mqtt_user = parts[1].rstrip()
                    case "mqtt_password":
                        self.mqtt_password = parts[1].rstrip()
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
            f.write(f"mqtt_ip={self.mqtt_ip}\n")
            f.write(f"mqtt_port={self.mqtt_port}\n")
            f.write(f"mqtt_user={self.mqtt_user}\n")
            f.write(f"mqtt_password={self.mqtt_password}\n")
        ui.notify("Settings saved.")
