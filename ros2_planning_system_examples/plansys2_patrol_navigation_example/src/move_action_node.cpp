// Copyright 2019 Intelligent Robotics Lab
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <math.h>

#include <memory>
#include <string>
#include <map>
#include <algorithm>

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "geometry_msgs/msg/pose.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"

#include "plansys2_executor/ActionExecutorClient.hpp"

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

using namespace std::chrono_literals;

class MoveAction : public plansys2::ActionExecutorClient
{
public:
  MoveAction()
  : plansys2::ActionExecutorClient("move", 100ms)
  {
    geometry_msgs::msg::PoseStamped wp;
    wp.header.frame_id = "/map";
    wp.header.stamp = now();
    wp.pose.position.x = -7.0;
    wp.pose.position.y = 1.5;
    wp.pose.position.z = 0.0;
    wp.pose.orientation.x = 0.0;
    wp.pose.orientation.y = 0.0;
    wp.pose.orientation.z = 0.0;
    wp.pose.orientation.w = 1.0;
    waypoints_["wp4"] = wp;

    wp.pose.position.x = 6.0;
    wp.pose.position.y = 2.0;
    waypoints_["wp1"] = wp;

    wp.pose.position.x = -3.0;
    wp.pose.position.y = -8.0;
    waypoints_["wp3"] = wp;

    wp.pose.position.x = 7.0;
    wp.pose.position.y = -5.0;
    waypoints_["wp2"] = wp;

    wp.pose.position.x = 2.0;
    wp.pose.position.y = 2.0;
    waypoints_["wp_control"] = wp;

    using namespace std::placeholders;
    pos_sub_ = create_subscription<nav_msgs::msg::Odometry>(
    "/odom",  // Change topic name to /odom
    10,
    std::bind(&MoveAction::current_pos_callback, this, _1)); 

    status_ = 0;
  }

  void current_pos_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    // Extract the pose from the Odometry message
    current_pos_ = msg->pose.pose;  // Pose is contained in the Odometry message
  }

private:
  double getDistance(const geometry_msgs::msg::Pose & pos1, const geometry_msgs::msg::Pose & pos2)
  {
    return sqrt(
      (pos1.position.x - pos2.position.x) * (pos1.position.x - pos2.position.x) +
      (pos1.position.y - pos2.position.y) * (pos1.position.y - pos2.position.y));
  }

void do_work()
{
  if (status_ == 0) {
    send_feedback(0.0, "Move starting");

    // Create the action client
    navigation_action_client_ =
      rclcpp_action::create_client<nav2_msgs::action::NavigateToPose>(
        shared_from_this(),
        "navigate_to_pose");

    // Wait for the action server to be ready
    bool is_action_server_ready = false;
    do {
      RCLCPP_INFO(get_logger(), "Waiting for navigation action server...");
      is_action_server_ready =
        navigation_action_client_->wait_for_action_server(std::chrono::seconds(5));
    } while (!is_action_server_ready);

    RCLCPP_INFO(get_logger(), "Navigation action server ready");

    // Retrieve the target waypoint
    auto wp_to_navigate = get_arguments()[2];
    RCLCPP_INFO(get_logger(), "Start navigation to [%s]", wp_to_navigate.c_str());

    goal_pos_ = waypoints_[wp_to_navigate];
    navigation_goal_.pose = goal_pos_;
    dist_to_move = getDistance(goal_pos_.pose, current_pos_);

    // Set up goal options
    auto send_goal_options =
      rclcpp_action::Client<nav2_msgs::action::NavigateToPose>::SendGoalOptions();

    send_goal_options.feedback_callback = [this](
      NavigationGoalHandle::SharedPtr,
      NavigationFeedback feedback) {
        send_feedback(
          std::min(1.0, std::max(0.0, 1.0 - (feedback->distance_remaining / dist_to_move))),
          "Move running");
      };

    // Send the goal to the action server
    future_navigation_goal_handle_ =
      navigation_action_client_->async_send_goal(navigation_goal_);

    RCLCPP_INFO(get_logger(), "Goal sent to navigation action server");

    // Transition to the next status
    status_ = 1;
  } else if (status_ == 1) {
    // Monitor progress toward the goal
    dist_to_move = getDistance(goal_pos_.pose, current_pos_);
    RCLCPP_INFO(get_logger(), "Reaching goal, distance: %f", dist_to_move);
    if (dist_to_move < 0.3) {
      status_ = 2; // Transition to the completion stage
    }
  } else if (status_ == 2) {
    status_ = 0;
    RCLCPP_INFO(get_logger(), "Goal reached!");
    // Complete the action
    finish(true, 1.0, "Move completed");
  }
}


  std::map<std::string, geometry_msgs::msg::PoseStamped> waypoints_;

  using NavigationGoalHandle =
    rclcpp_action::ClientGoalHandle<nav2_msgs::action::NavigateToPose>;
  using NavigationFeedback =
    const std::shared_ptr<const nav2_msgs::action::NavigateToPose::Feedback>;

  rclcpp_action::Client<nav2_msgs::action::NavigateToPose>::SharedPtr navigation_action_client_;
  std::shared_future<NavigationGoalHandle::SharedPtr> future_navigation_goal_handle_;
  NavigationGoalHandle::SharedPtr navigation_goal_handle_;

  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr pos_sub_;
  geometry_msgs::msg::Pose current_pos_;
  geometry_msgs::msg::PoseStamped goal_pos_;
  nav2_msgs::action::NavigateToPose::Goal navigation_goal_;

  double dist_to_move;
  int status_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<MoveAction>();

  node->set_parameter(rclcpp::Parameter("action_name", "move"));
  node->trigger_transition(lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);

  rclcpp::spin(node->get_node_base_interface());

  rclcpp::shutdown();

  return 0;
}
