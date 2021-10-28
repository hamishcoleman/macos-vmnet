Example code to quickly show how to drive a vmnet virtual interface on
macos.

The vmnet is included with macos and can provide much of the same features
as a tuntap driver.

# Current status

Things that are working in obvious ways:

- Send and Receive packets
- Give the virtual interface an IP4 address and netmask

Things that should be simple, but Apple have hidden from us:

- Set the MAC address on the virtual interface
- Learn the interface name that was created
- Any way to integrate with a select() or poll() mainloop

# Observations

- Creating a new vmnet create two new interfaces (names are examples, in a
  busier test system, the numbers might change)
  - a en1 interface
  - a bridge100 interface
- The en1 interface is added as a bridge member of the bridge100 interface
- The MAC addresses assigned to the two interfaces appear to be random but
  have some stability - they are probably created from either a hash of the
  vmnet create params or are created once for the machine and stored.
  TODO - more investigation required.
- Nothing appeared to break when the bridge membership was manually altered
  or the bridge interface destroyed
- With the bridge destroyed, the en1 interface could have an ip4 addr added
  as per normal
- The MAC address on the en1 interface could not be changed with ifconfig,
  however no error messages were generated
- Destroying the bridge made it impossible to start a second test (and get
  at least an en2 interface, if not a bridge101 interface)
- Running the simple test twice (reusing the same vmnet_start_address_key),
  resulted in both en1 and en2 virtual interfaces being created and both
  added to the bridge100 interface
- Running the simple test with two different vmnet_start_address_key values
  resulted in two completely independent sets of virtual ethernet and
  software bridge being created (en1 attached to bridge100, en2 attached
  to bridge101)
- The MAC address on the bridge101 interface could be changed with ifconfig.

# Conclusions

We can create a new vif with a known ip4addr with the vmnet framework.
This can be used for layer-2 VPN traffic, just like a tap device.

To get and or set the MAC address, we need to iterate through the
interfaces, looking for one with the known ip4addr, and then can use the
normal ifconfig command to set the MAC address (eg: `ifconfig bridge100
lladdr 02:00:01:02:03:55`)

Instead of using a select() or poll() loop, the Apple dispatch threading
system will automatically create a thread and RX packets can be handled
from there.  TX can happen from the main loop thread.

## Possible issues

- Apple may start enforcing MAC address filtering, but since we can ask not
  to have a guest address issued, it seems OK
- Finding the interface name in order to get the MAC address is fiddly and
  probably prone to issues
- Setting the MAC address may end up falling foul of any added filtering.

# Resources

These webpages helped me write this code:

- https://lists.gnu.org/archive/html/qemu-devel/2021-02/msg04637.html
- https://developer.apple.com/documentation/vmnet
