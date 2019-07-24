// Copyright 2019 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "laser_listener.h"

namespace beam::wallet::lightning
{
LaserListener::LaserListener(IClient& parent) : m_Parent(parent)
{

}

LaserListener::~LaserListener()
{

}

void LaserListener::OnNewTip()
{
    m_Parent.OnNewTip();
}

void LaserListener::OnRolledBack()
{

}

void LaserListener::OnOwnedNode(const PeerID&, bool bUp)
{

}

void LaserListener::OnComplete(Request&)
{

}

void LaserListener::OnMsg(proto::BbsMsg&&)
{

}

Block::SystemState::IHistory& LaserListener::get_History()
{
    return m_Parent.get_History();
}

}  // namespace beam::wallet::lightning
