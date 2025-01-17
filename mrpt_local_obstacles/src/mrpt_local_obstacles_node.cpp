/***********************************************************************************
 * Revised BSD License *
 * Copyright (c) 2014-2023, Jose-Luis Blanco <jlblanco@ual.es> *
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
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. *
 ***********************************************************************************/

#include <mp2p_icp_filters/FilterDecimateVoxels.h>
#include <mrpt/config/CConfigFile.h>
#include <mrpt/gui/CDisplayWindow3D.h>
#include <mrpt/maps/COccupancyGridMap2D.h>
#include <mrpt/maps/CSimplePointsMap.h>
#include <mrpt/obs/CObservation2DRangeScan.h>
#include <mrpt/obs/CObservationPointCloud.h>
#include <mrpt/obs/CSensoryFrame.h>
#include <mrpt/opengl/CGridPlaneXY.h>
#include <mrpt/opengl/COpenGLScene.h>
#include <mrpt/opengl/CPointCloud.h>
#include <mrpt/opengl/stock_objects.h>
#include <mrpt/ros1bridge/laser_scan.h>
#include <mrpt/ros1bridge/point_cloud2.h>
#include <mrpt/ros1bridge/pose.h>
#include <mrpt/system/CTimeLogger.h>
#include <mrpt/system/string_utils.h>
#include <nav_msgs/Odometry.h>
#include <ros/ros.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/PointCloud2.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2_ros/transform_listener.h>

#include <map>

using namespace mrpt::system;
using namespace mrpt::config;
using namespace mrpt::img;
using namespace mrpt::maps;
using namespace mrpt::obs;

// The ROS node
class LocalObstaclesNode
{
   private:
	struct TAuxInitializer
	{
		TAuxInitializer(int argc, char** argv)
		{
			ros::init(argc, argv, "mrpt_local_obstacles_node");
		}
	};

	CTimeLogger m_profiler;

	TAuxInitializer m_auxinit;	//!< Just to make sure ROS is init first
	ros::NodeHandle m_nh;  //!< The node handle
	ros::NodeHandle m_localn;  //!< "~"

	bool m_show_gui = true;

	std::string m_frameid_reference = "odom";
	std::string m_frameid_robot = "base_link";

	std::string m_topic_local_map_pointcloud = "local_map_pointcloud";

	std::string m_source_topics_2dscan = "scan,laser1";
	std::string m_source_topics_pointclouds = "";

	double m_time_window = 0.20;  //!< [s]can't be smaller than m_publish_period
	double m_publish_period = 0.05;	 //!< [s]

	ros::Timer m_timer_publish;

	// Sensor data:
	struct TInfoPerTimeStep
	{
		CObservation::Ptr observation;
		mrpt::poses::CPose3D robot_pose;
	};
	typedef std::multimap<double, TInfoPerTimeStep> TListObservations;
	TListObservations m_hist_obs;  //!< The history of past observations during
	//! the interest time window.
	boost::mutex m_hist_obs_mtx;

	/** The local maps */
	CSimplePointsMap::Ptr m_localmap_pts = CSimplePointsMap::Create();
	// COccupancyGridMap2D m_localmap_grid;

	/// Used for example to run voxel grid decimation, etc.
	/// Refer to mp2p_icp docs
	mp2p_icp_filters::FilterPipeline m_filter_pipeline;

	std::string m_filter_output_layer_name;	 //!< mp2p_icp output layer name

	mrpt::gui::CDisplayWindow3D::Ptr m_gui_win;

	/** @name ROS pubs/subs
	 *  @{ */
	ros::Publisher m_pub_local_map_pointcloud;

	//!< Subscriber to 2D laser scans
	std::vector<ros::Subscriber> m_subs_2dlaser;

	//!< Subscriber to point cloud sensors
	std::vector<ros::Subscriber> m_subs_pointclouds;

	tf2_ros::Buffer m_tf_buffer;
	tf2_ros::TransformListener m_tf_listener{m_tf_buffer};

	/**  @} */

	/**
	 * @brief Subscribe to a variable number of topics.
	 * @param lstTopics String with list of topics separated with ","
	 * @param subs[in,out] List of subscribers will be here at return.
	 * @return The number of topics subscribed to.
	 */
	template <typename CALLBACK_METHOD_TYPE>
	size_t subscribeToMultipleTopics(
		const std::string& lstTopics, std::vector<ros::Subscriber>& subs,
		CALLBACK_METHOD_TYPE cb)
	{
		std::vector<std::string> lstSources;
		mrpt::system::tokenize(lstTopics, " ,\t\n", lstSources);
		subs.resize(lstSources.size());
		for (size_t i = 0; i < lstSources.size(); i++)
			subs[i] = m_nh.subscribe(lstSources[i], 1, cb, this);
		return lstSources.size();
	}

	/** Callback: On new sensor data
	 */
	void onNewSensor_Laser2D(const sensor_msgs::LaserScanConstPtr& scan)
	{
		CTimeLoggerEntry tle(m_profiler, "onNewSensor_Laser2D");

		ros::Duration timeout(1.0);

		// Get the relative position of the sensor wrt the robot:
		geometry_msgs::TransformStamped sensorOnRobot;
		try
		{
			CTimeLoggerEntry tle2(
				m_profiler, "onNewSensor_Laser2D.lookupTransform_sensor");

			sensorOnRobot = m_tf_buffer.lookupTransform(
				m_frameid_robot, scan->header.frame_id, scan->header.stamp,
				timeout);
		}
		catch (const tf2::TransformException& ex)
		{
			ROS_ERROR("%s", ex.what());
			return;
		}

		// Convert data to MRPT format:
		const mrpt::poses::CPose3D sensorOnRobot_mrpt = [&]() {
			tf2::Transform tx;
			tf2::fromMsg(sensorOnRobot.transform, tx);
			return mrpt::ros1bridge::fromROS(tx);
		}();

		// In MRPT, CObservation2DRangeScan holds both: sensor data +
		// relative pose:
		auto obsScan = CObservation2DRangeScan::Create();
		mrpt::ros1bridge::fromROS(*scan, sensorOnRobot_mrpt, *obsScan);

		ROS_DEBUG(
			"[onNewSensor_Laser2D] %u rays, sensor pose on robot %s",
			static_cast<unsigned int>(obsScan->getScanSize()),
			sensorOnRobot_mrpt.asString().c_str());

		// Get sensor timestamp:
		const double timestamp = scan->header.stamp.toSec();

		// Get robot pose at that time in the reference frame, typ: /odom ->
		// /base_link
		mrpt::poses::CPose3D robotPose;
		try
		{
			CTimeLoggerEntry tle3(
				m_profiler, "onNewSensor_Laser2D.lookupTransform_robot");

			geometry_msgs::TransformStamped robotTfStamp;
			try
			{
				robotTfStamp = m_tf_buffer.lookupTransform(
					m_frameid_reference, m_frameid_robot, scan->header.stamp,
					timeout);
			}
			catch (const tf2::ExtrapolationException& ex)
			{
				ROS_ERROR("%s", ex.what());
				return;
			}

			robotPose = [&]() {
				tf2::Transform tx;
				tf2::fromMsg(robotTfStamp.transform, tx);
				return mrpt::ros1bridge::fromROS(tx);
			}();

			ROS_DEBUG(
				"[onNewSensor_Laser2D] robot pose %s",
				robotPose.asString().c_str());
		}
		catch (const tf2::TransformException& ex)
		{
			ROS_ERROR("%s", ex.what());
			return;
		}

		// Insert into the observation history:
		TInfoPerTimeStep ipt;
		ipt.observation = obsScan;
		ipt.robot_pose = robotPose;

		m_hist_obs_mtx.lock();
		m_hist_obs.insert(
			m_hist_obs.end(), TListObservations::value_type(timestamp, ipt));
		m_hist_obs_mtx.unlock();

	}  // end onNewSensor_Laser2D

	/** Callback: On new sensor data
	 */
	void onNewSensor_PointCloud(const sensor_msgs::PointCloud2::ConstPtr& pts)
	{
		CTimeLoggerEntry tle(m_profiler, "onNewSensor_PointCloud");

		ros::Duration timeout(1.0);
		// Get the relative position of the sensor wrt the robot:
		geometry_msgs::TransformStamped sensorOnRobot;
		try
		{
			CTimeLoggerEntry tle2(
				m_profiler, "onNewSensor_PointCloud.lookupTransform_sensor");

			sensorOnRobot = m_tf_buffer.lookupTransform(
				m_frameid_robot, pts->header.frame_id, pts->header.stamp,
				timeout);
		}
		catch (const tf2::TransformException& ex)
		{
			ROS_ERROR("%s", ex.what());
			return;
		}

		// Convert data to MRPT format:
		const mrpt::poses::CPose3D sensorOnRobot_mrpt = [&]() {
			tf2::Transform tx;
			tf2::fromMsg(sensorOnRobot.transform, tx);
			return mrpt::ros1bridge::fromROS(tx);
		}();

		// In MRPT, CObservation2DRangeScan holds both: sensor data +
		// relative pose:
		auto obsPts = CObservationPointCloud::Create();
		const auto ptsMap = mrpt::maps::CSimplePointsMap::Create();
		obsPts->pointcloud = ptsMap;
		obsPts->sensorPose = sensorOnRobot_mrpt;
		mrpt::ros1bridge::fromROS(*pts, *ptsMap);

		ROS_DEBUG(
			"[onNewSensor_PointCloud] %u points, sensor pose on robot %s",
			static_cast<unsigned int>(ptsMap->size()),
			sensorOnRobot_mrpt.asString().c_str());

		// Get sensor timestamp:
		const double timestamp = pts->header.stamp.toSec();

		// Get robot pose at that time in the reference frame, typ: /odom ->
		// /base_link
		mrpt::poses::CPose3D robotPose;
		try
		{
			CTimeLoggerEntry tle3(
				m_profiler, "onNewSensor_PointCloud.lookupTransform_robot");

			geometry_msgs::TransformStamped robotTfStamp;
			try
			{
				robotTfStamp = m_tf_buffer.lookupTransform(
					m_frameid_reference, m_frameid_robot, pts->header.stamp,
					timeout);
			}
			catch (const tf2::ExtrapolationException& ex)
			{
				ROS_ERROR("%s", ex.what());
				return;
			}

			robotPose = [&]() {
				tf2::Transform tx;
				tf2::fromMsg(robotTfStamp.transform, tx);
				return mrpt::ros1bridge::fromROS(tx);
			}();

			ROS_DEBUG(
				"[onNewSensor_PointCloud] robot pose %s",
				robotPose.asString().c_str());
		}
		catch (const tf2::TransformException& ex)
		{
			ROS_ERROR("%s", ex.what());
			return;
		}

		// Insert into the observation history:
		TInfoPerTimeStep ipt;
		ipt.observation = obsPts;
		ipt.robot_pose = robotPose;

		m_hist_obs_mtx.lock();
		m_hist_obs.insert(
			m_hist_obs.end(), TListObservations::value_type(timestamp, ipt));
		m_hist_obs_mtx.unlock();

	}  // end onNewSensor_Laser2D

	/** Callback: On recalc local map & publish it */
	void onDoPublish(const ros::TimerEvent&)
	{
		CTimeLoggerEntry tle(m_profiler, "onDoPublish");

		// Purge old observations & latch a local copy:
		TListObservations obs;
		{
			CTimeLoggerEntry tle(m_profiler, "onDoPublish.removingOld");
			m_hist_obs_mtx.lock();

			// Purge old obs:
			if (!m_hist_obs.empty())
			{
				const double last_time = m_hist_obs.rbegin()->first;
				TListObservations::iterator it_first_valid =
					m_hist_obs.lower_bound(last_time - m_time_window);
				const size_t nToRemove =
					std::distance(m_hist_obs.begin(), it_first_valid);
				ROS_DEBUG(
					"[onDoPublish] Removing %u old entries, last_time=%lf",
					static_cast<unsigned int>(nToRemove), last_time);
				m_hist_obs.erase(m_hist_obs.begin(), it_first_valid);
			}
			// Local copy in this thread:
			obs = m_hist_obs;
			m_hist_obs_mtx.unlock();
		}

		ROS_DEBUG(
			"Building local map with %u observations.",
			static_cast<unsigned int>(obs.size()));
		if (obs.empty()) return;

		// Build local map(s):
		// -----------------------------------------------
		m_localmap_pts->clear();
		mrpt::poses::CPose3D curRobotPose;
		{
			CTimeLoggerEntry tle2(m_profiler, "onDoPublish.buildLocalMap");

			// Get the latest robot pose in the reference frame (typ: /odom ->
			// /base_link)
			// so we can build the local map RELATIVE to it:
			ros::Duration timeout(1.0);

			try
			{
				geometry_msgs::TransformStamped tx;
				tx = m_tf_buffer.lookupTransform(
					m_frameid_reference, m_frameid_robot, ros::Time(0),
					timeout);

				tf2::Transform tfx;
				tf2::fromMsg(tx.transform, tfx);
				curRobotPose = mrpt::ros1bridge::fromROS(tfx);
			}
			catch (const tf2::ExtrapolationException& ex)
			{
				ROS_ERROR("%s", ex.what());
				return;
			}

			ROS_DEBUG(
				"[onDoPublish] Building local map relative to latest robot "
				"pose: %s",
				curRobotPose.asString().c_str());

			// For each observation: compute relative robot pose & insert obs
			// into map:
			for (TListObservations::const_iterator it = obs.begin();
				 it != obs.end(); ++it)
			{
				const TInfoPerTimeStep& ipt = it->second;

				// Relative pose in the past:
				mrpt::poses::CPose3D relPose(mrpt::poses::UNINITIALIZED_POSE);
				relPose.inverseComposeFrom(ipt.robot_pose, curRobotPose);

				// Insert obs:
				m_localmap_pts->insertObservationPtr(ipt.observation, relPose);

			}  // end for
		}

		// Filtering:
		mrpt::maps::CPointsMap::Ptr filteredPts;

		if (!m_filter_pipeline.empty())
		{
			mp2p_icp::metric_map_t mm;
			mm.layers[mp2p_icp::metric_map_t::PT_LAYER_RAW] = m_localmap_pts;
			mp2p_icp_filters::apply_filter_pipeline(m_filter_pipeline, mm);

			filteredPts = mm.point_layer(m_filter_output_layer_name);
		}
		else
		{
			filteredPts = m_localmap_pts;
		}

		// Publish them:
		if (m_pub_local_map_pointcloud.getNumSubscribers() > 0)
		{
			sensor_msgs::PointCloud2 msg_pts;
			msg_pts.header.frame_id = m_frameid_robot;
			msg_pts.header.stamp = ros::Time(obs.rbegin()->first);

			auto simplPts =
				std::dynamic_pointer_cast<mrpt::maps::CSimplePointsMap>(
					filteredPts);
			ASSERT_(simplPts);

			mrpt::ros1bridge::toROS(*simplPts, msg_pts.header, msg_pts);
			m_pub_local_map_pointcloud.publish(msg_pts);
		}

		// Show gui:
		if (m_show_gui)
		{
			if (!m_gui_win)
			{
				m_gui_win = mrpt::gui::CDisplayWindow3D::Create(
					"LocalObstaclesNode", 800, 600);
				mrpt::opengl::COpenGLScene::Ptr& scene =
					m_gui_win->get3DSceneAndLock();
				scene->insert(mrpt::opengl::CGridPlaneXY::Create());
				scene->insert(
					mrpt::opengl::stock_objects::CornerXYZSimple(1.0, 4.0));

				auto gl_obs = mrpt::opengl::CSetOfObjects::Create();
				gl_obs->setName("obstacles");
				scene->insert(gl_obs);

				auto gl_rawpts = mrpt::opengl::CPointCloud::Create();
				gl_rawpts->setName("raw_points");
				gl_rawpts->setPointSize(1.0);
				gl_rawpts->setColor_u8(TColor(0x00ff00));
				scene->insert(gl_rawpts);

				auto gl_pts = mrpt::opengl::CPointCloud::Create();
				gl_pts->setName("final_points");
				gl_pts->setPointSize(3.0);
				gl_pts->setColor_u8(TColor(0x0000ff));
				scene->insert(gl_pts);

				m_gui_win->unlockAccess3DScene();
			}

			auto& scene = m_gui_win->get3DSceneAndLock();
			auto gl_obs = mrpt::ptr_cast<mrpt::opengl::CSetOfObjects>::from(
				scene->getByName("obstacles"));
			ROS_ASSERT(!!gl_obs);
			gl_obs->clear();

			auto glRawPts = mrpt::ptr_cast<mrpt::opengl::CPointCloud>::from(
				scene->getByName("raw_points"));

			auto glFinalPts = mrpt::ptr_cast<mrpt::opengl::CPointCloud>::from(
				scene->getByName("final_points"));

			for (const auto& o : obs)
			{
				const TInfoPerTimeStep& ipt = o.second;
				// Relative pose in the past:
				mrpt::poses::CPose3D relPose(mrpt::poses::UNINITIALIZED_POSE);
				relPose.inverseComposeFrom(ipt.robot_pose, curRobotPose);

				mrpt::opengl::CSetOfObjects::Ptr gl_axis =
					mrpt::opengl::stock_objects::CornerXYZSimple(0.9, 2.0);
				gl_axis->setPose(relPose);
				gl_obs->insert(gl_axis);
			}  // end for

			glRawPts->loadFromPointsMap(m_localmap_pts.get());
			glFinalPts->loadFromPointsMap(filteredPts.get());

			m_gui_win->unlockAccess3DScene();
			m_gui_win->repaint();
		}

	}  // onDoPublish

   public:
	/**  Constructor: Inits ROS system */
	LocalObstaclesNode(int argc, char** argv)
		: m_auxinit(argc, argv), m_nh(), m_localn("~")
	{
		// Load params:
		m_localn.param("show_gui", m_show_gui, m_show_gui);

		m_localn.param(
			"frameid_reference", m_frameid_reference, m_frameid_reference);
		m_localn.param("frameid_robot", m_frameid_robot, m_frameid_robot);

		m_localn.param(
			"topic_local_map_pointcloud", m_topic_local_map_pointcloud,
			m_topic_local_map_pointcloud);

		m_localn.param(
			"source_topics_2dscan", m_source_topics_2dscan,
			m_source_topics_2dscan);
		m_localn.param(
			"source_topics_pointclouds", m_source_topics_pointclouds,
			m_source_topics_pointclouds);

		m_localn.param("time_window", m_time_window, m_time_window);
		m_localn.param("publish_period", m_publish_period, m_publish_period);

		ROS_ASSERT(m_time_window > m_publish_period);
		ROS_ASSERT(m_publish_period > 0);

		// Optional filter pipeline:
		if (const auto fil =
				m_localn.param<std::string>("filter_yaml_file", {});
			!fil.empty())
		{
			m_filter_pipeline =
				mp2p_icp_filters::filter_pipeline_from_yaml_file(fil);

			m_filter_output_layer_name =
				m_localn.param<std::string>("filter_output_layer_name", {});
			ASSERTMSG_(
				!m_filter_output_layer_name.empty(),
				"'filter_yaml_file' param also requires "
				"'filter_output_layer_name'");
		}

		// Init ROS publishers:
		m_pub_local_map_pointcloud = m_nh.advertise<sensor_msgs::PointCloud2>(
			m_topic_local_map_pointcloud, 10);

		// Init ROS subs:
		// Subscribe to one or more laser sources:
		size_t nSubsTotal = 0;
		nSubsTotal += this->subscribeToMultipleTopics(
			m_source_topics_2dscan, m_subs_2dlaser,
			&LocalObstaclesNode::onNewSensor_Laser2D);

		nSubsTotal += this->subscribeToMultipleTopics(
			m_source_topics_pointclouds, m_subs_pointclouds,
			&LocalObstaclesNode::onNewSensor_PointCloud);

		ROS_INFO(
			"Total number of sensor subscriptions: %u\n",
			static_cast<unsigned int>(nSubsTotal));
		ROS_ASSERT_MSG(
			nSubsTotal > 0,
			"*Error* It is mandatory to set at least one source topic for "
			"sensory information!");

		// Local map params:
		m_localmap_pts->insertionOptions.minDistBetweenLaserPoints = 0;
		m_localmap_pts->insertionOptions.also_interpolate = false;

		// Init timers:
		m_timer_publish = m_nh.createTimer(
			ros::Duration(m_publish_period), &LocalObstaclesNode::onDoPublish,
			this);

	}  // end ctor
};	// end class

int main(int argc, char** argv)
{
	LocalObstaclesNode the_node(argc, argv);
	ros::spin();
	return 0;
}
