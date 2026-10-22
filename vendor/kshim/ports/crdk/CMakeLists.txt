get_filename_component(KSHIM_CRDK_PORT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(KSHIM_CRDK_PROJECT "${KSHIM_CRDK_PORT}/../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CRDK_ROOT "${CMAKE_CURRENT_BINARY_DIR}/source" CACHE INTERNAL "Patched CrDK build copy")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${KSHIM_CRDK_PORT}/UPSTREAM.json"
    "${KSHIM_CRDK_PORT}/patches/0001-cranepkg-driver-fixes.patch"
    "${KSHIM_CRDK_PORT}/patches/0002-freestanding-resources.patch"
    "${KSHIM_CRDK_PORT}/prepare.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${KSHIM_CRDK_PORT}/prepare.py"
    --source "${KSHIM_CRDK_PROJECT}/lib/CrDK" --output "${CRDK_ROOT}"
    RESULT_VARIABLE CRDK_PREPARE_STATUS)
if(NOT CRDK_PREPARE_STATUS EQUAL 0)
    message(FATAL_ERROR "Cannot prepare pinned CrDK and verified patches")
endif()
option(CRDK_SPMI "Build SPMI and PMIC GPIO" ON)
option(CRDK_GPIO "Build TLMM GPIO and Android hardware tables" ON)
option(CRDK_GENI "Build I2C/SPI, clocks and RPMh" ON)
option(CRDK_UART "Build GENI polled console" ON)
option(CRDK_DISPLAY "Build continuous splash reader" ON)
set(CRDK_SOURCES)
if(CRDK_SPMI)
    list(APPEND CRDK_SOURCES Library/SpmiLib/spmi.c Library/SpmiLib/spmi_irq.c
        Library/SpmiLib/pmic_arb_common.c Library/SpmiLib/pmic_arb_v1.c
        Library/SpmiLib/pmic_arb_v2.c Library/SpmiLib/pmic_arb_v3.c
        Library/SpmiLib/pmic_arb_v5.c Library/SpmiLib/pmic_arb_v7.c
        Library/SpmiLib/pmic_arb_v8.c Library/PmicGpioLib/pmic_gpio.c)
endif()
if(CRDK_GPIO OR CRDK_GENI)
    list(APPEND CRDK_SOURCES Library/GpioLib/gpio.c Library/AndroidBindingsLib/android_bindings.c)
endif()
if(CRDK_GENI)
    list(APPEND CRDK_SOURCES Library/GeniSeLib/geni_fifo.c Library/GeniSeLib/geni_resources.c
        Library/CmdDBLib/cmddb.c Library/CmdDBLib/cmddb_bounded.c
        Library/RpmhLib/rpmh.c Library/RpmhLib/rpmh_poll.c
        Library/ClockLib/clock.c Library/ClockLib/gdsc.c Library/ClockLib/clock_poll.c)
endif()
if(CRDK_UART)
    list(APPEND CRDK_SOURCES Library/DebugUartLib/debug_uart_poll.c)
endif()
if(CRDK_DISPLAY)
    list(APPEND CRDK_SOURCES Library/DisplayLib/splash.c)
endif()
list(TRANSFORM CRDK_SOURCES PREPEND "${CRDK_ROOT}/")
add_library(crdk STATIC ${CRDK_SOURCES} crdk_port.c crdk_io.c)
add_library(crdk_spmi ALIAS crdk)
target_compile_definitions(crdk PUBLIC CR_FREESTANDING=1)
target_include_directories(crdk PUBLIC "${CRDK_ROOT}/Include" "${KSHIM_CRDK_PORT}/include")
target_compile_options(crdk PRIVATE -ffunction-sections -fdata-sections)
