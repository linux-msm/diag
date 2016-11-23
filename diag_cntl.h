#ifndef __DIAG_CNTL_H__
#define __DIAG_CNTL_H__

#include "diag.h"

int diag_cntl_recv(int fd, void *data);
void diag_cntl_send_feature_mask(struct peripheral *peripheral);

#endif
