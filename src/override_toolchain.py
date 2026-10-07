import glob
import os

Import("env")

# Override the toolchain path for ESP32-P4.
# The prebuilt Arduino libs (framework-arduinoespressif32-libs, built by
# bmorcelli/esp32-arduino-lib-builder for idf-release_v5.5) are compiled with
# Espressif's patched GCC 14.x, which is what supports the `xesppie` extension
# this SoC needs. A mismatched GCC produces errors that look nothing like a
# PATH problem ("unrecognisable command-line option", missing libgcc, cc1plus).
#
# So: prefer the newest installed 14.x toolchain. Only fall back to another
# major version if no 14.x exists, and say so loudly rather than silently.
PREFERRED_MAJOR_MINOR = "14."

tools_root = os.path.expanduser("~/.espressif/tools/riscv32-esp-elf")
candidates = sorted(
    glob.glob(os.path.join(tools_root, "*", "riscv32-esp-elf", "bin"))
)

preferred = [c for c in candidates if PREFERRED_MAJOR_MINOR in os.path.basename(
    os.path.dirname(os.path.dirname(c)))]

if preferred:
    idf_tc_path = preferred[-1]
elif candidates:
    idf_tc_path = candidates[-1]
    print(
        "WARNING: no riscv32-esp-elf GCC %s* found; falling back to %s. "
        "If the build fails with compiler errors, install an ESP-IDF 5.5.x "
        "toolchain (see BUILDING.md step 1)." % (PREFERRED_MAJOR_MINOR, idf_tc_path)
    )
else:
    raise SystemExit(
        "ERROR: no riscv32-esp-elf toolchain found under %s.\n"
        "Install one:\n"
        "  ~/esp/esp-idf-v5.5.x/install.sh\n"
        "or: python3 $IDF_PATH/tools/idf_tools.py install riscv32-esp-elf\n"
        "See BUILDING.md step 1." % tools_root
    )

env.PrependENVPath("PATH", idf_tc_path)

print(f"Toolchain path overridden: {idf_tc_path}")