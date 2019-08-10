////////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
////////////////////////////////////////////////////////////////////////
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
////////////////////////////////////////////////////////////////////////

#ifndef __SCHEDULER__
#define __SCHEDULER__

#include "dispatcher.h"

static constexpr int32_t SCHEDULER_MINTICKS = 50;

class SchedulerTask : public Task
{
	public:
		virtual ~SchedulerTask() {}

		void setEventId(uint32_t eventId) {m_eventId = eventId;}
		uint32_t getEventId() const {return m_eventId;}

		std::chrono::system_clock::time_point getCycle() const {return m_expiration;}
		bool operator<(const SchedulerTask& other) const {return getCycle() > other.getCycle();}

	protected:
		uint32_t m_eventId = 0;

		SchedulerTask(uint32_t delay, std::function<void (void)>&& f) : Task(delay, std::move(f)) {}
		friend SchedulerTask* createSchedulerTask(uint32_t, std::function<void (void)>);
};

SchedulerTask* createSchedulerTask(uint32_t delay, std::function<void (void)> f);

struct lessTask
{
	bool operator()(const SchedulerTask* lhs, const SchedulerTask* rhs) const {
		return lhs->getCycle() > rhs->getCycle();
	}
};

typedef std::set<uint32_t> EventIds;
class Scheduler : public ThreadHolder<Scheduler>
{
	public:
		uint32_t addEvent(SchedulerTask* task);
		bool stopEvent(uint32_t eventId);

		void stop();
		void shutdown();

		void threadMain();

	protected:

		uint32_t m_lastEvent;
		EventIds m_eventIds;

		std::mutex m_eventLock;
		std::condition_variable m_eventSignal;

		std::priority_queue<SchedulerTask*, std::vector<SchedulerTask*>, lessTask > m_eventList;		
};
extern Scheduler g_scheduler;

#endif