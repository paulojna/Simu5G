#ifndef _NODE_LOCATION_H_
#define _NODE_LOCATION_H_

namespace simu5g {

class NodeLocation {
    private:
        long x;
        long y;
        long z;

    public:
        // Default constructor
        NodeLocation();

        // Parameterized constructor
        NodeLocation(long newX, long newY, long newZ);

        // Destructor
        virtual ~NodeLocation();

        // Getter methods
        long getX();
        long getY();
        long getZ() ;

        // Setter methods
        void setX(long newX);
        void setY(long newY);
        void setZ(long newZ);
};
}

#endif /* _NODE_LOCATION_H_ */