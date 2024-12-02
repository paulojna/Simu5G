//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;

cplusplus {{
    #include "RavensControllerApp/DataUpdates/UserMEHUpdate.h"
    #include "RavensControllerApp/DataUpdates/UserEntryUpdate.h"
    typedef std::vector<::simu5g::UserMEHUpdate> UserMEHUpdatedList;
    typedef std::vector<::simu5g::UserEntryUpdate> UserEntryUpdateList;
}};

class UserMEHUpdatedList { @existingClass; };
class UserEntryUpdateList { @existingClass; };

namespace simu5g;

class RavensControllerUpdatePacket extends inet::FieldsChunk{
    int type;
}


class UserMEHUpdatedListMessage extends RavensControllerUpdatePacket
{
    UserMEHUpdatedList ueMehList;
}

class UserEntryListMessage extends RavensControllerUpdatePacket
{
    UserEntryUpdateList ueEntryList;
}