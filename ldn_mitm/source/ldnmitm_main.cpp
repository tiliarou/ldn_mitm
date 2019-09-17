/*
 * Copyright (c) 2018 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <malloc.h>

#include <switch.h>
#include <stratosphere.hpp>

#include "ldnmitm_service.hpp"

extern "C" {
    extern u32 __start__;

    u32 __nx_applet_type = AppletType_None;

    #define INNER_HEAP_SIZE 0x100000
    size_t nx_inner_heap_size = INNER_HEAP_SIZE;
    char   nx_inner_heap[INNER_HEAP_SIZE];
    
    void __libnx_initheap(void);
    void __appInit(void);
    void __appExit(void);
}


void __libnx_initheap(void) {
    void*  addr = nx_inner_heap;
    size_t size = nx_inner_heap_size;

    /* Newlib */
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = (char*)addr;
    fake_heap_end   = (char*)addr + size;
}

void __appInit(void) {
    Result rc;
    svcSleepThread(10000000000L);

    SetFirmwareVersionForLibnx();

    rc = smInitialize();
    if (R_FAILED(rc)) {
        fatalSimple(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    }
    
    rc = fsInitialize();
    if (R_FAILED(rc)) {
        fatalSimple(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    }

    rc = fsdevMountSdmc();
    if (R_FAILED(rc)) {
        fatalSimple(rc);
    }

    rc = ipinfoInit();
    if (R_FAILED(rc)) {
        fatalSimple(rc);
    }

    const SocketInitConfig socketInitConfig = {
        .bsdsockets_version = 1,

        .tcp_tx_buf_size = 0x800,
        .tcp_rx_buf_size = 0x1000,
        .tcp_tx_buf_max_size = 0x2000,
        .tcp_rx_buf_max_size = 0x2000,

        .udp_tx_buf_size = 0x2000,
        .udp_rx_buf_size = 0x2000,

        .sb_efficiency = 4,

        .serialized_out_addrinfos_max_size  = 0x1000,
        .serialized_out_hostent_max_size    = 0x200,
        .bypass_nsd                         = false,
        .dns_timeout                        = 0,
    };
    rc = socketInitialize(&socketInitConfig);
    if (R_FAILED(rc)) {
        fatalSimple(rc);
    }

    LogFormat("__appInit done");
}

void __appExit(void) {
    /* Cleanup services. */
    socketExit();
    ipinfoExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

struct LdnMitmManagerOptions {
    static const size_t PointerBufferSize = 0x1000;
    static const size_t MaxDomains = 0x10;
    static const size_t MaxDomainObjects = 0x100;
};
class LdnServiceSession : public ServiceSession {
    public:
        LdnServiceSession(Handle s_h, size_t pbs, ServiceObjectHolder &&h)  : ServiceSession(s_h, pbs, std::move(h)) { }

        virtual void PreProcessRequest(IpcResponseContext *ctx) {
            LogFormat("Request cmd_id %" PRIu64 " type %d", ctx->cmd_id, ctx->cmd_type);
        }
        virtual void PostProcessResponse(IpcResponseContext *ctx) {
            LogFormat("Reply rc %d", ctx->rc);
        }
};
template<typename ManagerOptions = LdnMitmManagerOptions>
class LdnMitmManager : public WaitableManager<ManagerOptions> {
    public:
        LdnMitmManager(u32 n, u32 ss = 0x8000) : WaitableManager<ManagerOptions>(n, ss) {}
        virtual void AddSession(Handle server_h, ServiceObjectHolder &&service) override {
            this->AddWaitable(new LdnServiceSession(server_h, ManagerOptions::PointerBufferSize, std::move(service)));
        }
};

int main(int argc, char **argv)
{
    LogFormat("main");

    auto server_manager = new LdnMitmManager(2);

    /* Create ldn:u mitm. */
    AddMitmServerToManager<LdnMitMService>(server_manager, "ldn:u", 3);
    server_manager->AddWaitable(new ManagedPortServer<LdnConfig>("ldnmitm", 3));

    /* Loop forever, servicing our services. */
    server_manager->Process();

    return 0;
}
