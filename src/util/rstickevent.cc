/*
 * libretroshare/src/util: rstickevent.cc
 *
 * Identity interface for RetroShare.
 *
 * Copyright 2012-2012 by Robert Fernie.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License Version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA.
 *
 * Please report all bugs and problems to "retroshare@lunamutt.com".
 *
 */

#include "util/rstickevent.h"

#include <iostream>
#include <list>

//#define DEBUG_EVENTS	1

void	RsTickEvent::tick_events()
{
#ifdef DEBUG_EVENTS
	std::cerr << "RsTickEvent::tick_events() Event List:";
	std::cerr << std::endl;
#endif

	time_t now = time(NULL);
	{
		RsStackMutex stack(mEventMtx); /********** STACK LOCKED MTX ******/

#ifdef DEBUG_EVENTS
		if (!mEvents.empty())
		{
			std::multimap<time_t, uint32_t>::iterator it;

			for(it = mEvents.begin(); it != mEvents.end(); it++)
			{
				std::cerr << "\tEvent type: ";
				std::cerr << it->second << " in " << it->first - now << " secs";
				std::cerr << std::endl;
			}
		}
#endif

		if (mEvents.empty())
		{
			return;
		}

		/* all events in the future */
		if (mEvents.begin()->first > now)
		{
			return;
		}
	}

	std::list<uint32_t> toProcess;
	std::list<uint32_t>::iterator it;

	{
		RsStackMutex stack(mEventMtx); /********** STACK LOCKED MTX ******/
		while((!mEvents.empty()) && (mEvents.begin()->first <= now))
		{
			std::multimap<time_t, uint32_t>::iterator it = mEvents.begin();
			uint32_t event_type = it->second;
			toProcess.push_back(event_type);
			mEvents.erase(it);

			count_adjust_locked(event_type, -1);
		}
	}

	for(it = toProcess.begin(); it != toProcess.end(); it++)
	{
		std::cerr << "RsTickEvent::tick_events() calling handle_event(";
		std::cerr << *it << ")";
		std::cerr << std::endl;
		handle_event(*it);
	}
}

void RsTickEvent::schedule_now(uint32_t event_type)
{
	RsTickEvent::schedule_in(event_type, 0);
}

void RsTickEvent::schedule_event(uint32_t event_type, time_t when)
{
	RsStackMutex stack(mEventMtx); /********** STACK LOCKED MTX ******/
	mEvents.insert(std::make_pair(when, event_type));

	count_adjust_locked(event_type, 1);
}

void RsTickEvent::schedule_in(uint32_t event_type, uint32_t in_secs)
{
	std::cerr << "RsTickEvent::schedule_in(" << event_type << ") in " << in_secs << " secs";
	std::cerr << std::endl;

	RsStackMutex stack(mEventMtx); /********** STACK LOCKED MTX ******/
	time_t event_time = time(NULL) + in_secs;
	mEvents.insert(std::make_pair(event_time, event_type));

	count_adjust_locked(event_type, 1);
}

void RsTickEvent::handle_event(uint32_t event_type)
{
	std::cerr << "RsTickEvent::handle_event(" << event_type << ") ERROR Not Handled";
	std::cerr << std::endl;
}


int32_t RsTickEvent::event_count(uint32_t event_type)
{
	RsStackMutex stack(mEventMtx); /********** STACK LOCKED MTX ******/
	std::map<uint32_t, int32_t>::iterator it;

	it = mEventCount.find(event_type);
	if (it == mEventCount.end())
	{
		return 0;
	}

	return it->second;
}


bool RsTickEvent::prev_event_ago(uint32_t event_type, int32_t &age)
{
	RsStackMutex stack(mEventMtx); /********** STACK LOCKED MTX ******/
	std::map<uint32_t, time_t>::iterator it;

	it = mPreviousEvent.find(event_type);
	if (it == mPreviousEvent.end())
	{
		return false;
	}

	age = time(NULL) - it->second;
	return true;
}

	
void RsTickEvent::count_adjust_locked(uint32_t event_type, int32_t change)
{
	std::map<uint32_t, int32_t>::iterator it;

	it = mEventCount.find(event_type);
	if (it == mEventCount.end())
	{
		mEventCount[event_type] = 0;
		it = mEventCount.find(event_type);
	}

	it->second += change;
	if (it->second < 0)
	{
		std::cerr << "RsTickEvent::count_adjust() ERROR: COUNT < 0";
		std::cerr << std::endl;

		it->second = 0;
	}
}


void RsTickEvent::note_event_locked(uint32_t event_type)
{
	mPreviousEvent[event_type] = time(NULL);
}


