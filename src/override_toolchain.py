import os
import platformio

Import("env")

# Override the toolchain path for ESP32-P4
# Use the IDF's Espressif-patched toolchain that supports xesppie
idf_tc_path = os.path.expanduser(
    "~/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin"
)

# Add to the front of PATH
env.PrependENVPath("PATH", idf_tc_path)

print(f"Toolchain path overridden: {idf_tc_path}")
