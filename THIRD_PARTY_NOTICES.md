# Third-party notices

Project dependencies are fetched at build time, not vendored into this source
export. They retain their own copyright notices and licenses. The project's MIT
license does not replace those licenses. For binary/hardware distribution, retain
all required notices and review the exact versions you build, including transitive
dependencies and any configuration-selected bundled code.

| Component | Tested version | License / upstream |
|---|---|---|
| LVGL, desktop | 9.3.0 | MIT — https://github.com/lvgl/lvgl |
| LVGL, firmware | 9.5.0 | MIT — https://components.espressif.com/components/lvgl/lvgl |
| ESP-IDF | 6.0.3 | Primarily Apache-2.0; bundled components have additional licenses — https://github.com/espressif/esp-idf |
| esp_lvgl_port | 2.8.0~1 | Apache-2.0 — https://components.espressif.com/components/espressif/esp_lvgl_port |
| esp_lcd_ili9341 | 2.0.2 | Apache-2.0 — https://components.espressif.com/components/espressif/esp_lcd_ili9341 |
| esp_lcd_touch | 1.2.1 | Apache-2.0 — https://components.espressif.com/components/espressif/esp_lcd_touch |
| cmake_utilities | 0.5.3 | Apache-2.0 — https://components.espressif.com/components/espressif/cmake_utilities |
| esp_lcd_touch_xpt2046 | 1.0.6 | MIT, copyright Mike Dunston — https://github.com/atanisoft/esp_lcd_touch_xpt2046 |
| SDL2 | System-provided | zlib — https://github.com/libsdl-org/SDL |

On some systems SDL2 is implemented by `sdl2-compat` over SDL3; consult the installed
packages for their actual notices. Neither SDL implementation is shipped here.

The firmware component versions and integrity hashes are recorded in
`firmware/esp32-2432S028R/dependencies.lock`. Desktop CMake fetches the v9.3.0 LVGL
release tag. Dependency downloads contain their license texts. Representative
license texts for the tested LVGL releases, XPT2046 driver, and Espressif components
are retained in `third_party/licenses/` as an aid to redistribution. They are not
a complete inventory of every possible linked ESP-IDF/system component and do
not replace reviewing upstream NOTICE files and configuration-specific licenses.

The limits of the project's MIT grant for third-party visual designs and lettering
are explained in [NOTICE.md](NOTICE.md).
