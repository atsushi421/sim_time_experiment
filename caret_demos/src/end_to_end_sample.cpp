#include "rclcpp/rclcpp.hpp"

#include "std_msgs/msg/int64.hpp"

#include <chrono>
#include <deque>
#include <fstream>
#include <memory>
#include <random>
#include <vector>

#define QOS_HISTORY_SIZE 10

using namespace std::chrono_literals;
using namespace std::chrono;

class TimerDependencyNode : public rclcpp::Node
{
public:
  TimerDependencyNode(std::string node_name, std::string sub_topic_name, int period_ms)
  : Node(node_name), sub_count_(0), timer_count_(0)
  {
    this->declare_parameter<std::string>(
      "log_path", "/home/atsushi/sim_time_experiment/output/log.txt");
    std::string log_path;
    this->get_parameter("log_path", log_path);
    log_file_.open(log_path, std::ios::out | std::ios::app);

    sub_ = create_subscription<std_msgs::msg::Int64>(
      sub_topic_name, QOS_HISTORY_SIZE, [&](std_msgs::msg::Int64::UniquePtr msg) {
        sub_count_++;
        auto now_wall_ns =
          duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
        log_file_ << now_wall_ns << " " << this->get_clock()->now().nanoseconds()
                  << " [TimerDependencyNode] Sub Start. ID: " << sub_count_ << std::endl;

        dummy_work();
        received_msgs_.push_back(msg->data);

        now_wall_ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
        log_file_ << now_wall_ns << " " << this->get_clock()->now().nanoseconds()
                  << " [TimerDependencyNode] Sub End. ID: " << sub_count_ << std::endl;
      });

    timer_ = rclcpp::create_timer(this, get_clock(), milliseconds(period_ms), [&]() {
      auto now_wall_ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
      log_file_ << now_wall_ns << " " << this->get_clock()->now().nanoseconds()
                << " [TimerDependencyNode] Timer triggered. ID: " << timer_count_++ << std::endl;

      while (!received_msgs_.empty()) {
        int64_t msg = received_msgs_.front();
        log_file_ << "Received message ID: " << msg << std::endl;
        received_msgs_.pop_front();
      }
    });
  }

  ~TimerDependencyNode() { log_file_.close(); }

private:
  rclcpp::Subscription<std_msgs::msg::Int64>::SharedPtr sub_;
  std::deque<int64_t> received_msgs_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::ofstream log_file_;
  int64_t sub_count_;
  int64_t timer_count_;

  // approx. 175ms
  void dummy_work()
  {
    long long sum = 0;
    for (int i = 0; i < 50000000; i++) {
      sum += i;
    }
  }
};

class SensorDummy : public rclcpp::Node
{
public:
  SensorDummy(std::string node_name, std::string topic_name, int period_ms)
  : Node(node_name), timer_count_(0)
  {
    this->declare_parameter<bool>("use_rosbag", false);
    bool use_rosbag = false;
    this->get_parameter("use_rosbag", use_rosbag);
    RCLCPP_INFO(this->get_logger(), "use_rosbag = %d", use_rosbag);
    if (use_rosbag) {
      return;
    }

    auto callback = [&]() {
      auto msg = std::make_unique<std_msgs::msg::Int64>();
      msg->data = timer_count_++;
      pub_->publish(std::move(msg));
    };

    pub_ = create_publisher<std_msgs::msg::Int64>(topic_name, QOS_HISTORY_SIZE);
    auto period = milliseconds(period_ms);
    timer_ = create_wall_timer(period, callback);
  }

private:
  rclcpp::Publisher<std_msgs::msg::Int64>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  int64_t timer_count_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto executor = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();

  std::vector<std::shared_ptr<rclcpp::Node>> nodes;
  nodes.emplace_back(std::make_shared<SensorDummy>("drive_node", "/drive", 300));
  nodes.emplace_back(std::make_shared<TimerDependencyNode>("timer_driven_node", "/drive", 1000));
  for (auto & node : nodes) {
    executor->add_node(node);
  }

  executor->spin();
  rclcpp::shutdown();
  return 0;
}
