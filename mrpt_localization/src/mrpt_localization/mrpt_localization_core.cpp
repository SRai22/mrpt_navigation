/***********************************************************************************
 * Revised BSD License                                                             *
 * Copyright (c) 2014, Markus Bader <markus.bader@tuwien.ac.at>                    *
 * All rights reserved.                                                            *
 *                                                                                 *
 * Redistribution and use in source and binary forms, with or without              *
 * modification, are permitted provided that the following conditions are met:     *
 *     * Redistributions of source code must retain the above copyright            *
 *       notice, this list of conditions and the following disclaimer.             *
 *     * Redistributions in binary form must reproduce the above copyright         *
 *       notice, this list of conditions and the following disclaimer in the       *
 *       documentation and/or other materials provided with the distribution.      *
 *     * Neither the name of the Vienna University of Technology nor the           *
 *       names of its contributors may be used to endorse or promote products      *
 *       derived from this software without specific prior written permission.     *
 *                                                                                 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND *
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED   *
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE          *
 * DISCLAIMED. IN NO EVENT SHALL Markus Bader BE LIABLE FOR ANY                    *
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES      *
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;    *
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND     *
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT      *
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS   *
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                    *                       *
 ***********************************************************************************/


#include <mrpt_localization/mrpt_localization_core.h>

#include <mrpt/base.h>
#include <mrpt/slam.h>

using namespace mrpt;
using namespace mrpt::slam;
using namespace mrpt::opengl;
using namespace mrpt::math;
using namespace mrpt::system;
using namespace mrpt::utils;
using namespace mrpt::random;
using namespace std;

PFLocalizationCore::~PFLocalizationCore()
{
}

PFLocalizationCore::PFLocalizationCore()
    : state_(NA)
{
}

void PFLocalizationCore::init(){
}


void PFLocalizationCore::initializeFilter(mrpt::utils::CPosePDFGaussian &p){
    mrpt::math::CMatrixDouble33 cov;
    mrpt::math::CPose2D mean_point;
    log_info("InitializeFilter: %4.3fm, %4.3fm, %4.3frad ", mean_point.x(), mean_point.y(), mean_point.phi());
    p.getCovarianceAndMean(cov, mean_point);
    float min_x = mean_point.x()-cov(0,0);
    float max_x = mean_point.x()+cov(0,0);
    float min_y = mean_point.y()-cov(1,1);
    float max_y = mean_point.y()+cov(1,1);
    float min_phi = mean_point.phi()-cov(2,2);
    float max_phi = mean_point.phi()+cov(2,2);
    pdf_.resetUniformFreeSpace( metric_map_.m_gridMaps[0].pointer(), 0.7f,
                                initialParticleCount_ , min_x,max_x, min_y, max_y, min_phi, max_phi);
    state_ = RUN;
}

void PFLocalizationCore::updateFilter(CActionCollectionPtr _action, CSensoryFramePtr _sf) {
    if(state_ == INIT) initializeFilter(initialPose_);
    tictac_.Tic();
    pf_.executeOn( pdf_, _action.pointer(), _sf.pointer(), &pf_stats_ );
    timeLastUpdate_ = _sf->getObservationByIndex(0)->timestamp;
    update_counter_++;
}

void PFLocalizationCore::observation(mrpt::slam::CSensoryFramePtr _sf, mrpt::slam::CObservationOdometryPtr _odometry) {
    
    mrpt::slam::CActionCollectionPtr action = mrpt::slam::CActionCollection::Create();
    mrpt::slam::CActionRobotMovement2D odom_move;
    odom_move.timestamp = _sf->getObservationByIndex(0)->timestamp;
    if(_odometry) {
        if(odomLastObservation_.empty()) {
            odomLastObservation_ = _odometry->odometry;
        }
        mrpt::poses::CPose2D incOdoPose = _odometry->odometry - odomLastObservation_;
        odomLastObservation_ = _odometry->odometry;
        odom_move.computeFromOdometry(incOdoPose, odom_params_);
        action->insert(odom_move);
    } else {
        log_info("No odometry at update %4i -> using dummy", update_counter_);
        odom_move.computeFromOdometry(CPose2D(0,0,0), odom_params_dummy_);
        action->insert(odom_move);
    }
    updateFilter(action, _sf);
}