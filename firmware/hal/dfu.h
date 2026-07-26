// hal/dfu.h — capability port: trigger the (signed) DFU path. core/ decides
#ifndef SKYBLIP_HAL_DFU_H
#define SKYBLIP_HAL_DFU_H

namespace skyblip::hal {

class Dfu {
   public:
    virtual ~Dfu() = default;
    virtual void trigger() = 0;
};

}

#endif
