#
# ESP32-CAM custom SPI camera driver
#

if(CONFIG_ESP32CAM_DRIVER)
zephyr_library()
zephyr_include_directories(.)
zephyr_library_sources(esp32cam.c)
endif()

