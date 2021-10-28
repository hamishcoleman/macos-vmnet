/*
 *
 */

#include <assert.h>
#include <unistd.h>
#include <sys/uio.h>
#include <vmnet/vmnet.h>

int tap_open() {

    operating_modes_t mode = VMNET_HOST_MODE;
    xpc_object_t interface_desc = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(
        interface_desc,
        vmnet_operation_mode_key,
        mode
    );

    dispatch_queue_t vmnet_dispatch_queue = dispatch_queue_create(
        "org.qemu.vmnet.iface_queue",
        DISPATCH_QUEUE_SERIAL
    );

    __block vmnet_return_t vmnet_start_status = 0;
    __block uint64_t vmnet_iface_mtu = 0;
    __block uint64_t vmnet_max_packet_size = 0;
    __block const char *vmnet_mac_address = NULL;
    /*
     * We can't refer to an array type directly within a block,
     * so hold a pointer instead.
     */
    uuid_string_t vmnet_iface_uuid = {0};
    __block uuid_string_t *vmnet_iface_uuid_ptr = &vmnet_iface_uuid;

    dispatch_semaphore_t vmnet_iface_sem = dispatch_semaphore_create(0);

    interface_ref vmnet_iface_ref = vmnet_start_interface(
        interface_desc,
        vmnet_dispatch_queue,
        ^(vmnet_return_t status, xpc_object_t  _Nullable interface_param) {
        vmnet_start_status = status;
        if (vmnet_start_status != VMNET_SUCCESS || !interface_param) {
            /* Early return if the interface couldn't be started */
            dispatch_semaphore_signal(vmnet_iface_sem);
            return;
        }

        vmnet_iface_mtu = xpc_dictionary_get_uint64(
            interface_param,
            vmnet_mtu_key
        );
        vmnet_max_packet_size = xpc_dictionary_get_uint64(
            interface_param,
            vmnet_max_packet_size_key
        );
        vmnet_mac_address = strdup(xpc_dictionary_get_string(
            interface_param,
            vmnet_mac_address_key
        ));

        const uint8_t *iface_uuid = xpc_dictionary_get_uuid(
            interface_param,
            vmnet_interface_id_key
        );
        uuid_unparse_upper(iface_uuid, *vmnet_iface_uuid_ptr);


        dispatch_semaphore_signal(vmnet_iface_sem);
    });

    /* And block until we receive a response from vmnet */
    dispatch_semaphore_wait(vmnet_iface_sem, DISPATCH_TIME_FOREVER);

    /* Did we manage to start the interface? */
    if (vmnet_start_status != VMNET_SUCCESS || !vmnet_iface_ref) {
        printf("Failed to start interface: %i\n", vmnet_start_status);
        if (vmnet_start_status == VMNET_FAILURE) {
            printf("Hint: vmnet requires running with root access\n");
        }
        return -1;
    }

    printf("Started vmnet interface with configuration:\n");
    printf("MTU:              %llu\n", vmnet_iface_mtu);
    printf("Max packet size:  %llu\n", vmnet_max_packet_size);
    printf("MAC:              %s\n", vmnet_mac_address);
    printf("UUID:             %s\n", vmnet_iface_uuid);

    vmnet_return_t event_cb_stat = vmnet_interface_set_event_callback(
        vmnet_iface_ref,
        VMNET_INTERFACE_PACKETS_AVAILABLE,
        vmnet_dispatch_queue,
        ^(interface_event_t event_mask, xpc_object_t  _Nonnull event) {
        if (event_mask != VMNET_INTERFACE_PACKETS_AVAILABLE) {
            printf("Unknown vmnet interface event 0x%08x\n", event_mask);
            return;
        }

        char *packet_buf = malloc(vmnet_max_packet_size);
        struct iovec *iov = malloc(sizeof(struct iovec));
        iov->iov_base = packet_buf;
        iov->iov_len = vmnet_max_packet_size;

        int pktcnt = 1;
        struct vmpktdesc *v = malloc(sizeof(struct vmpktdesc));
        v->vm_pkt_size = vmnet_max_packet_size;
        v->vm_pkt_iov = iov;
        v->vm_pkt_iovcnt = 1;
        v->vm_flags = 0;

        vmnet_return_t result = vmnet_read(vmnet_iface_ref, v, &pktcnt);
        if (result != VMNET_SUCCESS) {
            printf("Failed to read packet from host: %i\n", result);
        }

        /* Ensure we read exactly one packet */
        assert(pktcnt == 1);

        printf("RX\n");
        free(v);
        free(iov);
        free(packet_buf);
    });

    /* Did we manage to set an event callback? */
    if (event_cb_stat != VMNET_SUCCESS) {
        printf("Failed to set up a callback to receive packets: %i\n", vmnet_start_status);
        exit(1);
    }

    return 0;
}

int main(int argc, char **argv) {
    int error = tap_open();
    if (error!=0) {
        exit(1);
    }

    printf("Waiting for packets\n");
    for(;;) {
        // Do nothing - it is all in the callback
        sleep(1);
    }
}
