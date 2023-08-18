/***********************************************************************************
 * Revised BSD License *
 * Copyright (c) 2014, Markus Bader <markus.bader@tuwien.ac.at> *
 * All rights reserved. *
 *                                                                                 *
 * Redistribution and use in source and binary forms, with or without *
 * modification, are permitted provided that the following conditions are met: *
 *     * Redistributions of source code must retain the above copyright *
 *       notice, this list of conditions and the following disclaimer. *
 *     * Redistributions in binary form must reproduce the above copyright *
 *       notice, this list of conditions and the following disclaimer in the *
 *       documentation and/or other materials provided with the distribution. *
 *     * Neither the name of the Vienna University of Technology nor the *
 *       names of its contributors may be used to endorse or promote products *
 *       derived from this software without specific prior written permission. *
 *                                                                                 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *AND *
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 **
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE *
 * DISCLAIMED. IN NO EVENT SHALL Markus Bader BE LIABLE FOR ANY *
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES *
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 **
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND *
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 **
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **                       *
 ***********************************************************************************/

#pragma once

#include <mrpt/config/CConfigFile.h>
#include <mrpt/maps/CMultiMetricMap.h>
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "nav_msgs/srv/get_map.hpp"
#include "nav_msgs/msg/map_meta_data.hpp"
#include "rclcpp/rclcpp.hpp"

class MapServer : public rclcpp::Node
{
public:
  MapServer();
  ~MapServer();
  void init();
  void loop();

private:
  // member variables
  double m_frequency{0}; //!< rate at which the ros map is published
  bool m_debug{true}; //!< boolean flag for debugging
  // params that come from launch file
  std::string m_pub_metadata_str; //!< param name for map metadata publisher 
  std::string m_pub_map_ros_str; //!< param name for the map publisher
  std::string m_service_map_str; //!< param name for the map service
  // publishers and services
  rclcpp::Publisher<nav_msgs::msg::OccupancyGrid>::SharedPtr m_pub_map_ros; //!< publisher for map in ros format
  rclcpp::Publisher<nav_msgs::msg::MapMetaData>::SharedPtr m_pub_metadata; //!< publisher for map metadata
  rclcpp::Service<nav_msgs::srv::GetMap>::SharedPtr m_service_map; //!< service for map server
  nav_msgs::srv::GetMap::Response m_response_ros; //!< response from the map server 

  mrpt::maps::CMultiMetricMap::Ptr m_metric_map; //!< DS to hold the map in MRPT world

  void publish_map();

  bool map_callback(
      const std::shared_ptr<nav_msgs::srv::GetMap::Request> req,
      const std::shared_ptr<nav_msgs::srv::GetMap::Response> res);
};