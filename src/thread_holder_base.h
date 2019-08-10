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

#ifndef __THREAD_HOLDER_H__
#define __THREAD_HOLDER_H__

#include "enums.h"

template <typename Derived>
class ThreadHolder
{
	public:
		ThreadHolder() {}
		void start() {
			setState(STATE_RUNNING);
			thread = std::thread(&Derived::threadMain, static_cast<Derived*>(this));
		}

		void stop() {
			setState(STATE_CLOSING);
		}

		void join() {
			if (thread.joinable()) {
				thread.join();
			}
		}
	protected:
		void setState(ThreadState newState) {
			m_threadState.store(newState, std::memory_order_relaxed);
		}

		ThreadState getState() const {
			return m_threadState.load(std::memory_order_relaxed);
		}
		std::atomic<ThreadState> m_threadState{STATE_TERMINATED};
	private:
		std::thread thread;
};

#endif