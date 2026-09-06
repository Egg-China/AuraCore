cmake_minimum_required(VERSION 3.15)
project(launcher Java)
find_package(Java 1.7 REQUIRED COMPONENTS Development)

include(UseJava)
set(CMAKE_JAVA_JAR_ENTRY_POINT JavaCheck)
set(CMAKE_JAVA_COMPILE_FLAGS --release 8 -Xlint:deprecation -Xlint:unchecked)

set(SRC
    JavaCheck.java
)

add_jar(JavaCheck ${SRC})
install_jar(JavaCheck DESTINATION jars)
# Keep the canonical jar bundle adjacent to the backend binaries during
# development; CoreApplication::getJarPath probes <appdir>/../jars.
add_custom_command(TARGET JavaCheck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/JavaCheck.jar" "${CMAKE_BINARY_DIR}/jars/JavaCheck.jar")
