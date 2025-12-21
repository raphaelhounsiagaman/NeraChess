#pragma once

#include "Move.h"

#include <queue>
#include <mutex>

namespace ChessCore
{
	class MoveQueue
	{
	public:
		void Push(Move move)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Queue.push(move);
		}

		bool Pop(Move* move)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_Queue.empty())
				return false;

			*move = m_Queue.front();
			m_Queue.pop();
			return true;
		}

		void Clear()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			while (!m_Queue.empty()) {
				m_Queue.pop();
			}
		}

	private:
		std::queue<Move> m_Queue;
		std::mutex m_Mutex;

	};

}
