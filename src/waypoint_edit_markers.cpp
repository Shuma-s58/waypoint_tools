#include "waypoint_tools/waypoint_edit_markers.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "waypoint_tools/interactive_waypoints.hpp"

namespace waypoint_tools
{

using interactive_markers::MenuHandler;
using namespace std::chrono_literals;

namespace
{

void call(const std::function<void(size_t)> & callback, size_t index)
{
  if (callback) {
    callback(index);
  }
}

void call(const std::function<void()> & callback)
{
  if (callback) {
    callback();
  }
}

double marker_scale(const Waypoint & waypoint)
{
  return std::max(goal_radius(waypoint, 1.0), 0.3);
}

}  // namespace

WaypointEditMarkers::WaypointEditMarkers(
  rclcpp::Node & node, std::string frame_id, Waypoints & waypoints, Callbacks callbacks,
  Options options)
: logger_(node.get_logger()),
  frame_id_(std::move(frame_id)),
  waypoints_(waypoints),
  callbacks_(std::move(callbacks)),
  options_(options),
  server_(std::make_unique<interactive_markers::InteractiveMarkerServer>(
      kServerNamespace, &node))
{
  menu_handler_.insert(
    "insert after", [this](const auto & feedback) {insertCallback(feedback);});
  menu_handler_.insert(
    "delete", [this](const auto & feedback) {deleteCallback(feedback);});
  menu_handler_.insert("save", [this](const auto &) {call(callbacks_.save);});
  if (options_.stop_entry) {
    // チェックで停止点（stop: true）。状態は marker ごとに makeMarker で反映する。
    stop_handle_ = menu_handler_.insert(
      "stop", [this](const auto & feedback) {stopCallback(feedback);});
  }
  if (options_.file) {
    const auto task_menu = menu_handler_.insert("task");
    no_task_handle_ = menu_handler_.insert(
      task_menu, "none", [this](const auto & feedback) {taskCallback(feedback, "");});
    const auto action_menu = menu_handler_.insert("action");
    start_handle_ = menu_handler_.insert(
      action_menu, "start", [this](const auto & feedback) {actionCallback(feedback, "start");});
    stop_action_handle_ = menu_handler_.insert(
      action_menu, "stop", [this](const auto & feedback) {actionCallback(feedback, "stop");});
    task_menu_handle_ = task_menu;
  }

  defer_timer_ = node.create_wall_timer(
    1ms, [this]() {
      defer_timer_->cancel();
      auto tasks = std::move(deferred_);
      deferred_.clear();
      for (auto & task : tasks) {
        task();
      }
    });
  defer_timer_->cancel();
}

void WaypointEditMarkers::show()
{
  addTaskEntries();
  server_->clear();
  for (size_t index = 0; index < waypoints_.size(); ++index) {
    makeMarker(index);
  }
  server_->applyChanges();
}

void WaypointEditMarkers::addTaskEntries()
{
  if (!options_.file) {return;}