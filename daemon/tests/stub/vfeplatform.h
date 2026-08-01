// SPDX-License-Identifier: MIT
// vfeplatform.h (test stub) -- see stub/vfe.h. Supplies only the two session
// typedefs fd-daemon.cpp selects between with #ifdef _WIN32.
#ifndef FD_STUB_VFEPLATFORM_H
#define FD_STUB_VFEPLATFORM_H
#include "vfe.h"
namespace vfePlatform {
    class vfeUnixSession : public vfe::vfeSession {};
    class vfeWinSession  : public vfe::vfeSession { public: vfeWinSession(int) {} };
}
#endif
