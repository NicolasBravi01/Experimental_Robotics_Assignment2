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

#include <plansys2_pddl_parser/Utils.h>

#include <memory>

#include "plansys2_msgs/msg/action_execution_info.hpp"
#include "plansys2_msgs/msg/plan.hpp"

#include "plansys2_domain_expert/DomainExpertClient.hpp"
#include "plansys2_executor/ExecutorClient.hpp"
#include "plansys2_planner/PlannerClient.hpp"
#include "plansys2_problem_expert/ProblemExpertClient.hpp"
#include <std_msgs/msg/int32.hpp>
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

class PatrollingController : public rclcpp::Node
{
public:
  PatrollingController()
  : rclcpp::Node("patrolling_controller"), state_(STARTING)
  {
  }

  void init()
  {
    domain_expert_ = std::make_shared<plansys2::DomainExpertClient>();
    planner_client_ = std::make_shared<plansys2::PlannerClient>();
    problem_expert_ = std::make_shared<plansys2::ProblemExpertClient>();
    executor_client_ = std::make_shared<plansys2::ExecutorClient>();
    init_knowledge();
  }

  void init_knowledge()
  {
    problem_expert_->addInstance(plansys2::Instance{"r2d2", "robot"});
    problem_expert_->addInstance(plansys2::Instance{"wp_control", "waypoint"});
    problem_expert_->addInstance(plansys2::Instance{"wp1", "waypoint"});
    problem_expert_->addInstance(plansys2::Instance{"wp2", "waypoint"});
    problem_expert_->addInstance(plansys2::Instance{"wp3", "waypoint"});
    problem_expert_->addInstance(plansys2::Instance{"wp4", "waypoint"});

    problem_expert_->addPredicate(plansys2::Predicate("(robot_at r2d2 wp_control)"));
    problem_expert_->addPredicate(plansys2::Predicate("(connected wp_control wp1)"));
    problem_expert_->addPredicate(plansys2::Predicate("(connected wp1 wp2)"));
    problem_expert_->addPredicate(plansys2::Predicate("(connected wp2 wp3)"));
    problem_expert_->addPredicate(plansys2::Predicate("(connected wp3 wp4)"));

    problem_expert_->addPredicate(plansys2::Predicate("(connected wp4 wp1)"));
    problem_expert_->addPredicate(plansys2::Predicate("(connected wp4 wp3)"));
    problem_expert_->addPredicate(plansys2::Predicate("(connected wp3 wp2)"));

    subscriber_ = this->create_subscription<std_msgs::msg::Int32>(
      "aruco_marker_id", 10, std::bind(&PatrollingController::subscriberCallback, this, std::placeholders::_1));

    received_value_ = -1;
  }

  void subscriberCallback(const std_msgs::msg::Int32::SharedPtr msg)
  {
    RCLCPP_INFO(this->get_logger(), "Received value: %d", msg->data);
    received_value_ = msg->data;
  }

  void step()
  {
    switch (state_) {
      case STARTING:
        {
          // Set the goal for next state
          problem_expert_->setGoal(plansys2::Goal("(and (robot_at r2d2 wp4) (patrolled wp1) (patrolled wp2) (patrolled wp3) (patrolled wp4))"));

          // Compute the plan
          auto domain = domain_expert_->getDomain();
          auto problem = problem_expert_->getProblem();
          auto plan = planner_client_->getPlan(domain, problem);

          if (!plan.has_value()) {
            std::cout << "Could not find plan to reach goal " <<
              parser::pddl::toString(problem_expert_->getGoal()) << std::endl;
            break;
          }

          // Execute the plan
          if (executor_client_->start_plan_execution(plan.value())) {
            state_ = PATROL_FINISHED;
          }
        }
        break;
      case PATROL_FINISHED:
        {
          auto feedback = executor_client_->getFeedBack();

          for (const auto & action_feedback : feedback.action_execution_status) {
            std::cout << "[" << action_feedback.action << " " <<
              action_feedback.completion * 100.0 << "%]";
          }
          std::cout << std::endl;

          if (!executor_client_->execute_and_check_plan() && executor_client_->getResult()) {
            if (executor_client_->getResult().value().success) {
              std::cout << "Successful finished " << std::endl;

              // Cleaning up
              problem_expert_->removePredicate(plansys2::Predicate("(patrolled wp1)"));
              problem_expert_->removePredicate(plansys2::Predicate("(patrolled wp2)"));
              problem_expert_->removePredicate(plansys2::Predicate("(patrolled wp3)"));
              problem_expert_->removePredicate(plansys2::Predicate("(patrolled wp4)"));
              
              switch (received_value_) {
                case 0:
                  // Set the goal for next state
                  problem_expert_->setGoal(plansys2::Goal("(and(robot_at r2d2 wp1))"));
                  break;
                case 1:
                  problem_expert_->setGoal(plansys2::Goal("(and(robot_at r2d2 wp2))"));
                  break;
                case 2:
                  problem_expert_->setGoal(plansys2::Goal("(and(robot_at r2d2 wp3))"));
                  break;
                case 3:
                  problem_expert_->setGoal(plansys2::Goal("(and(robot_at r2d2 wp4))"));
                  break;
                default:
                  std::cout << "Invalid state :(" << std::endl;
                  break;
              }
            // Compute the plan
            auto domain = domain_expert_->getDomain();
            auto problem = problem_expert_->getProblem();
            auto plan = planner_client_->getPlan(domain, problem);

            if (!plan.has_value()) {
              std::cout << "Could not find plan to reach goal " <<
                parser::pddl::toString(problem_expert_->getGoal()) << std::endl;
              break;
            }

            // Execute the plan
            if (executor_client_->start_plan_execution(plan.value())) {
              state_ = GO_BACK;
            }

            } else {
              for (const auto & action_feedback : feedback.action_execution_status) {
                if (action_feedback.status == plansys2_msgs::msg::ActionExecutionInfo::FAILED) {
                  std::cout << "[" << action_feedback.action << "] finished with error: " <<
                    action_feedback.message_status << std::endl;
                }
              }

              // Replan
              auto domain = domain_expert_->getDomain();
              auto problem = problem_expert_->getProblem();
              auto plan = planner_client_->getPlan(domain, problem);

              if (!plan.has_value()) {
                std::cout << "Unsuccessful replan attempt to reach goal " <<
                  parser::pddl::toString(problem_expert_->getGoal()) << std::endl;
                break;
              }

              // Execute the plan
              executor_client_->start_plan_execution(plan.value());
            }
          }
        }
        break;
        case GO_BACK:
        {
          auto feedback = executor_client_->getFeedBack();

          for (const auto & action_feedback : feedback.action_execution_status) {
            std::cout << "[" << action_feedback.action << " " <<
              action_feedback.completion * 100.0 << "%]";
          }
          std::cout << std::endl;

          if (!executor_client_->execute_and_check_plan() && executor_client_->getResult()) {
            if (executor_client_->getResult().value().success) {
              std::cout << "Successful finished " << std::endl;

              switch (received_value_) {
                case 0:
                  problem_expert_->removePredicate(plansys2::Predicate("(robot_at r2d2 wp1)"));
                  break;
                case 1:
                  problem_expert_->removePredicate(plansys2::Predicate("(robot_at r2d2 wp2)"));
                  break;
                case 2:
                  problem_expert_->removePredicate(plansys2::Predicate("(robot_at r2d2 wp3)"));
                  break;
                case 3:
                  problem_expert_->removePredicate(plansys2::Predicate("(robot_at r2d2 wp4)"));
                  break;
                default:
                  std::cout << "Invalid state :(" << std::endl;
                  break;
              }

            } else {
              for (const auto & action_feedback : feedback.action_execution_status) {
                if (action_feedback.status == plansys2_msgs::msg::ActionExecutionInfo::FAILED) {
                  std::cout << "[" << action_feedback.action << "] finished with error: " <<
                    action_feedback.message_status << std::endl;
                }
              }

              // Replan
              auto domain = domain_expert_->getDomain();
              auto problem = problem_expert_->getProblem();
              auto plan = planner_client_->getPlan(domain, problem);

              if (!plan.has_value()) {
                std::cout << "Unsuccessful replan attempt to reach goal " <<
                  parser::pddl::toString(problem_expert_->getGoal()) << std::endl;
                break;
              }

              // Execute the plan
              executor_client_->start_plan_execution(plan.value());
            }
          }
        }
      default:
        break;
    }
    
  }

private:
  typedef enum {STARTING, PATROL_FINISHED, GO_BACK} StateType;
  StateType state_;

  std::shared_ptr<plansys2::DomainExpertClient> domain_expert_;
  std::shared_ptr<plansys2::PlannerClient> planner_client_;
  std::shared_ptr<plansys2::ProblemExpertClient> problem_expert_;
  std::shared_ptr<plansys2::ExecutorClient> executor_client_;
  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr subscriber_; 
  int32_t received_value_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<PatrollingController>();

  node->init();

  rclcpp::Rate rate(5);
  while (rclcpp::ok()) {
    node->step();

    rate.sleep();
    rclcpp::spin_some(node->get_node_base_interface());
  }

  rclcpp::shutdown();

  return 0;
}