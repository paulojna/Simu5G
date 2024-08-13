#ifndef _USERUPDATE_H_
#define _USERUPDATE_H_

#include <string>

namespace simu5g {

class UserUpdate
{
    protected:
        const std::string address;

    public:
        UserUpdate();
        UserUpdate(const std::string& address);
        virtual ~UserUpdate();

        // getters
        const std::string& getAddress();
};

}
#endif /* _USERUPDATE_H_ */