#include "waypoint_tools/waypoint_yaml.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace waypoint_tools
{

namespace
{

// 最短で元の値に戻る表記。PyYAML でも float と読めるよう必ず "." を含める。
std::string format_double(double value)
{
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  std::string text(buffer, result.ptr);
  if (text.find_first_of(".ein") == std::string::npos) {
    text += ".0";
  } else if (text.find('.') == std::string::npos && text.find('e') != std::string::npos) {
    text.insert(text.find('e'), ".0");
  }
  return text;
}

double get_double(const Waypoint & waypoint, const char * key)
{
  const YAML::Node value = waypoint.yaml()[key];
  return value ? value.as<double>() : 0.0;
}

}  // namespace

Waypoint::Waypoint()
: node_(YAML::NodeType::Map)
{
}

Waypoint::Waypoint(const YAML::Node & node)
: node_(node)
{
}

Waypoint & Waypoint::operator=(const Waypoint & other)
{
  node_.reset(other.node_);
  return *this;
}

Waypoint Waypoint::clone() const
{
  return Waypoint(YAML::Clone(node_));
}

std::string expand_user(const std::string & path)
{
  if (path.empty() || path[0] != '~' || (path.size() > 1 && path[1] != '/')) {
    return path;
  }
  const char * home = std::getenv("HOME");
  return home ? std::string(home) + path.substr(1) : path;
}

WaypointFile load_waypoint_file(const std::string & path)
{
  WaypointFile file;
  try {
    file.root = YAML::LoadFile(expand_user(path));
  } catch (const YAML::BadFile &) {
    throw std::runtime_error("Cannot open: " + path);
  }
  if (!file.root.IsMap()) {
    throw std::runtime_error("YAML root must be a map.");
  }
  const YAML::Node waypoints = file.root["waypoints"];
  if (!waypoints || !waypoints.IsSequence()) {
    throw std::runtime_error("YAML must contain a top-level \"waypoints\" list.");
  }
  for (const auto & waypoint : waypoints) {
    file.waypoints.emplace_back(waypoint);
  }
  return file;
}

void save_waypoint_file(const std::string & path, const WaypointFile & file)
{
  YAML::Node root = YAML::Clone(file.root);
  YAML::Node waypoints(YAML::NodeType::Sequence);
  for (const auto & waypoint : file.waypoints) {
    waypoints.push_back(waypoint.yaml());
  }
  root["waypoints"] = waypoints;

  YAML::Emitter emitter;