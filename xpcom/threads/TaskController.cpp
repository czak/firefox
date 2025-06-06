/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TaskController.h"
#include "IdleTaskRunner.h"
#include "nsIIdleRunnable.h"
#include "nsIRunnable.h"
#include "nsThreadUtils.h"
#include <algorithm>
#include "GeckoProfiler.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/EventQueue.h"
#include "mozilla/Hal.h"
#include "mozilla/InputTaskManager.h"
#include "mozilla/VsyncTaskManager.h"
#include "mozilla/IOInterposer.h"
#include "mozilla/Perfetto.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/FlowMarkers.h"
#include "mozilla/StaticPrefs_memory.h"
#include "nsIThreadInternal.h"
#include "nsThread.h"
#include "prenv.h"
#include "prsystem.h"

namespace mozilla {

StaticAutoPtr<TaskController> TaskController::sSingleton;

std::atomic<uint64_t> Task::sCurrentTaskSeqNo = 0;

const int32_t kMinimumPoolThreadCount = 2;
const int32_t kMaximumPoolThreadCount = 8;

struct PoolThread {
  const size_t mIndex;
  PRThread* mThread = nullptr;

  CondVar mThreadCV;
  RefPtr<Task> mCurrentTask;

  // This may be higher than mCurrentTask's priority due to priority
  // propagation. This is -only- valid when mCurrentTask != nullptr.
  uint32_t mEffectiveTaskPriority = 0;

  PoolThread(size_t aIndex, Mutex& aGraphMutex)
      : mIndex(aIndex), mThreadCV(aGraphMutex, "PoolThread::mThreadCV") {}
};

/* static */
int32_t TaskController::GetPoolThreadCount() {
  if (PR_GetEnv("MOZ_TASKCONTROLLER_THREADCOUNT")) {
    return strtol(PR_GetEnv("MOZ_TASKCONTROLLER_THREADCOUNT"), nullptr, 0);
  }

  int32_t numCores = 0;
#if defined(XP_MACOSX) && defined(__aarch64__)
  if (const auto& cpuInfo = hal::GetHeterogeneousCpuInfo()) {
    // -1 because of the main thread.
    numCores = cpuInfo->mBigCpus.Count() + cpuInfo->mMediumCpus.Count() - 1;
  } else
#endif
  {
    numCores = std::max<int32_t>(1, PR_GetNumberOfProcessors());
  }

  return std::clamp<int32_t>(numCores, kMinimumPoolThreadCount,
                             kMaximumPoolThreadCount);
}

#if defined(MOZ_COLLECTING_RUNNABLE_TELEMETRY)

// This struct is duplicated below as 'IncompleteTaskMarker'.
// Make sure you keep the two in sync.
// The only difference between the two schemas is the type of the "task" field:
// TaskMarker uses TerminatingFlow and IncompleteTaskMarker uses Flow.
// We have two schemas so that we don't need to emit a separate marker for the
// TerminatingFlow in the common case.
struct TaskMarker : BaseMarkerType<TaskMarker> {
  static constexpr const char* Name = "Task";
  static constexpr const char* Description =
      "Marker representing a task being executed in TaskController.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"name", MS::InputType::CString, "Task Name", MS::Format::String,
       MS::PayloadFlags::Searchable},
      {"priority", MS::InputType::Uint32, "Priority level",
       MS::Format::Integer},
      {"task", MS::InputType::Uint64, "Task", MS::Format::TerminatingFlow,
       MS::PayloadFlags::Searchable},
      {"priorityName", MS::InputType::CString, "Priority Name"}};

  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* ChartLabel = "{marker.data.name}";
  static constexpr const char* TableLabel =
      "{marker.name} - {marker.data.name} - priority: "
      "{marker.data.priorityName} ({marker.data.priority})"
      " task: {marker.data.task}";

  static constexpr bool IsStackBased = true;

  static constexpr MS::ETWMarkerGroup Group = MS::ETWMarkerGroup::Scheduling;

  static void TranslateMarkerInputToSchema(void* aContext,
                                           const nsCString& aName,
                                           uint32_t aPriority, Flow aFlow) {
    ETW::OutputMarkerSchema(aContext, TaskMarker{}, aName, aPriority, aFlow,
                            ProfilerStringView(""));
  }

  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   const nsCString& aName, uint32_t aPriority,
                                   Flow aFlow) {
    aWriter.StringProperty("name", aName);
    aWriter.IntProperty("priority", aPriority);

#  define EVENT_PRIORITY(NAME, VALUE)                \
    if (aPriority == (VALUE)) {                      \
      aWriter.StringProperty("priorityName", #NAME); \
    } else
    EVENT_QUEUE_PRIORITY_LIST(EVENT_PRIORITY)
#  undef EVENT_PRIORITY
    {
      aWriter.StringProperty("priorityName", "Invalid Value");
    }
    aWriter.FlowProperty("task", aFlow);
  }
};

// This is a duplicate of the code above with the format of the 'task'
// field changed from `TerminatingFlow` to Flow`
struct IncompleteTaskMarker : BaseMarkerType<IncompleteTaskMarker> {
  static constexpr const char* Name = "Task";
  static constexpr const char* Description =
      "Marker representing a task being executed in TaskController.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"name", MS::InputType::CString, "Task Name", MS::Format::String,
       MS::PayloadFlags::Searchable},
      {"priority", MS::InputType::Uint32, "Priority level",
       MS::Format::Integer},
      {"task", MS::InputType::Uint64, "Task", MS::Format::Flow,
       MS::PayloadFlags::Searchable},
      {"priorityName", MS::InputType::CString, "Priority Name"}};

  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* ChartLabel = "{marker.data.name}";
  static constexpr const char* TableLabel =
      "{marker.name} - {marker.data.name} - priority: "
      "{marker.data.priorityName} ({marker.data.priority})"
      " task: {marker.data.task}";

  static constexpr bool IsStackBased = true;

  static constexpr MS::ETWMarkerGroup Group = MS::ETWMarkerGroup::Scheduling;

  static void TranslateMarkerInputToSchema(void* aContext,
                                           const nsCString& aName,
                                           uint32_t aPriority, Flow aFlow) {
    ETW::OutputMarkerSchema(aContext, IncompleteTaskMarker{}, aName, aPriority,
                            aFlow, ProfilerStringView(""));
  }

  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   const nsCString& aName, uint32_t aPriority,
                                   Flow aFlow) {
    aWriter.StringProperty("name", aName);
    aWriter.IntProperty("priority", aPriority);

#  define EVENT_PRIORITY(NAME, VALUE)                \
    if (aPriority == (VALUE)) {                      \
      aWriter.StringProperty("priorityName", #NAME); \
    } else
    EVENT_QUEUE_PRIORITY_LIST(EVENT_PRIORITY)
#  undef EVENT_PRIORITY
    {
      aWriter.StringProperty("priorityName", "Invalid Value");
    }
    aWriter.FlowProperty("task", aFlow);
  }
};

// Wrap task->Run() so that we can add markers for it
Task::TaskResult TaskController::RunTask(Task* aTask) {
  if (!profiler_is_collecting_markers()) {
    return aTask->Run();
  }

  TimeStamp startTime = TimeStamp::Now();

  nsAutoCString name;
  aTask->GetName(name);

  PERFETTO_TRACE_EVENT("task", perfetto::DynamicString{name.get()});
  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING_NONSENSITIVE("Task", OTHER, name);

  auto result = aTask->Run();

  if (profiler_thread_is_being_profiled_for_markers()) {
    AUTO_PROFILER_LABEL("AutoProfileTask", PROFILER);
    AUTO_PROFILER_STATS(AUTO_PROFILE_TASK);
    auto priority = aTask->GetPriority();
    auto flow = Flow::FromPointer(aTask);
    if (result == Task::TaskResult::Complete) {
      profiler_add_marker("Runnable", baseprofiler::category::OTHER,
                          MarkerTiming::IntervalUntilNowFrom(startTime),
                          TaskMarker{}, name, priority, flow);
    } else {
      profiler_add_marker("Runnable", baseprofiler::category::OTHER,
                          MarkerTiming::IntervalUntilNowFrom(startTime),
                          IncompleteTaskMarker{}, name, priority, flow);
    }
  }

  return result;
}
#else
Task::TaskResult TaskController::RunTask(Task* aTask) { return aTask->Run(); }
#endif

bool TaskManager::
    UpdateCachesForCurrentIterationAndReportPriorityModifierChanged(
        const MutexAutoLock& aProofOfLock, IterationType aIterationType) {
  mCurrentSuspended = IsSuspended(aProofOfLock);

  if (aIterationType == IterationType::EVENT_LOOP_TURN && !mCurrentSuspended) {
    int32_t oldModifier = mCurrentPriorityModifier;
    mCurrentPriorityModifier =
        GetPriorityModifierForEventLoopTurn(aProofOfLock);

    if (mCurrentPriorityModifier != oldModifier) {
      return true;
    }
  }
  return false;
}

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
class MOZ_RAII AutoSetMainThreadRunnableName {
 public:
  explicit AutoSetMainThreadRunnableName(const nsCString& aName) {
    MOZ_ASSERT(NS_IsMainThread());
    // We want to record our current runnable's name in a static so
    // that BHR can record it.
    mRestoreRunnableName = nsThread::sMainThreadRunnableName;

    // Copy the name into sMainThreadRunnableName's buffer, and append a
    // terminating null.
    uint32_t length = std::min((uint32_t)nsThread::kRunnableNameBufSize - 1,
                               (uint32_t)aName.Length());
    memcpy(nsThread::sMainThreadRunnableName.begin(), aName.BeginReading(),
           length);
    nsThread::sMainThreadRunnableName[length] = '\0';
  }

  ~AutoSetMainThreadRunnableName() {
    nsThread::sMainThreadRunnableName = mRestoreRunnableName;
  }

 private:
  Array<char, nsThread::kRunnableNameBufSize> mRestoreRunnableName;
};
#endif

Task* Task::GetHighestPriorityDependency() {
  Task* currentTask = this;

  while (!currentTask->mDependencies.empty()) {
    auto iter = currentTask->mDependencies.begin();

    while (iter != currentTask->mDependencies.end()) {
      if ((*iter)->mCompleted) {
        auto oldIter = iter;
        iter++;
        // Completed tasks are removed here to prevent needlessly keeping them
        // alive or iterating over them in the future.
        currentTask->mDependencies.erase(oldIter);
        continue;
      }

      currentTask = iter->get();
      break;
    }
  }

  return currentTask == this ? nullptr : currentTask;
}

#ifdef MOZ_MEMORY
static StaticRefPtr<IdleTaskRunner> sIdleMemoryCleanupRunner;
static StaticRefPtr<nsITimer> sIdleMemoryCleanupWantsLater;
static bool sIdleMemoryCleanupWantsLaterScheduled = false;

static const char kEnableLazyPurgePref[] = "memory.lazypurge.enable";
static const char kMaxPurgeDelayPref[] = "memory.lazypurge.maximum_delay";
static const char kMinPurgeBudgetPref[] =
    "memory.lazypurge.minimum_idle_budget";
static const char kMinPurgeReuseGracePref[] =
    "memory.lazypurge.reuse_grace_period";
#endif

void TaskController::Initialize() {
  MOZ_ASSERT(!sSingleton);
  sSingleton = new TaskController();
}

void ThreadFuncPoolThread(void* aData) {
  auto* thread = static_cast<PoolThread*>(aData);
  TaskController::Get()->RunPoolThread(thread);
}

TaskController::TaskController()
    : mGraphMutex("TaskController::mGraphMutex"),
      mMainThreadCV(mGraphMutex, "TaskController::mMainThreadCV"),
#ifdef MOZ_MEMORY
      mIsLazyPurgeEnabled(false),
#endif
      mRunOutOfMTTasksCounter(0) {
  InputTaskManager::Init();
  VsyncTaskManager::Init();
  mMTProcessingRunnable = NS_NewRunnableFunction(
      "TaskController::ExecutePendingMTTasks()",
      []() { TaskController::Get()->ProcessPendingMTTask(); });
  mMTBlockingProcessingRunnable = NS_NewRunnableFunction(
      "TaskController::ExecutePendingMTTasks()",
      []() { TaskController::Get()->ProcessPendingMTTask(true); });
}

void TaskController::InitializeThreadPool() {
  mPoolInitializationMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(!mThreadPoolInitialized);
  mThreadPoolInitialized = true;

  int32_t poolSize = GetPoolThreadCount();
  for (int32_t i = 0; i < poolSize; i++) {
    auto thread = MakeUnique<PoolThread>(i, mGraphMutex);
    thread->mThread =
        PR_CreateThread(PR_USER_THREAD, ThreadFuncPoolThread, thread.get(),
                        PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                        PR_JOINABLE_THREAD, nsIThreadManager::LargeStackSize());
    MOZ_RELEASE_ASSERT(thread->mThread,
                       "Failed to create TaskController pool thread");
    mPoolThreads.emplace_back(std::move(thread));
  }

  mIdleThreadCount = mPoolThreads.size();
}

/* static */
size_t TaskController::GetThreadStackSize() {
  return nsIThreadManager::LargeStackSize();
}

void TaskController::SetPerformanceCounterState(
    PerformanceCounterState* aPerformanceCounterState) {
  mPerformanceCounterState = aPerformanceCounterState;
}

/* static */
void TaskController::Shutdown() {
  InputTaskManager::Cleanup();
  VsyncTaskManager::Cleanup();
  if (sSingleton) {
    sSingleton->ShutdownThreadPoolInternal();
    sSingleton = nullptr;
  }
  MOZ_ASSERT(!sSingleton);

#ifdef MOZ_MEMORY
  // We choose to not disable lazy purge on our shutdown as this might do a
  // useless sync purge of all arenas during process shutdown.
  // Note that we already stopped scheduling new idle purges after
  // ShutdownPhase::AppShutdownConfirmed, so most likely it's already gone.
  if (sIdleMemoryCleanupRunner) {
    sIdleMemoryCleanupRunner->Cancel();
    sIdleMemoryCleanupRunner = nullptr;
  }
  if (sIdleMemoryCleanupWantsLater) {
    sIdleMemoryCleanupWantsLater->Cancel();
    sIdleMemoryCleanupWantsLater = nullptr;
    sIdleMemoryCleanupWantsLaterScheduled = false;
  }
#endif
}

void TaskController::ShutdownThreadPoolInternal() {
  {
    // Prevent race condition on mShuttingDown and wait.
    MutexAutoLock lock(mGraphMutex);
    mShuttingDown = true;
    for (auto& thread : mPoolThreads) {
      thread->mThreadCV.NotifyAll();
    }
  }
  for (auto& thread : mPoolThreads) {
    PR_JoinThread(thread->mThread);
  }

  MOZ_ASSERT(mIdleThreadCount == mPoolThreads.size());
}

void TaskController::RunPoolThread(PoolThread* aThread) {
  IOInterposer::RegisterCurrentThread();

  nsAutoCString threadName;
  threadName.AppendLiteral("TaskController #");
  threadName.AppendInt(static_cast<int64_t>(aThread->mIndex));
  AUTO_PROFILER_REGISTER_THREAD(threadName.get());

  MutexAutoLock lock(mGraphMutex);
  while (!mShuttingDown) {
    if (!aThread->mCurrentTask) {
      AUTO_PROFILER_LABEL("TaskController::RunPoolThread", IDLE);
      aThread->mThreadCV.Wait();
      continue;
    }

    Task* task = aThread->mCurrentTask;
    bool taskCompleted = false;

    {
      MutexAutoUnlock unlock(mGraphMutex);
      taskCompleted = RunTask(task) == Task::TaskResult::Complete;
    }

    task->mInProgress = false;

    if (!taskCompleted) {
      // Presumably this task was interrupted, leave its dependencies
      // unresolved and reinsert into the queue.
      auto insertion = mThreadableTasks.insert(aThread->mCurrentTask);
      MOZ_ASSERT(insertion.second);
      task->mIterator = insertion.first;
    } else {
      task->mCompleted = true;
#ifdef DEBUG
      task->mIsInGraph = false;
#endif
      task->mDependencies.clear();
      // This may have unblocked a main thread task. We could do this only
      // if there was a main thread task before this one in the dependency
      // chain.
      mMayHaveMainThreadTask = true;
      // Since this could have multiple dependencies thare are restricted
      // to the main thread. Let's make sure that's awake.
      EnsureMainThreadTasksScheduled();

      MaybeInterruptTask(GetHighestPriorityMTTask(), lock);
    }

    // Clear the current task to mark ourselves idle.
    RefPtr<Task> lastTask = aThread->mCurrentTask.forget();
    mIdleThreadCount++;
    MOZ_ASSERT(mIdleThreadCount <= mPoolThreads.size());

    // Dispatch any other tasks that depended on this one.
    DispatchThreadableTasks(lock);

    // Ensure the last task is released before we enter the wait state. This
    // happens outside the lock. This is required since it's perfectly feasible
    // for task destructors to post events themselves.
    {
      MutexAutoUnlock unlock(mGraphMutex);
      lastTask = nullptr;
    }
  }

  MOZ_ASSERT(mThreadableTasks.empty());

  IOInterposer::UnregisterCurrentThread();
}

void TaskController::AddTask(already_AddRefed<Task>&& aTask) {
  RefPtr<Task> task(aTask);

  if (task->GetKind() == Task::Kind::OffMainThreadOnly) {
    MutexAutoLock lock(mPoolInitializationMutex);
    if (!mThreadPoolInitialized) {
      InitializeThreadPool();
    }
  }

  MutexAutoLock lock(mGraphMutex);

  if (TaskManager* manager = task->GetManager()) {
    if (manager->mTaskCount == 0) {
      mTaskManagers.insert(manager);
    }
    manager->DidQueueTask();

    // Set this here since if this manager's priority modifier doesn't change
    // we will not reprioritize when iterating over the queue.
    task->mPriorityModifier = manager->mCurrentPriorityModifier;
  }

  if (profiler_is_active_and_unpaused()) {
    task->mInsertionTime = TimeStamp::Now();
  }

#ifdef DEBUG
  task->mIsInGraph = true;

  for (const RefPtr<Task>& otherTask : task->mDependencies) {
    MOZ_ASSERT(!otherTask->mTaskManager ||
               otherTask->mTaskManager == task->mTaskManager);
  }
#endif

  LogTask::LogDispatch(task);
  PROFILER_MARKER("TaskController::AddTask", OTHER, {}, FlowMarker,
                  Flow::FromPointer(task.get()));

  std::pair<std::set<RefPtr<Task>, Task::PriorityCompare>::iterator, bool>
      insertion;
  switch (task->GetKind()) {
    case Task::Kind::MainThreadOnly:
      if (task->GetPriority() >=
              static_cast<uint32_t>(EventQueuePriority::Normal) &&
          !mMainThreadTasks.empty()) {
        insertion = std::pair(
            mMainThreadTasks.insert(--mMainThreadTasks.end(), std::move(task)),
            true);
      } else {
        insertion = mMainThreadTasks.insert(std::move(task));
      }
      break;
    case Task::Kind::OffMainThreadOnly:
      insertion = mThreadableTasks.insert(std::move(task));
      break;
  }
  (*insertion.first)->mIterator = insertion.first;
  MOZ_ASSERT(insertion.second);

  MaybeInterruptTask(*insertion.first, lock);
}

void TaskController::DispatchThreadableTasks(
    const MutexAutoLock& aProofOfLock) {
  while (MaybeDispatchOneThreadableTask(aProofOfLock)) {
    // Loop.
  }
}

bool TaskController::MaybeDispatchOneThreadableTask(
    const MutexAutoLock& aProofOfLock) {
  if (mThreadableTasks.empty() || mIdleThreadCount == 0) {
    return false;
  }

  auto [task, effetivePriority] = TakeThreadableTaskToRun(aProofOfLock);
  if (!task) {
    return false;
  }

  PoolThread* thread = SelectThread(aProofOfLock);

  MOZ_ASSERT(!thread->mCurrentTask);
  MOZ_ASSERT(mIdleThreadCount != 0);
  thread->mCurrentTask = task;
  thread->mEffectiveTaskPriority = effetivePriority;
  thread->mThreadCV.Notify();
  task->mInProgress = true;
  mIdleThreadCount--;

  return true;
}

TaskController::TaskToRun TaskController::TakeThreadableTaskToRun(
    const MutexAutoLock& aProofOfLock) {
  MOZ_ASSERT(!mThreadableTasks.empty());

  // Search for the highest priority dependency of the highest priority task.
  for (const RefPtr<Task>& rootTask : mThreadableTasks) {
    MOZ_ASSERT(!rootTask->mTaskManager);

    Task* task = rootTask;
    while (Task* nextTask = task->GetHighestPriorityDependency()) {
      task = nextTask;
    }

    if (task->GetKind() != Task::Kind::MainThreadOnly && !task->mInProgress) {
      TaskToRun taskToRun{task, rootTask->GetPriority()};
      mThreadableTasks.erase(task->mIterator);
      task->mIterator = mThreadableTasks.end();
      return taskToRun;
    }
  }

  return TaskToRun();
}

PoolThread* TaskController::SelectThread(const MutexAutoLock& aProofOfLock) {
  MOZ_ASSERT(mIdleThreadCount != 0);

  // This just picks the first free thread.
  for (auto& thread : mPoolThreads) {
    if (!thread->mCurrentTask) {
      return thread.get();
    }
  }

  MOZ_CRASH("Couldn't find idle thread");
}

void TaskController::WaitForTaskOrMessage() {
  MutexAutoLock lock(mGraphMutex);
  while (!mMayHaveMainThreadTask) {
    AUTO_PROFILER_LABEL("TaskController::WaitForTaskOrMessage", IDLE);
    mMainThreadCV.Wait();
  }
}

void TaskController::ExecuteNextTaskOnlyMainThread() {
  MOZ_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mGraphMutex);
  ExecuteNextTaskOnlyMainThreadInternal(lock);
}

void TaskController::ProcessPendingMTTask(bool aMayWait) {
  MOZ_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mGraphMutex);

  for (;;) {
    // We only ever process one event here. However we may sometimes
    // not actually process a real event because of suspended tasks.
    // This loop allows us to wait until we've processed something
    // in that scenario.

    mMTTaskRunnableProcessedTask = ExecuteNextTaskOnlyMainThreadInternal(lock);

    if (mMTTaskRunnableProcessedTask || !aMayWait) {
      break;
    }

#ifdef MOZ_ENABLE_BACKGROUND_HANG_MONITOR
    // Unlock before calling into the BackgroundHangMonitor API as it uses
    // the timer API.
    {
      MutexAutoUnlock unlock(mGraphMutex);
      BackgroundHangMonitor().NotifyWait();
    }
#endif

    {
      // ProcessNextEvent will also have attempted to wait, however we may have
      // given it a Runnable when all the tasks in our task graph were suspended
      // but we weren't able to cheaply determine that.
      AUTO_PROFILER_LABEL("TaskController::ProcessPendingMTTask", IDLE);
      mMainThreadCV.Wait();
    }

#ifdef MOZ_ENABLE_BACKGROUND_HANG_MONITOR
    {
      MutexAutoUnlock unlock(mGraphMutex);
      BackgroundHangMonitor().NotifyActivity();
    }
#endif
  }

  if (mMayHaveMainThreadTask) {
    EnsureMainThreadTasksScheduled();
  }
}

void TaskController::ReprioritizeTask(Task* aTask, uint32_t aPriority) {
  MutexAutoLock lock(mGraphMutex);
  std::set<RefPtr<Task>, Task::PriorityCompare>* queue = &mMainThreadTasks;
  if (aTask->GetKind() == Task::Kind::OffMainThreadOnly) {
    queue = &mThreadableTasks;
  }

  MOZ_ASSERT(aTask->mIterator != queue->end());
  queue->erase(aTask->mIterator);

  aTask->mPriority = aPriority;

  auto insertion = queue->insert(aTask);
  MOZ_ASSERT(insertion.second);
  aTask->mIterator = insertion.first;

  MaybeInterruptTask(aTask, lock);
}

// Code supporting runnable compatibility.
// Task that wraps a runnable.
class RunnableTask : public Task {
 public:
  RunnableTask(already_AddRefed<nsIRunnable>&& aRunnable, int32_t aPriority,
               Kind aKind)
      : Task(aKind, aPriority), mRunnable(aRunnable) {}

  virtual TaskResult Run() override {
    mRunnable->Run();
    mRunnable = nullptr;
    return TaskResult::Complete;
  }

  void SetIdleDeadline(TimeStamp aDeadline) override {
    nsCOMPtr<nsIIdleRunnable> idleRunnable = do_QueryInterface(mRunnable);
    if (idleRunnable) {
      idleRunnable->SetDeadline(aDeadline);
    }
  }

  virtual bool GetName(nsACString& aName) override {
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
    if (nsCOMPtr<nsINamed> named = do_QueryInterface(mRunnable)) {
      MOZ_ALWAYS_TRUE(NS_SUCCEEDED(named->GetName(aName)));
    } else {
      aName.AssignLiteral("non-nsINamed runnable");
    }
    if (aName.IsEmpty()) {
      aName.AssignLiteral("anonymous runnable");
    }
    return true;
#else
    return false;
#endif
  }

 private:
  RefPtr<nsIRunnable> mRunnable;
};

void TaskController::DispatchRunnable(already_AddRefed<nsIRunnable>&& aRunnable,
                                      uint32_t aPriority,
                                      TaskManager* aManager) {
  RefPtr<RunnableTask> task = new RunnableTask(std::move(aRunnable), aPriority,
                                               Task::Kind::MainThreadOnly);

  task->SetManager(aManager);
  TaskController::Get()->AddTask(task.forget());
}

nsIRunnable* TaskController::GetRunnableForMTTask(bool aReallyWait) {
  MutexAutoLock lock(mGraphMutex);

  while (mMainThreadTasks.empty()) {
    if (!aReallyWait) {
      return nullptr;
    }

    AUTO_PROFILER_LABEL("TaskController::GetRunnableForMTTask::Wait", IDLE);
    mMainThreadCV.Wait();
  }

  return aReallyWait ? mMTBlockingProcessingRunnable : mMTProcessingRunnable;
}

bool TaskController::HasMainThreadPendingTasks() {
  MOZ_ASSERT(NS_IsMainThread());
  auto resetIdleState = MakeScopeExit([&idleManager = mIdleTaskManager] {
    if (idleManager) {
      idleManager->State().ClearCachedIdleDeadline();
    }
  });

  for (bool considerIdle : {false, true}) {
    if (considerIdle && !mIdleTaskManager) {
      continue;
    }

    MutexAutoLock lock(mGraphMutex);

    if (considerIdle) {
      mIdleTaskManager->State().ForgetPendingTaskGuarantee();
      // Temporarily unlock so we can peek our idle deadline.
      // XXX We could do this _before_ we take the lock if the API would let us.
      // We do want to do this before looking at mMainThreadTasks, in case
      // someone adds one while we're unlocked.
      {
        MutexAutoUnlock unlock(mGraphMutex);
        mIdleTaskManager->State().CachePeekedIdleDeadline(unlock);
      }
    }

    // Return early if there's no tasks at all.
    if (mMainThreadTasks.empty()) {
      return false;
    }

    // We can cheaply count how many tasks are suspended.
    uint64_t totalSuspended = 0;
    for (TaskManager* manager : mTaskManagers) {
      DebugOnly<bool> modifierChanged =
          manager
              ->UpdateCachesForCurrentIterationAndReportPriorityModifierChanged(
                  lock, TaskManager::IterationType::NOT_EVENT_LOOP_TURN);
      MOZ_ASSERT(!modifierChanged);

      // The idle manager should be suspended unless we're doing the idle pass.
      MOZ_ASSERT(manager != mIdleTaskManager || manager->mCurrentSuspended ||
                     considerIdle,
                 "Why are idle tasks not suspended here?");

      if (manager->mCurrentSuspended) {
        // XXX - If managers manage off-main-thread tasks this breaks! This
        // scenario is explicitly not supported.
        //
        // This is only incremented inside the lock -or- decremented on the main
        // thread so this is safe.
        totalSuspended += manager->mTaskCount;
      }
    }

    // This would break down if we have a non-suspended task depending on a
    // suspended task. This is why for the moment we do not allow tasks
    // to be dependent on tasks managed by another taskmanager.
    if (mMainThreadTasks.size() > totalSuspended) {
      // If mIdleTaskManager->mTaskCount is 0, we never updated the suspended
      // state of mIdleTaskManager above, hence shouldn't even check it here.
      // But in that case idle tasks are not contributing to our suspended task
      // count anyway.
      if (mIdleTaskManager && mIdleTaskManager->mTaskCount &&
          !mIdleTaskManager->mCurrentSuspended) {
        MOZ_ASSERT(considerIdle, "Why is mIdleTaskManager not suspended?");
        // Check whether the idle tasks were really needed to make our "we have
        // an unsuspended task" decision.  If they were, we need to force-enable
        // idle tasks until we run our next task.
        if (mMainThreadTasks.size() - mIdleTaskManager->mTaskCount <=
            totalSuspended) {
          mIdleTaskManager->State().EnforcePendingTaskGuarantee();
        }
      }
      return true;
    }
  }
  return false;
}

uint64_t TaskController::PendingMainthreadTaskCountIncludingSuspended() {
  MutexAutoLock lock(mGraphMutex);
  return mMainThreadTasks.size();
}

#ifdef MOZ_MEMORY
void TaskController::UpdateIdleMemoryCleanupPrefs() {
  mIsLazyPurgeEnabled = StaticPrefs::memory_lazypurge_enable();
  moz_enable_deferred_purge(mIsLazyPurgeEnabled);
}

static void PrefChangeCallback(const char* aPrefName, void* aNull) {
  MOZ_ASSERT((0 == strcmp(aPrefName, kEnableLazyPurgePref)) ||
             (0 == strcmp(aPrefName, kMaxPurgeDelayPref)) ||
             (0 == strcmp(aPrefName, kMinPurgeBudgetPref)) ||
             (0 == strcmp(aPrefName, kMinPurgeReuseGracePref)));

  TaskController::Get()->UpdateIdleMemoryCleanupPrefs();
}

// static
void TaskController::SetupIdleMemoryCleanup() {
  Preferences::RegisterCallback(PrefChangeCallback, kEnableLazyPurgePref);
  Preferences::RegisterCallback(PrefChangeCallback, kMaxPurgeDelayPref);
  Preferences::RegisterCallback(PrefChangeCallback, kMinPurgeBudgetPref);
  Preferences::RegisterCallback(PrefChangeCallback, kMinPurgeReuseGracePref);
  TaskController::Get()->UpdateIdleMemoryCleanupPrefs();
}

bool RunIdleMemoryCleanup(TimeStamp aDeadline, uint32_t aWantsLaterDelay);

void CheckIdleMemoryCleanupNeeded(nsITimer* aTimer, void* aClosure);

void CancelIdleMemoryCleanupTimerAndRunner() {
  if (sIdleMemoryCleanupRunner) {
    sIdleMemoryCleanupRunner->Cancel();
    sIdleMemoryCleanupRunner = nullptr;
  }
  if (sIdleMemoryCleanupWantsLaterScheduled) {
    MOZ_ASSERT(sIdleMemoryCleanupWantsLater);
    sIdleMemoryCleanupWantsLater->Cancel();
    sIdleMemoryCleanupWantsLaterScheduled = false;
  }
}

void ScheduleWantsLaterTimer(uint32_t aWantsLaterDelay) {
  if (sIdleMemoryCleanupRunner) {
    sIdleMemoryCleanupRunner->Cancel();
    sIdleMemoryCleanupRunner = nullptr;
  }
  if (!sIdleMemoryCleanupWantsLater) {
    auto res = NS_NewTimerWithFuncCallback(
        CheckIdleMemoryCleanupNeeded, (void*)"IdleMemoryCleanupWantsLaterCheck",
        aWantsLaterDelay, nsITimer::TYPE_ONE_SHOT_LOW_PRIORITY,
        "IdleMemoryCleanupWantsLaterCheck");
    if (res.isOk()) {
      sIdleMemoryCleanupWantsLater = res.unwrap().forget();
    }
  } else {
    if (sIdleMemoryCleanupWantsLaterScheduled) {
      sIdleMemoryCleanupWantsLater->Cancel();
    }
    sIdleMemoryCleanupWantsLater->InitWithNamedFuncCallback(
        CheckIdleMemoryCleanupNeeded, (void*)"IdleMemoryCleanupWantsLaterCheck",
        aWantsLaterDelay, nsITimer::TYPE_ONE_SHOT_LOW_PRIORITY,
        "IdleMemoryCleanupWantsLaterCheck");
  }
  sIdleMemoryCleanupWantsLaterScheduled = true;
}

void ScheduleIdleMemoryCleanup(uint32_t aWantsLaterDelay) {
  TimeDuration maxPurgeDelay = TimeDuration::FromMilliseconds(
      StaticPrefs::memory_lazypurge_maximum_delay());
  TimeDuration minPurgeBudget = TimeDuration::FromMilliseconds(
      StaticPrefs::memory_lazypurge_minimum_idle_budget());

  CancelIdleMemoryCleanupTimerAndRunner();
  sIdleMemoryCleanupRunner = IdleTaskRunner::Create(
      [aWantsLaterDelay](TimeStamp aDeadline) {
        return RunIdleMemoryCleanup(aDeadline, aWantsLaterDelay);
      },
      "TaskController::IdlePurgeRunner", TimeDuration(), maxPurgeDelay,
      minPurgeBudget, true, nullptr, nullptr);
}
}  // namespace mozilla

namespace geckoprofiler::markers {
struct IdlePurgePeekMarker : mozilla::BaseMarkerType<IdlePurgePeekMarker> {
  static constexpr const char* Name = "IdlePurgePeek";
  static constexpr const char* Description = "Check if we should purge memory";

  using MS = mozilla::MarkerSchema;
  using String8View = mozilla::ProfilerString8View;

  static constexpr MS::PayloadField PayloadFields[] = {
      {"status", MS::InputType::CString, "Status", MS::Format::String}};

  static void StreamJSONMarkerData(
      mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
      const String8View& aStatus) {
    aWriter.StringProperty("status", aStatus);
  }

  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
};
}  // namespace geckoprofiler::markers

namespace mozilla {
// Check if a purge needs to be scheduled now or later.
// Both used as timer callback and directly from MayScheduleIdleMemoryCleanup.
//
// We schedule our runner if we are about to go idle and there is a purge
// due now (NeedsMore). We (re-)schedule instead a low-priority timer if
// we need to check again for a possible future purge (WantsLater). We use
// a timer for this instead of the same IdleTaskRunner in order to avoid it
// to post some runnables to the main thread to find idle time before the
// (very cheap) check actually runs.
//
// aTimer:   Not used
// aClosure: Not used
void CheckIdleMemoryCleanupNeeded(nsITimer* aTimer, void* aClosure) {
  uint32_t reuseGracePeriod =
      StaticPrefs::memory_lazypurge_reuse_grace_period();

  // The wantsLaterDelay is used as a last resort when the main thread stays
  // idle but we knew we should come back.
  // We double the grace time to increase the chance that all arenas' grace
  // periods expired if we really ever trigger it after going idle and to
  // reduce the impact of occasionally firing while being busy.
  uint32_t wantsLaterDelay = reuseGracePeriod * 2;

  MOZ_ASSERT(!sIdleMemoryCleanupRunner ||
             !sIdleMemoryCleanupWantsLaterScheduled);
  auto result =
      moz_may_purge_now(/* aPeekOnly */ true, reuseGracePeriod, Nothing());
  switch (result) {
    case may_purge_now_result_t::Done:
      // Currently we unqueue purge requests only:
      // if we run moz_may_purge_one_now with aPeekOnly==false and that happens
      // only in the IdleTaskRunner which cancels itself when done
      // OR
      // if something else causes a MayPurgeAll (like
      // jemalloc_free_(excess)_dirty_pages or moz_set_max_dirty_page_modifier)
      // which can happen anytime.
      if (sIdleMemoryCleanupRunner || sIdleMemoryCleanupWantsLaterScheduled) {
        PROFILER_MARKER("IdlePurgePeek", GCCC, MarkerTiming::InstantNow(),
                        IdlePurgePeekMarker,
                        ProfilerString8View::WrapNullTerminatedString(
                            "Done (Cancel timer or runner)"));
        CancelIdleMemoryCleanupTimerAndRunner();
      }
      break;
    case may_purge_now_result_t::WantsLater:
      if (!sIdleMemoryCleanupWantsLaterScheduled) {
        PROFILER_MARKER(
            "IdlePurgePeek", GCCC, MarkerTiming::InstantNow(),
            IdlePurgePeekMarker,
            ProfilerString8View::WrapNullTerminatedString(
                "WantsLater (First schedule of low priority timer)"));
      }
      // We always want to (re-)schedule the timer to prevent it from firing
      // as much as possible.
      ScheduleWantsLaterTimer(wantsLaterDelay);
      break;
    case may_purge_now_result_t::NeedsMore:
      // We can get here from the main thread going repeatedly idle after we
      // already scheduled a runner. Just keep it.
      if (!sIdleMemoryCleanupRunner) {
        PROFILER_MARKER("IdlePurgePeek", GCCC, MarkerTiming::InstantNow(),
                        IdlePurgePeekMarker,
                        ProfilerString8View::WrapNullTerminatedString(
                            "NeedsMore (Schedule as-soon-as-idle cleanup)"));
        ScheduleIdleMemoryCleanup(wantsLaterDelay);
      } else {
        MOZ_ASSERT(!sIdleMemoryCleanupWantsLaterScheduled);
      }
      break;
  }
}
}  // namespace mozilla

namespace geckoprofiler::markers {
struct IdlePurgeMarker : mozilla::BaseMarkerType<IdlePurgeMarker> {
  static constexpr const char* Name = "IdlePurge";
  static constexpr const char* Description =
      "Purge memory from mozjemalloc in idle time";

  using MS = mozilla::MarkerSchema;
  using String8View = mozilla::ProfilerString8View;

  static constexpr MS::PayloadField PayloadFields[] = {
      {"num_calls", MS::InputType::Uint32, "Number of PurgeNow() calls",
       MS::Format::Integer},
      {"next", MS::InputType::CString, "Last result", MS::Format::String}};

  static void StreamJSONMarkerData(
      mozilla::baseprofiler::SpliceableJSONWriter& aWriter, uint32_t aNumCalls,
      const String8View& aLastResult) {
    aWriter.IntProperty("num_calls", aNumCalls);
    aWriter.StringProperty("last_result", aLastResult);
  }

  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
};
}  // namespace geckoprofiler::markers

namespace mozilla {

// Do some purging until our idle budget is used.
//
// At the time the runner actually runs, the situation might have changed wrt
// when our runner has been scheduled, such that we might find nothing to do.
// And if we reached our budget and it still NeedsMore, we just keep the runner
// alive to get another slice of idle time from the current instance.
// Otherwise we just (un)schedule accordingly like CheckIdleMemoryCleanupNeeded
// would do.
//
// aDeadline:        Deadline passed by the IdleTaskRunner until which we are
//                   allowed to consume time.
// aWantsLaterDelay: (Minimum) delay to be used for the WantsLater timer.
bool RunIdleMemoryCleanup(TimeStamp aDeadline, uint32_t aWantsLaterDelay) {
  MOZ_ASSERT(!sIdleMemoryCleanupWantsLaterScheduled);

  TimeStamp start_time = TimeStamp::Now();
  uint32_t num_calls = 0;

  uint32_t reuseGracePeriod =
      StaticPrefs::memory_lazypurge_reuse_grace_period();

  may_purge_now_result_t result;
  do {
    num_calls++;
    result = moz_may_purge_now(
        /* aPeekOnly */ false, reuseGracePeriod, Some([aDeadline] {
          return aDeadline.IsNull() || TimeStamp::Now() <= aDeadline;
        }));
  } while ((result == may_purge_now_result_t::NeedsMore) &&
           (aDeadline.IsNull() || TimeStamp::Now() <= aDeadline));

  const char* last_result;
  switch (result) {
    case may_purge_now_result_t::Done:
      last_result = "Done (Cancel timer and runner)";
      CancelIdleMemoryCleanupTimerAndRunner();
      break;
    case may_purge_now_result_t::WantsLater:
      last_result = "WantsLater (First schedule of low priority timer)";
      ScheduleWantsLaterTimer(aWantsLaterDelay);
      break;
    case may_purge_now_result_t::NeedsMore:
      last_result = "NeedsMore (wait for next idle slice)";
      break;
  }

  PROFILER_MARKER("IdlePurge", GCCC,
                  MarkerTiming::IntervalUntilNowFrom(start_time),
                  IdlePurgeMarker, num_calls,
                  ProfilerString8View::WrapNullTerminatedString(last_result));

  return true;
};

void TaskController::MayScheduleIdleMemoryCleanup() {
  if (PendingMainthreadTaskCountIncludingSuspended() > 0) {
    // This is a hot code path for the main thread, so please be cautious when
    // adding more logic here or before.
    // For example it is counterproductive to try to detect here if the main
    // thread is busy and cancel the timer in case.
    return;
  }
  if (!mIsLazyPurgeEnabled) {
    return;
  }

  if (AppShutdown::IsShutdownImpending()) {
    CancelIdleMemoryCleanupTimerAndRunner();
    return;
  }

  CheckIdleMemoryCleanupNeeded(nullptr, (void*)"MayScheduleIdleMemoryCleanup");
}
#endif

bool TaskController::ExecuteNextTaskOnlyMainThreadInternal(
    const MutexAutoLock& aProofOfLock) MOZ_REQUIRES(mGraphMutex) {
  MOZ_ASSERT(NS_IsMainThread());
  mGraphMutex.AssertCurrentThreadOwns();
  // Block to make it easier to jump to our cleanup.
  bool taskRan = false;
  do {
    taskRan = DoExecuteNextTaskOnlyMainThreadInternal(aProofOfLock);
    if (taskRan) {
      if (mIdleTaskManager && mIdleTaskManager->mTaskCount &&
          mIdleTaskManager->IsSuspended(aProofOfLock)) {
        uint32_t activeTasks = mMainThreadTasks.size();
        for (TaskManager* manager : mTaskManagers) {
          if (manager->IsSuspended(aProofOfLock)) {
            activeTasks -= manager->mTaskCount;
          } else {
            break;
          }
        }

        if (!activeTasks) {
          // We have only idle (and maybe other suspended) tasks left, so need
          // to update the idle state. We need to temporarily release the lock
          // while we do that.
          MutexAutoUnlock unlock(mGraphMutex);
          mIdleTaskManager->State().RequestIdleDeadlineIfNeeded(unlock);
        }
      }
      break;
    }

    if (!mIdleTaskManager) {
      break;
    }

    if (mIdleTaskManager->mTaskCount) {
      // We have idle tasks that we may not have gotten above because
      // our idle state is not up to date.  We need to update the idle state
      // and try again.  We need to temporarily release the lock while we do
      // that.
      MutexAutoUnlock unlock(mGraphMutex);
      mIdleTaskManager->State().UpdateCachedIdleDeadline(unlock);
    } else {
      MutexAutoUnlock unlock(mGraphMutex);
      mIdleTaskManager->State().RanOutOfTasks(unlock);
    }

    // When we unlocked, someone may have queued a new task on us.  So try to
    // see whether we can run things again.
    taskRan = DoExecuteNextTaskOnlyMainThreadInternal(aProofOfLock);
  } while (false);

  if (mIdleTaskManager) {
    // The pending task guarantee is not needed anymore, since we just tried
    // running a task
    mIdleTaskManager->State().ForgetPendingTaskGuarantee();

    if (mMainThreadTasks.empty()) {
      ++mRunOutOfMTTasksCounter;

      // XXX the IdlePeriodState API demands we have a MutexAutoUnlock for it.
      // Otherwise we could perhaps just do this after we exit the locked block,
      // by pushing the lock down into this method.  Though it's not clear that
      // we could check mMainThreadTasks.size() once we unlock, and whether we
      // could maybe substitute mMayHaveMainThreadTask for that check.
      MutexAutoUnlock unlock(mGraphMutex);
      mIdleTaskManager->State().RanOutOfTasks(unlock);
    }
  }

  return taskRan;
}

bool TaskController::DoExecuteNextTaskOnlyMainThreadInternal(
    const MutexAutoLock& aProofOfLock) MOZ_REQUIRES(mGraphMutex) {
  mGraphMutex.AssertCurrentThreadOwns();

  nsCOMPtr<nsIThread> mainIThread;
  NS_GetMainThread(getter_AddRefs(mainIThread));

  nsThread* mainThread = static_cast<nsThread*>(mainIThread.get());
  if (mainThread) {
    mainThread->SetRunningEventDelay(TimeDuration(), TimeStamp());
  }

  uint32_t totalSuspended = 0;
  for (TaskManager* manager : mTaskManagers) {
    bool modifierChanged =
        manager
            ->UpdateCachesForCurrentIterationAndReportPriorityModifierChanged(
                aProofOfLock, TaskManager::IterationType::EVENT_LOOP_TURN);
    if (modifierChanged) {
      ProcessUpdatedPriorityModifier(manager);
    }
    if (manager->mCurrentSuspended) {
      totalSuspended += manager->mTaskCount;
    }
  }

  MOZ_ASSERT(mMainThreadTasks.size() >= totalSuspended);

  // This would break down if we have a non-suspended task depending on a
  // suspended task. This is why for the moment we do not allow tasks
  // to be dependent on tasks managed by another taskmanager.
  if (mMainThreadTasks.size() > totalSuspended) {
    for (auto iter = mMainThreadTasks.begin(); iter != mMainThreadTasks.end();
         iter++) {
      Task* task = iter->get();

      if (task->mTaskManager && task->mTaskManager->mCurrentSuspended) {
        // Even though we may want to run some dependencies of this task, we
        // will run them at their own priority level and not the priority
        // level of their dependents.
        continue;
      }

      task = GetFinalDependency(task);

      if (task->GetKind() == Task::Kind::OffMainThreadOnly ||
          task->mInProgress ||
          (task->mTaskManager && task->mTaskManager->mCurrentSuspended)) {
        continue;
      }

      mCurrentTasksMT.push(task);
      mMainThreadTasks.erase(task->mIterator);
      task->mIterator = mMainThreadTasks.end();
      task->mInProgress = true;
      TaskManager* manager = task->GetManager();
      bool result = false;

      {
        MutexAutoUnlock unlock(mGraphMutex);
        if (manager) {
          manager->WillRunTask();
          if (manager != mIdleTaskManager) {
            // Notify the idle period state that we're running a non-idle task.
            // This needs to happen while our mutex is not locked!
            mIdleTaskManager->State().FlagNotIdle();
          } else {
            TimeStamp idleDeadline =
                mIdleTaskManager->State().GetCachedIdleDeadline();
            MOZ_ASSERT(
                idleDeadline,
                "How can we not have a deadline if our manager is enabled?");
            task->SetIdleDeadline(idleDeadline);
          }
        }
        if (mIdleTaskManager) {
          // We found a task to run; we can clear the idle deadline on our idle
          // task manager.  This _must_ be done before we actually run the task,
          // because running the task could reenter via spinning the event loop
          // and we want to make sure there's no cached idle deadline at that
          // point.  But we have to make sure we do it after out SetIdleDeadline
          // call above, in the case when the task is actually an idle task.
          mIdleTaskManager->State().ClearCachedIdleDeadline();
        }

        TimeStamp now = TimeStamp::Now();

        if (mainThread) {
          if (task->GetPriority() < uint32_t(EventQueuePriority::InputHigh) ||
              task->mInsertionTime.IsNull()) {
            mainThread->SetRunningEventDelay(TimeDuration(), now);
          } else {
            mainThread->SetRunningEventDelay(now - task->mInsertionTime, now);
          }
        }

        nsAutoCString name;
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
        task->GetName(name);
#endif

        PerformanceCounterState::Snapshot snapshot =
            mPerformanceCounterState->RunnableWillRun(
                now, manager == mIdleTaskManager);

        {
          LogTask::Run log(task);
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
          AutoSetMainThreadRunnableName nameGuard(name);
#endif

          result = RunTask(task) == Task::TaskResult::Complete;
        }

        // Task itself should keep manager alive.
        if (manager) {
          manager->DidRunTask();
        }

        mPerformanceCounterState->RunnableDidRun(name, std::move(snapshot));
      }

      // Task itself should keep manager alive.
      if (manager && result && manager->mTaskCount == 0) {
        mTaskManagers.erase(manager);
      }

      task->mInProgress = false;

      if (!result) {
        // Presumably this task was interrupted, leave its dependencies
        // unresolved and reinsert into the queue.
        auto insertion =
            mMainThreadTasks.insert(std::move(mCurrentTasksMT.top()));
        MOZ_ASSERT(insertion.second);
        task->mIterator = insertion.first;
        if (manager) {
          manager->WillRunTask();
        }
      } else {
        task->mCompleted = true;
#ifdef DEBUG
        task->mIsInGraph = false;
#endif
        // Clear dependencies to release references.
        task->mDependencies.clear();

        // Dispatch any tasks that are now ready to run.
        DispatchThreadableTasks(aProofOfLock);
      }

      mCurrentTasksMT.pop();
      return true;
    }
  }

  mMayHaveMainThreadTask = false;
  if (mIdleTaskManager) {
    // We did not find a task to run.  We still need to clear the cached idle
    // deadline on our idle state, because that deadline was only relevant to
    // the execution of this function.  Had we found a task, we would have
    // cleared the deadline before running that task.
    mIdleTaskManager->State().ClearCachedIdleDeadline();
  }
  return false;
}

Task* TaskController::GetFinalDependency(Task* aTask) {
  Task* nextTask;

  while ((nextTask = aTask->GetHighestPriorityDependency())) {
    aTask = nextTask;
  }

  return aTask;
}

void TaskController::MaybeInterruptTask(Task* aTask,
                                        const MutexAutoLock& aProofOfLock) {
  mGraphMutex.AssertCurrentThreadOwns();

  if (!aTask) {
    return;
  }

  // This optimization prevents many slow lookups in long chains of similar
  // priority.
  if (!aTask->mDependencies.empty()) {
    Task* firstDependency = aTask->mDependencies.begin()->get();
    if (aTask->GetPriority() <= firstDependency->GetPriority() &&
        !firstDependency->mCompleted &&
        aTask->GetKind() == firstDependency->GetKind()) {
      // This task has the same or a higher priority as one of its dependencies,
      // never any need to interrupt.
      return;
    }
  }

  Task* finalDependency = GetFinalDependency(aTask);

  if (finalDependency->mInProgress) {
    // No need to wake anything, we can't schedule this task right now anyway.
    return;
  }

  if (aTask->GetKind() == Task::Kind::MainThreadOnly) {
    mMayHaveMainThreadTask = true;

    EnsureMainThreadTasksScheduled();

    if (mCurrentTasksMT.empty()) {
      return;
    }

    // We could go through the steps above here and interrupt an off main
    // thread task in case it has a lower priority.
    if (finalDependency->GetKind() == Task::Kind::OffMainThreadOnly) {
      return;
    }

    if (mCurrentTasksMT.top()->GetPriority() < aTask->GetPriority()) {
      mCurrentTasksMT.top()->RequestInterrupt(aTask->GetPriority());
    }
  } else {
    if (mIdleThreadCount != 0) {
      DispatchThreadableTasks(aProofOfLock);

      // There was a free thread, no need to interrupt anything.
      return;
    }

    Task* lowestPriorityTask = nullptr;
    for (auto& thread : mPoolThreads) {
      MOZ_ASSERT(thread->mCurrentTask);
      if (!lowestPriorityTask) {
        lowestPriorityTask = thread->mCurrentTask.get();
        continue;
      }

      // This should possibly select the lowest priority task which was started
      // the latest. But for now we ignore that optimization.
      // This also doesn't guarantee a task is interruptable, so that's an
      // avenue for improvements as well.
      if (lowestPriorityTask->GetPriority() > thread->mEffectiveTaskPriority) {
        lowestPriorityTask = thread->mCurrentTask.get();
      }
    }

    if (lowestPriorityTask->GetPriority() < aTask->GetPriority()) {
      lowestPriorityTask->RequestInterrupt(aTask->GetPriority());
    }

    // We choose not to interrupt main thread tasks for tasks which may be
    // executed off the main thread.
  }
}

Task* TaskController::GetHighestPriorityMTTask() {
  mGraphMutex.AssertCurrentThreadOwns();

  if (!mMainThreadTasks.empty()) {
    return mMainThreadTasks.begin()->get();
  }
  return nullptr;
}

void TaskController::EnsureMainThreadTasksScheduled() {
  if (mObserver) {
    mObserver->OnDispatchedEvent();
  }
  if (mExternalCondVar) {
    mExternalCondVar->Notify();
  }
  mMainThreadCV.Notify();
}

void TaskController::ProcessUpdatedPriorityModifier(TaskManager* aManager) {
  mGraphMutex.AssertCurrentThreadOwns();

  MOZ_ASSERT(NS_IsMainThread());

  int32_t modifier = aManager->mCurrentPriorityModifier;

  std::vector<RefPtr<Task>> storedTasks;
  // Find all relevant tasks.
  for (auto iter = mMainThreadTasks.begin(); iter != mMainThreadTasks.end();) {
    if ((*iter)->mTaskManager == aManager) {
      storedTasks.push_back(*iter);
      iter = mMainThreadTasks.erase(iter);
    } else {
      iter++;
    }
  }

  // Reinsert found tasks with their new priorities.
  for (RefPtr<Task>& ref : storedTasks) {
    // Kept alive at first by the vector and then by mMainThreadTasks.
    Task* task = ref;
    task->mPriorityModifier = modifier;
    auto insertion = mMainThreadTasks.insert(std::move(ref));
    MOZ_ASSERT(insertion.second);
    task->mIterator = insertion.first;
  }
}

}  // namespace mozilla
