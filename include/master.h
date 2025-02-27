//
// Created by pkuyo on 25-2-27.
//

#ifndef MASTER_H
#define MASTER_H
#include <def.h>

class Master {
    ProcContext ctx;
public:
    explicit Master(ProcContext& _ctx);
    ~Master();

    void master_loop();
};
#endif //MASTER_H
