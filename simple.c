/*
 *
 */

#include <assert.h>
#include <unistd.h>
#include <sys/uio.h>
#include <vmnet/vmnet.h>
#include <SystemConfiguration/SCNetworkConfiguration.h>

#include <stdio.h>
#include <time.h>
#include <CoreFoundation/CoreFoundation.h>

void fhexdump(unsigned int display_addr, void *in, int size, FILE *stream) {
  uint8_t *p = in;

  while(size>0) {
    int i;

    fprintf(stream, "%03x: ", display_addr);

    for (i = 0; i < 16; i++) {
      if (i < size) {
        fprintf(stream, "%02x", p[i]);
      } else {
        fprintf(stream, "  ");
      }
      if (i==7) {
        fprintf(stream, "  ");
      } else {
        fprintf(stream, " ");
      }
    }
    fprintf(stream, "  |");

    for (i = 0; i < 16; i++) {
      if (i < size) {
        char ch = p[i];
        if (ch>=0x20 && ch<=0x7e) {
          fprintf(stream, "%c", ch);
        } else {
          fprintf(stream, " ");
        }
      }
    }
    fprintf(stream, "|\n");

    size -= 16;
    display_addr += 16;
    p += 16;
  }
}

/************************************************************************/

int tap_write(interface_ref vmnet_iface_ref, char *buf, int len) {
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = len;
    struct vmpktdesc v;
    v.vm_pkt_size = len;
    v.vm_pkt_iov = &iov;
    v.vm_pkt_iovcnt = 1;
    v.vm_flags = 0;

    int pktcnt = 1;
    vmnet_return_t result = vmnet_write(vmnet_iface_ref, &v, &pktcnt);
    if (result != VMNET_SUCCESS || pktcnt != 1) {
        printf("Failed to read packet from host: %i\n", result);
        return -1;
    }

    return v.vm_pkt_size;
}

void send_dummy(interface_ref vmnet_iface_ref) {
    /*
     * Generate a dummy packet and transmit it
     *
     * Use purely for testing that sending packets works
     *
     */
    char dummy[] =
        "\xff\xff\xff\xff\xff\xff"  // eth dest
        "\x02\x10\x20\x30\x40\x50"  // eth src
        "\x55\xaa"                  // eth proto
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    int size = sizeof(dummy);
    size = tap_write(vmnet_iface_ref, dummy, size);
    if (size != sizeof(dummy)) {
        printf("write error: size=%i\n", size);
    }
}

void handle_rx_packet(interface_ref vmnet_iface_ref, char *buf, int len) {
    if (len==0) {
        return;
    }

    /* For debugging, dump the packet we got */
    fhexdump(0, buf, len, stdout);
    printf("\n");

    /* For generating some reply traffic, send a dummy packet */
    send_dummy(vmnet_iface_ref);
}

interface_ref tap_open(const char *ip4addr) {

    operating_modes_t mode = VMNET_HOST_MODE;
    xpc_object_t interface_desc = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(
        interface_desc,
        vmnet_operation_mode_key,
        mode
    );

#define INSANE 0
#if INSANE
    uuid_t set_uuid;
    uuid_parse("000000-0000-0000-0000-000000000001", set_uuid);
    xpc_dictionary_set_uuid(interface_desc,
        vmnet_interface_id_key,
        set_uuid
    );
#endif /* INSANE */

    xpc_dictionary_set_string(interface_desc,
        vmnet_start_address_key,
        ip4addr
    );
    xpc_dictionary_set_string(interface_desc,
        vmnet_end_address_key,
        ip4addr
    );
    xpc_dictionary_set_string(interface_desc,
        vmnet_subnet_mask_key,
        "255.255.255.0"
    );
#if 0
    // Available from 11.0
    xpc_dictionary_set_uuid(interface_desc,
        vmnet_network_identifier_key,
        set_uuid
    );
    xpc_dictionary_set_string(interface_desc,
        vmnet_host_ip_address_key,
        "10.20.30.40"
    );
    xpc_dictionary_set_string(interface_desc,
        vmnet_host_subnet_mask_key,
        "255.255.255.0"
    );

    // for completeness sake
    // vmnet_host_ipv6_address_key

    xpc_dictionary_set_bool(interface_desc,
        vmnet_enable_isolation_key,
        true
    );

#endif

    // Appears to simply generate a mac address unlikely to be used elsewhere.
    // No edge mac filtering was seen with this simple test tool.
    // If false then the interface_param generated will not have either a
    // vmnet_mac_address or a vmnet_interface_id key.
    //
    // The documentation implies that if you want the same mac address, you
    // set the interface_desc vmnet_interface_id_key to the uuid you get from
    // the interface_param.  However, this all refers to the "guest" MAC
    // and we are trying to emulate a VPN...
    xpc_dictionary_set_bool(interface_desc,
        vmnet_allocate_mac_address_key,
        false
    );

    dispatch_queue_t vmnet_dispatch_queue = dispatch_queue_create(
        "org.qemu.vmnet.iface_queue",
        DISPATCH_QUEUE_SERIAL
    );

    __block vmnet_return_t vmnet_start_status = 0;
    __block uint64_t vmnet_iface_mtu = 0;
    __block uint64_t vmnet_max_packet_size = 0;

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

        printf("got interface_param with:\n");
        xpc_dictionary_apply(interface_param,
            ^bool (const char * _Nonnull key, xpc_object_t _Nonnull value) {
            char *desc = xpc_copy_description(value);
            printf("  %s %s\n",
                key,
                desc
            );
            free(desc);
            return true;
        });
        printf("\n\n");

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
        return NULL;
    }

    printf("Started vmnet interface with configuration:\n");
    printf("MTU:              %llu\n", vmnet_iface_mtu);
    printf("Max packet size:  %llu\n", vmnet_max_packet_size);
    printf("\n\n");

    vmnet_return_t event_cb_stat = vmnet_interface_set_event_callback(
        vmnet_iface_ref,
        VMNET_INTERFACE_PACKETS_AVAILABLE,
        vmnet_dispatch_queue,
        ^(interface_event_t event_mask, xpc_object_t  _Nonnull event) {
        if (event_mask != VMNET_INTERFACE_PACKETS_AVAILABLE) {
            printf("Unknown vmnet interface event 0x%08x\n", event_mask);
            return;
        }

        char buf[1600]; /* TODO: this should be vmnet_max_packet_size */
        struct iovec iov;
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);

        struct vmpktdesc v;
        v.vm_pkt_size = sizeof(buf);
        v.vm_pkt_iov = &iov;
        v.vm_pkt_iovcnt = 1;
        v.vm_flags = 0;

        int pktcnt = 1;
        vmnet_return_t result = vmnet_read(vmnet_iface_ref, &v, &pktcnt);
        if (result != VMNET_SUCCESS) {
            printf("Failed to read packet from host: %i\n", result);
            return;
        }

        /* Ensure we read exactly one packet */
        assert(pktcnt == 1);

        /* Pass the received packet to the handler */
        handle_rx_packet(vmnet_iface_ref, buf, v.vm_pkt_size);
    });

    /* Did we manage to set an event callback? */
    if (event_cb_stat != VMNET_SUCCESS) {
        printf("Failed to set up a callback to receive packets: %i\n", event_cb_stat);
        return NULL;
    }

    return vmnet_iface_ref;
}

/*
 * Experiments in querying the network details
 */
void interface_list1() {
    xpc_object_t list = vmnet_copy_shared_interface_list();

    printf("interface list from vmnet_copy_shared_interface_list():\n");
    xpc_array_apply(list, ^bool(size_t index, xpc_object_t value) {
        char *desc = xpc_copy_description(value);
        printf("  %lu %s\n",
            index,
            desc
        );
        free(desc);
        return true;
    });
    printf("\n");
    free(list);
}

void interface_list2() {
    CFArrayRef list2 = SCNetworkInterfaceCopyAll();

    printf("interface list from SCNetworkInterfaceCopyAll():\n");
    CFShow(list2);
    printf("\n");
    CFRelease(list2);

    list2 = SCVLANInterfaceCopyAvailablePhysicalInterfaces();

    printf("interface list from SCVLANInterfaceCopyAvailablePhysicalInterfaces():\n");
    CFShow(list2);
    printf("\n");
    CFRelease(list2);
}

void interface_list() {
    interface_list1();
    interface_list2();
}

int main(int argc, char **argv) {
    char *ip4addr = "10.20.30.40";
    if (argc>1) {
        ip4addr = argv[1];
    }

    if (0==strcmp(ip4addr,"list")) {
        interface_list();
        return(0);
    }

    int timeout = 1000000;
    if (argc>2) {
        timeout = atoi(argv[2]);
    }

    interface_ref vmnet_iface_ref = tap_open(ip4addr);
    if (vmnet_iface_ref == NULL) {
        exit(1);
    }

    printf("Waiting for packets\n");

    int now = time(NULL);
    int stopat = now + timeout;

    for(;;) {
        // Everything is done in the dispatch thread
        sleep(1);

        now = time(NULL);
        if (now > stopat) {
            break;
        }
    }
}
