Import("env")

from pathlib import Path

# ESP32-S3 TinyUSB device and USB-Serial/JTAG share one PHY.
# Board defaults enable secondary USB-Serial/JTAG console, which select()s
# CONFIG_USJ_ENABLE_USB_SERIAL_JTAG. Patch sdkconfig (not sdkconfig.h) only.

REPLACEMENTS = [
    (
        "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y",
        "# CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG is not set",
    ),
    (
        "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y",
        "# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED is not set",
    ),
    (
        "CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y",
        "# CONFIG_USJ_ENABLE_USB_SERIAL_JTAG is not set",
    ),
]


def _ensure_secondary_none(text: str) -> str:
    if "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y" in text:
        return text
    needle = "# CONFIG_ESP_CONSOLE_SECONDARY_NONE is not set"
    if needle in text:
        return text.replace(needle, "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y", 1)
    return text + "\nCONFIG_ESP_CONSOLE_SECONDARY_NONE=y\n"


def _patch_sdkconfig(path: Path) -> None:
    if not path.exists() or path.suffix == ".h":
        return
    text = path.read_text()
    orig = text
    for old, new in REPLACEMENTS:
        text = text.replace(old, new)
    text = _ensure_secondary_none(text)
    if text != orig:
        path.write_text(text)
        print(f"[disable_usj] patched {path.name}")


proj = Path(env["PROJECT_DIR"])
_patch_sdkconfig(proj / "sdkconfig.esp32s3")
_patch_sdkconfig(proj / "sdkconfig")
