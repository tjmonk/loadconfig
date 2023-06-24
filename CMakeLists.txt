cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(loadconfig
	VERSION 0.1
    DESCRIPTION "Configuration Management utility"
)

add_executable( ${PROJECT_NAME}
	src/loadconfig.c
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE inc
)

target_link_libraries( ${PROJECT_NAME}
	${CMAKE_THREAD_LIBS_INIT}
	rt
	varserver
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

file(GLOB test_files "test/*.cfg")

install( FILES ${test_files}
	DESTINATION /etc/loadconfig
)