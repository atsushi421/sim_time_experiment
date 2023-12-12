#include <chrono>
#include <memory>
#include <deque>
#include <vector>
#include <fstream>
#include <random>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int64.hpp"

#define QOS_HISTORY_SIZE 10

using namespace std::chrono_literals;

class TimerDependencyNode : public rclcpp::Node
{
public:
  TimerDependencyNode(
    std::string node_name, std::string sub_topic_name, std::string pub_topic_name,
    int period_ms)
  : Node(node_name)
  {
    pub_ = create_publisher<std_msgs::msg::Int64>(pub_topic_name, QOS_HISTORY_SIZE);
    sub_ = create_subscription<std_msgs::msg::Int64>(
      sub_topic_name, QOS_HISTORY_SIZE,
      [&](std_msgs::msg::Int64::UniquePtr msg)
      {
        rclcpp::sleep_for(std::chrono::milliseconds(45));
        received_msgs_.push_back(msg->data);
        if (received_msgs_.size() > 10) {
          received_msgs_.pop_front();
        }
      });


    timer_ = create_wall_timer(
      std::chrono::milliseconds(period_ms), [&]()
      {
        rclcpp::sleep_for(std::chrono::milliseconds(45));
        RCLCPP_INFO(this->get_logger(), "Count: '%ld'", count_++);
        while (!received_msgs_.empty()) {
            int64_t msg = received_msgs_.front();
            RCLCPP_INFO(this->get_logger(), "Received message: '%ld'", msg);
            received_msgs_.pop_front();
        }
      });

      log_file_.open("output/1.0/sub_timer_node_log.txt", std::ios::out | std::ios::app);
  }

  ~TimerDependencyNode()
  {
    log_file_.close();
  }

private:
  rclcpp::Publisher<std_msgs::msg::Int64>::SharedPtr pub_;
  rclcpp::Subscription<std_msgs::msg::Int64>::SharedPtr sub_;
  std::deque<int64_t> received_msgs_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::ofstream log_file_;
  int64_t count_;
};

class SubDependencyNode : public rclcpp::Node
{
public:
  SubDependencyNode(
    std::string node_name,
    std::string subsequent_sub_topic_name,
    std::string pub_topic_name
  )
  : Node(node_name)
  {
    sub = create_subscription<std_msgs::msg::Int64>(
      subsequent_sub_topic_name, QOS_HISTORY_SIZE, [&](std_msgs::msg::Int64::UniquePtr msg)
      {
        (void)msg;
        rclcpp::sleep_for(std::chrono::milliseconds(45));
        if (msg_) {
          pub_->publish(std::move(msg_));
        }
      });
    pub_ = create_publisher<std_msgs::msg::Int64>(pub_topic_name, QOS_HISTORY_SIZE);
  }

private:
  rclcpp::Publisher<std_msgs::msg::Int64>::SharedPtr pub_;
  rclcpp::Subscription<std_msgs::msg::Int64>::SharedPtr sub;
  std_msgs::msg::Int64::UniquePtr msg_;
};

class SensorDummy : public rclcpp::Node
{
public:
  SensorDummy(std::string node_name, std::string topic_name, int period_ms)
  : Node(node_name)
  {
    this->declare_parameter<bool>("use_rosbag", false);
    bool use_rosbag = false;
    this->get_parameter("use_rosbag", use_rosbag);
    RCLCPP_INFO(this->get_logger(), "use_rosbag = %d", use_rosbag);
    if (use_rosbag) {
      return;
    }

    auto period = std::chrono::milliseconds(period_ms);

    auto callback = [&]() {
        auto msg = std::make_unique<std_msgs::msg::Int64>();
        rclcpp::sleep_for(std::chrono::milliseconds(50));
        pub_->publish(std::move(msg));
      };
    pub_ = create_publisher<std_msgs::msg::Int64>(topic_name, QOS_HISTORY_SIZE);
    timer_ = create_wall_timer(period, callback);
  }

private:
  rclcpp::Publisher<std_msgs::msg::Int64>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  auto executor = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();

  std::vector<std::shared_ptr<rclcpp::Node>> nodes;

  nodes.emplace_back(std::make_shared<SensorDummy>("drive_node", "/drive", 3000));
  nodes.emplace_back(
    std::make_shared<SubDependencyNode>("message_driven_node", "/drive", "/topic3"));
  nodes.emplace_back(
    std::make_shared<TimerDependencyNode>("timer_driven_node", "/topic3", "/topic4", 10000));

  for (auto & node : nodes) {
    executor->add_node(node);
  }

  executor->spin();
  rclcpp::shutdown();

  return 0;
}
