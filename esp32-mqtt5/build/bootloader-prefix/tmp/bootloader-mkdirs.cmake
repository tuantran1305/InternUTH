# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/Hi/esp/v5.2.2/esp-idf/components/bootloader/subproject"
  "D:/Personal_project/IoT_esp32/esp32-mqtt5/build/bootloader"
  "D:/Personal_project/IoT_esp32/esp32-mqtt5/build/bootloader-prefix"
  "D:/Personal_project/IoT_esp32/esp32-mqtt5/build/bootloader-prefix/tmp"
  "D:/Personal_project/IoT_esp32/esp32-mqtt5/build/bootloader-prefix/src/bootloader-stamp"
  "D:/Personal_project/IoT_esp32/esp32-mqtt5/build/bootloader-prefix/src"
  "D:/Personal_project/IoT_esp32/esp32-mqtt5/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Personal_project/IoT_esp32/esp32-mqtt5/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Personal_project/IoT_esp32/esp32-mqtt5/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
