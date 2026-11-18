  {
    save_waypoint_file(yaml_path_, file_);
    RCLCPP_INFO(get_logger(), "Saved waypoints: %s", yaml_path_.c_str());
  }

  // -----------------------------------------------------------
  // サービス
  // -----------------------------------------------------------
  Result trySendFrom(size_t index)
  {
    try {
      sendFrom(index);
    } catch (const std::exception & e) {
      return {false, e.what()};
    }
    return {true, "Sent " + yaml_path_ + " from waypoint " + std::to_string(index) + "."};
  }

  // 最初の waypoint から走行を開始する（走行中ならやり直す）。
  Result start()
  {
    cancelCurrent();
    return trySendFrom(0);
  }

  // 指定した waypoint から走行を開始する（どの状態からでも。走行中ならやり直す）。
  Result startFrom(int64_t index)
  {
    if (index < 0 || static_cast<size_t>(index) >= waypoints().size()) {
      return {
        false, "Waypoint " + std::to_string(index) + " is out of range (0.." +
        std::to_string(waypoints().size() - 1) + ")."};
    }
    cancelCurrent();
    return trySendFrom(static_cast<size_t>(index));
  }

  // 次の waypoint へ進む。
  // 未送信 -> 走行開始 / 走行中・停止点・失敗 -> 現在の点をやめて次の点へ /
  // 編集で一時停止 -> 向かっていた点から再開。
  Result nextWp()
  {
    switch (state_) {
      case State::kIdle:
        return trySendFrom(0);
      case State::kPaused:
        goTo(wp_index_);
        return {true, "Resumed to waypoint " + std::to_string(wp_index_) + "."};
      case State::kFinished:
        return {false, "Already reached the last waypoint."};
      case State::kCallingService:
        return {false, "Waiting for the arrival service response."};
      default:
        break;
    }
    cancelCurrent();
    advance();
    if (state_ == State::kFinished) {
      return {true, "Reached the last waypoint."};
    }
    return {true, "Go to waypoint " + std::to_string(wp_index_) + "."};
  }

  std::string yaml_path_;
  std::string frame_id_;
  std::string robot_frame_;
  double switch_radius_;
  int64_t max_retries_;
  bool skip_on_failure_;

  WaypointFile file_;
  size_t wp_index_ = 0;
  State state_ = State::kIdle;

  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::unique_ptr<tf2_ros::TransformListener> tf_listener_;

  rclcpp_action::Client<NavigateToPose>::SharedPtr action_client_;
  // goal を送るたびに増やす。古い（先行 goal に置き換えられた）goal の
  // 結果は世代が合わないので無視する。
  uint64_t goal_generation_ = 0;
  GoalHandle::SharedPtr goal_handle_;
  rclcpp::Client<Trigger>::SharedPtr arrival_client_;
  rclcpp::Client<SetBool>::SharedPtr arrival_control_client_;
  rclcpp::TimerBase::SharedPtr arrival_timeout_timer_;
  int64_t retries_ = 0;

  rclcpp::Service<Trigger>::SharedPtr send_all_service_;
  rclcpp::Service<Trigger>::SharedPtr next_wp_service_;
  rclcpp::Service<SetBool>::SharedPtr edit_service_;
  rclcpp::Subscription<Int32>::SharedPtr start_from_sub_;

  rclcpp::Publisher<MarkerArray>::SharedPtr marker_pub_;
  // 直近で publish した waypoint 数。減ったときに古い marker を消す。
  size_t published_count_ = 0;

  bool editing_ = false;
  std::optional<WaypointEditMarkers> edit_markers_;

  rclcpp::TimerBase::SharedPtr control_timer_;
  rclcpp::TimerBase::SharedPtr marker_timer_;
};

}  // namespace waypoint_tools

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int code = 0;
  try {
    rclcpp::spin(std::make_shared<waypoint_tools::WaypointSenderNode>());
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("waypoint_sender_node"), "%s", e.what());
    code = 1;
  }
  rclcpp::shutdown();
  return code;
}
