//
// Created by pkuyo on 25-2-27.
//

#ifndef MASTER_H
#define MASTER_H

#include "def.h"
#include "process.h"

class Master : public Process {
public:
    explicit Master(ProcContext&& _ctx);
    bool master_loop();
};
#endif //MASTER_H
