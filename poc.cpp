#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <binder/IPCThreadState.h>
#include <utils/String16.h>
#include <utils/RefBase.h>
#include <binder/Parcel.h>
#include <stdio.h>

using namespace android;

int main() {
    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> service = sm->getService(String16("vendor.qti.hardware.systemhelper"));
    if (service == nullptr) {
        printf("failed to get service\n");
        return -1;
    }

    Parcel data, reply;
    data.writeInterfaceToken(String16("vendor.qti.hardware.systemhelper"));
    data.writeInt32(0x41);
    status_t status = service->transact(10, &data, &reply);
    printf("transact returned: %d\n", status);

    return 0;
}
