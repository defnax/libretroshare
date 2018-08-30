/*******************************************************************************
 * libretroshare/src/gxs: rsgxsrequesttypes.cc                                 *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2012-2012 by Christopher Evi-Parker                               *
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

#include "rsgxsrequesttypes.h"
#include "util/rsstd.h"

GroupMetaReq::~GroupMetaReq()
{
	//rsstd::delete_all(mGroupMetaData.begin(), mGroupMetaData.end());	// now memory ownership is kept by the cache.
	mGroupMetaData.clear();
}

GroupDataReq::~GroupDataReq()
{
	rsstd::delete_all(mGroupData.begin(), mGroupData.end());
}

MsgMetaReq::~MsgMetaReq()
{
	for (GxsMsgMetaResult::iterator it = mMsgMetaData.begin(); it != mMsgMetaData.end(); ++it) {
		rsstd::delete_all(it->second.begin(), it->second.end());
	}
}

MsgDataReq::~MsgDataReq()
{
	for (NxsMsgDataResult::iterator it = mMsgData.begin(); it != mMsgData.end(); ++it) {
		rsstd::delete_all(it->second.begin(), it->second.end());
	}
}

MsgRelatedInfoReq::~MsgRelatedInfoReq()
{
	for (MsgRelatedMetaResult::iterator metaIt = mMsgMetaResult.begin(); metaIt != mMsgMetaResult.end(); ++metaIt) {
		rsstd::delete_all(metaIt->second.begin(), metaIt->second.end());
	}
	for (NxsMsgRelatedDataResult::iterator dataIt = mMsgDataResult.begin(); dataIt != mMsgDataResult.end(); ++dataIt) {
		rsstd::delete_all(dataIt->second.begin(), dataIt->second.end());
	}
}
