# Trusted unlock: do not ask for the PIN on trusted networks or moments.
#
# Four artefacts out of two shared static libraries:
#
#   smart_unlock_common   config schema + NetworkManager reads (Qt Core/DBus only)
#   smart_unlock_backend  the editable settings model, over the common lib
#   kcm_smart_unlock.so   the Settings module, inside Preferencias
#   desbloqueo          the standalone application, in the app drawer
#   smart-unlockd         the daemon: watches, decides, arms/disarms the lock
#
# The split mirrors appsvivas and app-usage, and for the same two reasons. The
# app is not a duplicate of the KCM: a KCM is a plugin tied to the KF6 ABI and an
# update can leave it unable to load (kcm_lookandfeel.so on this device already
# is), while the app needs only Qt and Kirigami, so the settings stay reachable.
# And the daemon is QtCore/DBus only -- it runs for days and never draws, so
# linking QtQuick into it would make the cheap thing expensive.
#
# Built natively on the surya, like app-usage:
#   cmake -S . -B build -G Ninja && cmake --build build
cmake_minimum_required(VERSION 3.20)
project(smart-unlock VERSION 1.2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(KF_MIN_VERSION 6.0)
set(QT_MIN_VERSION 6.6)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets DBus)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
	CoreAddons
	Config
	I18n
	KCMUtils
)

qt_standard_project_setup(REQUIRES 6.6)

# --- the shared, UI-less core -----------------------------------------------
# The on-disk schema and the NetworkManager reads. Everything else links this,
# including the daemon, which is why it must not pull in Gui/Qml.
add_library(smart_unlock_common STATIC
	src/config.cpp
	src/config.h
	src/netinfo.cpp
	src/netinfo.h
)
target_include_directories(smart_unlock_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(smart_unlock_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(smart_unlock_common PUBLIC
	Qt::Core
	Qt::DBus
	KF6::ConfigCore
)

# --- the editable settings model, shared by both front-ends ------------------
add_library(smart_unlock_backend STATIC backend.cpp backend.h)
target_include_directories(smart_unlock_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(smart_unlock_backend PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(smart_unlock_backend PUBLIC
	smart_unlock_common
	Qt::Core
	KF6::CoreAddons
)

# --- the Settings module -----------------------------------------------------
# kcmutils_add_qml_kcm() GLOB_RECURSEs ui/, so ui/main.qml and ui/ConfigView.qml
# both become the KCM's QML. App.qml is NOT in ui/ on purpose: it belongs to the
# app, and would otherwise be swept into the module too.
kcmutils_add_qml_kcm(kcm_smart_unlock SOURCES kcm.cpp)
target_link_libraries(kcm_smart_unlock PRIVATE
	smart_unlock_backend
	KF6::I18n
	KF6::KCMUtils
	KF6::KCMUtilsQuick
)

# --- the standalone application ----------------------------------------------
qt_add_executable(smart-unlock main.cpp)

# ConfigView.qml is shared, but one .qml cannot belong to two QML modules:
# qt_add_qml_module stores the URI in source-file properties, so the KCM's copy
# and the app's would fight. Same fix as appsvivas -- a build-time copy for the
# app, with ui/ConfigView.qml staying the one file to edit.
configure_file(ui/ConfigView.qml ${CMAKE_CURRENT_BINARY_DIR}/appqml/ConfigView.qml COPYONLY)
configure_file(App.qml ${CMAKE_CURRENT_BINARY_DIR}/appqml/App.qml COPYONLY)

# Generated files carry an absolute path; Qt refuses to put those in a resource
# without being told their name inside it, or the types would not be reachable
# as SmartUnlock.App / SmartUnlock.ConfigView.
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/appqml/App.qml
	PROPERTIES QT_RESOURCE_ALIAS App.qml)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/appqml/ConfigView.qml
	PROPERTIES QT_RESOURCE_ALIAS ConfigView.qml)

qt_add_qml_module(smart-unlock
	URI SmartUnlock
	VERSION 1.0
	QML_FILES
		${CMAKE_CURRENT_BINARY_DIR}/appqml/App.qml
		${CMAKE_CURRENT_BINARY_DIR}/appqml/ConfigView.qml
)

target_link_libraries(smart-unlock PRIVATE
	smart_unlock_backend
	Qt::Gui
	Qt::Qml
	Qt::Quick
	Qt::QuickControls2
	Qt::Widgets
	KF6::CoreAddons
	KF6::I18n
)

# --- the daemon --------------------------------------------------------------
qt_add_executable(smart-unlockd daemon/main.cpp)
target_link_libraries(smart-unlockd PRIVATE
	smart_unlock_common
	Qt::Core
	Qt::DBus
	KF6::ConfigCore
)

# --- install -----------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS smart-unlock smart-unlockd
	BUNDLE DESTINATION .
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES org.kde.smartunlock.desktop
	DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES org.kde.smartunlock.svg
	DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# Installed disabled. `systemctl --user enable` is the setup script's job, so
# installing the package never starts deciding about the lock behind the user's
# back.
#
# The path is literal 'lib/', NOT ${CMAKE_INSTALL_LIBDIR}. Under a plain QEMU
# build KDEInstallDirs resolves that to lib64, and abuild then refuses the
# package outright ("must not put anything under /usr/lib64"). systemd only ever
# looks in /usr/lib/systemd/user anyway, and Alpine has no lib64, so hardcoding
# it is both correct and the thing that builds either way.
install(FILES smart-unlockd.service
	DESTINATION lib/systemd/user)

install(FILES README.md
	DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/smart-unlock)
