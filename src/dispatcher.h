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

#ifndef __TASKS__
#define __TASKS__

#include "thread_holder_base.h"

const int DISPATCHER_TASK_EXPIRATION = 2000;
const auto SYSTEM_TIME_ZERO = std::chrono::system_clock::time_point(std::chrono::milliseconds(0));

class Task
{
	public:
		explicit Task(std::function<void (void)>&& f) : m_f(std::move(f)) {}
		Task(uint32_t ms, std::function<void (void)>&& f) :
			m_expiration(std::chrono::system_clock::now() + std::chrono::milliseconds(ms)), m_f(std::move(f)) {}

		virtual ~Task() {}
		void operator()() {m_f();}

		void unsetExpiration() {m_expiration = SYSTEM_TIME_ZERO;}
		bool hasExpired() const
		{
			if (m_expiration == SYSTEM_TIME_ZERO) {
				return false;
			}
			return m_expiration < std::chrono::system_clock::now();
		}

	protected:
		std::chrono::system_clock::time_point m_expiration = SYSTEM_TIME_ZERO;
		std::function<void (void)> m_f;
};

Task* createTask(std::function<void (void)> f);

Task* createTask(uint32_t expiration, std::function<void (void)> f);

class Dispatcher : public ThreadHolder<Dispatcher>
{
	public:
		void addTask(Task* task, bool front = false);

		void stop();
		void shutdown();

		void threadMain();

	protected:
		void flush();

		std::mutex m_taskLock;
		std::condition_variable m_taskSignal;

		std::list<Task*> m_taskList;
};
extern Dispatcher g_dispatcher;
#endif
