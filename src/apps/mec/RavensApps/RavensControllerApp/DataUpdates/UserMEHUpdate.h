#ifndef USERMEHUPDATE_H
#define USERMEHUPDATE_H
#include <string>

namespace simu5g {

class UserMEHUpdate
{
    protected:
        std::string address;
        std::string lastMEHId;
        std::string newMEHId;

    public:
        UserMEHUpdate();
        UserMEHUpdate(const std::string& address, const std::string& lastMEHId, const std::string& newMEHId);
        virtual ~UserMEHUpdate();

        // setters
        void setLastMEHId(const std::string& lastMEHId);
        void setNewMEHId(const std::string& newMEHId);
        void setAddress(const std::string& address);

        // getters
        const std::string& getLastMEHId() const;
        const std::string& getNewMEHId() const;
        const std::string& getAddress() const;
};

}

#endif // USERMEHUPDATE_H
