/*********************************************************************************
 *  Copyright (c) 2015, The Underwater Robotics Team
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *********************************************************************************/

#include "riptide_controllers/thruster_controller.h"

#define debug
#undef report
#undef progress

tf::Matrix3x3 rotation_matrix;
tf::Vector3 ang_v;

// Thrust limits (N):
double MIN_THRUST = -5.0;
double MAX_THRUST = 5.0;

// Vehicle mass (kg):
double MASS = 34.47940950;

// Moments of inertia (kg*m^3)
double Ixx = 1.335;
double Iyy = 1.501;
double Izz = 0.6189;

// Acceleration commands (m/s^2):
double cmdSurge = 0.0;
double cmdSway = 0.0;
double cmdHeave = 0.0;
double cmdRoll = 0.0;
double cmdPitch = 0.0;
double cmdYaw = 0.0;

struct vector
{
  double x;
  double y;
  double z;
};

vector v_imu;

void get_transform(vector *v, tf::StampedTransform *tform)
{
  v->x = tform->getOrigin().x();
  v->y = tform->getOrigin().y();
  v->z = tform->getOrigin().z();
  return;
}

/*** Thruster Positions ***/
// Positions are in meters relative to the center of mass.
vector pos_surge_stbd_hi;
vector pos_surge_port_hi;
vector pos_surge_port_lo;
vector pos_surge_stbd_lo;
vector pos_sway_fwd;
vector pos_sway_aft;
vector pos_heave_port_aft;
vector pos_heave_stbd_aft;
vector pos_heave_stbd_fwd;
vector pos_heave_port_fwd;

/*** EQUATIONS ***/
// These equations solve for linear/angular acceleration in all axes

// Linear Equations
struct surge
{
  template <typename T>
  bool operator()(const T *const surge_port_hi, const T *const surge_stbd_hi, const T *const surge_port_lo,
                  const T *const surge_stbd_lo, const T *const sway_fwd, const T *const sway_aft,
                  const T *const heave_port_fwd, const T *const heave_stbd_fwd, const T *const heave_port_aft,
                  const T *const heave_stbd_aft, T *residual) const
  {
    residual[0] =
        (rotation_matrix.getRow(0).x() * (surge_port_lo[0] + surge_stbd_lo[0]) +
         rotation_matrix.getRow(0).z() *
             (heave_port_fwd[0] + heave_stbd_fwd[0])) /
            T(MASS) -
        T(cmdSurge);
    return true;
  }
};

struct sway
{
  template <typename T>
  bool operator()(const T *const surge_port_hi, const T *const surge_stbd_hi, const T *const surge_port_lo,
                  const T *const surge_stbd_lo, const T *const sway_fwd, const T *const sway_aft,
                  const T *const heave_port_fwd, const T *const heave_stbd_fwd, const T *const heave_port_aft,
                  const T *const heave_stbd_aft, T *residual) const
  {
    residual[0] =
        (rotation_matrix.getRow(1).x() * (surge_port_lo[0] + surge_stbd_lo[0]) +
         rotation_matrix.getRow(1).z() *
             (heave_port_fwd[0] + heave_stbd_fwd[0])) /
            T(MASS) -
        T(cmdSway);
    return true;
  }
};

struct heave
{
  template <typename T>
  bool operator()(const T *const surge_port_hi, const T *const surge_stbd_hi, const T *const surge_port_lo,
                  const T *const surge_stbd_lo, const T *const sway_fwd, const T *const sway_aft,
                  const T *const heave_port_fwd, const T *const heave_stbd_fwd, const T *const heave_port_aft,
                  const T *const heave_stbd_aft, T *residual) const
  {
    residual[0] =
        (rotation_matrix.getRow(2).x() * (surge_port_lo[0] + surge_stbd_lo[0]) +
         rotation_matrix.getRow(2).z() *
             (heave_port_fwd[0] + heave_stbd_fwd[0])) /
            T(MASS) -
        T(cmdHeave);
    return true;
  }
};

// Angular equations
struct roll
{
  template <typename T>
  bool operator()(const T *const sway_fwd, const T *const sway_aft, const T *const heave_port_fwd,
                  const T *const heave_stbd_fwd, const T *const heave_port_aft, const T *const heave_stbd_aft,
                  T *residual) const
  {
    residual[0] = (heave_port_fwd[0] * T(pos_heave_port_fwd.y) + heave_stbd_fwd[0] * T(pos_heave_stbd_fwd.y) +
                   T(Iyy) * T(ang_v.y()) * T(ang_v.z()) - T(Izz) * T(ang_v.y()) * T(ang_v.z())) /
                      T(Ixx) -
                  T(cmdRoll);
    return true;
  }
};

struct pitch
{
  template <typename T>
  bool operator()(const T *const surge_port_hi, const T *const surge_stbd_hi, const T *const surge_port_lo,
                  const T *const surge_stbd_lo, const T *const heave_port_fwd, const T *const heave_stbd_fwd,
                  const T *const heave_port_aft, const T *const heave_stbd_aft, T *residual) const
  {
    residual[0] = (
                   surge_port_lo[0] * T(pos_surge_port_lo.z) + surge_stbd_lo[0] * T(pos_surge_stbd_lo.z) +
                   heave_port_fwd[0] * T(-pos_heave_port_fwd.x) + heave_stbd_fwd[0] * T(-pos_heave_stbd_fwd.x) +
                   T(Izz) * T(ang_v.x()) * T(ang_v.z()) - T(Ixx) * T(ang_v.x()) * T(ang_v.z())) /
                      T(Iyy) -
                  T(cmdPitch);
    return true;
  }
};

struct yaw
{
  template <typename T>
  bool operator()(const T *const surge_port_hi, const T *const surge_stbd_hi, const T *const surge_port_lo,
                  const T *const surge_stbd_lo, const T *const sway_fwd, const T *const sway_aft, T *residual) const
  {
    residual[0] = (
                   surge_port_lo[0] * T(-pos_surge_port_lo.y) + surge_stbd_lo[0] * T(-pos_surge_stbd_lo.y) +
                   T(Ixx) * T(ang_v.x()) * T(ang_v.y()) - T(Iyy) * T(ang_v.x()) * T(ang_v.y())) /
                      T(Izz) -
                  T(cmdYaw);
    return true;
  }
};

int main(int argc, char **argv)
{
  ros::init(argc, argv, "thruster_controller");
  tf::TransformListener tf_listener;
  ThrusterController thruster_controller(argv, &tf_listener);
  thruster_controller.loop();
}

ThrusterController::ThrusterController(char **argv, tf::TransformListener *listener_adr)
{
  rotation_matrix.setIdentity();
  ang_v.setZero();

  listener = listener_adr;

  thrust.header.frame_id = "base_link";

  state_sub = nh.subscribe<sensor_msgs::Imu>("state/imu", 1, &ThrusterController::state, this);
  cmd_sub = nh.subscribe<geometry_msgs::Accel>("command/accel", 1, &ThrusterController::callback, this);
  cmd_pub = nh.advertise<riptide_msgs::ThrustStamped>("command/thrust", 1);

  listener->waitForTransform("/base_link", "/surge_port_hi_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/surge_port_hi_link", ros::Time(0), tf_surge[0]);
  listener->waitForTransform("/base_link", "/surge_stbd_hi_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/surge_stbd_hi_link", ros::Time(0), tf_surge[1]);
  listener->waitForTransform("/base_link", "/surge_port_lo_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/surge_port_lo_link", ros::Time(0), tf_surge[2]);
  listener->waitForTransform("/base_link", "/surge_stbd_lo_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/surge_stbd_lo_link", ros::Time(0), tf_surge[3]);
  listener->waitForTransform("/base_link", "/sway_fwd_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/sway_fwd_link", ros::Time(0), tf_sway[0]);
  listener->waitForTransform("/base_link", "/sway_aft_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/sway_aft_link", ros::Time(0), tf_sway[1]);
  listener->waitForTransform("/base_link", "/heave_port_fwd_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/heave_port_fwd_link", ros::Time(0), tf_heave[0]);
  listener->waitForTransform("/base_link", "/heave_stbd_fwd_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/heave_stbd_fwd_link", ros::Time(0), tf_heave[1]);
  listener->waitForTransform("/base_link", "/heave_port_aft_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/heave_port_aft_link", ros::Time(0), tf_heave[2]);
  listener->waitForTransform("/base_link", "/heave_stbd_aft_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/heave_stbd_aft_link", ros::Time(0), tf_heave[3]);

  tf::StampedTransform tf_imu;

  listener->waitForTransform("/base_link", "/imu_one_link", ros::Time(0), ros::Duration(10.0));
  listener->lookupTransform("/base_link", "/imu_one_link", ros::Time(0), tf_imu);

  get_transform(&v_imu, &tf_imu);

  get_transform(&pos_surge_port_hi, &tf_surge[0]);
  get_transform(&pos_surge_stbd_hi, &tf_surge[1]);
  get_transform(&pos_surge_port_lo, &tf_surge[2]);
  get_transform(&pos_surge_stbd_lo, &tf_surge[3]);
  get_transform(&pos_sway_fwd, &tf_sway[0]);
  get_transform(&pos_sway_aft, &tf_sway[1]);
  get_transform(&pos_heave_port_fwd, &tf_heave[0]);
  get_transform(&pos_heave_stbd_fwd, &tf_heave[1]);
  get_transform(&pos_heave_port_aft, &tf_heave[2]);
  get_transform(&pos_heave_stbd_aft, &tf_heave[3]);

  google::InitGoogleLogging(argv[0]);

  // PROBLEM SETUP

  // Add residual blocks (equations)

  // Linear
  problem.AddResidualBlock(new ceres::AutoDiffCostFunction<surge, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1>(new surge), NULL,
                           &surge_port_hi, &surge_stbd_hi, &surge_port_lo, &surge_stbd_lo, &sway_fwd, &sway_aft,
                           &heave_port_fwd, &heave_stbd_fwd, &heave_port_aft, &heave_stbd_aft);
  problem.AddResidualBlock(new ceres::AutoDiffCostFunction<sway, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1>(new sway), NULL,
                           &surge_port_hi, &surge_stbd_hi, &surge_port_lo, &surge_stbd_lo, &sway_fwd, &sway_aft,
                           &heave_port_fwd, &heave_stbd_fwd, &heave_port_aft, &heave_stbd_aft);
  problem.AddResidualBlock(new ceres::AutoDiffCostFunction<heave, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1>(new heave), NULL,
                           &surge_port_hi, &surge_stbd_hi, &surge_port_lo, &surge_stbd_lo, &sway_fwd, &sway_aft,
                           &heave_port_fwd, &heave_stbd_fwd, &heave_stbd_aft, &heave_port_aft);

  // Angular
  problem.AddResidualBlock(new ceres::AutoDiffCostFunction<roll, 1, 1, 1, 1, 1, 1, 1>(new roll), NULL, &sway_fwd,
                           &sway_aft, &heave_port_fwd, &heave_stbd_fwd, &heave_port_aft, &heave_stbd_aft);
  problem.AddResidualBlock(new ceres::AutoDiffCostFunction<pitch, 1, 1, 1, 1, 1, 1, 1, 1, 1>(new pitch), NULL,
                           &surge_port_hi, &surge_stbd_hi, &surge_port_lo, &surge_stbd_lo, &heave_port_fwd,
                           &heave_stbd_fwd, &heave_port_aft, &heave_stbd_aft);
  problem.AddResidualBlock(new ceres::AutoDiffCostFunction<yaw, 1, 1, 1, 1, 1, 1, 1>(new yaw), NULL, &surge_port_hi,
                           &surge_stbd_hi, &surge_port_lo, &surge_stbd_lo, &sway_fwd, &sway_aft);

  // Set constraints (min/max thruster force)

  // Surge thrusters
  problem.SetParameterLowerBound(&surge_port_hi, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&surge_port_hi, 0, MAX_THRUST);

  problem.SetParameterLowerBound(&surge_stbd_hi, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&surge_stbd_hi, 0, MAX_THRUST);

  problem.SetParameterLowerBound(&surge_port_lo, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&surge_port_lo, 0, MAX_THRUST);

  problem.SetParameterLowerBound(&surge_stbd_lo, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&surge_stbd_lo, 0, MAX_THRUST);

  // Sway thrusters
  problem.SetParameterLowerBound(&sway_fwd, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&sway_fwd, 0, MAX_THRUST);

  problem.SetParameterLowerBound(&sway_aft, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&sway_aft, 0, MAX_THRUST);

  // Heave thrusters
  problem.SetParameterLowerBound(&heave_port_fwd, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&heave_port_fwd, 0, MAX_THRUST);

  problem.SetParameterLowerBound(&heave_stbd_fwd, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&heave_stbd_fwd, 0, MAX_THRUST);

  problem.SetParameterLowerBound(&heave_port_aft, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&heave_port_aft, 0, MAX_THRUST);

  problem.SetParameterLowerBound(&heave_stbd_aft, 0, MIN_THRUST);
  problem.SetParameterUpperBound(&heave_stbd_aft, 0, MAX_THRUST);

  // Configure solver
  options.max_num_iterations = 100;
  options.linear_solver_type = ceres::DENSE_QR;

#ifdef progress
  options.minimizer_progress_to_stdout = true;
#endif
}

void ThrusterController::state(const sensor_msgs::Imu::ConstPtr &msg)
{
  tf::Quaternion tf;
  quaternionMsgToTF(msg->orientation, tf);
  rotation_matrix.setRotation(tf.normalized());
  vector3MsgToTF(msg->angular_velocity, ang_v);
}

void ThrusterController::callback(const geometry_msgs::Accel::ConstPtr &a)
{
  cmdSurge = a->linear.x;
  cmdSway = a->linear.y;
  cmdHeave = a->linear.z;

  cmdRoll = a->angular.x;
  cmdPitch = a->angular.y;
  cmdYaw = a->angular.z;

  // These forced initial guesses don't make much of a difference.
  // We currently experience a sort of gimbal lock w/ or w/o them.
  surge_stbd_hi = 0.0;
  surge_port_hi = 0.0;
  surge_port_lo = 0.0;
  surge_stbd_lo = 0.0;
  sway_fwd = 0.0;
  sway_aft = 0.0;
  heave_port_aft = 0.0;
  heave_stbd_aft = 0.0;
  heave_stbd_fwd = 0.0;
  heave_port_fwd = 0.0;

  #ifdef debug
   std::cout << "surge_port_lo transform: " << pos_surge_port_lo.x << ", " << pos_surge_port_lo.y << ", " << pos_surge_port_lo.z << ", " << std::endl;
   std::cout << "surge_stbd_lo transform: " << pos_surge_stbd_lo.x << ", " << pos_surge_stbd_lo.y << ", " << pos_surge_stbd_lo.z << ", " << std::endl;
   std::cout << "heave_port_fwd transform: " << pos_heave_port_fwd.x << ", " << pos_heave_port_fwd.y << ", " << pos_heave_port_fwd.z << ", " << std::endl;
   std::cout << "heave_stbd_fwd transform: " <<  pos_heave_stbd_fwd.x << ", " << pos_heave_stbd_fwd.y << ", " << pos_heave_stbd_fwd.z << ", " << std::endl << std::endl;
  #endif

//
// #ifdef debug
//   std::cout << "Initial surge_stbd_hi = " << surge_stbd_hi << ", surge_port_hi = " << surge_port_hi
//             << ", surge_port_lo = " << surge_port_lo << ", surge_stbd_lo = " << surge_stbd_lo
//             << ", sway_fwd = " << sway_fwd << ", sway_aft = " << sway_aft << ", heave_port_aft = " << heave_port_aft
//             << ", heave_stbd_aft = " << heave_stbd_aft << ", heave_stbd_fwd = " << heave_stbd_fwd
//             << ", heave_port_fwd = " << heave_port_fwd << std::endl;
// #endif

  // Solve all my problems
  ceres::Solve(options, &problem, &summary);

#ifdef report
  std::cout << summary.FullReport() << std::endl;
#endif
//
// #ifdef debug
//   std::cout << "Final surge_stbd_hi = " << surge_stbd_hi << ", surge_port_hi = " << surge_port_hi
//             << ", surge_port_lo = " << surge_port_lo << ", surge_stbd_lo = " << surge_stbd_lo
//             << ", sway_fwd = " << sway_fwd << ", sway_aft = " << sway_aft << ", heave_port_aft = " << heave_port_aft
//             << ", heave_stbd_aft = " << heave_stbd_aft << ", heave_stbd_fwd = " << heave_stbd_fwd
//             << ", heave_port_fwd = " << heave_port_fwd << std::endl;
// #endif

  // Create stamped thrust message
  thrust.header.stamp = ros::Time::now();

  thrust.force.surge_stbd_hi = surge_stbd_hi;
  thrust.force.surge_port_hi = surge_port_hi;
  thrust.force.surge_port_lo = surge_port_lo;
  thrust.force.surge_stbd_lo = surge_stbd_lo;
  thrust.force.sway_fwd = sway_fwd;
  thrust.force.sway_aft = sway_aft;
  thrust.force.heave_port_aft = heave_port_aft;
  thrust.force.heave_stbd_aft = heave_stbd_aft;
  thrust.force.heave_stbd_fwd = heave_stbd_fwd;
  thrust.force.heave_port_fwd = heave_port_fwd;

  cmd_pub.publish(thrust);
}

void ThrusterController::loop()
{
  ros::spin();
}
