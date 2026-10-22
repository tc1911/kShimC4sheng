#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_build=${KSHIM_BUS_TEST_BUILD_DIR:-"$task_root/build/qcom-bus-tests"}
cmake -S "$task_root/tests" -B "$task_build" -DKSHIM_TEST_SANITIZERS=ON
cmake --build "$task_build" --target qcom_bus_test qcom_touch_platform_test hdk8250_touch_platform_test -j8
ctest --test-dir "$task_build" --output-on-failure -R '^(qcom_bus_test|qcom_touch_platform_test|hdk8250_touch_platform_test)$'
