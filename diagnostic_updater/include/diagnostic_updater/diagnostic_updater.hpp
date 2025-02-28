/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2008, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

#ifndef DIAGNOSTIC_UPDATER__DIAGNOSTIC_UPDATER_HPP_
#define DIAGNOSTIC_UPDATER__DIAGNOSTIC_UPDATER_HPP_

#include <functional>  // for bind()
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "builtin_interfaces/msg/time.hpp"

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"

#include "diagnostic_updater/diagnostic_status_wrapper.hpp"

#include "rcl/time.h"

#include "rclcpp/node_interfaces/node_base_interface.hpp"
#include "rclcpp/node_interfaces/node_clock_interface.hpp"
#include "rclcpp/node_interfaces/node_logging_interface.hpp"
#include "rclcpp/node_interfaces/node_parameters_interface.hpp"
#include "rclcpp/node_interfaces/node_timers_interface.hpp"
#include "rclcpp/node_interfaces/node_topics_interface.hpp"

namespace diagnostic_updater
{

typedef std::function<void (DiagnosticStatusWrapper &)> TaskFunction;
typedef std::function<void (diagnostic_msgs::msg::DiagnosticStatus &)>
  UnwrappedTaskFunction;

/**
 * \brief DiagnosticTask is an abstract base class for collecting diagnostic
 * data.
 *
 * Subclasses are provided for generating common diagnostic information.
 *
 * A DiagnosticTask has a name, and a function that is called to cleate a
 * DiagnosticStatusWrapper.
 */
class DiagnosticTask
{
public:
  /**
   * \brief Constructs a DiagnosticTask setting its name in the process.
   */
  explicit DiagnosticTask(const std::string name)
  : name_(name) {}

  /**
   * \brief Returns the name of the DiagnosticTask.
   */
  const std::string & getName() {return name_;}

  /**
   * \brief Fills out this Task's DiagnosticStatusWrapper.
   */
  virtual void run(diagnostic_updater::DiagnosticStatusWrapper & stat) = 0;

  /**
   * Virtual destructor as this is a base class.
   */
  virtual ~DiagnosticTask() {}

private:
  const std::string name_;
};

/**
 * \brief a DiagnosticTask based on a std::function.
 *
 * The GenericFunctionDiagnosticTask calls the function when it updates. The
 * function
 * updates the DiagnosticStatusWrapper and collects data.
 *
 * This is useful for gathering information about a device or driver, like
 * temperature,
 * calibration, etc.
 */
template<class T>
class GenericFunctionDiagnosticTask : public DiagnosticTask
{
public:
  /**
   * Constructs a GenericFunctionDiagnosticTask based on the given name and
   * function.
   *
   * \param name Name of the function.
   *
   * \param fn Function to be called when DiagnosticTask::run is called.
   */
  GenericFunctionDiagnosticTask(
    const std::string & name,
    std::function<void(T &)> fn)
  : DiagnosticTask(name), fn_(fn) {}

  virtual void run(DiagnosticStatusWrapper & stat) {fn_(stat);}

private:
  const std::string name_;
  const TaskFunction fn_;
};

typedef GenericFunctionDiagnosticTask<diagnostic_msgs::msg::DiagnosticStatus>
  UnwrappedFunctionDiagnosticTask;
typedef GenericFunctionDiagnosticTask<DiagnosticStatusWrapper>
  FunctionDiagnosticTask;

/**
 * \brief Merges CompositeDiagnosticTask into a single DiagnosticTask.
 *
 * The CompositeDiagnosticTask allows multiple DiagnosticTask instances to
 * be combined into a single task that produces a single single
 * DiagnosticStatusWrapped. The output of the combination has the max of
 * the status levels, and a concatenation of the non-zero-level messages.
 *
 * For instance, this could be used to combine the calibration and offset data
 * from an
 * IMU driver.
 */
class CompositeDiagnosticTask : public DiagnosticTask
{
public:
  /**
   * \brief Constructs a CompositeDiagnosticTask with the given name.
   */
  explicit CompositeDiagnosticTask(const std::string name)
  : DiagnosticTask(name) {}

  /**
   * \brief Runs each child and merges their outputs.
   */
  virtual void run(DiagnosticStatusWrapper & stat)
  {
    DiagnosticStatusWrapper combined_summary;
    DiagnosticStatusWrapper original_summary;

    original_summary.summary(stat);

    for (std::vector<DiagnosticTask *>::iterator i = tasks_.begin();
      i != tasks_.end(); i++)
    {
      // Put the summary that was passed in.
      stat.summary(original_summary);
      // Let the next task add entries and put its summary.
      (*i)->run(stat);
      // Merge the new summary into the combined summary.
      combined_summary.mergeSummary(stat);
    }

    // Copy the combined summary into the output.
    stat.summary(combined_summary);
  }

  /**
   * \brief Adds a child CompositeDiagnosticTask.
   *
   * This CompositeDiagnosticTask will be called each time this
   * CompositeDiagnosticTask is run.
   */
  void addTask(DiagnosticTask * t) {tasks_.push_back(t);}

private:
  std::vector<DiagnosticTask *> tasks_;
};

/**
 * \brief Internal use only.
 *
 * Base class for diagnostic_updater::Updater and self_test::Dispatcher.
 * The class manages a collection of diagnostic updaters. It contains the
 * common functionality used for producing diagnostic updates and for
 * self-tests.
 */
class DiagnosticTaskVector
{
protected:
  /**
   * \brief Class used to represent a diagnostic task internally in
   * DiagnosticTaskVector.
   */
  class DiagnosticTaskInternal
  {
public:
    DiagnosticTaskInternal(const std::string name, TaskFunction f)
    : name_(name), fn_(f) {}

    void run(diagnostic_updater::DiagnosticStatusWrapper & stat) const
    {
      stat.name = name_;
      fn_(stat);
    }

    const std::string & getName() const {return name_;}

private:
    std::string name_;
    TaskFunction fn_;
  };

  std::mutex lock_;

  /**
   * \brief Returns the vector of tasks.
   */
  const std::vector<DiagnosticTaskInternal> & getTasks() {return tasks_;}

public:
  virtual ~DiagnosticTaskVector() {}

  /**
   * \brief Add a DiagnosticTask embodied by a name and function to the
   * DiagnosticTaskVector
   *
   * \param name Name to autofill in the DiagnosticStatusWrapper for this task.
   *
   * \param f Function to call to fill out the DiagnosticStatusWrapper.
   * This function need not remain valid after the last time the tasks are
   * called, and in particular it need not be valid at the time the
   * DiagnosticTaskVector is destructed.
   */
  void add(const std::string & name, TaskFunction f)
  {
    DiagnosticTaskInternal int_task(name, f);
    addInternal(int_task);
  }

  /**
   * \brief Add a DiagnosticTask to the DiagnosticTaskVector
   *
   * \param task The DiagnosticTask to be added. It must remain live at
   * least until the last time its diagnostic method is called. It need not be
   * valid at the time the DiagnosticTaskVector is destructed.
   */
  void add(DiagnosticTask & task)
  {
    TaskFunction f = std::bind(&DiagnosticTask::run, &task, std::placeholders::_1);
    add(task.getName(), f);
  }

  /**
   * \brief Add a DiagnosticTask embodied by a name and method to the
   * DiagnosticTaskVector
   *
   * \param name Name to autofill in the DiagnosticStatusWrapper for this task.
   *
   * \param c Class instance the method is being called on.
   *
   * \param f Method to call to fill out the DiagnosticStatusWrapper.
   * This method need not remain valid after the last time the tasks are
   * called, and in particular it need not be valid at the time the
   * DiagnosticTaskVector is destructed.
   */
  template<class T>
  void add(
    const std::string name, T * c,
    void (T::* f)(diagnostic_updater::DiagnosticStatusWrapper &))
  {
    DiagnosticTaskInternal int_task(name, std::bind(f, c, std::placeholders::_1));
    addInternal(int_task);
  }

  /**
   * \brief Remove a task based on its name.
   *
   * Removes the first task that matches the specified name. (New in
   * version 1.1.2)
   *
   * \param name Name of the task to remove.
   *
   * \return Returns true if a task matched and was removed.
   */
  bool removeByName(const std::string name)
  {
    std::unique_lock<std::mutex> lock(lock_);
    for (std::vector<DiagnosticTaskInternal>::iterator iter = tasks_.begin();
      iter != tasks_.end(); iter++)
    {
      if (iter->getName() == name) {
        tasks_.erase(iter);
        return true;
      }

      diagnostic_updater::DiagnosticStatusWrapper status;
    }
    return false;
  }

private:
  /**
   * Allows an action to be taken when a task is added. The Updater class
   * uses this to immediately publish a diagnostic that says that the node
   * is loading.
   */
  virtual void addedTaskCallback(DiagnosticTaskInternal &) {}
  std::vector<DiagnosticTaskInternal> tasks_;

protected:
  /**
   * Common code for all add methods.
   */
  void addInternal(DiagnosticTaskInternal & task)
  {
    std::unique_lock<std::mutex> lock(lock_);
    tasks_.push_back(task);
    addedTaskCallback(task);
  }
};

/**
 * \brief Manages a list of diagnostic tasks, and calls them in a
 * rate-limited manner.
 *
 * This class manages a list of diagnostic tasks. Its update function
 * should be called frequently. At some predetermined rate, the update
 * function will cause all the diagnostic tasks to run, and will collate
 * and publish the resulting diagnostics. The publication rate is
 * determined by the "~/diagnostic_updater.period" ros2 parameter.
 * The force_update function can always be triggered async to the period interval.
 */
class Updater : public DiagnosticTaskVector
{
public:
  bool verbose_;

  /**
   * \brief Constructs an updater class.
   *
   * \param node Node pointer to set up diagnostics
   * \param period Value in seconds to set the update period
   * \note The given period value not being used if the `diagnostic_updater.period`
   * ros2 parameter was set previously.
   */
  template<class NodeT>
  explicit Updater(NodeT node, double period = 1.0)
  : Updater(
      node->get_node_base_interface(),
      node->get_node_clock_interface(),
      node->get_node_logging_interface(),
      node->get_node_parameters_interface(),
      node->get_node_timers_interface(),
      node->get_node_topics_interface(),
      period)
  {}

  Updater(
    std::shared_ptr<rclcpp::node_interfaces::NodeBaseInterface> base_interface,
    std::shared_ptr<rclcpp::node_interfaces::NodeClockInterface> clock_interface,
    std::shared_ptr<rclcpp::node_interfaces::NodeLoggingInterface> logging_interface,
    std::shared_ptr<rclcpp::node_interfaces::NodeParametersInterface> parameters_interface,
    std::shared_ptr<rclcpp::node_interfaces::NodeTimersInterface> timers_interface,
    std::shared_ptr<rclcpp::node_interfaces::NodeTopicsInterface> topics_interface,
    double period = 1.0);

  /**
   * \brief Returns the interval between updates.
   */
  auto getPeriod() const {return period_;}

  /**
   * \brief Sets the period as a rclcpp::Duration
   */
  void setPeriod(rclcpp::Duration period)
  {
    period_ = period;
    reset_timer();
  }

  /**
   * \brief Sets the period given a value in seconds
   */
  void setPeriod(double period)
  {
    setPeriod(rclcpp::Duration::from_seconds(period));
  }

  /**
   * \brief Forces to send out an update for all known DiagnosticStatus.
   */
  void force_update()
  {
    update();
  }

  /**
   * \brief Output a message on all the known DiagnosticStatus.
   *
   * Useful if something drastic is happening such as shutdown or a
   * self-test.
   *
   * \param lvl Level of the diagnostic being output.
   *
   * \param msg Status message to output.
   */
  void broadcast(unsigned char lvl, const std::string msg);

  void setHardwareIDf(const char * format, ...);

  void setHardwareID(const std::string & hwid) {hwid_ = hwid;}

private:
  void reset_timer();

  /**
   * \brief Causes the diagnostics to update if the inter-update interval
   * has been exceeded.
   */
  void update();

  /**
   * Recheck the diagnostic_period on the parameter server. (Cached)
   */

  // TODO(Karsten1987) Follow up PR for eloquent
  // void update_diagnostic_period()
  // {
  //   // rcl_duration_value_t old_period = period_;
  //   // next_time_ = next_time_ +
  //   //   rclcpp::Duration(period_ - old_period);             // Update next_time_
  // }

  /**
   * Publishes a single diagnostic status.
   */
  void publish(diagnostic_msgs::msg::DiagnosticStatus & stat);

  /**
   * Publishes a vector of diagnostic statuses.
   */
  void publish(std::vector<diagnostic_msgs::msg::DiagnosticStatus> & status_vec);

  /**
   * Causes a placeholder DiagnosticStatus to be published as soon as a
   * diagnostic task is added to the Updater.
   */
  virtual void addedTaskCallback(DiagnosticTaskInternal & task);

  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base_interface_;
  rclcpp::node_interfaces::NodeTimersInterface::SharedPtr timers_interface_;
  rclcpp::Clock::SharedPtr clock_;
  rclcpp::Duration period_;
  rclcpp::TimerBase::SharedPtr update_timer_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr publisher_;
  rclcpp::Logger logger_;

  std::string hwid_;
  std::string node_name_;
  bool warn_nohwid_done_;
};
}   // namespace diagnostic_updater

#endif  // DIAGNOSTIC_UPDATER__DIAGNOSTIC_UPDATER_HPP_
