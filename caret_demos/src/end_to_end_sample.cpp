#include <chrono>
#include <memory>
#include <vector>
#include <random>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

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
    pub_ = create_publisher<sensor_msgs::msg::Image>(pub_topic_name, QOS_HISTORY_SIZE);
    sub_ = create_subscription<sensor_msgs::msg::Image>(
      sub_topic_name, QOS_HISTORY_SIZE,
      [&](sensor_msgs::msg::Image::UniquePtr msg)
      {
        rclcpp::sleep_for(std::chrono::milliseconds(45));
        msg_ = std::move(msg);
      });


    timer_ = create_wall_timer(
      std::chrono::milliseconds(period_ms), [&]()
      {
        rclcpp::sleep_for(std::chrono::milliseconds(45));
        if (msg_) {
          pub_->publish(*msg_);
        }
      });
  }

private:
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
  sensor_msgs::msg::Image::UniquePtr msg_;
  rclcpp::TimerBase::SharedPtr timer_;
};

class SubDependencyNode : public rclcpp::Node
{
public:
  SubDependencyNode(
    std::string node_name,
    std::string sub_topic_name,
    std::string subsequent_sub_topic_name,
    std::string pub_topic_name
  )
  : Node(node_name)
  {
    sub1_ = create_subscription<sensor_msgs::msg::Image>(
      sub_topic_name, QOS_HISTORY_SIZE, [&](sensor_msgs::msg::Image::UniquePtr msg)
      {
        auto msg_tmp = std::make_unique<sensor_msgs::msg::Image>();
        msg_tmp->header.stamp = msg->header.stamp;
        rclcpp::sleep_for(std::chrono::milliseconds(45));
        msg_ = std::move(msg);
      });
    sub2_ = create_subscription<sensor_msgs::msg::Image>(
      subsequent_sub_topic_name, QOS_HISTORY_SIZE, [&](sensor_msgs::msg::Image::UniquePtr msg)
      {
        (void)msg;
        rclcpp::sleep_for(std::chrono::milliseconds(45));
        if (msg_) {
          pub_->publish(std::move(msg_));
        }
      });
    pub_ = create_publisher<sensor_msgs::msg::Image>(pub_topic_name, QOS_HISTORY_SIZE);
  }

private:
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub1_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub2_;
  sensor_msgs::msg::Image::UniquePtr msg_;
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
        auto msg = std::make_unique<sensor_msgs::msg::Image>();
        rclcpp::sleep_for(std::chrono::milliseconds(50));
        msg->header.stamp = now();
        pub_->publish(std::move(msg));
      };
    pub_ = create_publisher<sensor_msgs::msg::Image>(topic_name, QOS_HISTORY_SIZE);
    timer_ = create_wall_timer(period, callback);
  }

private:
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  auto executor = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();

  std::vector<std::shared_ptr<rclcpp::Node>> nodes;

  nodes.emplace_back(
    std::make_shared<SubDependencyNode>("message_driven_node", "/topic2", "/drive", "/topic3"));
  nodes.emplace_back(
    std::make_shared<TimerDependencyNode>("timer_driven_node", "/topic3", "/topic4", 100)); // 10Hz
  nodes.emplace_back(std::make_shared<SensorDummy>("drive_node", "/drive", 100)); // 10Hz

  for (auto & node : nodes) {
    executor->add_node(node);
  }

  executor->spin();
  rclcpp::shutdown();

  return 0;
}
