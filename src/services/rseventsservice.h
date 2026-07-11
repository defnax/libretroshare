/*******************************************************************************
 * Retroshare events service                                                   *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2019-2020  Gioacchino Mazzurco <gio@retroshare.cc>             *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Lesser General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/
#pragma once

#include <memory>
#include <cstdint>
#include <deque>
#include <array>
#include <mutex>

#include "retroshare/rsevents.h"
#include "util/rsthreads.h"
#include "util/rsdebug.h"

class RsEventsService :
        public RsEvents, public RsTickingThread
{
public:
	RsEventsService():
        mHandlerMapMtx("RsEventsService::mHandlerMapMtx"),
        mLastHandlerId(1),
        mHandlerMaps(static_cast<std::size_t>(RsEventType::__MAX)),
        mEventQueueMtx("RsEventsService::mEventQueueMtx")  {}

    /// @see RsEvents
	std::error_condition postEvent(
	        std::shared_ptr<const RsEvent> event ) override;

	/// @see RsEvents
	std::error_condition sendEvent(
	        std::shared_ptr<const RsEvent> event ) override;

	/// @see RsEvents
	RsEventsHandlerId_t generateUniqueHandlerId() override;

    /// @see RsEvents
    RsEventType getDynamicEventType(const std::string& unique_service_identifier) override;

    /// @see RsEvents
	std::error_condition registerEventsHandler(
	        std::function<void(std::shared_ptr<const RsEvent>)> multiCallback,
	        RsEventsHandlerId_t& hId = RS_DEFAULT_STORAGE_PARAM(RsEventsHandlerId_t, 0),
	        RsEventType eventType = RsEventType::__NONE ) override;

	/// @see RsEvents
	std::error_condition unregisterEventsHandler(
	        RsEventsHandlerId_t hId ) override;

protected:
	std::error_condition isEventTypeInvalid(RsEventType eventType);
	std::error_condition isEventInvalid(std::shared_ptr<const RsEvent> event);

	RsMutex mHandlerMapMtx;

	/** Held by handleEvent() for the whole duration of the callbacks dispatch
	 * loop, so that unregisterEventsHandler() can act as a barrier: after it
	 * returns, the removed handler is guaranteed to be neither running nor about
	 * to start. Without this, unregister only removes the handler from the map,
	 * but handleEvent() runs callbacks on a *copy* taken outside mHandlerMapMtx
	 * (on purpose, to let callbacks re-enter), so a callback whose owner is
	 * being destroyed on another thread could still fire against a dangling
	 * object -> use-after-free (typically a SIGSEGV in qobject_cast<QThread*>
	 * inside RsQThreadUtils::postToObject at shutdown). Recursive so that a
	 * callback re-entering (self-unregister or synchronous sendEvent) on the
	 * dispatching thread does not deadlock. */
	std::recursive_mutex mDispatchMtx;

	RsEventsHandlerId_t mLastHandlerId;

	/** Storage for event handlers, keep 10 extra types for plugins that might
	 * be released indipendently */
    std::vector<
	    std::map<
	        RsEventsHandlerId_t,
            std::function<void(std::shared_ptr<const RsEvent>)> >
	> mHandlerMaps;

    /** Extra event types registered by plugins */
    std::map<std::string,RsEventType> mRegisteredExtraEventTypes;

	RsMutex mEventQueueMtx;
	std::deque< std::shared_ptr<const RsEvent> > mEventQueue;

	void threadTick() override; /// @see RsTickingThread

	void handleEvent(std::shared_ptr<const RsEvent> event);
	RsEventsHandlerId_t generateUniqueHandlerId_unlocked();

	RS_SET_CONTEXT_DEBUG_LEVEL(3)
};
