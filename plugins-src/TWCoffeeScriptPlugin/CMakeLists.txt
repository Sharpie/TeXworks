# Build CoffeeScript Plugin
# =========================

INCLUDE_DIRECTORIES(
  ${TeXworks_SOURCE_DIR}/src
  ${QT_INCLUDE_DIR}
)

LINK_DIRECTORIES(${QT_LIBRARY_DIR})

# **NOTE:**
# _Re-moccing headers from the main source directory that have already been
# mocced during the build of the main program seems redundant, but I'm not wise
# enough in the ways of Qt and CMake to figure out a good alternative._
QT4_WRAP_CPP(COFFEESCRIPT_PLUGIN_MOC ${TeXworks_SCRIPT_API_H} TWCoffeeScriptPlugin.h)
QT4_ADD_RESOURCES(COFFEESCRIPT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/TWCoffeeScriptPlugin.qrc)

ADD_LIBRARY(TWCoffeeScriptPlugin SHARED
  TWCoffeeScriptPlugin.cpp
  ${TeXworks_SCRIPT_API}
  ${COFFEESCRIPT_PLUGIN_MOC}
  ${COFFEESCRIPT_RESOURCES}
)
TARGET_LINK_LIBRARIES(TWCoffeeScriptPlugin ${QT_LIBRARIES})

INSTALL(TARGETS TWCoffeeScriptPlugin
  LIBRARY DESTINATION ${TeXworks_PLUGIN_DIR}
)
