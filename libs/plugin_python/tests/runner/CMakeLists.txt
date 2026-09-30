cmake_minimum_required(VERSION 3.22)

# setting-up the tests for the runner is a bit complex because we need a tons of
# things

# first we configure the tests as with other tests
add_executable(runner-tests EXCLUDE_FROM_ALL)
target_sources(runner-tests PRIVATE main.cpp)
mo2_default_source_group()
mo2_target_sources(runner-tests
	FOLDER src
	PRIVATE
		test_diagnose.cpp
		test_filemapper.cpp
		test_game.cpp
		test_installer.cpp
		test_iplugin.cpp
		test_lifetime.cpp
		test_module_switch.cpp
)
mo2_target_sources(runner-tests
	FOLDER src/mocks
	PRIVATE
		../mocks/DummyFileTree.h
		../mocks/MockOrganizer.h
)
mo2_target_sources(runner-tests
	FOLDER src/plugins
	PRIVATE
		plugins/dummy-diagnose.py
		plugins/dummy-filemapper.py
		plugins/dummy-game.py
		plugins/dummy-installer.py
		plugins/dummy-iplugin.py
		plugins/no-interfaces.py
		plugins/module_switch_a/same_plugin/__init__.py
		plugins/module_switch_a/same_plugin/identity.py
		plugins/module_switch_b/same_plugin/__init__.py
		plugins/module_switch_b/same_plugin/identity.py
		plugins/legacy_instance_a/plugins/configurator.py
		plugins/legacy_instance_a/plugins/data/legacy_dialog.py
		plugins/legacy_instance_b/plugins/configurator.py
		plugins/legacy_instance_b/plugins/data/legacy_dialog.py
		plugins/legacy_instance_without_data/plugins/configurator.py
		plugins/legacy_failed/plugins/configurator.py
		plugins/legacy_failed/plugins/data/legacy_dialog.py
		plugins/legacy_empty/plugins/configurator.py
		plugins/legacy_empty/plugins/data/legacy_dialog.py
)
if(COMMAND mo2_configure_tests)
	mo2_configure_tests(runner-tests NO_MAIN NO_SOURCES WARNINGS 4)
else()
	# Native monorepo builds do not load the Windows-oriented upstream target
	# configuration module. Register this bridge suite directly with CTest.
	find_package(GTest REQUIRED)
	include(GoogleTest)
	set_target_properties(runner-tests PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
	target_link_libraries(runner-tests PRIVATE GTest::gtest GTest::gmock)
	gtest_add_tests(TARGET runner-tests TEST_LIST runner-tests_gtests
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

set_target_properties(runner-tests PROPERTIES FOLDER tests/runner)

# link to runner
target_link_libraries(runner-tests PUBLIC runner)

# linking to Python - this is not required to get proper linking but required so that
# CMake generator variables will lookup appropriate DLLs for Python and update PATH
# accordingly thanks to mo2_configure_tests
target_link_libraries(runner-tests PUBLIC Python::Python)

# add mocks
target_include_directories(runner-tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../mocks)

set(PYLIB_DIR ${CMAKE_CURRENT_BINARY_DIR}/pylibs)
mo2_python_pip_install(runner-tests
	DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/pylibs
	PACKAGES
		pytest
		PyQt${MO2_QT_VERSION_MAJOR}==${MO2_PYQT_VERSION}
		PyQt${MO2_QT_VERSION_MAJOR}-Qt${MO2_QT_VERSION_MAJOR}==${MO2_QT_VERSION})

add_dependencies(runner-tests mobase)

if(WIN32)
	set(PYTHON_PATH_SEPARATOR "\\;")
else()
	set(PYTHON_PATH_SEPARATOR ":")
endif()
set(PYTHONPATH
	"${PYLIB_DIR}${PYTHON_PATH_SEPARATOR}$<TARGET_FILE_DIR:mobase>${PYTHON_PATH_SEPARATOR}${Python_DLL_DIR}${PYTHON_PATH_SEPARATOR}${Python_LIB_DIR}")

set_tests_properties(${runner-tests_gtests}
    PROPERTIES
    ENVIRONMENT "PLUGIN_DIR=${CMAKE_CURRENT_SOURCE_DIR}/plugins;PYTHONPATH=${PYTHONPATH};QT_QPA_PLATFORM=offscreen"
)
