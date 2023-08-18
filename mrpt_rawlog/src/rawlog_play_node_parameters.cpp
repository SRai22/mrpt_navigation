/* +------------------------------------------------------------------------+
   |                             mrpt_navigation                            |
   |                                                                        |
   | Copyright (c) 2014-2023, Individual contributors, see commit authors   |
   | See: https://github.com/mrpt-ros-pkg/mrpt_navigation                   |
   | All rights reserved. Released under BSD 3-Clause license. See LICENSE  |
   +------------------------------------------------------------------------+ */

#include "rawlog_play_node.h"
#include "rawlog_play_node_defaults.h"

RawlogPlayNode::ParametersNode::ParametersNode() : Parameters(), node("~")
{
	node.param<double>("rate", rate, RAWLOG_PLAY_NODE_DEFAULT_RATE);
	ROS_INFO("rate: %f", rate);
	node.param<int>(
		"parameter_update_skip", parameter_update_skip,
		RAWLOG_PLAY_NODE_DEFAULT_PARAMETER_UPDATE_SKIP);
	ROS_INFO("parameter_update_skip: %i", parameter_update_skip);
	node.getParam("rawlog_file", rawlog_file);
	ROS_INFO("rawlog_file: %s", rawlog_file.c_str());
	node.param<std::string>("odom_frame", odom_frame, "odom");
	ROS_INFO("odom_frame: %s", odom_frame.c_str());
	node.param<std::string>("base_frame", base_frame, "base_link");
	ROS_INFO("base_frame: %s", base_frame.c_str());
	reconfigureFnc_ = boost::bind(
		&RawlogPlayNode::ParametersNode::callbackParameters, this, _1, _2);
	reconfigureServer_.setCallback(reconfigureFnc_);
}

void RawlogPlayNode::ParametersNode::update(const unsigned long& loop_count)
{
	if (loop_count % parameter_update_skip) return;
	node.getParam("debug", debug);
	if (loop_count == 0) ROS_INFO("debug:  %s", (debug ? "true" : "false"));
}

void RawlogPlayNode::ParametersNode::callbackParameters(
	mrpt_rawlog::RawLogRecordConfig& config, uint32_t level)
{
	using mrpt::obs::CActionRobotMovement2D;

	if (config.motion_noise_type == MOTION_MODEL_GAUSSIAN)
	{
		auto& m = motionModelOptions;

		m.modelSelection = CActionRobotMovement2D::mmGaussian;
		m.gaussianModel.a1 = config.motion_gaussian_alpha_1;
		m.gaussianModel.a2 = config.motion_gaussian_alpha_2;
		m.gaussianModel.a3 = config.motion_gaussian_alpha_3;
		m.gaussianModel.a4 = config.motion_gaussian_alpha_4;
		m.gaussianModel.minStdXY = config.motion_gaussian_alpha_xy;
		m.gaussianModel.minStdPHI = config.motion_gaussian_alpha_phi;

		ROS_INFO("gaussianModel.a1: %f", m.gaussianModel.a1);
		ROS_INFO("gaussianModel.a2: %f", m.gaussianModel.a2);
		ROS_INFO("gaussianModel.a3: %f", m.gaussianModel.a3);
		ROS_INFO("gaussianModel.a4: %f", m.gaussianModel.a4);
		ROS_INFO("gaussianModel.minStdXY: %f", m.gaussianModel.minStdXY);
		ROS_INFO("gaussianModel.minStdPHI: %f", m.gaussianModel.minStdPHI);
	}
}
