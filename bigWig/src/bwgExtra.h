//
//  bwgExtra.h
//  bigWigXC
//
//  Created by Andre Martins on 10/27/13.
//  Copyright (c) 2013 Andre Martins. All rights reserved.
//

#ifndef bigWigXC_bwgExtra_h
#define bigWigXC_bwgExtra_h

#include "bwg_new.h"


struct bbiInterval *bigWigIntervalQueryNoClip(struct bbiFile *bwf, char *chrom, bits32
                                              start, bits32 end,
                                              struct lm *lm);

#endif
