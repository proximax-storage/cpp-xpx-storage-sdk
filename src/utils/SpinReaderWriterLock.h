/**
*** Copyright (c) 2016-present,
*** Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp. All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#pragma once
#include "exceptions.h"
#include "functions.h"
#include <atomic>
#include <thread>

namespace sirius { namespace utils {

#ifndef DEADLOCK_THRESHOLD_MILLISECONDS
#define DEADLOCK_THRESHOLD_MILLISECONDS 10 * 60 * 1000 // 10 minutes
#endif

	// minutes before we declare waiting for lock as deadlock
	constexpr auto deadlock_threshold = std::chrono::milliseconds(DEADLOCK_THRESHOLD_MILLISECONDS);

	/// Custom reader writer lock implemented by using an atomic that allows multiple readers and a single writer
	/// and prefers writers.
	/// \note
	/// - 128 max writers
	/// - 256 max readers
	/// - writer lock must be acquired via a reader lock promotion
	template<typename TReaderNotificationPolicy>
	class BasicSpinReaderWriterLock : private TReaderNotificationPolicy {
	private:
		// 0[active writer]|1234567[total writers]|01234567[total readers]
		static constexpr uint16_t Active_Writer_Flag = 0x8000;
		static constexpr uint16_t Pending_Writer_Mask = 0x7F00;
		static constexpr uint16_t Reader_Mask = 0x00FF;
		static constexpr uint16_t Writer_Mask = Pending_Writer_Mask | Active_Writer_Flag;

		static constexpr uint16_t Active_Reader_Increment = 0x0001;
		static constexpr uint16_t Pending_Writer_Increment = 0x0100;

	private:
#pragma push_macro("Yield")
#undef Yield
		static void Yield() {
			std::this_thread::yield();
		}
#pragma pop_macro("Yield")

	private:
		// region LockGuard

		/// Base class for RAII lock guards.
		class LockGuard {
		protected:
			explicit LockGuard(const action& resetFunc)
					: m_resetFunc(resetFunc)
					, m_isMoved(false)
			{}

			~LockGuard() {
				if (m_isMoved)
					return;

				m_resetFunc();
			}

		public:
			LockGuard(LockGuard&& rhs) : m_resetFunc(rhs.m_resetFunc), m_isMoved(false) {
				rhs.m_isMoved = true;
			}

		private:
			action m_resetFunc;
			bool m_isMoved;
		};

		// endregion

	public:
		// region WriterLockGuard

		/// RAII writer lock guard.
		class WriterLockGuard : public LockGuard {
		public:
			/// Creates a guard around \a value.
			explicit WriterLockGuard(std::atomic<uint16_t>& value)
					: LockGuard([&value]() {
						// unset the active writer flag
						value.fetch_sub(Active_Writer_Flag + Pending_Writer_Increment);
					})
			{}

			/// Creates a guard around \a value and \a isActive.
			/// \note This constructor is used when writer is created by promotion.
			explicit WriterLockGuard(std::atomic<uint16_t>& value, bool& isActive)
					: LockGuard([&value, &isActive]() {
						// unset the active writer flag and change the writer to a reader
						value.fetch_sub(Active_Writer_Flag + Pending_Writer_Increment - Active_Reader_Increment);
						isActive = false;
					})
			{}

			/// Default move constructor.
			WriterLockGuard(WriterLockGuard&&) = default;
		};

		// endregion

		// region ReaderLockGuard

		/// RAII reader lock guard.
		class ReaderLockGuard : public LockGuard {
		public:
			/// Creates a guard around \a value and \a notificationPolicy.
			explicit ReaderLockGuard(std::atomic<uint16_t>& value, TReaderNotificationPolicy& notificationPolicy)
					: LockGuard([&value, &notificationPolicy]() {
						// decrease the number of readers by one
						value.fetch_sub(Active_Reader_Increment);
						notificationPolicy.readerReleased();
					})
					, m_value(value)
					, m_isWriterActive(false) {
				notificationPolicy.readerAcquired();
			}

			/// Default move constructor.
			ReaderLockGuard(ReaderLockGuard&&) = default;

		public:
			/// Promotes this reader lock to a writer lock.
			/// \note Deadlock is possible when promoteToWriter is called concurrently by multiple threads for the same lock.
			///       Each of the concurrent threads holds a reader lock, so a writer lock cannot be acquired by any thread.
			WriterLockGuard promoteToWriter() {
				markActiveWriter();

				// mark a pending write by changing the reader to a writer
				m_value.fetch_add(Pending_Writer_Increment - Active_Reader_Increment);

				// wait for exclusive access
				AcquireWriter(m_value);
				return WriterLockGuard(m_value, m_isWriterActive);
			}

		private:
			void markActiveWriter() {
				if (m_isWriterActive)
					CATAPULT_THROW_RUNTIME_ERROR("reader lock has already been promoted");

				m_isWriterActive = true;
			}

		private:
			std::atomic<uint16_t>& m_value;
			bool m_isWriterActive;
		};

		// endregion

	public:
		/// Creates an unlocked lock.
		BasicSpinReaderWriterLock() : m_value(0)
		{}

	public:
		/// Returns \c true if there is a pending (or active) writer.
		inline bool isWriterPending() const {
			return isSet(Pending_Writer_Mask);
		}

		/// Returns \c true if there is an active writer.
		inline bool isWriterActive() const {
			return isSet(Active_Writer_Flag);
		}

		/// Returns \c true if there is an active reader.
		inline bool isReaderActive() const {
			return isSet(Reader_Mask);
		}

	private:
		inline bool isSet(uint16_t mask) const {
			return 0 != (m_value & mask);
		}

	public:
		/// Blocks until a reader lock can be acquired.
		inline ReaderLockGuard acquireReader() {
			uint16_t current = m_value;
			auto start = std::chrono::high_resolution_clock::now();
			auto end = start + deadlock_threshold;

			for (;;) {
				if (std::chrono::high_resolution_clock::now() > end)
					CATAPULT_THROW_RUNTIME_ERROR("Deadlock occur waiting for reader lock");

				// wait for any pending writes to complete
				if (0 != (current & Pending_Writer_Mask)) {
					Yield();
					current = m_value;
					continue;
				}

				// try to increment the number of readers by one
				uint16_t desired = current + Active_Reader_Increment;
				if (m_value.compare_exchange_strong(current, desired))
					break;

				Yield();
			}

			return ReaderLockGuard(m_value, *this);
		}

		/// Blocks until a writer lock can be acquired.
		inline WriterLockGuard acquireWriter() {
			// mark a pending write
			m_value.fetch_add(Pending_Writer_Increment);

			// wait for exclusive access
			AcquireWriter(m_value);
			return WriterLockGuard(m_value);
		}

	private:
		static void AcquireWriter(std::atomic<uint16_t>& value) {
			// wait for exclusive access (when there is no active writer and no readers)
			uint16_t expected = value & Pending_Writer_Mask;
			auto start = std::chrono::high_resolution_clock::now();
			auto end = start + deadlock_threshold;

			while (!value.compare_exchange_strong(expected, expected | Active_Writer_Flag)) {
				if (std::chrono::high_resolution_clock::now() > end)
					CATAPULT_THROW_RUNTIME_ERROR("Deadlock occur waiting for writer lock");

				Yield();
				expected = value & Pending_Writer_Mask;
			}
		}

	private:
		std::atomic<uint16_t> m_value;
	};

	/// No-op reader notification policy.
	struct NoOpReaderNotificationPolicy {
		/// Reader was acquried by the current thread.
		constexpr void readerAcquired()
		{}

		/// Reader was released by the current thread.
		constexpr void readerReleased()
		{}
	};
}}

#ifdef ENABLE_CATAPULT_DIAGNOSTICS
#include "ReentrancyCheckReaderNotificationPolicy.h"
#endif

namespace sirius { namespace utils {

#ifdef ENABLE_CATAPULT_DIAGNOSTICS
	using DefaultReaderNotificationPolicy = ReentrancyCheckReaderNotificationPolicy;
#else
	using DefaultReaderNotificationPolicy = NoOpReaderNotificationPolicy;
#endif

	/// Default reader writer lock.
	using SpinReaderWriterLock = BasicSpinReaderWriterLock<DefaultReaderNotificationPolicy>;
}}
