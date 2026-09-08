#include <dolphin/types.h>
#include <dolphin/mcc.h>
// macOS has no GameCube EXI Host I/O adapter. Report SDK failure codes for
// that device, rather than claiming that debug transfers or host files exist.
static u8 mcc_error, fio_error;
static int no_channel(void) { mcc_error=1; return 0; }
int MCCInit(enum MCC_EXI channel,u8 timeout,MCC_CBSysEvent callback) {
    (void)channel;(void)timeout;(void)callback;mcc_error=4;return 0;
}
void MCCExit(void) { mcc_error=0; }
int MCCEnumDevices(MCC_CBEnumDevices callback) { (void)callback;mcc_error=13;return 0; }
u8 MCCGetLastError(void) {return mcc_error;}
u8 MCCGetFreeBlocks(enum MCC_MODE mode) {(void)mode;return no_channel();}
int MCCGetConnectionStatus(enum MCC_CHANNEL channel,enum MCC_CONNECT* connection) {
    (void)channel;(void)connection;return no_channel();
}
int MCCOpen(enum MCC_CHANNEL channel,u8 size,MCC_CBEvent callback) {(void)channel;(void)size;(void)callback;return no_channel();}
int MCCClose(enum MCC_CHANNEL channel) {(void)channel;return no_channel();}
int MCCNotify(enum MCC_CHANNEL channel,u32 notice) {(void)channel;(void)notice;return no_channel();}
int MCCRead(enum MCC_CHANNEL channel,u32 offset,void* data,int size,enum MCC_SYNC_STATE async) {
    (void)channel;(void)offset;(void)data;(void)size;(void)async;return no_channel();
}
int MCCWrite(enum MCC_CHANNEL channel,u32 offset,void* data,int size,enum MCC_SYNC_STATE async) {
    (void)channel;(void)offset;(void)data;(void)size;(void)async;return no_channel();
}
int MCCStreamOpen(enum MCC_CHANNEL channel,u8 size) {(void)channel;(void)size;return no_channel();}
int FIOInit(enum MCC_EXI channel,enum MCC_CHANNEL id,u8 size) {
    (void)id;(void)size;MCCInit(channel,10,0);fio_error=0x87;return 0;
}
void FIOExit(void) {fio_error=0;}
int FIOQuery(void) {return 0;}
u8 FIOGetLastError(void) {return fio_error;}
int FIOFopen(const char* name,u32 mode) {(void)name;(void)mode;fio_error=0x87;return -1;}
int FIOFclose(int handle) {(void)handle;fio_error=0x87;return 0;}
u32 FIOFwrite(int handle,void* data,u32 size) {(void)handle;(void)data;(void)size;fio_error=0x87;return (u32)-1;}
