cmake_minimum_required(VERSION 3.1)
project(mrpt_local_obstacles)

## Find catkin macros and libraries
## if COMPONENTS list like find_package(catkin REQUIRED COMPONENTS xyz)
## is used, also find other catkin packages
find_package(catkin REQUIRED COMPONENTS
  dynamic_reconfigure
  roscpp
  sensor_msgs
  tf2
  visualization_msgs
  tf2_geometry_msgs
)

## System dependencies are found with CMake's conventions
find_package(mrpt-maps REQUIRED)
find_package(mrpt-obs REQUIRED)
find_package(mrpt-gui REQUIRED)
find_package(mrpt-ros1bridge REQUIRED)

if (CMAKE_COMPILER_IS_GNUCXX)
	# High level of warnings.
	# The -Wno-long-long is required in 64bit systems when including sytem headers.
	# The -Wno-variadic-macros was needed for Eigen3, StdVector.h
	add_compile_options(-Wall -Wno-long-long -Wno-variadic-macros)
	# Workaround: Eigen <3.4 produces *tons* of warnings in GCC >=6. See http://eigen.tuxfamily.org/bz/show_bug.cgi?id=1221
	if (NOT ${CMAKE_CXX_COMPILER_VERSION} LESS "6.0")
		add_compile_options(-Wno-ignored-attributes -Wno-int-in-bool-context)
	endif()
endif()
IF(CMAKE_COMPILER_IS_GNUCXX AND NOT CMAKE_BUILD_TYPE MATCHES "Debug")
  add_compile_options(-O3)
ENDIF()

###################################
## catkin specific configuration ##
###################################
## The catkin_package macro generates cmake config files for your package
## Declare things to be passed to dependent projects
## INCLUDE_DIRS: uncomment this if you package contains header files
## LIBRARIES: libraries you create in this project that dependent projects also need
## CATKIN_DEPENDS: catkin_packages dependent projects also need
## DEPENDS: system dependencies of this project that dependent projects also need
catkin_package(
  LIBRARIES ${PROJECT_NAME}
  CATKIN_DEPENDS dynamic_reconfigure roscpp sensor_msgs tf2 tf2_geometry_msgs visualization_msgs
  # DEPENDS mrpt
)

###########
## Build ##
###########

# TODO: Remove and move as an independent ROS package:
add_subdirectory(submodules/mola-common)
set(mola-common_DIR ${CMAKE_BINARY_DIR} CACHE PATH "path to mola-common-config.cmake" FORCE)

add_subdirectory(submodules/mp2p_icp)

## Specify additional locations of header files
## Your package locations should be listed before other locations
include_directories(
  ${catkin_INCLUDE_DIRS}
)

## Declare a cpp executable
add_executable(${PROJECT_NAME}_node
  src/mrpt_local_obstacles_node.cpp
)

# Specify libraries to link a library or executable target against
target_link_libraries(${PROJECT_NAME}_node
  ${catkin_LIBRARIES}
  mrpt::maps
  mrpt::obs
  mrpt::gui
  mrpt::ros1bridge
  # mp2p_icp
  mp2p_icp_filters
)

#############
## Install ##
#############

# all install targets should use catkin DESTINATION variables
# See http://ros.org/doc/api/catkin/html/adv_user_guide/variables.html

## Mark executables and/or libraries for installation
install(TARGETS ${PROJECT_NAME}_node
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

#############
## Testing ##
#############
